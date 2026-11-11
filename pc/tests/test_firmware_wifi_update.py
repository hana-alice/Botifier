import contextlib
import hashlib
import http.client
import importlib.util
import io
import json
from pathlib import Path
import struct
import tempfile
import unittest
from unittest import mock


SCRIPT = Path(__file__).resolve().parents[2] / 'scripts' / 'Update-FirmwareWifi.py'
spec = importlib.util.spec_from_file_location('firmware_wifi_update', SCRIPT)
ota = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ota)
MAC = '02:00:00:00:11:22'  # Synthetic locally administered fixture; never a real device.
KEY = 'a1' * 16
OLD_ELF, NEW_ELF = '11' * 32, '22' * 32


def image_bytes(*, segments=1):
    header = bytearray(24)
    header[0], header[1], header[23] = 0xE9, segments, 1
    struct.pack_into('<H', header, 12, 9)
    desc = bytearray(256)
    struct.pack_into('<I', desc, 0, 0xABCD5432)
    desc[16:19], desc[48:58], desc[144:176] = b'2.0', b'stack-chan', bytes.fromhex(NEW_ELF)
    # Slice lengths must not resize the fixed descriptor.
    assert len(desc) == 256
    payloads = [bytes(desc)] + [b'\x01\x02\x03\x04' * 4] * (segments - 1)
    result, checksum = bytes(header), 0xEF
    for index, payload in enumerate(payloads):
        result += struct.pack('<II', 0x3C000020 + index * 0x1000, len(payload)) + payload
        for value in payload:
            checksum ^= value
    result += bytes((len(result) | 15) - len(result)) + bytes([checksum])
    return result + hashlib.sha256(result).digest()


def before(**kwargs):
    return dict(dict(v=1, mac=MAC, project_name='stack-chan', version='1.0', elf_sha256=OLD_ELF,
                     running_slot='ota_0', boot_slot='ota_0', ota_state=2,
                     max_image_bytes=0x4F0000, upload_supported=True), **kwargs)


def after(**kwargs):
    return before(**dict(dict(elf_sha256=NEW_ELF, running_slot='ota_1', boot_slot='ota_1'), **kwargs))


def acknowledgement(data, **kwargs):
    return dict(dict(accepted=True, bytes=len(data), sha256=hashlib.sha256(data).hexdigest(),
                     target_slot='ota_1', rebooting=True), **kwargs)


class Clock:
    def __init__(self):
        self.now = 0

    def __call__(self):
        return self.now

    def sleep(self, seconds):
        self.now += seconds


class FakeTransport:
    def __init__(self, states, post=None):
        self.states, self.post_result, self.posts, self.gets = list(states), post, [], 0
        self.last = self.states[-1]

    def get(self, deadline):
        self.gets += 1
        state = self.states.pop(0) if self.states else self.last
        if isinstance(state, BaseException):
            raise state
        return state

    def post(self, data, current, image, deadline, progress=None):
        self.posts.append((data, current, image))
        if isinstance(self.post_result, BaseException):
            raise self.post_result
        if progress is not None:
            progress(len(data), len(data))
        return self.post_result or (200, acknowledgement(data), len(data))


class FakeResponse:
    def __init__(self, payload, *, status=200, declared=None, content_type='application/json'):
        self.status = status
        self.data = json.dumps(payload).encode() if not isinstance(payload, bytes) else payload
        self.headers = {'Content-Type': content_type,
                        'Content-Length': str(len(self.data) if declared is None else declared)}

    def getheader(self, key, default=None):
        return self.headers.get(key, default)

    def read1(self, count):
        data, self.data = self.data[:count], self.data[count:]
        return data


class Connections:
    def __init__(self, responses):
        self.responses, self.instances = list(responses), []

    def __call__(self, host, port, timeout):
        owner = self

        class Connection:
            sock = None

            def __init__(self):
                self.host, self.port, self.timeout = host, port, timeout
                self.headers, self.data, self.method, self.path = {}, bytearray(), None, None
                self.closed = False

            def request(self, method, path, headers):
                self.method, self.path, self.headers = method, path, headers

            def putrequest(self, method, path, **_kwargs):
                self.method, self.path = method, path

            def putheader(self, key, value):
                self.headers[key] = value

            def endheaders(self):
                pass

            def send(self, value):
                self.data.extend(value)

            def getresponse(self):
                result = owner.responses.pop(0)
                if isinstance(result, BaseException):
                    raise result
                return result

            def close(self):
                self.closed = True

        connection = Connection()
        self.instances.append(connection)
        return connection


class ImageTests(unittest.TestCase):
    def test_standard_multi_segment_image_descriptor_offsets_and_digest(self):
        for count in (1, 3, 16):
            data = image_bytes(segments=count)
            result = ota.validate_image(data)
            self.assertEqual(result['segments'], count)
            self.assertEqual(result['project_name'], 'stack-chan')
            self.assertEqual(result['elf_sha256'], NEW_ELF)
            self.assertEqual(result['sha256'], hashlib.sha256(data).hexdigest())

    def test_magic_chip_segment_checksum_digest_truncation_and_trailer_rejected(self):
        original = image_bytes()
        for offset in (0, 1, 12, 23, 40, len(original) - 33, len(original) - 1):
            damaged = bytearray(original)
            damaged[offset] ^= 0x55
            with self.assertRaises(ota.UpdateError, msg=offset):
                ota.validate_image(bytes(damaged))
        for data in (original[:287], original[:-1], original + b'\0', b'not firmware' * 40):
            with self.assertRaises(ota.UpdateError):
                ota.validate_image(data)

    def test_invalid_segment_length_rejected_before_network(self):
        data = bytearray(image_bytes())
        struct.pack_into('<I', data, 28, 0x7FFFFFFF)
        transport = FakeTransport([(200, before())])
        clock = Clock()
        report = ota.perform_update(bytes(data), MAC, transport, clock=clock, sleep=clock.sleep)
        self.assertFalse(report['success'])
        self.assertEqual(transport.gets, 0)


class GuardTests(unittest.TestCase):
    def test_key_file_or_env_only_and_no_invalid_key_echo(self):
        self.assertEqual(ota.load_key(environ={'BOTIFIER_DEVICE_KEY': KEY}), KEY)
        for key in ('', 'TOKEN_SECRET', 'a' * 31, 'a' * 33, KEY + '\n'):
            with self.assertRaises(ota.UpdateError) as raised:
                ota.load_key(environ={'BOTIFIER_DEVICE_KEY': key})
            self.assertNotIn('TOKEN_SECRET', str(raised.exception))
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / 'key.txt'
            path.write_bytes(KEY.upper().encode() + b'\r\n')
            self.assertEqual(ota.load_key(path, environ={}), KEY)
            path.write_text('SECRET_NON_KEY')
            with self.assertRaises(ota.UpdateError):
                ota.load_key(path, environ={})

    def test_host_cannot_select_remote_url_port_path_or_redirect(self):
        for host in ('https://192.168.1.2', '192.168.1.2:8765', '192.168.1.2/path',
                     '8.8.8.8', '0.0.0.0', 'example.com', '::1'):
            with self.assertRaises(ota.UpdateError):
                ota.local_host(host)
        self.assertEqual(ota.local_host('192.168.137.2'), '192.168.137.2')
        clock = Clock()
        transport = FakeTransport([(302, {'secret': 'NEVER_LOG'})])
        report = ota.perform_update(image_bytes(), MAC, transport, clock=clock, sleep=clock.sleep)
        self.assertFalse(report['success'])
        self.assertEqual(transport.posts, [])
        self.assertNotIn('NEVER_LOG', json.dumps(report))

    def test_wrong_mac_project_state_boot_slot_support_and_size_block_post(self):
        cases = ((dict(mac='00:11:22:33:44:55'), 'device_mac_mismatch'),
                 (dict(project_name='other'), 'device_project_mismatch'),
                 (dict(ota_state=1), 'current_firmware_not_valid'),
                 (dict(boot_slot='ota_1'), 'current_boot_slot_mismatch'),
                 (dict(upload_supported=False), 'upload_unsupported'),
                 (dict(max_image_bytes=100), 'image_exceeds_slot'))
        for changes, code in cases:
            transport, clock = FakeTransport([(200, before(**changes))]), Clock()
            report = ota.perform_update(image_bytes(), MAC, transport, clock=clock, sleep=clock.sleep)
            self.assertEqual(report['error'], code)
            self.assertEqual(transport.posts, [])

    def test_metadata_extra_fields_or_bad_types_never_leak(self):
        transport, clock = FakeTransport([(200, dict(before(), token='NEVER_LOG'))]), Clock()
        report = ota.perform_update(image_bytes(), MAC, transport, clock=clock, sleep=clock.sleep)
        self.assertEqual(report['error'], 'metadata_invalid')
        self.assertNotIn('NEVER_LOG', json.dumps(report))
        for field, value in (('ota_state', True), ('max_image_bytes', True), ('elf_sha256', 'short')):
            with self.assertRaises(ota.UpdateError):
                ota.validate_snapshot(before(**{field: value}))


class UpdateTests(unittest.TestCase):
    def run_update(self, states, post=None):
        clock, transport = Clock(), FakeTransport(states, post)
        return ota.perform_update(image_bytes(), MAC, transport, timeout=5, clock=clock, sleep=clock.sleep), transport

    def test_reboot_pending_then_valid_requires_new_elf_and_target_slot(self):
        report, _ = self.run_update([(200, before()), ota.InterruptedRequest(),
                                     (200, after(ota_state=1)), (200, after())])
        self.assertTrue(report['success'])
        self.assertEqual(report['poll_attempts'], 3)
        self.assertEqual(report['after']['running_slot'], 'ota_1')
        self.assertEqual(report['after']['ota_state'], 2)

    def test_post_disconnect_can_succeed_only_after_independent_valid_metadata(self):
        report, _ = self.run_update([(200, before()), (200, after())],
                                    ota.InterruptedRequest(len(image_bytes())))
        self.assertTrue(report['success'])
        self.assertTrue(report['upload']['interrupted'])
        self.assertFalse(report['upload']['accepted_response'])

    def test_upload_ack_does_not_make_no_change_or_pending_or_wrong_slot_success(self):
        for stale in (before(), after(ota_state=1), after(boot_slot='ota_0'), after(elf_sha256=OLD_ELF)):
            report, _ = self.run_update([(200, before()), (200, stale)])
            self.assertFalse(report['success'])
            self.assertTrue(report['upload']['accepted_response'])
            self.assertEqual(report['error'], 'new_firmware_not_confirmed_before_timeout')

    def test_post_failure_or_wrong_ack_rejected_without_poll_success(self):
        for status, ack in ((500, {'token': 'NEVER_LOG'}),
                            (200, acknowledgement(image_bytes(), target_slot='ota_0')),
                            (200, acknowledgement(image_bytes(), bytes=1)),
                            (200, acknowledgement(image_bytes(), sha256='00' * 32)),
                            (200, acknowledgement(image_bytes(), accepted=False))):
            report, transport = self.run_update([(200, before()), (200, after())], (status, ack, len(image_bytes())))
            self.assertFalse(report['success'])
            self.assertEqual(transport.gets, 1)
            self.assertNotIn('NEVER_LOG', json.dumps(report))

    def test_identity_change_during_reboot_is_failure(self):
        report, _ = self.run_update([(200, before()), (200, after(mac='00:11:22:33:44:55'))])
        self.assertEqual(report['error'], 'device_mac_mismatch')
        self.assertFalse(report['success'])


class HttpTests(unittest.TestCase):
    def test_fixed_endpoint_condition_headers_length_and_exact_body(self):
        data, clock = image_bytes(), Clock()
        connections = Connections([FakeResponse(before()), FakeResponse(acknowledgement(data)), FakeResponse(after())])
        transport = ota.Transport('192.168.137.2', KEY, clock=clock, connection_factory=connections)
        report = ota.perform_update(data, MAC, transport, clock=clock, sleep=clock.sleep)
        self.assertTrue(report['success'])
        post = connections.instances[1]
        self.assertEqual((post.host, post.port, post.method, post.path),
                         ('192.168.137.2', 8765, 'POST', '/api/firmware'))
        self.assertEqual(post.headers['X-Botifier-Key'], KEY)
        self.assertEqual(post.headers['X-Expected-MAC'], MAC)
        self.assertEqual(post.headers['X-Expected-ELF-SHA256'], OLD_ELF)
        self.assertEqual(post.headers['X-Firmware-SHA256'], hashlib.sha256(data).hexdigest())
        self.assertEqual(post.headers['Content-Length'], str(len(data)))
        self.assertEqual(post.headers['Content-Type'], 'application/octet-stream')
        self.assertEqual(bytes(post.data), data)
        self.assertTrue(all(item.closed for item in connections.instances))
        self.assertNotIn(KEY, json.dumps(report))

    def test_truncated_post_response_or_reboot_disconnect_requires_poll_confirmation(self):
        data = image_bytes()
        for response in (FakeResponse(b'{', declared=100), http.client.RemoteDisconnected('TOKEN_NEVER_LOG')):
            clock = Clock()
            connections = Connections([FakeResponse(before()), response, FakeResponse(after())])
            transport = ota.Transport('127.0.0.1', KEY, clock=clock, connection_factory=connections)
            report = ota.perform_update(data, MAC, transport, clock=clock, sleep=clock.sleep)
            self.assertTrue(report['success'])
            self.assertTrue(report['upload']['interrupted'])
            self.assertNotIn('TOKEN_NEVER_LOG', json.dumps(report))

    def test_truncated_initial_response_redirect_or_json_error_never_upload(self):
        for response in (FakeResponse(b'{', declared=100), FakeResponse(b'NEVER_LOG', status=302),
                         FakeResponse(b'{"token":"NEVER_LOG"'),
                         FakeResponse(b'{"v":1,"v":1}'), FakeResponse(b'NaN')):
            clock = Clock()
            connections = Connections([response])
            transport = ota.Transport('127.0.0.1', KEY, clock=clock, connection_factory=connections)
            report = ota.perform_update(image_bytes(), MAC, transport, clock=clock, sleep=clock.sleep)
            self.assertFalse(report['success'])
            self.assertEqual(len(connections.instances), 1)
            self.assertNotIn('NEVER_LOG', json.dumps(report))

    def test_network_exception_cannot_echo_credentials(self):
        clock = Clock()
        connections = Connections([OSError('Authorization: TOKEN_NEVER_LOG')])
        transport = ota.Transport('127.0.0.1', KEY, clock=clock, connection_factory=connections)
        report = ota.perform_update(image_bytes(), MAC, transport, clock=clock, sleep=clock.sleep)
        self.assertFalse(report['success'])
        self.assertNotIn('TOKEN_NEVER_LOG', json.dumps(report))


class CliTests(unittest.TestCase):
    def test_invalid_command_line_key_argument_is_not_echoed(self):
        output = io.StringIO()
        with contextlib.redirect_stderr(output), self.assertRaises(SystemExit):
            ota.main(['--key', 'TOKEN_NEVER_LOG'])
        self.assertNotIn('TOKEN_NEVER_LOG', output.getvalue())

    def test_existing_report_is_untouched_and_invalid_key_has_safe_new_report(self):
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            report, image, key = folder / 'report.json', folder / 'app.bin', folder / 'key.txt'
            image.write_bytes(image_bytes())
            key.write_text('TOKEN_NEVER_LOG')
            args = ['--host', '127.0.0.1', '--expected-mac', MAC, '--image', str(image),
                    '--key-file', str(key), '--report', str(report)]
            output = io.StringIO()
            with contextlib.redirect_stdout(output), mock.patch.object(ota, 'Transport') as transport:
                self.assertEqual(ota.main(args), 1)
                transport.assert_not_called()
            self.assertNotIn('TOKEN_NEVER_LOG', report.read_text() + output.getvalue())
            saved = report.read_bytes()
            with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
                ota.main(args)
            self.assertEqual(report.read_bytes(), saved)


if __name__ == '__main__':
    unittest.main()
