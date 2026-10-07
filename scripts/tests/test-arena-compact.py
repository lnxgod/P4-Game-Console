#!/usr/bin/env python3
"""Offline byte preservation and actual pinned dependency checks."""
import importlib.util
import json
from pathlib import Path
import struct
import re
import tempfile
from unittest.mock import patch
import unittest

ROOT=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location("trim",ROOT/"scripts/doom/arena-compact.py")
trim=importlib.util.module_from_spec(spec)
spec.loader.exec_module(trim)

def directory(data):
    magic,count,offset=struct.unpack_from("<4sII",data)
    assert magic==b"IWAD" and offset+count*16==len(data)
    result=[]
    for i in range(count):
        pos,size,name=struct.unpack_from("<II8s",data,offset+i*16)
        assert 12<=pos<=offset and size<=offset-pos
        result.append((name,data[pos:pos+size]))
    return result

def decoded(raw): return raw.split(b"\0",1)[0].decode("ascii").upper()

def validate_texture_dependencies(lumps):
    byname={decoded(n):data for n,data in lumps}
    pn=byname["PNAMES"]
    count=struct.unpack_from("<I",pn)[0]
    names=[decoded(pn[4+i*8:12+i*8])for i in range(count)]
    for table in ("TEXTURE1","TEXTURE2"):
        if table not in byname:continue
        data=byname[table]
        for i in range(struct.unpack_from("<I",data)[0]):
            offset=struct.unpack_from("<I",data,4+i*4)[0]
            for j in range(struct.unpack_from("<H",data,offset+20)[0]):
                index=struct.unpack_from("<H",data,offset+26+j*10)[0]
                assert names[index] in byname, names[index]

class CandidateTests(unittest.TestCase):
    def test_recipe_drift_is_rejected_before_publication(self):
        captured = trim.RECIPE.read_bytes()
        with tempfile.TemporaryDirectory() as temporary:
            changed = Path(temporary)/'recipe.json'
            changed.write_bytes(captured + b' ')
            with patch.object(trim, 'RECIPE', changed):
                with self.assertRaisesRegex(ValueError, 'recipe changed during generation'):
                    trim.analyze(captured)

    @classmethod
    def setUpClass(cls):
        recipe=trim.load_recipe()
        if any(not (ROOT/x["path"]).is_file() for x in recipe["inputs"]):
            raise unittest.SkipTest("pinned local WAD inputs absent; run arena-content.py --fetch")
        cls.report,cls.candidate=trim.analyze()
        cls.original=(trim.ROOT/"local-data/doom/freedoom2.wad").read_bytes()
        cls.old=directory(cls.original)
        cls.new=directory(cls.candidate)

    def test_whole_identity_and_determinism(self):
        report,generated=trim.analyze()
        self.assertEqual(generated,self.candidate)
        self.assertEqual(report["candidate_sha256"],self.report["candidate_sha256"])
        self.assertEqual(len(generated),12253462)

    def test_exact_retained_payload_names_and_order(self):
        retained=self.report["retained_lumps"]
        self.assertEqual(len(retained),len(self.new))
        self.assertEqual([r["original"]["index"]for r in retained],sorted(r["original"]["index"]for r in retained))
        for i,r in enumerate(retained):
            original=self.old[r["original"]["index"]]
            self.assertEqual(self.new[i][0],original[0])
            if decoded(original[0])not in("TEXTURE1","TEXTURE2"):
                self.assertEqual(self.new[i][1],original[1])

    def test_only_allowed_removals(self):
        removed=self.report["removed_lumps"]
        start=next(i for i,x in enumerate(self.old)if decoded(x[0])=="P_START")
        end=next(i for i,x in enumerate(self.old)if decoded(x[0])=="P_END")
        for item in removed:
            if item["name"]not in trim.GEOMETRY:
                self.assertTrue(start<item["index"]<end)
        self.assertEqual(sum(x["name"]in trim.GEOMETRY for x in removed),320)
        self.assertEqual(sum(x["name"]not in trim.GEOMETRY for x in removed),709)

    def test_every_sprite_flat_sound_music_ui_preserved(self):
        old={decoded(n):d for n,d in self.old if decoded(n)not in trim.GEOMETRY}
        new={decoded(n):d for n,d in self.new}
        a=list(old).index("P_START");b=list(old).index("P_END")
        for index,(n,d)in enumerate(old.items()):
            if n not in("TEXTURE1","TEXTURE2")and not a<index<b:
                self.assertIn(n,new)
                self.assertEqual(d,new[n])

    def test_map_lookup_markers_and_all29_dependencies(self):
        names={decoded(n)for n,d in self.new}
        for i in range(1,33):self.assertIn(f"MAP{i:02}",names)
        self.assertEqual(len(self.report["supported_map_dependencies"]),29)
        retained={x["name"]for x in self.report["retained_textures"]}
        for m in self.report["supported_map_dependencies"]:
            self.assertTrue(set(m["textures"])<=retained)
            self.assertTrue(set(m["flats"])<=names)
        self.assertEqual(self.report["texture_zero"],self.report["retained_textures"][0]["name"])

    def test_texture_records_are_exact_and_patch_closure_complete(self):
        _,original,_=trim.parse(self.original)
        _,records=trim.textures(original)
        original_records={x["name"]:x for x in records}
        for record in self.report["retained_textures"]:
            self.assertEqual(record["record_sha256"],trim.sha(original_records[record["name"]]["raw"]))
        validate_texture_dependencies(self.new)
        old=dict((decoded(n),d)for n,d in self.old)
        new=dict((decoded(n),d)for n,d in self.new)
        self.assertEqual(old["PNAMES"],new["PNAMES"])
        # Independently unpack the emitted table, not just the analyzer report.
        for table in ("TEXTURE1","TEXTURE2"):
            if table not in new:continue
            data=new[table]
            expected=[r for r in self.report["retained_textures"]if r["table"]==table]
            self.assertEqual(struct.unpack_from("<I",data)[0],len(expected))
            for index,record in enumerate(expected):
                offset=struct.unpack_from("<I",data,4+index*4)[0]
                count=struct.unpack_from("<H",data,offset+20)[0]
                self.assertEqual(data[offset:offset+22+count*10],original_records[record["name"]]["raw"])

    def test_negative_missing_live_patch_is_rejected(self):
        required=self.report["retained_textures"][0]["patch_names"][0]
        mutant=[x for x in self.new if decoded(x[0])!=required]
        with self.assertRaises(AssertionError):validate_texture_dependencies(mutant)

    def test_switch_and_animation_intervals_preserved(self):
        names=[x["name"]for x in self.report["retained_textures"]]
        for pair in self.report["switch_pairs"]:self.assertTrue(set(pair)<=set(names))
        for interval in self.report["animation_intervals"]:
            if interval["kind"]=="texture":
                self.assertEqual(names[names.index(interval["start"]):names.index(interval["end"])+1],interval["names"])


    def test_untrusted_bounds_fail_closed(self):
        with self.assertRaises(ValueError):trim.parse(b"IWAD")
        with self.assertRaises(ValueError):trim.parse(struct.pack("<4sII",b"IWAD",1,0xffffffff))
        bad=bytearray(self.original)
        offset=struct.unpack_from("<I",bad,8)[0]
        struct.pack_into("<I",bad,offset+4,0xffffffff)
        with self.assertRaises(ValueError):trim.parse(bytes(bad))

class OutputTests(unittest.TestCase):
    def test_exclusive_idempotent_and_conflict_preserving_output(self):
        with tempfile.TemporaryDirectory() as directory, patch.object(trim.subprocess,"run") as ignored:
            target=Path(directory)/"ARENA2.WAD"
            trim.install_generated(target,b"exact")
            trim.install_generated(target,b"exact")
            self.assertEqual(target.read_bytes(),b"exact")
            with self.assertRaisesRegex(ValueError,"Conflicting"):
                trim.install_generated(target,b"different")
            self.assertEqual(target.read_bytes(),b"exact")
            ignored.assert_called()
    def test_symlink_output_refused_and_destination_unchanged(self):
        with tempfile.TemporaryDirectory() as directory, patch.object(trim.subprocess,"run"):
            destination=Path(directory)/"original";destination.write_bytes(b"original")
            target=Path(directory)/"ARENA2.WAD";target.symlink_to(destination)
            with self.assertRaisesRegex(ValueError,"Conflicting"):
                trim.install_generated(target,b"new")
            self.assertEqual(destination.read_bytes(),b"original")
    def test_recipe_matches_current_engine_asset_rules(self):
        recipe=trim.load_recipe()
        engine=ROOT/"third_party/doomgeneric/doomgeneric"
        switches=re.findall(r'\{"([A-Z0-9_]+)",\s*"([A-Z0-9_]+)",\s*[123]\}',(engine/"p_switch.c").read_text())
        self.assertEqual([list(x)for x in switches],recipe["switch_pairs"])
        animations=re.findall(r'\{(true|false),\s*"([A-Z0-9_]+)",\s*"([A-Z0-9_]+)",\s*8\}',(engine/"p_spec.c").read_text())
        self.assertEqual([(x["kind"],x["end"],x["start"])for x in recipe["animation_intervals"]],[("texture"if kind=="true"else"flat",end,start)for kind,end,start in animations])
        self.assertIn("#undef FEATURE_DEHACKED",(engine/"doomfeatures.h").read_text())
        # A new directly referenced patch requires an explicit recipe review.
        if all((ROOT/x["path"]).is_file()for x in recipe["inputs"]):
            _,lumps,_=trim.parse((ROOT/recipe["inputs"][0]["path"]).read_bytes())
            pnames,_=trim.textures(lumps)
            protected=set(recipe["protected_patch_literals"])
            for directory in (engine,ROOT/"apps/console_os/main",ROOT/"apps/doom_audio_probe/components/doom_engine_audio",ROOT/"apps/doom_embedded_touch_audio/main"):
                for path in directory.rglob("*"):
                    if path.suffix not in (".c",".h"):continue
                    for literal in re.findall(r'"([^"\n]*)"',path.read_text()):
                        if literal.upper()in pnames:self.assertIn(literal.upper(),protected,str(path))

if __name__=="__main__":unittest.main(verbosity=2)
