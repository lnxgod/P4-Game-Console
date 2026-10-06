#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Behavioral native bundle proof using temporary files, never hardware."""
import argparse
import contextlib
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest import mock
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'scripts'))
spec=importlib.util.spec_from_file_location('sd_bundle',ROOT/'scripts/install-olimex-sd-card.py')
installer=importlib.util.module_from_spec(spec);spec.loader.exec_module(installer)

class NativeBundleTests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory(prefix='p4-native-bundle-');self.root=Path(self.tmp.name)
        self.repo=self.root/'repo';self.bundle=self.root/'build/sd-card';self.target=self.root/'sd'
        self.bundle.mkdir(parents=True);self.target.mkdir()
        self.wad=b'fixture shareware content';self.readme=b'fixture native storage instructions'
        manifest={'enabled':True,'id':'org.test.game','launcher_id':101,'package_file':'TEST.P4G',
                  'resource_file':'TEST.P4R','required_capabilities':['video'],'optional_capabilities':[]}
        path=self.repo/'games/test/game.json';path.parent.mkdir(parents=True);path.write_text(json.dumps(manifest))
        path=self.repo/'apps/console_os/game-storage/README.TXT';path.parent.mkdir(parents=True);path.write_bytes(self.readme)
        (self.bundle/'GAMES').mkdir();(self.bundle/'UPDATE').mkdir()
        (self.bundle/'DOOM1.WAD').write_bytes(self.wad);(self.bundle/'README.TXT').write_bytes(self.readme)
        payload=b'native fixture';header=bytearray(256);header[:8]=b'P4GAME1\0'
        struct.pack_into('<9IHH',header,8,256,256+len(payload),256,len(payload),1,1,101,1,0,0,1)
        header[48:80]=hashlib.sha256(payload).digest();header[80:93]=b'org.test.game'
        (self.bundle/'GAMES/TEST.P4G').write_bytes(header+payload)
        payload=b'resource fixture';header=bytearray(128);header[:8]=b'P4RES01\0'
        struct.pack_into('<6I',header,8,128,128+len(payload),128,len(payload),1,0)
        header[32:64]=hashlib.sha256(payload).digest();header[64:77]=b'org.test.game'
        (self.bundle/'GAMES/TEST.P4R').write_bytes(header+payload)
        payload=b'firmware fixture';header=bytearray(256);header[:8]=b'P4OSUP1\0'
        struct.pack_into('<6I',header,8,256,256+len(payload),256,len(payload),1,0)
        header[32:64]=hashlib.sha256(payload).digest();header[160:168]=b'esp32p4\0'
        (self.bundle/'UPDATE/P4UPDATE.P4U').write_bytes(header+payload)
        self.patches=[mock.patch.object(installer,'ROOT',self.repo),mock.patch.object(installer,'DOOM_BYTES',len(self.wad)),
                      mock.patch.object(installer,'DOOM_SHA256',hashlib.sha256(self.wad).hexdigest())]
        for patch in self.patches:patch.start()
    def tearDown(self):
        for patch in reversed(self.patches):patch.stop()
        self.tmp.cleanup()
    def test_complete_native_bundle_validates_with_no_lua_directory_or_seed(self):
        files=installer.validate_bundle(self.bundle)
        self.assertEqual({str(p) for p in files},{'GAMES/TEST.P4G','GAMES/TEST.P4R','DOOM1.WAD','README.TXT','UPDATE/P4UPDATE.P4U'})
    def test_mixed_bundle_rejected_before_target_inspection_or_copy(self):
        cart=self.bundle/'P4/GAMES/old.p4cart';cart.parent.mkdir(parents=True);cart.write_bytes(b'P4CART1\0archive')
        with mock.patch.object(sys,'argv',['install','--bundle',str(self.bundle),'--target',str(self.target)]), \
             mock.patch.object(installer,'mounted_card') as mounted,mock.patch.object(installer,'copy_atomic') as copied:
            with self.assertRaisesRegex(SystemExit,'retired'):installer.main()
            mounted.assert_not_called();copied.assert_not_called()
        self.assertEqual(list(self.target.iterdir()),[]);self.assertTrue(cart.is_file())
    def test_native_install_preserves_existing_archived_user_carts(self):
        old=self.target/'P4/GAMES/USER.P4CART';old.parent.mkdir(parents=True);old.write_bytes(b'user archive')
        with mock.patch.object(sys,'argv',['install','--bundle',str(self.bundle),'--target',str(self.target)]), \
             mock.patch.object(installer,'mounted_card',return_value=self.target),contextlib.redirect_stdout(io.StringIO()) as output:
            installer.main()
        report=json.loads(output.getvalue());self.assertEqual(report['native_game_count'],1)
        self.assertEqual(report['script_games'],[]);self.assertEqual(report['script_game_count'],0)
        self.assertEqual(old.read_bytes(),b'user archive')
        self.assertEqual((self.target/'GAMES/TEST.P4G').read_bytes(),(self.bundle/'GAMES/TEST.P4G').read_bytes())
    def test_native_payload_corruption_still_rejected(self):
        game=self.bundle/'GAMES/TEST.P4G';data=bytearray(game.read_bytes());data[-1]^=1;game.write_bytes(data)
        with self.assertRaisesRegex(SystemExit,'validation failed'):installer.validate_bundle(self.bundle)
    def test_historical_lua_installer_stops_before_external_actions(self):
        result=subprocess.run(['bash',str(ROOT/'scripts/install-waveshare-console-os-p4cart.sh'),'--preflight'],
                              capture_output=True,text=True,check=False)
        self.assertEqual(result.returncode,2);self.assertIn('retired Lua',result.stderr)
        self.assertIn('No device or content write',result.stderr);self.assertEqual(result.stdout,'')
if __name__=='__main__':unittest.main()
