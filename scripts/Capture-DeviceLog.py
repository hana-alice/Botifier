"""Capture the existing firmware's USB log; never send commands or flash."""

import argparse
from pathlib import Path
import re
import time

import serial


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--seconds", type=int, default=120)
    parser.add_argument("--log", type=Path, required=True)
    args = parser.parse_args()
    if not 1 <= args.seconds <= 600:
        parser.error("--seconds must be between 1 and 600")

    sensitive = re.compile(
        r"password|passwd|passphrase|\bpwd\b|\bpsk\b|token|secret|authorization|api[_-]?key",
        re.IGNORECASE,
    )
    args.log.parent.mkdir(parents=True, exist_ok=True)
    port = serial.Serial(port=None, baudrate=115200, timeout=0.25)
    port.dtr = True
    port.rts = False
    port.port = args.port
    try:
        port.open()
        print(f"Capturing {args.port} for {args.seconds}s; no commands sent.", flush=True)
        deadline = time.monotonic() + args.seconds
        with args.log.open("w", encoding="utf-8") as output:
            pending = b""
            while time.monotonic() < deadline:
                raw = port.read_until()
                if not raw:
                    continue
                pending += raw
                if not pending.endswith(b"\n"):
                    continue
                line = pending.decode("utf-8", errors="replace").rstrip("\r\n")
                pending = b""
                if sensitive.search(line):
                    line = "[REDACTED: credential-bearing log line]"
                print(line, flush=True)
                output.write(line + "\n")
                output.flush()
    finally:
        port.close()


if __name__ == "__main__":
    main()
