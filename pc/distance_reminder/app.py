"""Console application, serial lifecycle, simulation, and asynchronous alerts."""

import argparse
from dataclasses import asdict, fields
from decimal import Decimal, InvalidOperation
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time

from .policy import Config, ReminderPolicy
from .protocol import MAX_LINE_LENGTH, PREFIX, Sample, parse_line

DEFAULT_SETTINGS = Path(__file__).resolve().parents[1] / "reminder-settings.json"
NOTIFICATION_SCRIPT = Path(__file__).resolve().parents[1] / "Show-Notification.ps1"


class LineBuffer:
    """Keep at most one bounded partial line; overflow is discarded to newline."""

    def __init__(self):
        self._pending = bytearray()
        self._discarding = False

    def feed(self, chunk: bytes) -> list[str]:
        lines = []
        for byte in chunk:
            if byte == 10:
                if not self._discarding:
                    try:
                        lines.append(self._pending.decode("utf-8"))
                    except UnicodeDecodeError:
                        pass
                self._pending.clear()
                self._discarding = False
            elif not self._discarding:
                if len(self._pending) == MAX_LINE_LENGTH:
                    self._pending.clear()
                    self._discarding = True
                else:
                    self._pending.append(byte)
        return lines


class FrameProcessor:
    """Track new protocol frames separately from sensor validity and serial I/O."""

    def __init__(self, policy: ReminderPolicy):
        self.policy = policy
        self.sample: Sample | None = None
        self.last_frame_ms: int | None = None
        self._seq: int | None = None
        self._uptime_ms: int | None = None

    def disconnect(self):
        self.policy.invalidate()
        self.sample = None
        self.last_frame_ms = None
        self._seq = None
        self._uptime_ms = None

    def feed(self, sample: Sample, now_ms: int) -> bool:
        if self._seq is not None:
            if sample.seq < self._seq or sample.uptime_ms < self._uptime_ms:
                self.policy.invalidate()
            elif sample.seq == self._seq:
                return False
        self._seq = sample.seq
        self._uptime_ms = sample.uptime_ms
        self.last_frame_ms = now_ms
        self.sample = sample
        return self.policy.feed(sample.distance_mm if sample.valid else None, now_ms)

    def expire(self, now_ms: int) -> bool:
        if (self.last_frame_ms is not None
                and now_ms - self.last_frame_ms > self.policy.config.sample_timeout_ms):
            self.policy.invalidate()
            self.sample = None
            self.last_frame_ms = None
            return True
        return False


class Notifier:
    def __init__(self, enabled: bool, sound: bool):
        self.enabled = enabled
        self.sound = sound
        self.process = None

    def poll(self):
        if self.process is not None:
            code = self.process.poll()
            if code is not None:
                if code != 0:
                    print(f"Notification helper failed (exit code {code}); console ALERT remains available.",
                          file=sys.stderr, flush=True)
                self.process = None

    def notify(self, distance_mm: int, simulated: bool = False):
        self.poll()
        if not self.enabled or self.process is not None:
            return
        powershell = Path(os.environ.get("SystemRoot", r"C:\Windows")) / (
            "System32/WindowsPowerShell/v1.0/powershell.exe")
        command = [str(powershell), "-NoProfile", "-NonInteractive", "-STA", "-ExecutionPolicy", "Bypass",
                   "-File", str(NOTIFICATION_SCRIPT), "-DistanceMm", str(distance_mm)]
        if simulated:
            command.append("-Simulated")
        if self.sound:
            command.append("-Sound")
        try:
            self.process = subprocess.Popen(
                command, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL, shell=False,
                creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        except OSError as error:
            print(f"Cannot start Windows notification helper: {error}", file=sys.stderr, flush=True)


class Reporter:
    def __init__(self):
        self._last_key = None
        self._last_ms = None
        self._last_line = None

    def status(self, connection: str, processor: FrameProcessor, now_ms: int, force: bool = False):
        sample, policy = processor.sample, processor.policy
        sensor = sample.state if sample else "waiting_data"
        robot_alert = ("unknown" if sample is None or sample.robot_alert is None
                       else "on" if sample.robot_alert else "off")
        key = (connection, policy.state, sensor, robot_alert)
        raw = sample.distance_mm if sample and sample.valid else "-"
        filtered = policy.filtered_mm if policy.filtered_mm is not None else "-"
        line = (f"connection={connection} raw_mm={raw} filtered_mm={filtered} "
                f"near_ms={policy.near_elapsed_ms}/{policy.config.near_duration_ms} "
                f"state={policy.state} sensor={sensor} robot_alert={robot_alert}")
        if not force:
            if line == self._last_line:
                return
            if (key == self._last_key and self._last_ms is not None
                    and now_ms - self._last_ms < 1000):
                return
        self._last_key, self._last_ms, self._last_line = key, now_ms, line
        print(line, flush=True)


def _scaled_integer(value: str, multiplier: int) -> int:
    try:
        scaled = Decimal(value) * multiplier
        if not scaled.is_finite() or scaled != scaled.to_integral_value():
            raise ValueError
        return int(scaled)
    except (InvalidOperation, ValueError, OverflowError):
        raise argparse.ArgumentTypeError("Use a finite value with millimetre/millisecond precision") from None


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="StackChan USB distance reminder")
    parser.add_argument("--port", help="Serial port; otherwise find the unique USB VID303A/PID1001")
    parser.add_argument("--threshold-cm", type=lambda value: _scaled_integer(value, 10), default=None)
    parser.add_argument("--hysteresis-cm", type=lambda value: _scaled_integer(value, 10), default=None)
    parser.add_argument("--hold-seconds", type=lambda value: _scaled_integer(value, 1000), default=None)
    parser.add_argument("--cooldown-seconds", type=lambda value: _scaled_integer(value, 1000), default=None)
    parser.add_argument("--settings", type=Path, default=DEFAULT_SETTINGS)
    parser.add_argument("--sound", action=argparse.BooleanOptionalAction, default=None)
    parser.add_argument("--simulate", action="store_true", help="Use virtual time; never open serial or save settings")
    parser.add_argument("--no-notify", action="store_true", help="Only print console ALERT messages")
    return parser


def load_settings(path: Path) -> dict:
    try:
        with path.open("rb") as handle:
            content = handle.read(16_385)
    except FileNotFoundError:
        return {}
    if len(content) > 16_384:
        raise ValueError("Settings file exceeds 16 KiB")
    value = json.loads(content)
    if not isinstance(value, dict):
        raise ValueError("Settings must contain a JSON object")
    allowed = {field.name for field in fields(Config)} | {"port", "sound"}
    if value.keys() - allowed:
        raise ValueError("Settings contain unknown fields")
    return value


def effective_settings(args) -> tuple[Config, str | None, bool]:
    saved = load_settings(args.settings.expanduser())
    names = {field.name for field in fields(Config)}
    values = {key: value for key, value in saved.items() if key in names}
    for source, target in (("threshold_cm", "threshold_mm"), ("hysteresis_cm", "hysteresis_mm"),
                           ("hold_seconds", "near_duration_ms"), ("cooldown_seconds", "cooldown_ms")):
        value = getattr(args, source)
        if value is not None:
            values[target] = value
    config = Config(**values)
    port = args.port if args.port is not None else saved.get("port")
    sound = args.sound if args.sound is not None else saved.get("sound", False)
    if port is not None and (not isinstance(port, str) or not port.strip()
                             or len(port) > 128 or any(ord(ch) < 32 for ch in port)):
        raise ValueError("port must be a non-empty serial port name or null")
    if type(sound) is not bool:
        raise ValueError("sound must be true or false")
    return config, port, sound


def save_settings(path: Path, config: Config, port: str | None, sound: bool):
    """Replace only after a complete validated settings file is on disk."""
    path = path.expanduser()
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(mode="w", encoding="utf-8", dir=path.parent,
                                         prefix=f".{path.name}.", suffix=".tmp", delete=False) as handle:
            temporary = Path(handle.name)
            json.dump({**asdict(config), "port": port, "sound": sound}, handle, indent=2)
            handle.write("\n")
            handle.flush()
            os.fsync(handle.fileno())
        os.replace(temporary, path)
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


def _alert(processor: FrameProcessor, notifier: Notifier, simulated: bool = False):
    distance = processor.policy.filtered_mm
    label = "SIMULATED ALERT" if simulated else "ALERT"
    print(f"{label}: distance={distance} mm; close for "
          f"{processor.policy.near_elapsed_ms} ms. Consider moving back.", flush=True)
    notifier.notify(distance, simulated)


def simulation(config: Config, notifier: Notifier) -> int:
    if config.threshold_mm == 40:
        raise ValueError("Simulation needs threshold above 40 mm to produce a valid below-threshold sample")
    print("SIMULATED: safe 1s -> near for hold+1s -> invalid -> safe; virtual time, no serial.", flush=True)
    processor = FrameProcessor(ReminderPolicy(config))
    reporter = Reporter()
    safe = min(4000, config.threshold_mm + config.hysteresis_mm + 100)
    near = max(40, config.threshold_mm - 100)
    step = min(100, config.sample_timeout_ms)
    near_end = 1000 + config.near_duration_ms + 1000
    alerts, seq = 0, 0
    for start, end, distance in ((0, 1000, safe), (1000, near_end, near),
                                 (near_end, near_end + step, None),
                                 (near_end + step, near_end + 1000, safe)):
        for now_ms in range(start, end, step):
            line = PREFIX + json.dumps({"v": 1, "seq": seq, "uptime_ms": now_ms,
                "valid": distance is not None, "distance_mm": distance,
                "raw_status": 9 if distance is not None else 0,
                "state": "ranging", "error": 0})
            seq += 1
            sample = parse_line(line)
            if sample is None:
                raise RuntimeError("Internal simulated frame failed protocol validation")
            previous = processor.policy.state
            if processor.feed(sample, now_ms):
                alerts += 1
                _alert(processor, notifier, simulated=True)
            if processor.policy.state != previous or seq == 1:
                reporter.status("SIMULATED", processor, now_ms, force=True)
    reporter.status("SIMULATED", processor, near_end + 1000, force=True)
    print(f"SIMULATED complete: {alerts} reminder(s).", flush=True)
    if alerts == 0:
        raise RuntimeError("Simulation did not produce a reminder")
    return 0


def _keyboard():
    if os.name != "nt" or not sys.stdin.isatty():
        return None
    import msvcrt
    if msvcrt.kbhit():
        key = msvcrt.getwch()
        if key in ("\x00", "\xe0"):
            if msvcrt.kbhit():
                msvcrt.getwch()
            return None
        return key.lower()
    return None


def _choose_port(explicit_port: str | None, ports) -> str | None:
    if explicit_port:
        return explicit_port
    matches = [port.device for port in ports if port.vid == 0x303A and port.pid == 0x1001]
    if len(matches) > 1:
        raise ValueError("Multiple StackChan USB devices found; specify --port")
    return matches[0] if matches else None


def run_serial(config: Config, port: str | None, notifier: Notifier) -> int:
    try:
        import serial
        from serial.tools import list_ports
    except ImportError:
        raise RuntimeError("pyserial is unavailable. Use the existing Espressif Python environment launcher.") from None
    policy = ReminderPolicy(config)
    processor, reporter = FrameProcessor(policy), Reporter()
    connection, buffer = None, LineBuffer()
    next_retry_ms, last_notice = 0, None
    print("P: pause/resume; Q: quit. Waiting for new firmware distance data (SC_DISTANCE).", flush=True)
    try:
        while True:
            now_ms = time.monotonic_ns() // 1_000_000
            notifier.poll()
            key = _keyboard()
            if key == "q":
                return 0
            if key == "p":
                policy.set_paused(policy.state != "paused", now_ms)
                reporter.status(str(connection.port) if connection is not None else "disconnected",
                                processor, now_ms, force=True)
            if connection is None:
                if now_ms < next_retry_ms:
                    time.sleep(0.1)
                    continue
                next_retry_ms = now_ms + 2000
                selected = _choose_port(port, list_ports.comports() if not port else ())
                if selected is None:
                    notice = "Waiting for USB VID303A/PID1001; retrying every 2 seconds."
                    if notice != last_notice:
                        print(notice, flush=True)
                        last_notice = notice
                    continue
                candidate = None
                try:
                    candidate = serial.Serial(port=None, baudrate=115200, timeout=0.25)
                    candidate.dtr = True
                    candidate.rts = False
                    candidate.port = selected
                    candidate.open()
                    connection = candidate
                    buffer = LineBuffer()
                    processor.disconnect()
                    last_notice = None
                    print(f"Serial connected: {selected}. Waiting for new firmware distance data (SC_DISTANCE).",
                          flush=True)
                except (serial.SerialException, OSError) as error:
                    if candidate is not None:
                        candidate.close()
                    notice = f"Cannot open {selected}: {error}. Retrying every 2 seconds."
                    if notice != last_notice:
                        print(notice, file=sys.stderr, flush=True)
                        last_notice = notice
                    continue
            try:
                chunk = connection.read(256)
                now_ms = time.monotonic_ns() // 1_000_000
                for line in buffer.feed(chunk):
                    sample = parse_line(line)
                    if sample is not None and processor.feed(sample, now_ms):
                        _alert(processor, notifier)
                processor.expire(now_ms)
                reporter.status(str(connection.port), processor, now_ms)
            except (serial.SerialException, OSError) as error:
                print(f"Serial disconnected: {error}. Retrying every 2 seconds.", file=sys.stderr, flush=True)
                connection.close()
                connection = None
                buffer = LineBuffer()
                processor.disconnect()
                reporter.status("disconnected", processor, now_ms, force=True)
                next_retry_ms = now_ms + 2000
    finally:
        if connection is not None:
            connection.close()


def main(argv=None) -> int:
    args = build_parser().parse_args(argv)
    try:
        config, port, sound = effective_settings(args)
        notifier = Notifier(enabled=not args.no_notify, sound=sound)
        if args.simulate:
            return simulation(config, notifier)
        save_settings(args.settings, config, port, sound)
        return run_serial(config, port, notifier)
    except KeyboardInterrupt:
        print("Stopped.", flush=True)
        return 0
    except (OSError, TypeError, ValueError, RuntimeError) as error:
        print(f"ERROR: {error}", file=sys.stderr, flush=True)
        return 1
