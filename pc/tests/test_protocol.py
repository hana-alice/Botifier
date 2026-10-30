import contextlib
import dataclasses
import io
import json
import unittest

from pc.distance_reminder.protocol import MAX_LINE_LENGTH, PREFIX, parse_line


def frame(**updates):
    value = {"v": 1, "seq": 0, "uptime_ms": 123, "valid": True,
             "distance_mm": 400, "raw_status": 9, "state": "ranging", "error": 0}
    value.update(updates)
    return PREFIX + json.dumps(value)


class ProtocolTests(unittest.TestCase):
    def test_complete_valid_sample_is_immutable(self):
        sample = parse_line(frame(seq=27, uptime_ms=9876, distance_mm=345))
        self.assertEqual(dataclasses.asdict(sample), {
            "v": 1, "seq": 27, "uptime_ms": 9876, "valid": True,
            "distance_mm": 345, "raw_status": 9, "state": "ranging", "error": 0})
        with self.assertRaises(dataclasses.FrozenInstanceError):
            sample.seq = 2

    def test_non_protocol_logs_are_ignored_without_output(self):
        output = io.StringIO()
        with contextlib.redirect_stdout(output), contextlib.redirect_stderr(output):
            for line in ("", "WiFi password=do-not-print", "I (123) SC_DISTANCE {}",
                         " SC_DISTANCE {}", "SC_DISTANCE{}", "random " + frame()):
                self.assertIsNone(parse_line(line))
        self.assertEqual(output.getvalue(), "")

    def test_version_and_counter_types_are_strict(self):
        for name in ("v", "seq", "uptime_ms", "raw_status", "error"):
            for value in (True, False, 1.0, "1", None, [], {}):
                with self.subTest(field=name, value=value):
                    self.assertIsNone(parse_line(frame(**{name: value})))
        for values in ({"v": 2}, {"seq": -1}, {"seq": 0x100000000},
                       {"uptime_ms": -1}, {"uptime_ms": 0x10000000000000000},
                       {"raw_status": -1}, {"raw_status": 256}):
            with self.subTest(values=values):
                self.assertIsNone(parse_line(frame(**values)))

    def test_validity_is_boolean_and_valid_distance_is_in_hardware_range(self):
        for value in (0, 1, "true", None, []):
            with self.subTest(valid=value):
                self.assertIsNone(parse_line(frame(valid=value)))
        for value in (None, True, False, 39, 4001, 400.0, "400"):
            with self.subTest(distance=value):
                self.assertIsNone(parse_line(frame(distance_mm=value)))
        for value in (40, 4000):
            self.assertEqual(parse_line(frame(distance_mm=value)).distance_mm, value)

    def test_invalid_sensor_frames_allow_integer_or_null_distance(self):
        for distance in (None, 0, 8191, -1):
            sample = parse_line(frame(valid=False, distance_mm=distance,
                                      state="sensor_missing", error=-1, raw_status=255))
            self.assertIsNotNone(sample)
            self.assertFalse(sample.valid)
            self.assertEqual(sample.state, "sensor_missing")
        for distance in (True, "400", 400.0):
            self.assertIsNone(parse_line(frame(valid=False, distance_mm=distance)))

    def test_valid_measurements_require_consistent_quality_state_and_error(self):
        for values in ({"raw_status": 0}, {"raw_status": 1}, {"raw_status": 8},
                       {"raw_status": 10}, {"state": "sensor_missing"},
                       {"state": "recovering"}, {"error": -1}, {"error": 1}):
            with self.subTest(values=values):
                self.assertIsNone(parse_line(frame(**values)))
        for raw_status in (9, 41, 137, 233):
            self.assertIsNotNone(parse_line(frame(raw_status=raw_status)))

    def test_missing_fields_bad_json_and_duplicate_keys_are_rejected(self):
        original = json.loads(frame()[len(PREFIX):])
        for name in original:
            incomplete = original.copy()
            del incomplete[name]
            with self.subTest(missing=name):
                self.assertIsNone(parse_line(PREFIX + json.dumps(incomplete)))
        for body in ("", "{", "null", "[]", "true", "{} garbage",
                     frame()[len(PREFIX):-1] + ',"seq":1}',
                     frame()[len(PREFIX):-1] + ',"extra":NaN}'):
            with self.subTest(body=body):
                self.assertIsNone(parse_line(PREFIX + body))

    def test_state_must_be_a_small_plain_identifier(self):
        for state in (None, 1, "", "x" * 65, "bad\nstate", "bad state", "\u001b[31m"):
            self.assertIsNone(parse_line(frame(state=state)))

    def test_line_length_limit_and_crlf(self):
        line = frame()
        self.assertIsNotNone(parse_line(line + "\r\n"))
        self.assertIsNotNone(parse_line(line + " " * (MAX_LINE_LENGTH - len(line))))
        self.assertIsNone(parse_line(line + " " * (MAX_LINE_LENGTH + 1 - len(line))))


if __name__ == "__main__":
    unittest.main()
