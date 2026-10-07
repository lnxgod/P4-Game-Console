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
                raise RuntimeError('Device does not match the reviewed Core2 identity')
            if esp.secure_download_mode or esp.get_secure_boot_enabled() or esp.get_flash_encryption_enabled():
                raise RuntimeError('Unexpected flash security state; no writes performed')
            esp=esp.run_stub();esp.change_baud(921600)
            if (1 << (esp.flash_id() >> 16))!=profile['flash_bytes']:
                raise RuntimeError('Live flash size differs from the reviewed Core2 profile')
            table=esp.read_flash(0x8000,0x1000)
            if len(table)!=0x1000:
                raise RuntimeError('Incomplete live partition table; refusing app-only write')
            partitions=[];terminated=False
            for index in range(0,0x1000,32):
                record=table[index:index+32]
                magic=struct.unpack_from('<H',record)[0]
                if magic==0xffff:
                    terminated=True
                    break
                if magic==0xebeb:
                    if record[:16]!=b'\xeb\xeb'+b'\xff'*14:
                        raise RuntimeError('Invalid live partition checksum marker')
                    if record[16:]!=hashlib.md5(table[:index]).digest():
                        raise RuntimeError('Live partition table checksum differs')
                    terminated=True
                    break
                if magic!=0x50aa:
                    raise RuntimeError('Invalid live partition entry')
                partitions.append(struct.unpack('<HBBII16sI',record))
            if not terminated:
                raise RuntimeError('Live partition table has no terminator')
            ranges=sorted((e[3],e[4]) for e in partitions)
            apps=sorted((e[2],e[3],e[4]) for e in partitions if e[1]==0)
            otadata=[(e[3],e[4]) for e in partitions if e[1:3]==(1,0)]
            reviewed=(profile['app_offset'],profile['app_partition_bytes'])
            if (len(apps)!=2 or [a[0] for a in apps]!=[0x10,0x11] or
                    apps[0][1:]!=reviewed or otadata!=[(0xe000,0x2000)] or
                    any(offset<0x9000 or size<=0 or offset+size>profile['flash_bytes']
                        for offset,size in ranges) or
                    any(offset+size>following for (offset,size),(following,_) in zip(ranges,ranges[1:]))):
                raise RuntimeError('Live partition layout is not the reviewed app0/OTA scope')
            candidates=[]
            for off in (0xe000,0xf000):
                selection=esp.read_flash(off,32)
                if len(selection)!=32:
                    raise RuntimeError('Incomplete live OTA selection; refusing app-only write')
                seq,_,state,crc=struct.unpack('<I20sII',selection)
                if seq not in (0,0xffffffff) and state not in (3,4) and crc==zlib.crc32(selection[:4],0xffffffff):
                    candidates.append(seq)
            if not candidates or apps[(max(candidates)-1)%2][1:]!=reviewed:
                raise RuntimeError('Active OTA application is not the reviewed app0; refusing to change boot selection')
            if args.install:
                prefix_checksum=esp.flash_md5sum(0,0x10000)
                if artifact.read_bytes()!=image:
                    raise RuntimeError('Reviewed application artifact changed before write')
                esptool.main(['--chip','esp32','--port',args.port,'--baud','921600','--no-stub','--after','no_reset_stub',
                    'write_flash','--flash_size','keep',hex(profile['app_offset']),str(artifact)],esp=esp)
                if esp.flash_md5sum(profile['app_offset'],len(image))!=hashlib.md5(image).hexdigest():
                    raise RuntimeError('Application device checksum failed')
                if esp.flash_md5sum(0,0x10000)!=prefix_checksum:
                    raise RuntimeError('Boot/partition/NVS prefix changed during app-only write')
            esp.hard_reset()
    finally:
        if esp is not None: esp._port.close()
        text=redact(output.getvalue())
        print('\n'.join(line for line in text.splitlines() if 'verified' in line.lower() or 'error' in line.lower()))
    result={'board':'M5Stack Core2','device_identity_sha256':identity,'port':args.port,
        'app_offset':profile['app_offset'],'app_bytes':len(image),'app_sha256':sha(image),
        'verification_method':'device-checksum' if args.install else None,
        'app_checksum_verified':args.install,'full_app_readback_verified':False,
        'boot_partition_nvs_prefix_preserved':args.install,'installed':args.install,
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
