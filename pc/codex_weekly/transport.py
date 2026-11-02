"""One serial owner continuously drains device logs without printing them."""

import os
import sys
import threading
import time


def choose_port(explicit, ports):
    if explicit:
        return explicit
    matches = [item.device for item in ports if item.vid == 0x303a and item.pid == 0x1001]
    if len(matches) > 1:
        raise ValueError('usb_ambiguous')
    return matches[0] if matches else None


class SerialLink:
    def __init__(self, store, port=None, serial_module=None, ports_provider=None,
                 monotonic=time.monotonic, notice=None):
        self.store, self.port, self.serial = store, port, serial_module
        self.ports_provider, self.monotonic = ports_provider, monotonic
        self.notice = notice or (lambda code: print('Codex weekly USB: ' + code, file=sys.stderr, flush=True))
        self.connection = None
        self.next_retry = self.next_send = 0
        self.last_sequence = self.last_notice = None
        self._stop = threading.Event()
        self._thread = None

    def _notice(self, code):
        if code != self.last_notice:
            self.notice(code)
            self.last_notice = code

    def _close(self):
        if self.connection is not None:
            try:
                self.connection.close()
            except OSError:
                pass
            self.connection = None

    def step(self):
        now = self.monotonic()
        if self.connection is None:
            if now < self.next_retry:
                return
            self.next_retry = now + 3
            candidate = None
            try:
                selected = choose_port(self.port, self.ports_provider() if not self.port else ())
                if selected is None:
                    self._notice('waiting_for_device')
                    return
                options = dict(port=None, baudrate=115200, timeout=0.2, write_timeout=1)
                if os.name == 'posix':
                    options['exclusive'] = True
                candidate = self.serial.Serial(**options)
                candidate.dtr, candidate.rts, candidate.port = True, False, selected
                candidate.open()
                self.connection = candidate
                self.last_sequence = None
                self.next_send = now
                self._notice('connected')
            except (OSError, ValueError, self.serial.SerialException):
                if candidate is not None:
                    try:
                        candidate.close()
                    except OSError:
                        pass
                self._notice('port_busy_unavailable_or_ambiguous')
                return
        try:
            # Deliberately discard every byte, including credentials in boot logs.
            self.connection.read(min(max(self.connection.in_waiting, 1), 4096))
            sequence, payload = self.store.packet()
            if sequence != self.last_sequence or now >= self.next_send:
                if self.connection.write(payload) != len(payload):
                    raise OSError('short_write')
                self.last_sequence = sequence
                self.next_send = self.monotonic() + 15
        except (OSError, ValueError, self.serial.SerialException):
            self._close()
            self.next_retry = self.monotonic() + 3
            self._notice('disconnected')

    def start(self):
        if self.serial is None:
            import serial
            from serial.tools import list_ports
            self.serial, self.ports_provider = serial, list_ports.comports
        def run():
            try:
                while not self._stop.is_set():
                    self.step()
                    self._stop.wait(0.05)
            finally:
                self._close()
        self._thread = threading.Thread(target=run, name='codex-weekly-usb', daemon=True)
        self._thread.start()

    def stop(self):
        self._stop.set()
        if self._thread is not None:
            self._thread.join(timeout=4)
