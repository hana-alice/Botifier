import contextlib
import io
import json
from pathlib import Path
import subprocess
import sys
import tempfile
from types import ModuleType, SimpleNamespace
import unittest
from unittest import mock

from pc.distance_reminder import app
from pc.distance_reminder.policy import Config, ReminderPolicy
from pc.distance_reminder.protocol import PREFIX, parse_line


PROJECT_ROOT = Path(__file__).resolve().parents[2]


def frame(seq, uptime_ms=0, valid=True, distance_mm=300):
    return PREFIX + json.dumps({"v": 1, "seq": seq, "uptime_ms": uptime_ms,
        "valid": valid, "distance_mm": distance_mm, "raw_status": 9,
        "state": "ranging", "error": 0})


class BufferTests(unittest.TestCase):
    def test_partial_chunks_are_not_new_measurements(self):
        buffer = app.LineBuffer()
        line = frame(1).encode()
        self.assertEqual(buffer.feed(line[:30]), [])
        self.assertEqual(buffer.feed(line[30:]), [])
        self.assertEqual(buffer.feed(b"\r\n"), [line.decode() + "\r"])

    def test_overlong_and_invalid_utf8_lines_are_discarded_then_reading_recovers(self):
        buffer = app.LineBuffer()
        self.assertEqual(buffer.feed(b"x" * 100_000), [])
        self.assertEqual(buffer.feed(b"\n\xff\n" + frame(2).encode() + b"\n"), [frame(2)])

    def test_multiple_frames_can_arrive_in_one_chunk(self):
        self.assertEqual(app.LineBuffer().feed((frame(1) + "\n" + frame(2) + "\n").encode()),
                         [frame(1), frame(2)])


class ProcessorTests(unittest.TestCase):
    def setUp(self):
        self.processor = app.FrameProcessor(ReminderPolicy(Config(
            near_duration_ms=500, cooldown_ms=2000, sample_timeout_ms=250)))

    def feed(self, seq, now_ms, **values):
        return self.processor.feed(parse_line(frame(seq, **values)), now_ms)

    def test_device_uptime_never_accelerates_pc_dwell(self):
        reminders = [now for seq, now in enumerate(range(0, 801, 100))
                     if self.feed(seq, now, uptime_ms=seq * 1_000_000)]
        self.assertEqual(reminders, [700])

    def test_duplicate_sequence_neither_refills_filter_nor_keeps_connection_fresh(self):
        self.feed(1, 0)
        self.feed(2, 100)
        self.feed(3, 200)
        self.assertFalse(self.feed(3, 400, uptime_ms=1000))
        self.assertTrue(self.processor.expire(451))
        self.assertEqual(self.processor.policy.state, "waiting")
        self.assertIsNone(self.processor.sample)
        self.assertFalse(self.feed(3, 500, uptime_ms=1001))
        self.assertIsNone(self.processor.sample)

    def test_sequence_or_device_uptime_reset_breaks_proximity(self):
        for reset_seq, reset_uptime in ((0, 10_000), (14, 0)):
            with self.subTest(reset_seq=reset_seq, reset_uptime=reset_uptime):
                self.setUp()
                for offset in range(4):
                    self.feed(offset + 10, offset * 100, uptime_ms=offset * 1000 + 1000)
                self.assertFalse(self.feed(reset_seq, 400, uptime_ms=reset_uptime))
                self.assertEqual(self.processor.policy.state, "waiting")
                self.assertIsNone(self.processor.policy.filtered_mm)

    def test_invalid_measurement_is_not_a_distance_even_when_numeric(self):
        for seq in range(3):
            self.feed(seq, seq * 100)
        self.assertFalse(self.feed(3, 300, valid=False, distance_mm=300))
        self.assertEqual(self.processor.policy.state, "waiting")
        self.assertIsNone(self.processor.policy.filtered_mm)

    def test_disconnect_clears_sequence_without_bypassing_cooldown(self):
        self.assertTrue(any(self.feed(seq, seq * 100) for seq in range(8)))
        self.processor.disconnect()
        self.assertIsNone(self.processor.sample)
        reminders = [now for seq, now in enumerate(range(800, 2801, 100)) if self.feed(seq, now)]
        self.assertEqual(reminders, [2700])


class SettingsAndSimulationTests(unittest.TestCase):
    def test_cli_overrides_saved_values_and_keeps_other_settings(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "settings.json"
            path.write_text(json.dumps({"threshold_mm": 300, "hysteresis_mm": 50,
                "near_duration_ms": 1000, "cooldown_ms": 10000, "port": "COM9", "sound": True}))
            args = app.build_parser().parse_args(["--settings", str(path), "--threshold-cm", "45.5",
                "--hold-seconds", "2", "--port", "COM38", "--no-sound"])
            config, port, sound = app.effective_settings(args)
            self.assertEqual((config.threshold_mm, config.hysteresis_mm, config.near_duration_ms,
                              config.cooldown_ms, port, sound), (455, 50, 2000, 10000, "COM38", False))
            app.save_settings(path, config, port, sound)
            self.assertEqual(json.loads(path.read_text())["threshold_mm"], 455)

    def test_invalid_config_and_failed_save_never_open_serial_or_replace_settings(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "settings.json"
            original = '{"threshold_mm":400}'
            path.write_text(original)
            with mock.patch.object(app, "run_serial") as serial_runner, contextlib.redirect_stderr(io.StringIO()):
                self.assertEqual(app.main(["--settings", str(path), "--threshold-cm", "500"]), 1)
                with mock.patch.object(app.os, "replace", side_effect=PermissionError("read-only settings")):
                    self.assertEqual(app.main(["--settings", str(path)]), 1)
                serial_runner.assert_not_called()
            self.assertEqual(path.read_text(), original)
            self.assertEqual(list(Path(directory).iterdir()), [path])

    def test_bad_saved_settings_are_reported_without_overwriting_them(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "settings.json"
            for saved in ({"sound": 1}, {"port": True}, {"sample_timeout_ms": True},
                          {"unknown": 1}, [], "broken JSON"):
                with self.subTest(saved=saved):
                    original = json.dumps(saved) if not isinstance(saved, str) else saved
                    path.write_text(original)
                    with mock.patch.object(app, "run_serial") as serial_runner, \
                            contextlib.redirect_stderr(io.StringIO()) as output:
                        self.assertEqual(app.main(["--settings", str(path), "--no-notify"]), 1)
                        serial_runner.assert_not_called()
                    self.assertIn("ERROR:", output.getvalue())
                    self.assertEqual(path.read_text(), original)

    def test_simulation_uses_protocol_and_policy_without_importing_serial_or_saving(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "settings.json"
            path.write_text('{"threshold_mm":400,"sound":true,"port":"COM999"}')
            original = path.read_bytes()
            script = (
                "import sys\n"
                "class RejectSerial:\n"
                " def find_spec(self, fullname, path=None, target=None):\n"
                "  if fullname == 'serial' or fullname.startswith('serial.'):\n"
                "   raise AssertionError('simulation imported serial')\n"
                "sys.meta_path.insert(0, RejectSerial())\n"
                "from pc.distance_reminder.app import main\n"
                "raise SystemExit(main(sys.argv[1:]))\n"
            )
            result = subprocess.run([sys.executable, "-c", script, "--simulate", "--no-notify",
                                     "--settings", str(path)], cwd=PROJECT_ROOT,
                                    capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn("SIMULATED ALERT", result.stdout)
            self.assertIn("state=near", result.stdout)
            self.assertIn("state=waiting", result.stdout)
            self.assertIn("state=safe", result.stdout)
            self.assertEqual(path.read_bytes(), original)

    def test_simulation_never_creates_a_settings_file(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "new.json"
            with contextlib.redirect_stdout(io.StringIO()), mock.patch.object(app.subprocess, "Popen") as spawn:
                self.assertEqual(app.main(["--simulate", "--no-notify", "--settings", str(path)]), 0)
                spawn.assert_not_called()
            self.assertFalse(path.exists())

    def test_units_reject_nonfinite_and_unrepresentable_values(self):
        for flag, value in (("--threshold-cm", "NaN"), ("--hold-seconds", "inf"),
                            ("--threshold-cm", "40.01"), ("--hold-seconds", "0.0001")):
            with self.subTest(flag=flag, value=value), contextlib.redirect_stderr(io.StringIO()):
                with self.assertRaises(SystemExit):
                    app.build_parser().parse_args([flag, value])


class NotificationTests(unittest.TestCase):
    def test_only_one_hidden_notification_process_runs_at_once(self):
        process = mock.Mock()
        process.poll.return_value = None
        with mock.patch.object(app.subprocess, "Popen", return_value=process) as spawn:
            notifier = app.Notifier(True, False)
            notifier.notify(300, simulated=True)
            notifier.notify(250)
            spawn.assert_called_once()
            args, options = spawn.call_args
            self.assertIsInstance(args[0], list)
            self.assertIn("-Simulated", args[0])
            self.assertNotIn("-Sound", args[0])
            self.assertFalse(options["shell"])
            self.assertEqual(options["creationflags"], getattr(subprocess, "CREATE_NO_WINDOW", 0))
            process.poll.return_value = 0
            notifier.notify(200)
            self.assertEqual(spawn.call_count, 2)

    def test_sound_is_explicit_and_disabled_notifications_never_spawn(self):
        with mock.patch.object(app.subprocess, "Popen") as spawn:
            app.Notifier(False, True).notify(300)
            spawn.assert_not_called()
            app.Notifier(True, True).notify(300)
            self.assertIn("-Sound", spawn.call_args.args[0])

    def test_helper_errors_remain_visible_without_blocking_serial(self):
        with mock.patch.object(app.subprocess, "Popen", side_effect=OSError("cannot launch")), \
                contextlib.redirect_stderr(io.StringIO()) as output:
            app.Notifier(True, False).notify(300)
        self.assertIn("Cannot start", output.getvalue())
        notifier = app.Notifier(True, False)
        notifier.process = mock.Mock()
        notifier.process.poll.return_value = 1
        with contextlib.redirect_stderr(io.StringIO()) as output:
            notifier.poll()
        self.assertIn("exit code 1", output.getvalue())
        self.assertIsNone(notifier.process)


class SerialBoundaryTests(unittest.TestCase):
    def test_missing_usb_retries_every_two_seconds_without_repeating_status(self):
        serial = ModuleType("serial")
        serial.SerialException = type("SerialException", (Exception,), {})
        serial.Serial = mock.Mock()
        tools = ModuleType("serial.tools")
        tools.list_ports = SimpleNamespace(comports=mock.Mock(return_value=[]))
        with mock.patch.dict(sys.modules, {"serial": serial, "serial.tools": tools}), \
                mock.patch.object(app, "_keyboard", side_effect=[None] * 5 + ["q"]), \
                mock.patch.object(app.time, "monotonic_ns", side_effect=[i * 1_000_000_000 for i in range(6)]), \
                mock.patch.object(app.time, "sleep") as sleep, \
                contextlib.redirect_stdout(io.StringIO()) as output:
            self.assertEqual(app.run_serial(Config(), None, app.Notifier(False, False)), 0)
        serial.Serial.assert_not_called()
        self.assertEqual(tools.list_ports.comports.call_count, 3)
        self.assertEqual(sleep.call_count, 2)
        self.assertEqual(output.getvalue().count("Waiting for USB"), 1)

    def test_unchanged_waiting_status_is_not_repeated(self):
        reporter = app.Reporter()
        processor = app.FrameProcessor(ReminderPolicy(Config()))
        with contextlib.redirect_stdout(io.StringIO()) as output:
            for now in (0, 1000, 2000, 60_000):
                reporter.status("COM38", processor, now)
        self.assertEqual(len(output.getvalue().splitlines()), 1)

    def test_auto_selection_requires_one_matching_usb_device(self):
        a = SimpleNamespace(device="COM38", vid=0x303A, pid=0x1001)
        b = SimpleNamespace(device="COM39", vid=0x303A, pid=0x1001)
        other = SimpleNamespace(device="COM1", vid=1, pid=2)
        self.assertIsNone(app._choose_port(None, [other]))
        self.assertEqual(app._choose_port(None, [a, other]), "COM38")
        with self.assertRaises(ValueError):
            app._choose_port(None, [a, b])
        self.assertEqual(app._choose_port("COM99", [a, b]), "COM99")

    def test_serial_control_lines_are_set_before_open_and_ordinary_logs_are_not_printed(self):
        events = []

        class FakeSerial:
            def __setattr__(self, name, value):
                events.append((name, value))
                object.__setattr__(self, name, value)

            def open(self):
                events.append(("open", self.port, self.dtr, self.rts))

            def read(self, size):
                return b"WiFi password=never-forward-this\n"

            def close(self):
                events.append(("close",))

        serial = ModuleType("serial")
        serial.SerialException = type("SerialException", (Exception,), {})
        serial.Serial = mock.Mock(return_value=FakeSerial())
        tools = ModuleType("serial.tools")
        tools.list_ports = SimpleNamespace(comports=mock.Mock(return_value=[]))
        with mock.patch.dict(sys.modules, {"serial": serial, "serial.tools": tools}), \
                mock.patch.object(app, "_keyboard", side_effect=[None, None, "q"]), \
                mock.patch.object(app.time, "monotonic_ns", return_value=0), \
                contextlib.redirect_stdout(io.StringIO()) as output:
            self.assertEqual(app.run_serial(Config(), "COM38", app.Notifier(False, False)), 0)
        serial.Serial.assert_called_once_with(port=None, baudrate=115200, timeout=0.25)
        self.assertEqual(events[:4], [("dtr", True), ("rts", False), ("port", "COM38"),
                                     ("open", "COM38", True, False)])
        self.assertEqual(events[-1], ("close",))
        self.assertNotIn("never-forward-this", output.getvalue())
        self.assertIn("Waiting for new firmware distance data", output.getvalue())


if __name__ == "__main__":
    unittest.main()
