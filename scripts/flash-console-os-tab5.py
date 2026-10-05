#!/usr/bin/env python3
"""Install a hash-bound Tab5 candidate on one backed-up unit; retain recovery evidence."""
from __future__ import annotations
import argparse
import binascii
import contextlib
import datetime
import hashlib
import importlib.util
import io
import json
import pathlib
import re
import struct
import sys
import tempfile
import time

ROOT = pathlib.Path(__file__).resolve().parents[1]
BUILD = ROOT / 'apps/console_os/build-tab5'
LAYOUT = {'0x2000': 'bootloader/bootloader.bin', '0x8000': 'partition_table/partition-table.bin',
          '0x10000': 'ota_data_initial.bin', '0x20000': 'p4_console_os.bin'}
LIMITS = {'0x2000': 0x6000, '0x8000': 0x1000, '0x10000': 0x2000, '0x20000': 0x7f0000}


def require(ok, message):
    if not ok:
        raise ValueError(message)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def redact(text):
    text = re.sub(r'(?i)(?:[0-9a-f]{2}:){5}[0-9a-f]{2}', '[redacted]', text)
    return re.sub(r'\x1b\[[0-9;]*[A-Za-z]', '', text)


def validate_image(data):
    require(len(data) > 24 and data[0] == 0xe9, 'invalid ESP image')
    require(struct.unpack_from('<H', data, 12)[0] == 18, 'wrong SoC image')
    require(struct.unpack_from('<HH', data, 15) == (100, 199), 'wrong silicon family')


def validate_live(esp, identity):
    require(esp.CHIP_NAME == 'ESP32-P4', 'connected chip is not ESP32-P4')
    require(not esp.secure_download_mode, 'secure download mode unsupported')
    actual = sha(bytes(esp.read_mac()).hex().encode('ascii'))
    require(actual == identity, 'live identity differs from selected backed-up unit')
    major, minor = esp.get_major_chip_version(), esp.get_minor_chip_version()
    require(major == 1 and 0 <= minor <= 99, 'live silicon outside locked revision 1.x family')
    require((esp.flash_id() >> 16) == 0x18, 'live flash is not 16 MiB')
    require(not esp.get_secure_boot_enabled(), 'secure boot enabled')
    require(not esp.get_flash_encryption_enabled(), 'flash encryption enabled')
    return {'identity_sha256': actual, 'revision': f'v{major}.{minor}', 'flash_bytes': 16777216}


def prepare(auth, unit):
    require(auth['board'] == 'm5stack-tab5' and auth['install_authorized'] is True,
            'missing explicit Tab5 installation authorization')
    require(auth['operation'] in ('first-console-os-layout', 'app-only'), 'unsupported installation scope')
    require(set(auth['artifacts']) == set(LAYOUT), 'unexpected write ranges')
    selected = auth['units'][unit]
    require(selected['model_confirmed'] is True, 'physical Tab5 model unconfirmed')
    identity = selected['identity_sha256']
    require(re.fullmatch('[0-9a-f]{64}', identity) is not None, 'invalid identity binding')
    manifest = json.loads((ROOT / 'hardware/backups/manifest.json').read_text())
    matches = [d for d in manifest['additional_devices'] if d['device']['identity']['sha256'] == identity]
    require(len(matches) == 1, 'unit must have exactly one full backup record')
    backup = matches[0]['backup']
    require(backup['bytes'] == 16777216 and backup['sha256'] == selected['backup_sha256'],
            'authorization and backup manifest differ')
    recovery = (ROOT / backup['file']).read_bytes()
    require(len(recovery) == 16777216 and sha(recovery) == backup['sha256'], 'backup corrupt or incomplete')
    spec = importlib.util.spec_from_file_location('tab5_verify', ROOT / 'scripts/verify-console-os-tab5.py')
    verifier = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(verifier)
    verified = verifier.verify(BUILD)
    require(verified["usb_host_enabled"] == auth.get("usb_host_enabled", False),
            "USB host selection differs from exact-artifact authorization")
    artifacts = {}
    for offset, relative in LAYOUT.items():
        entry = auth['artifacts'][offset]
        require(entry['file'] == str(pathlib.Path('apps/console_os/build-tab5') / relative), 'artifact path differs')
        data = (ROOT / entry['file']).read_bytes()
        require(0 < len(data) <= LIMITS[offset], 'artifact crosses allowed flash range')
        require(len(data) == entry['bytes'] and sha(data) == entry['sha256'], 'artifact hash or size differs')
        if offset in ('0x2000', '0x20000'):
            validate_image(data)
        artifacts[offset] = data
    if auth['operation'] == 'app-only':
        previous = selected['predecessor']
        predecessor = (ROOT / previous['file']).read_bytes()
        require(len(predecessor) == previous['bytes'] and sha(predecessor) == previous['sha256'],
                'app-only predecessor artifact changed')
        validate_image(predecessor)
    else:
        predecessor = None
    return identity, recovery, artifacts, predecessor


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--authorization', type=pathlib.Path, required=True)
    parser.add_argument('--authorization-sha256', required=True)
    parser.add_argument('--unit', choices=('A', 'B'), required=True)
    parser.add_argument('--port')
    parser.add_argument('--monitor-seconds', type=int, default=60,
                        help='capture boot and operator testing on one open connection (30-600 seconds)')
    parser.add_argument('--install', action='store_true', help='write after all checks; default checks local inputs only')
    args = parser.parse_args()
    require(30 <= args.monitor_seconds <= 600, 'monitor duration must be 30-600 seconds')
    raw = args.authorization.read_bytes()
    require(sha(raw) == args.authorization_sha256, 'authorization digest differs')
    auth = json.loads(raw)
    identity, recovery, artifacts, predecessor = prepare(auth, args.unit)
    print(f'Tab5 {args.unit}: artifact and complete backup verified', flush=True)
    if not args.install:
        return
    require(args.port is not None and pathlib.Path(args.port).is_char_device(), 'explicit live serial port required')
    import esptool
    import serial
    require(esptool.__version__ == '4.12.0', 'use the pinned IDF Python environment (esptool 4.12.0)')
    run = pathlib.Path(tempfile.mkdtemp(prefix=f'tab5-{args.unit.lower()}-install-', dir=ROOT / 'hardware/backups'))
    staged = {}
    for offset, data in artifacts.items():
        path = run / f'{offset}.bin'
        path.write_bytes(data)
        path.chmod(0o600)
        staged[offset] = path
    receipt = {'board': 'm5stack-tab5', 'unit': args.unit, 'identity_sha256': identity,
               'started_at': datetime.datetime.now(datetime.timezone.utc).isoformat(),
               'authorization_sha256': args.authorization_sha256, 'artifacts': auth['artifacts'],
               'port': args.port, 'write_started': False, 'readback_verified': False,
               'boot_ready': False, 'physical_acceptance': 'pending'}
    esp = None
    def save():
        (run / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
    def quiet(fn):
        out = io.StringIO()
        try:
            with contextlib.redirect_stdout(out), contextlib.redirect_stderr(out):
                return fn()
        finally:
            with (run / 'esptool.log').open('a') as log:
                log.write(redact(out.getvalue()))
    try:
        save()
        esp = quiet(lambda: esptool.detect_chip(args.port, connect_mode='default_reset'))
        esp = quiet(esp.run_stub)
        receipt['live'] = quiet(lambda: validate_live(esp, identity))
        # One open loader connection binds the identity check, write and readback.
        quiet(lambda: esp.change_baud(921600))
        esp.flash_set_parameters(16777216)
        if auth['operation'] == 'app-only':
            # Keep the installed bootloader/partition table and active OTA selector.
            for offset in ('0x2000', '0x8000'):
                require(esp.read_flash(int(offset, 0), len(artifacts[offset])) == artifacts[offset],
                        'app-only requires the matching installed bootloader and partition table')
            ota = esp.read_flash(0x10000, 8192)
            sequences = []
            for pos in (0, 4096):
                seq, state, crc = struct.unpack_from('<I20xII', ota, pos)
                if seq not in (0, 0xffffffff) and state not in (3, 4) and crc == binascii.crc32(ota[pos:pos+4], 0xffffffff):
                    sequences.append(seq)
            require(sequences and (max(sequences)-1) % 2 == 0, 'app-only requires active OTA slot 0')
            require(esp.read_flash(0x20000, len(predecessor)) == predecessor,
                    'installed app differs from authorized predecessor')
            writes = {'0x20000': artifacts['0x20000']}
        else:
            # Verify the ranges to be replaced still match the captured predecessor.
            for offset, data in artifacts.items():
                start = int(offset, 0)
                old = esp.read_flash(start, len(data))
                require(old == recovery[start:start+len(data)], 'live predecessor differs from backup; no write performed')
            writes = artifacts
        quiet(lambda: validate_live(esp, identity))
        receipt['write_offsets'] = list(writes)
        receipt['write_started'] = True
        save()
        print(f'Tab5 {args.unit}: identity, security and predecessor matched; writing {len(writes)} reviewed range(s)', flush=True)
        # The verified connection already owns a running stub; do not upload it again.
        command = ['--chip', 'esp32p4', '--port', args.port, '--no-stub', '--baud', '921600', '--after', 'no_reset_stub', 'write_flash',
                   '--flash_mode', 'keep', '--flash_freq', 'keep', '--flash_size', 'keep']
        for offset in writes:
            path = staged[offset]
            require(sha(path.read_bytes()) == auth['artifacts'][offset]['sha256'], 'staged artifact changed')
            command.extend([offset, str(path)])
        quiet(lambda: esptool.main(command, esp=esp))
        for offset, data in writes.items():
            actual = esp.read_flash(int(offset, 0), len(data))
            require(actual == data, f'exact readback mismatch at {offset}; left in loader')
        quiet(lambda: validate_live(esp, identity))
        receipt['readback_verified'] = True
        save()
        print(f'Tab5 {args.unit}: all written ranges matched on readback; booting', flush=True)
        quiet(esp.watchdog_reset)
        esp._port.close()
        esp = None
        captured = bytearray()
        deadline = time.monotonic() + args.monitor_seconds
        announced = set()
        while time.monotonic() < deadline and len(captured) < 1024*1024:
            port = serial.Serial(port=None, baudrate=115200, timeout=.25)
            port.dtr = False
            port.rts = False
            port.port = args.port
            try:
                port.open()
                while time.monotonic() < deadline and len(captured) < 1024*1024:
                    captured.extend(port.read(4096))
                    # Keep a sanitized live log so a game failure can be inspected
                    # without closing/reopening native USB and resetting the board.
                    text = captured.decode('utf-8', 'replace')
                    complete = text.rsplit('\n', 1)[0] + '\n' if '\n' in text else ''
                    (run / 'runtime.log').write_text(redact(complete))
                    for marker in ('P4_CONSOLE_OS READY board=m5stack-tab5',
                                   'OTA_BOOT_VALID result=ESP_OK', 'Guru Meditation',
                                   'P4_CONSOLE_OS FATAL_HOLD'):
                        if marker in text and marker not in announced:
                            print(f'Tab5 {args.unit}: {marker}', flush=True)
                            announced.add(marker)
            except (serial.SerialException, OSError):
                time.sleep(.25)
            finally:
                port.close()
        log = redact(captured.decode('utf-8', 'replace'))
        (run / 'runtime.log').write_text(log)
        receipt['boot_ready'] = 'P4_CONSOLE_OS READY board=m5stack-tab5' in log and not any(
            marker in log for marker in ('Guru Meditation', 'panic_abort', 'abort() was called', 'P4_CONSOLE_OS HALT', 'P4_CONSOLE_OS FATAL_HOLD'))
        receipt['runtime_log_sha256'] = sha(log.encode())
        receipt['health_ready'] = 'OTA_BOOT_VALID result=ESP_OK' in log
        print(log[-24000:])
        require(receipt['boot_ready'], 'flash/readback passed, but launcher boot acceptance failed; inspect runtime.log')
    finally:
        save()
        if esp is not None:
            esp._port.close()
        print(f'Install evidence: {run}', flush=True)


if __name__ == '__main__':
    try:
        main()
    except Exception as error:
        raise SystemExit(redact(str(error))) from None
