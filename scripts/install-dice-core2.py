#!/usr/bin/env python3
"""Guarded, same-handle app-only install on the recorded Core2; never erase all."""
import argparse
import contextlib
from datetime import datetime, timezone
import hashlib
import io
import json
from pathlib import Path
import re
import struct
import time
import zlib
import esptool
import serial
ROOT=Path(__file__).resolve().parent.parent

def sha(data): return hashlib.sha256(data).hexdigest()
def redact(text): return re.sub(r'(?i)(?:[0-9a-f]{2}:){5}[0-9a-f]{2}', '[identity redacted]',text)
def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port',required=True)
    parser.add_argument('--install',action='store_true',help='write the reviewed app after all gates pass')
    args=parser.parse_args()
    profile=json.loads((ROOT/'hardware/boards/m5stack-core2-dice.json').read_text())
    manifest=json.loads((ROOT/'hardware/backups/manifest.json').read_text())
    record=next(r for r in manifest['accessory_backups'] if r['device_identity_sha256']==profile['device_identity_sha256'])
    backup=(ROOT/record['file']).read_bytes()
    if len(backup)!=profile['flash_bytes'] or sha(backup)!=record['sha256']:
        raise SystemExit('Factory backup does not match recorded size/hash')
    artifact=ROOT/'apps/dice_core2/build-core2/p4_dice_core2.bin'
    image=artifact.read_bytes()
    metadata=json.loads(artifact.with_name('artifact.json').read_text())
    if sha(image)!=metadata['sha256'] or len(image)!=metadata['bytes'] or len(image)>profile['app_partition_bytes']:
        raise SystemExit('Artifact hash/size gate failed')
    parsed=esptool.bin_image.LoadFirmwareImage('esp32',str(artifact))
    if parsed.chip_id!=0:
        raise SystemExit('Image is not ESP32')
    output=io.StringIO();esp=None
    try:
        with contextlib.redirect_stdout(output),contextlib.redirect_stderr(output):
            esp=esptool.detect_chip(args.port)
            if esp.CHIP_NAME!='ESP32' or esp.get_chip_revision()!=profile['chip_revision']:
                raise RuntimeError('Wrong chip/revision')
            identity=sha(bytes(esp.read_mac()).hex().encode())
            if identity!=profile['device_identity_sha256']:
                raise RuntimeError('Device does not match factory backup binding')
            if esp.secure_download_mode or esp.get_secure_boot_enabled() or esp.get_flash_encryption_enabled():
                raise RuntimeError('Unexpected flash security state; no writes performed')
            esp=esp.run_stub();esp.change_baud(921600)
            if (1 << (esp.flash_id() >> 16))!=len(backup):
                raise RuntimeError('Live flash size differs from backup')
            prefix=esp.read_flash(0,0x10000)
            if prefix[0x8000:0x9000]!=backup[0x8000:0x9000]:
                raise RuntimeError('Live partition table differs from preserved factory table')
            # Original table: OTA app0 at 0x10000, app1 at 0x650000.
            entries=[struct.unpack('<HBBII16sI',prefix[i:i+32]) for i in range(0x8000,0x9000,32)]
            apps=sorted((e[2],e[3],e[4]) for e in entries if e[0]==0x50aa and e[1]==0)
            candidates=[]
            for off in (0xe000,0xf000):
                seq,_,state,crc=struct.unpack('<I20sII',prefix[off:off+32])
                if seq not in (0,0xffffffff) and state not in (3,4) and crc==zlib.crc32(prefix[off:off+4],0xffffffff):
                    candidates.append(seq)
            if len(apps)!=2 or not candidates or apps[(max(candidates)-1)%2][1:]!=(profile['app_offset'],profile['app_partition_bytes']):
                raise RuntimeError('Active OTA application is not the reviewed app0; refusing to change boot selection')
            if args.install:
                esptool.main(['--chip','esp32','--port',args.port,'--baud','921600','--no-stub','--after','no_reset_stub',
                    'write_flash','--flash_size','keep',hex(profile['app_offset']),str(artifact)],esp=esp)
                readback=esp.read_flash(profile['app_offset'],len(image))
                if sha(readback)!=sha(image): raise RuntimeError('Full application readback failed')
                if esp.read_flash(0,0x10000)!=prefix: raise RuntimeError('Boot/partition/NVS prefix changed during app-only write')
            esp.hard_reset()
    finally:
        if esp is not None: esp._port.close()
        text=redact(output.getvalue())
        print('\n'.join(line for line in text.splitlines() if 'verified' in line.lower() or 'error' in line.lower()))
    result={'board':'M5Stack Core2','device_identity_sha256':identity,'port':args.port,
        'app_offset':profile['app_offset'],'app_bytes':len(image),'app_sha256':sha(image),
        'full_app_readback_verified':args.install,'boot_partition_nvs_prefix_preserved':args.install,
        'factory_backup_sha256':record['sha256'],'installed':args.install,
        'acoustic_acceptance':False,'haptic_acceptance':False,'wireless_game_acceptance':False}
    if args.install:
        uart=serial.Serial(port=None,baudrate=115200,timeout=0.2)
        uart.dtr=False;uart.rts=False;uart.port=args.port;uart.open()
        data=bytearray();deadline=time.monotonic()+15
        while time.monotonic()<deadline:
            data.extend(uart.read(4096))
        uart.close()
        log=redact(data.decode(errors='replace'))
        dest=ROOT/'hardware/test-runs';dest.mkdir(exist_ok=True)
        stamp=datetime.now(timezone.utc).strftime('%Y-%m-%dT%H%M%SZ')
        stem=f'{stamp}-core2-dice-{sha(image)[:12]}'
        log_path=dest/(stem+'-boot.log')
        log_path.write_text(log)
        result['boot_log']=str(log_path.relative_to(ROOT))
        result['boot_marker_observed']='P4_DICE BOOT board=core2 imu=ready' in log
        result['panic_observed']=any(x in log for x in ('Guru Meditation','assert failed','abort()','Brownout'))
        print('\n'.join(line for line in log.splitlines() if 'P4_DICE' in line or 'Guru' in line or 'abort' in line))
        receipt=dest/(stem+'-install.json')
        receipt.write_text(json.dumps(result,indent=2)+'\n')
        print('Install evidence:',receipt)
    print(json.dumps(result,indent=2))
if __name__=='__main__': main()
