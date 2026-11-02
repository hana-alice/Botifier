import contextlib
import io
import json
import threading
import time
from types import SimpleNamespace
import unittest

from pc.codex_weekly.quota import SnapshotStore, select_weekly
from pc.codex_weekly.transport import SerialLink, choose_port


class FakePort:
    def __init__(self, *, busy=False, disconnect=False):
        self.busy, self.disconnect = busy, disconnect
        self.writes, self.reads, self.closed = [], 0, False
        self.in_waiting = 100000

    def open(self):
        if self.busy:
            raise OSError('token=DO_NOT_ECHO_BUSY')
        assert self.dtr is True and self.rts is False

    def read(self, count):
        self.reads += 1
        assert count <= 4096
        if self.disconnect:
            self.disconnect = False
            raise OSError('password=DO_NOT_ECHO_DISCONNECT')
        return b'authorization=DO_NOT_ECHO_DEVICE_LOG\n'

    def write(self, value):
        self.writes.append(value)
        return len(value)

    def close(self):
        self.closed = True


class SerialTests(unittest.TestCase):
    def setUp(self):
        self.now = 0.0
        self.store = SnapshotStore(lambda: 1790000000 + self.now, lambda: self.now)
        self.store.publish(select_weekly({'rateLimitsByLimitId': {'codex': {'primary': {
            'usedPercent': 24, 'windowDurationMins': 10080, 'resetsAt': 1790100000}}}}, 1790000000, 0))

    def link(self, ports):
        def factory(**options):
            self.assertEqual((options['timeout'], options['write_timeout']), (0.2, 1))
            return ports.pop(0)
        module = SimpleNamespace(Serial=factory, SerialException=OSError)
        return SerialLink(self.store, 'COM_FAKE', serial_module=module, ports_provider=lambda: (),
                          monotonic=lambda: self.now)

    def test_auto_selection_requires_unique_vid_pid_or_explicit_port(self):
        candidate = SimpleNamespace(device='COM_FAKE', vid=0x303a, pid=0x1001)
        self.assertIsNone(choose_port(None, []))
        self.assertEqual(choose_port(None, [candidate]), 'COM_FAKE')
        with self.assertRaises(ValueError):
            choose_port(None, [candidate, candidate])
        self.assertEqual(choose_port('COM_EXPLICIT', [candidate, candidate]), 'COM_EXPLICIT')

    def test_held_port_does_not_start_second_owner_and_no_raw_errors_or_logs_leak(self):
        busy, available = FakePort(busy=True), FakePort()
        link = self.link([busy, available])
        output = io.StringIO()
        with contextlib.redirect_stdout(output), contextlib.redirect_stderr(output):
            link.step()
            self.assertTrue(busy.closed)
            self.assertIsNone(link.connection)
            self.now = 3
            link.step()
            self.assertIs(link.connection, available)
            self.assertGreater(available.reads, 0)
        self.assertNotIn('DO_NOT_ECHO', output.getvalue())
        self.assertEqual(len(available.writes), 1)
        link._close()

    def test_reconnect_resends_same_observation_with_reduced_ttl(self):
        first, second = FakePort(), FakePort()
        link = self.link([first, second])
        link.notice = lambda _: None
        link.step()
        original = first.writes[0]
        self.now = 15
        link.step()
        retransmitted = json.loads(first.writes[-1][8:])
        self.assertEqual(retransmitted['sequence'], json.loads(original[8:])['sequence'])
        self.assertEqual(retransmitted['observed_at'], 1790000000)
        self.assertEqual(retransmitted['ttl_ms'], 885000)
        first.disconnect = True
        self.now = 16
        link.step()
        self.assertTrue(first.closed)
        # The device comes back long after the original query, with no RTC.
        self.now = 800
        link.step()
        self.assertEqual(len(second.writes), 1)
        self.assertEqual(json.loads(second.writes[0][8:])['ttl_ms'], 100000)
        self.now = 841
        link.step()
        self.assertIn(b'"available":false', second.writes[-1])
        self.assertNotIn(b'observed_at', second.writes[-1])
        link._close()

    def test_background_owner_keeps_draining_while_main_thread_waits_and_closes(self):
        port = FakePort()
        drained = threading.Event()
        original_read = port.read
        def read(count):
            drained.set()
            return original_read(count)
        port.read = read
        link = self.link([port])
        link.notice = lambda _: None
        try:
            link.start()
            self.assertTrue(drained.wait(timeout=1))
            previous_reads = port.reads
            time.sleep(0.15)  # Stand-in for an account RPC waiting on its own process.
            self.assertGreater(port.reads, previous_reads)
        finally:
            link.stop()
        self.assertTrue(port.closed)
        self.assertFalse(link._thread.is_alive())
