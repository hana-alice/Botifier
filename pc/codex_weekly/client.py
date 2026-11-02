"""Read-only Codex app-server stdio client; no thread/turn or auth-file access."""

import json
import math
import os
from pathlib import Path
import platform
import queue
import shutil
import subprocess
import threading
import time

from .quota import QuotaError, select_weekly


MAX_RPC_LINE = 65536


def native_command():
    # The npm JavaScript entry spawns a native child. Launch that same installed
    # binary directly so timeout cleanup owns exactly one server process.
    if os.name == 'nt':
        arch = 'aarch64' if platform.machine().lower() in ('arm64', 'aarch64') else 'x86_64'
        package_arch = 'arm64' if arch == 'aarch64' else 'x64'
        appdata = Path(os.environ.get('APPDATA', Path.home() / 'AppData/Roaming'))
        npm_roots = [appdata / 'npm']
        wrapper = shutil.which('codex.cmd') or shutil.which('codex')
        if wrapper:
            npm_roots.insert(0, Path(wrapper).parent)
        for npm in npm_roots:
            package = npm / 'node_modules/@openai/codex'
            for vendor in (package / f'node_modules/@openai/codex-win32-{package_arch}/vendor', package / 'vendor'):
                executable = vendor / f'{arch}-pc-windows-msvc/bin/codex.exe'
                if executable.is_file():
                    return [str(executable), 'app-server', '--stdio']
    else:
        executable = shutil.which('codex')
        if executable:
            with open(executable, 'rb') as source:
                if source.read(2) != b'#!':
                    return [executable, 'app-server', '--stdio']
    raise QuotaError('codex_native_unavailable')


def strict_json(line):
    def pairs(items):
        result = {}
        for key, value in items:
            if key in result:
                raise ValueError('duplicate')
            result[key] = value
        return result

    def finite(value):
        value = float(value)
        if not math.isfinite(value):
            raise ValueError('nonfinite')
        return value

    def constant(_):
        raise ValueError('nonfinite')

    return json.loads(line, object_pairs_hook=pairs, parse_float=finite, parse_constant=constant)


def close_server(process, reader):
    try:
        process.stdin.close()
    except (OSError, ValueError):
        pass
    try:
        try:
            process.wait(timeout=0.75)
        except subprocess.TimeoutExpired:
            try:
                process.terminate()
            except OSError:
                pass
            try:
                process.wait(timeout=2)
            except subprocess.TimeoutExpired:
                try:
                    process.kill()
                except OSError:
                    pass
                process.wait(timeout=2)
    except (OSError, subprocess.TimeoutExpired):
        raise QuotaError('server_cleanup_failed') from None
    finally:
        reader.join(timeout=1)
        if not reader.is_alive():
            process.stdout.close()


class AppServerClient:
    def __init__(self, command=None, timeout=35, wall=time.time, monotonic=time.monotonic):
        self.command, self.timeout = command, timeout
        self.wall, self.monotonic = wall, monotonic

    def fetch(self):
        command = self.command if self.command is not None else native_command()
        try:
            process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                stderr=subprocess.DEVNULL, shell=False, creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
        except OSError:
            raise QuotaError('server_start_failed') from None
        messages = queue.Queue(maxsize=16)
        overflow = threading.Event()

        def receive():
            discarding = False
            try:
                while True:
                    line = process.stdout.readline(MAX_RPC_LINE + 1)
                    if not line:
                        break
                    if len(line) > MAX_RPC_LINE or discarding:
                        discarding = not line.endswith(b'\n')
                        continue
                    try:
                        value = strict_json(line)
                    except (ValueError, RecursionError, OverflowError):
                        continue
                    if not isinstance(value, dict) or type(value.get('id')) is not int or value['id'] not in (0, 1):
                        continue
                    try:
                        messages.put_nowait(value)
                    except queue.Full:
                        overflow.set()
                        break
            except (OSError, ValueError):
                pass
            finally:
                try:
                    messages.put_nowait(None)
                except queue.Full:
                    overflow.set()

        reader = threading.Thread(target=receive, name='codex-quota-rpc', daemon=True)
        reader.start()
        deadline = self.monotonic() + self.timeout

        def send(value):
            try:
                process.stdin.write(json.dumps(value, allow_nan=False).encode('utf-8') + b'\n')
                process.stdin.flush()
            except (OSError, ValueError):
                raise QuotaError('server_closed') from None

        def response(request_id):
            while True:
                if overflow.is_set():
                    raise QuotaError('rpc_overflow')
                remaining = deadline - self.monotonic()
                if remaining <= 0:
                    raise QuotaError('server_timeout')
                try:
                    value = messages.get(timeout=remaining)
                except queue.Empty:
                    raise QuotaError('server_timeout') from None
                if value is None:
                    raise QuotaError('server_closed')
                if value['id'] != request_id:
                    continue
                if 'error' in value:
                    code = value['error'].get('code') if isinstance(value['error'], dict) else None
                    if type(code) is not int or not -(1 << 31) <= code < (1 << 31):
                        code = None
                    raise QuotaError('rpc_error', code)
                if not isinstance(value.get('result'), dict):
                    raise QuotaError('invalid_rpc_result')
                return value['result']

        try:
            send(dict(id=0, method='initialize', params=dict(clientInfo=dict(name='stackchan_weekly', version='0.1.0'))))
            response(0)
            send(dict(method='initialized', params={}))
            send(dict(id=1, method='account/rateLimits/read'))
            result = response(1)
            return select_weekly(result, int(self.wall()), self.monotonic())
        finally:
            close_server(process, reader)
