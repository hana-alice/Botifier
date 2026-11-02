import contextlib
import io
import json
import subprocess
import sys
import time
import unittest
from unittest import mock

from pc.codex_weekly.client import AppServerClient, strict_json
from pc.codex_weekly.quota import QuotaError


SERVER = r'''
import json,sys
def receive(): return json.loads(sys.stdin.readline())
first=receive()
assert first['method']=='initialize' and first['id']==0
print('authorization=DO_NOT_ECHO_CHILD_LOG',flush=True)
print(json.dumps({'id':0,'result':{}}),flush=True)
assert receive()['method']=='initialized'
last=receive()
assert last=={'id':1,'method':'account/rateLimits/read'}
reply=REPLY
print(json.dumps(reply),flush=True)
assert sys.stdin.readline()==''
'''


class AppServerTests(unittest.TestCase):
    def fetch(self, reply):
        command = [sys.executable, '-u', '-c', SERVER.replace('REPLY', repr(reply))]
        return AppServerClient(command=command, timeout=3).fetch()

    def test_actual_stdio_handshake_only_reads_metadata_and_discards_logs(self):
        reply = {'id': 1, 'result': {'rateLimitsByLimitId': {'codex': {'primary': {
            'usedPercent': 24, 'windowDurationMins': 10080, 'resetsAt': 0xffffffff}}}}}
        stdout, stderr = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(stdout), contextlib.redirect_stderr(stderr):
            quota = self.fetch(reply)
        self.assertEqual(quota.remaining_percent, 76)
        self.assertEqual(stdout.getvalue() + stderr.getvalue(), '')

    def test_upstream_error_keeps_numeric_code_but_discards_message(self):
        with self.assertRaises(QuotaError) as failure:
            self.fetch({'id': 1, 'error': {'code': -32000, 'message': 'token=DO_NOT_ECHO'}})
        self.assertEqual((failure.exception.code, failure.exception.rpc_code), ('rpc_error', -32000))
        self.assertNotIn('DO_NOT_ECHO', str(failure.exception))

    def test_timeout_terminates_only_owned_child(self):
        processes = []
        real_popen = subprocess.Popen
        def spawn(*args, **kwargs):
            process = real_popen(*args, **kwargs)
            processes.append(process)
            return process
        started = time.monotonic()
        with mock.patch('pc.codex_weekly.client.subprocess.Popen', side_effect=spawn):
            with self.assertRaises(QuotaError) as failure:
                AppServerClient([sys.executable, '-c', 'import time;time.sleep(60)'], timeout=0.1).fetch()
        self.assertEqual(failure.exception.code, 'server_timeout')
        self.assertLess(time.monotonic() - started, 4)
        self.assertEqual(len(processes), 1)
        self.assertIsNotNone(processes[0].poll())

    def test_process_start_or_early_exit_are_safe_failures(self):
        with mock.patch('pc.codex_weekly.client.subprocess.Popen', side_effect=OSError('password=DO_NOT_ECHO')):
            with self.assertRaises(QuotaError) as failure:
                AppServerClient(['fake']).fetch()
        self.assertEqual(failure.exception.code, 'server_start_failed')
        with self.assertRaises(QuotaError):
            AppServerClient([sys.executable, '-c', 'pass'], timeout=1).fetch()

    def test_rpc_json_rejects_duplicates_and_nonfinite_even_unknown_fields(self):
        for line in (b'{"id":1,"id":1}', b'{"unused":NaN}', b'{"unused":1e999}'):
            with self.subTest(line=line), self.assertRaises(ValueError):
                strict_json(line)
