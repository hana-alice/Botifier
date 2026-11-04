#!/usr/bin/env python3
"""Upload a validated ESP32-S3 app to the authenticated LAN firmware endpoint.

The device key comes only from --key-file or BOTIFIER_DEVICE_KEY. Reports never
contain keys, response bodies outside the metadata allowlist, or raw exceptions.
An upload acknowledgement is not success: the new ELF, target slot and VALID
state must all be observed after reboot. Standard IDF images with an appended
SHA256 are supported; signed/trailing-image extensions are deliberately refused.
"""

import argparse
from datetime import datetime, timezone
import hashlib
import http.client
import ipaddress
import json
import os
from pathlib import Path
import re
import struct
import time


PORT, ENDPOINT = 8765, '/api/firmware'
MAX_IMAGE_BYTES, MAX_RESPONSE_BYTES, CHUNK_BYTES = 16 * 1024 * 1024, 4096, 64 * 1024
SNAPSHOT_KEYS = {'v', 'mac', 'project_name', 'version', 'elf_sha256', 'running_slot',
                 'boot_slot', 'ota_state', 'max_image_bytes', 'upload_supported'}
ACK_KEYS = {'accepted', 'bytes', 'sha256', 'target_slot', 'rebooting'}
SLOTS = {'ota_0', 'ota_1'}
HASH = re.compile(r'[0-9a-fA-F]{64}\Z')
MAC = re.compile(r'(?:[0-9a-fA-F]{2}:){5}[0-9a-fA-F]{2}\Z')
KEY = re.compile(r'[0-9a-fA-F]{32}\Z')
PROJECT = re.compile(r'[A-Za-z0-9_.-]{1,31}\Z')


class UpdateError(Exception):
    """A fixed, credential-free error code."""


class InterruptedRequest(UpdateError):
    def __init__(self, sent=0):
        super().__init__('connection_interrupted')
        self.sent = sent


class Parser(argparse.ArgumentParser):
    def error(self, _message):
        # Unknown arguments could contain an accidentally supplied secret.
        self.exit(2, 'Invalid arguments; use --help. Keys are never command-line arguments.\n')


def require(condition, code):
    if not condition:
        raise UpdateError(code)


def integer(value, minimum=0, maximum=(1 << 32) - 1):
    return type(value) is int and minimum <= value <= maximum


def normalize_mac(value):
    require(type(value) is str and MAC.fullmatch(value) is not None, 'invalid_expected_mac')
    return value.lower()


def local_host(value):
    """Only bare RFC1918, link-local or loopback IPv4 literals, on fixed port."""
    try:
        address = ipaddress.IPv4Address(value)
    except (ipaddress.AddressValueError, TypeError):
        raise UpdateError('host_must_be_lan_ipv4') from None
    allowed = ('10.0.0.0/8', '172.16.0.0/12', '192.168.0.0/16', '169.254.0.0/16', '127.0.0.0/8')
    require(any(address in ipaddress.IPv4Network(network) for network in allowed), 'host_must_be_lan_ipv4')
    require(str(address) == value, 'host_must_be_lan_ipv4')
    return str(address)


def load_key(key_file=None, environ=None):
    if key_file is not None:
        try:
            with Path(key_file).open('rb') as handle:
                raw = handle.read(36)
            require(re.fullmatch(rb'[0-9a-fA-F]{32}(?:\r?\n)?', raw) is not None, 'invalid_device_key')
            value = raw.rstrip(b'\r\n').decode('ascii')
        except OSError:
            raise UpdateError('key_file_unreadable') from None
    else:
        value = (os.environ if environ is None else environ).get('BOTIFIER_DEVICE_KEY', '')
    require(type(value) is str and KEY.fullmatch(value) is not None, 'invalid_device_key')
    return value.lower()


def image_text(data, offset, size, code):
    field = data[offset:offset + size]
    require(b'\0' in field, code)
    try:
        text = field.split(b'\0', 1)[0].decode('ascii')
    except UnicodeError:
        raise UpdateError(code) from None
    require(text and all(32 <= ord(character) < 127 for character in text), code)
    return text


def validate_image(data):
    """IDF24-byte header + <=16 segments + XOR checksum + appended SHA256.

    SDK references: bootloader_support/include/esp_app_format.h and
    esp_app_format/include/esp_app_desc.h. Descriptor offsets are relative to
    the first segment data at file offset32: project_name48, ELF SHA256144.
    """
    require(type(data) is bytes and 288 <= len(data) <= MAX_IMAGE_BYTES, 'invalid_image_size')
    require(data[0] == 0xE9, 'image_magic')
    require(1 <= data[1] <= 16, 'image_segment_count')
    require(struct.unpack_from('<H', data, 12)[0] == 9, 'image_not_esp32s3')
    require(data[23] == 1, 'image_appended_digest_required')
    position, checksum = 24, 0xEF
    for index in range(data[1]):
        require(position + 8 <= len(data), 'image_segment_header_truncated')
        address, length = struct.unpack_from('<II', data, position)
        position += 8
        require(length % 4 == 0 and address + length <= 1 << 32, 'image_segment_range')
        require(position + length <= len(data), 'image_segment_truncated')
        if index == 0:
            require(position == 32 and length >= 256, 'image_app_descriptor_missing')
        for value in memoryview(data)[position:position + length]:
            checksum ^= value
        position += length
    checksum_offset = position | 15
    require(checksum_offset + 33 == len(data), 'image_truncated_or_trailing_data')
    require(not any(data[position:checksum_offset]), 'image_padding_invalid')
    require(data[checksum_offset] == checksum, 'image_checksum_mismatch')
    require(hashlib.sha256(data[:checksum_offset + 1]).digest() == data[checksum_offset + 1:],
            'image_digest_mismatch')
    require(struct.unpack_from('<I', data, 32)[0] == 0xABCD5432, 'image_app_descriptor_magic')
    project = image_text(data, 32 + 48, 32, 'image_project_invalid')
    require(PROJECT.fullmatch(project) is not None, 'image_project_invalid')
    version = image_text(data, 32 + 16, 32, 'image_version_invalid')
    elf_sha = data[32 + 144:32 + 176]
    require(elf_sha not in (bytes(32), b'\xff' * 32), 'image_elf_sha_invalid')
    return dict(bytes=len(data), sha256=hashlib.sha256(data).hexdigest(), elf_sha256=elf_sha.hex(),
                project_name=project, version=version, chip_id=9, segments=data[1],
                checksum_verified=True, appended_sha256_verified=True)


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        require(key not in result, 'response_json_invalid')
        result[key] = value
    return result


def reject_constant(_):
    raise UpdateError('response_json_invalid')


def validate_snapshot(value):
    require(type(value) is dict and set(value) == SNAPSHOT_KEYS, 'metadata_invalid')
    require(type(value['v']) is int and value['v'] == 1, 'metadata_invalid')
    require(type(value['mac']) is str and MAC.fullmatch(value['mac']) is not None, 'metadata_invalid')
    require(type(value['project_name']) is str and PROJECT.fullmatch(value['project_name']) is not None,
            'metadata_invalid')
    require(type(value['version']) is str and 1 <= len(value['version']) <= 31
            and all(32 <= ord(c) < 127 for c in value['version']), 'metadata_invalid')
    require(type(value['elf_sha256']) is str and HASH.fullmatch(value['elf_sha256']) is not None,
            'metadata_invalid')
    require(type(value['running_slot']) is str and value['running_slot'] in SLOTS
            and type(value['boot_slot']) is str and value['boot_slot'] in SLOTS, 'metadata_invalid')
    require(integer(value['ota_state'], -1, (1 << 31) - 1)
            and integer(value['max_image_bytes'], 1, MAX_IMAGE_BYTES)
            and type(value['upload_supported']) is bool, 'metadata_invalid')
    return dict(value, mac=value['mac'].lower(), elf_sha256=value['elf_sha256'].lower())


class Transport:
    """No proxies or redirects: exactly the caller's validated IP and endpoint."""

    def __init__(self, host, key, *, clock=time.monotonic, connection_factory=http.client.HTTPConnection):
        self.host, self._key, self.clock, self.connection_factory = local_host(host), key, clock, connection_factory
        require(type(key) is str and KEY.fullmatch(key) is not None, 'invalid_device_key')

    def _remaining(self, connection, deadline):
        remaining = deadline - self.clock()
        require(remaining > 0, 'deadline_expired')
        connection.timeout = min(10.0, remaining)
        if getattr(connection, 'sock', None) is not None:
            connection.sock.settimeout(connection.timeout)

    def _read_json(self, response, connection, deadline):
        if response.status != 200:
            return response.status, None  # Never retain or parse arbitrary error bodies.
        require(response.getheader('Content-Type', '').split(';', 1)[0].lower() == 'application/json',
                'response_content_type')
        length = response.getheader('Content-Length')
        if length is not None:
            require(re.fullmatch(r'\d{1,6}', length) is not None and int(length) <= MAX_RESPONSE_BYTES,
                    'response_length_invalid')
        raw = bytearray()
        while length is None or len(raw) < int(length):
            self._remaining(connection, deadline)
            part = response.read1(min(1024, MAX_RESPONSE_BYTES + 1 - len(raw)))
            if not part:
                break
            raw.extend(part)
            require(len(raw) <= MAX_RESPONSE_BYTES, 'response_too_large')
        require(len(raw) <= MAX_RESPONSE_BYTES, 'response_too_large')
        if length is not None and len(raw) != int(length):
            raise InterruptedRequest()
        try:
            value = json.loads(raw.decode('utf-8'), object_pairs_hook=unique_object, parse_constant=reject_constant)
        except (UnicodeError, ValueError, RecursionError):
            raise UpdateError('response_json_invalid') from None
        return response.status, value

    def _request(self, method, deadline, data=None, headers=None, progress=None):
        remaining = deadline - self.clock()
        require(remaining > 0, 'deadline_expired')
        connection = self.connection_factory(self.host, PORT, timeout=min(10.0, remaining))
        sent = 0
        try:
            request_headers = {'X-Botifier-Key': self._key, 'Accept': 'application/json'}
            if method == 'GET':
                connection.request('GET', ENDPOINT, headers=request_headers)
            else:
                request_headers.update(headers)
                connection.putrequest('POST', ENDPOINT, skip_accept_encoding=True)
                for name, value in request_headers.items():
                    connection.putheader(name, value)
                connection.endheaders()
                while sent < len(data):
                    self._remaining(connection, deadline)
                    chunk = data[sent:sent + CHUNK_BYTES]
                    connection.send(chunk)
                    sent += len(chunk)
                    if progress is not None:
                        progress(sent, len(data))
            self._remaining(connection, deadline)
            status, value = self._read_json(connection.getresponse(), connection, deadline)
            return status, value, sent
        except InterruptedRequest:
            raise InterruptedRequest(sent) from None
        except (OSError, http.client.HTTPException):
            raise InterruptedRequest(sent) from None
        finally:
            try:
                connection.close()
            except OSError:
                pass

    def get(self, deadline):
        status, value, _ = self._request('GET', deadline)
        return status, value

    def post(self, data, before, image, deadline, progress=None):
        headers = {'Content-Type': 'application/octet-stream', 'Content-Length': str(len(data)),
                   'X-Firmware-SHA256': image['sha256'], 'X-Expected-ELF-SHA256': before['elf_sha256'],
                   'X-Expected-MAC': before['mac']}
        return self._request('POST', deadline, data, headers, progress)


def perform_update(data, expected_mac, transport, *, timeout=180, clock=time.monotonic,
                   sleep=time.sleep, progress=None):
    report = dict(v=1, success=False, error=None, image=None, before=None, after=None,
                  upload=None, poll_attempts=0, observations=[],
                  scope='Authenticated LAN OTA; success requires target-slot new ELF and OTA VALID after reboot.')
    started, deadline = clock(), clock() + timeout
    try:
        expected_mac = normalize_mac(expected_mac)
        require(type(timeout) in (int, float) and 1 <= timeout <= 1800, 'invalid_timeout')
        image = report['image'] = validate_image(data)
        status, value = transport.get(deadline)
        report['metadata_http_status'] = status
        require(status == 200, 'metadata_http_status')
        before = report['before'] = validate_snapshot(value)
        require(before['mac'] == expected_mac, 'device_mac_mismatch')
        require(before['project_name'] == image['project_name'], 'device_project_mismatch')
        require(before['upload_supported'], 'upload_unsupported')
        require(before['ota_state'] == 2, 'current_firmware_not_valid')
        require(before['boot_slot'] == before['running_slot'], 'current_boot_slot_mismatch')
        require(len(data) <= before['max_image_bytes'], 'image_exceeds_slot')
        target = 'ota_1' if before['running_slot'] == 'ota_0' else 'ota_0'
        upload = report['upload'] = dict(bytes_sent=0, accepted_response=False, interrupted=False,
                                         target_slot=target, http_status=None)
        try:
            status, ack, sent = transport.post(data, before, image, deadline, progress)
            upload.update(bytes_sent=sent, http_status=status)
            require(status == 200, 'upload_http_status')
            require(type(ack) is dict and set(ack) == ACK_KEYS, 'upload_ack_invalid')
            require(ack['accepted'] is True and ack['rebooting'] is True
                    and integer(ack['bytes']) and ack['bytes'] == len(data)
                    and type(ack['sha256']) is str and ack['sha256'].lower() == image['sha256']
                    and ack['target_slot'] == target, 'upload_ack_mismatch')
            upload['accepted_response'] = True
        except InterruptedRequest as error:
            upload.update(bytes_sent=error.sent, interrupted=True)
        while clock() < deadline:
            report['poll_attempts'] += 1
            try:
                status, value = transport.get(deadline)
                if status != 200:
                    report['last_poll_error'], report['last_poll_http_status'] = 'metadata_http_status', status
                else:
                    current = validate_snapshot(value)
                    report['after'] = current
                    report['observations'] = (report['observations'] + [current])[-16:]
                    require(current['mac'] == expected_mac, 'device_mac_mismatch')
                    require(current['project_name'] == image['project_name'], 'device_project_mismatch')
                    require(clock() < deadline, 'deadline_expired')
                    if (current['elf_sha256'] == image['elf_sha256']
                            and current['running_slot'] == target and current['boot_slot'] == target
                            and current['ota_state'] == 2):
                        report['success'] = True
                        return report
            except InterruptedRequest:
                report['last_poll_error'] = 'connection_interrupted'
            except UpdateError as error:
                if error.args[0] in {'device_mac_mismatch', 'device_project_mismatch'}:
                    raise
                report['last_poll_error'] = error.args[0]
            sleep(max(0.0, min(2.0, deadline - clock())))
        raise UpdateError('new_firmware_not_confirmed_before_timeout')
    except UpdateError as error:
        report['error'] = error.args[0]
    except KeyboardInterrupt:
        report['error'] = 'interrupted_not_confirmed'
    except Exception:
        # Network libraries can put request URLs/headers in arbitrary messages.
        report['error'] = 'unexpected_failure_not_confirmed'
    finally:
        report['elapsed_seconds'] = round(clock() - started, 3)
    return report


def main(argv=None):
    parser = Parser(description=__doc__)
    parser.add_argument('--host', required=True, help='Bare LAN IPv4; fixed port8765')
    parser.add_argument('--image', required=True, type=Path, help='Local ESP32-S3 app.bin')
    parser.add_argument('--expected-mac', required=True, help='Required device MAC; prevents wrong-device uploads')
    parser.add_argument('--report', required=True, type=Path, help='New JSON report; never overwritten')
    parser.add_argument('--key-file', type=Path, help='Only32 hex characters, optionally one trailing newline')
    parser.add_argument('--timeout', type=int, default=180, help='Total upload/confirmation deadline,1..1800 seconds')
    args = parser.parse_args(argv)
    if args.report.exists():
        parser.error('report_exists')
    args.report.parent.mkdir(parents=True, exist_ok=True)
    report = dict(v=1, success=False, error=None)
    with args.report.open('x', encoding='utf-8') as output:
        try:
            host = local_host(args.host)
            expected_mac = normalize_mac(args.expected_mac)
            require(1 <= args.timeout <= 1800, 'invalid_timeout')
            require(args.image.resolve() != args.report.resolve(), 'image_report_paths_overlap')
            key = load_key(args.key_file)
            require(args.image.is_file() and 288 <= args.image.stat().st_size <= MAX_IMAGE_BYTES, 'invalid_image_size')
            with args.image.open('rb') as image_file:
                data = image_file.read(MAX_IMAGE_BYTES + 1)
            last_printed = [0]

            def progress(sent, total):
                if sent == total or sent - last_printed[0] >= 1024 * 1024:
                    print(f'Sent {sent}/{total} bytes', flush=True)
                    last_printed[0] = sent

            report = perform_update(data, expected_mac, Transport(host, key), timeout=args.timeout, progress=progress)
            report.update(host=host, expected_mac=expected_mac)
        except UpdateError as error:
            report['error'] = error.args[0]
        except KeyboardInterrupt:
            report['error'] = 'interrupted_not_confirmed'
        except Exception:
            report['error'] = 'local_failure_not_confirmed'
        report['recorded_at_utc'] = datetime.now(timezone.utc).isoformat()
        output.write(json.dumps(report, indent=2, allow_nan=False) + '\n')
    if report['success']:
        print('New firmware confirmed VALID; report written.')
    else:
        print(f"Update not confirmed ({report['error']}); report written.")
    return 0 if report['success'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
