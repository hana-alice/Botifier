import contextlib
import io
import json
import unittest
from unittest import mock

from pc.codex_weekly import app
from pc.codex_weekly.quota import QuotaError, SnapshotStore, TTL_MS, select_weekly


NOW = 1_790_000_000


def result(position='primary', used=24, reset=NOW + 10000):
    return {'rateLimitsByLimitId': {'codex': {position: {
        'usedPercent': used, 'windowDurationMins': 10080, 'resetsAt': reset}}}}


class WeeklySelectionTests(unittest.TestCase):
    def test_weekly_can_be_primary_or_secondary_independent_of_short_window(self):
        for position in ('primary', 'secondary'):
            value = result(position)
            other = 'secondary' if position == 'primary' else 'primary'
            value['rateLimitsByLimitId']['codex'][other] = {
                'usedPercent': 95, 'windowDurationMins': 300, 'resetsAt': NOW + 500}
            self.assertEqual(select_weekly(value, NOW, 0).remaining_percent, 76)

    def test_zero_hundred_and_fractional_usage_are_conservative(self):
        for used, expected in ((0, 100), (100, 0), (24.25, 75)):
            self.assertEqual(select_weekly(result(used=used), NOW, 0).remaining_percent, expected)

    def test_only_exact_codex_bucket_and_explicit_legacy_id_are_allowed(self):
        value = result()
        bucket = value['rateLimitsByLimitId']['codex']
        self.assertEqual(select_weekly({'rateLimits': {'limitId': 'codex', **bucket}}, NOW, 0).remaining_percent, 76)
        for wrong in ({}, {'rateLimits': bucket}, {'rateLimitsByLimitId': {'other': bucket}},
                      {'rateLimitsByLimitId': {'codex': {'limitId': 'other', **bucket}}}):
            with self.subTest(wrong=wrong), self.assertRaises(QuotaError):
                select_weekly(wrong, NOW, 0)

    def test_missing_and_ambiguous_weekly_windows_fail_closed(self):
        for bucket in ({}, {'primary': None}, {'primary': {'windowDurationMins': 300}},
                       {'primary': {'windowDurationMins': 10080}, 'secondary': {'windowDurationMins': 10080}},
                       {'primary': {'windowDurationMins': None}}):
            with self.subTest(bucket=bucket), self.assertRaises(QuotaError):
                select_weekly({'rateLimitsByLimitId': {'codex': bucket}}, NOW, 0)

    def test_bad_numeric_fields_and_elapsed_reset_are_rejected(self):
        for field, values in {
            'usedPercent': (True, float('nan'), float('inf'), -1, 101, 10**500, '24', None),
            'resetsAt': (True, NOW, NOW - 1, float(NOW + 100), 0x100000000, None),
            'windowDurationMins': (True, 10080.0, '10080', None),
        }.items():
            for invalid in values:
                value = result()
                value['rateLimitsByLimitId']['codex']['primary'][field] = invalid
                with self.subTest(field=field, invalid=repr(invalid)[:20]), self.assertRaises(QuotaError):
                    select_weekly(value, NOW, 0)


class SnapshotTests(unittest.TestCase):
    def setUp(self):
        self.wall, self.mono = NOW, 0
        self.store = SnapshotStore(lambda: self.wall, lambda: self.mono)

    def publish(self, reset=NOW + 10000):
        return self.store.publish(select_weekly(result(reset=reset), self.wall, self.mono))

    def test_retransmission_keeps_sequence_observation_and_reduces_wire_ttl(self):
        initial = self.publish()
        original_sequence, original_packet = self.store.packet()
        self.wall += 15
        self.mono += 15
        self.assertEqual(self.store.current().encode(), initial.encode())
        sequence, packet = self.store.packet()
        self.assertEqual(sequence, original_sequence)
        self.assertEqual(json.loads(original_packet[8:])['ttl_ms'], TTL_MS)
        self.assertEqual(json.loads(packet[8:])['ttl_ms'], TTL_MS - 15000)
        payload = json.loads(initial.encode().decode('ascii')[8:])
        self.assertEqual((payload['observed_at'], payload['ttl_ms']), (NOW, TTL_MS))
        self.assertEqual(set(payload), {'v', 'type', 'available', 'sequence', 'remaining_percent', 'reset_at', 'observed_at', 'ttl_ms'})

    def test_reboot_retransmit_near_expiry_never_grants_another_fifteen_minutes(self):
        first = self.publish()
        self.wall += 800
        self.mono += 800
        sequence, packet = self.store.packet()
        decoded = json.loads(packet[8:])
        self.assertEqual((sequence, decoded['observed_at'], decoded['ttl_ms']), (first.sequence, NOW, 100000))
        self.wall += 40
        self.mono += 40
        self.assertEqual(json.loads(self.store.packet()[1][8:])['ttl_ms'], 60000)
        self.wall += 1
        self.mono += 1
        sequence, packet = self.store.packet()
        self.assertGreater(sequence, first.sequence)
        self.assertFalse(json.loads(packet[8:])['available'])

    def test_stale_or_reset_becomes_minimal_unavailable_with_new_sequence(self):
        for reset, elapsed in ((NOW + 10000, 900), (NOW + 10, 10)):
            self.setUp()
            first = self.publish(reset)
            self.wall += elapsed
            self.mono += elapsed
            expired = self.store.current()
            self.assertIsNone(expired.quota)
            self.assertGreater(expired.sequence, first.sequence)
            self.assertEqual(set(json.loads(expired.encode()[8:])), {'v', 'type', 'available', 'sequence'})
            self.assertEqual(self.store.current(), expired)

    def test_wall_rollback_and_monotonic_ttl_do_not_refresh_old_snapshot(self):
        self.publish()
        self.wall -= 1
        self.assertIsNone(self.store.current().quota)
        self.setUp()
        self.publish()
        self.mono += 901
        self.assertIsNone(self.store.current().quota)

    def test_refresh_failure_immediately_replaces_good_snapshot_and_hides_raw_error(self):
        first = self.publish()
        fake = mock.Mock()
        fake.fetch.side_effect = OSError('token=DO_NOT_ECHO')
        stdout, stderr = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(stdout), contextlib.redirect_stderr(stderr):
            self.assertFalse(app.query(fake, self.store))
        self.assertGreater(self.store.current().sequence, first.sequence)
        self.assertEqual(json.loads(stdout.getvalue()), {'available': False})
        self.assertNotIn('DO_NOT_ECHO', stderr.getvalue())

    def test_simulation_and_once_never_open_serial_and_simulation_never_queries(self):
        with mock.patch.object(app, 'AppServerClient') as client, mock.patch.object(app, 'SerialLink') as link:
            with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
                self.assertEqual(app.main(['--simulate']), 0)
                client.assert_not_called()
                link.assert_not_called()
                client.return_value.fetch.return_value = select_weekly(result(reset=0xffffffff), NOW, 0)
                # Supply a fresh snapshot at actual runtime through query's store.
                client.return_value.fetch.side_effect = QuotaError('unavailable')
                self.assertEqual(app.main(['--once']), 1)
                link.assert_not_called()
