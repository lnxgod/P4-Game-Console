#!/usr/bin/env python3
"""Generate the exact compact Arena-only IWAD from pinned local inputs.

Artwork, audio, sprites and flats retain their original bytes. Campaign geometry
and unused wall patches are removed; retained texture records preserve order.
This derivative is not a general/campaign IWAD. No downloads or device access.
"""
from __future__ import annotations
import argparse
import collections
import hashlib
import json
import os
import tempfile
from pathlib import Path
import re
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[2]
RECIPE = ROOT / "third_party/arena-compact-v1.json"

def load_recipe(content=None):
    data = json.loads(RECIPE.read_bytes() if content is None else content)
    if data["schema"] != 1 or data["id"] != "p4-arena-compact-v1":
        raise ValueError("unsupported compact Arena recipe")
    return data

def require_recipe_unchanged(content):
    if RECIPE.is_symlink() or RECIPE.read_bytes() != content:
        raise ValueError("compact Arena recipe changed during generation")

GEOMETRY = "THINGS LINEDEFS SIDEDEFS VERTEXES SEGS SSECTORS NODES SECTORS REJECT BLOCKMAP".split()

def sha(data):
    return hashlib.sha256(data).hexdigest()

def name(raw):
    return raw.split(b"\0", 1)[0].decode("ascii").upper()

def parse(data):
    if len(data) < 12:
        raise ValueError("short WAD header")
    magic, count, directory = struct.unpack_from("<4sII", data)
    if magic not in (b"IWAD", b"PWAD") or not count or directory < 12 or count > (len(data)-directory)//16:
        raise ValueError("invalid WAD directory")
    lumps = []
    for i in range(count):
        offset, size, rawname = struct.unpack_from("<II8s", data, directory+i*16)
        if offset > len(data) or size > len(data)-offset:
            raise ValueError("out-of-range lump")
        if size and (offset < 12 or not (offset+size <= directory or offset >= directory+count*16)):
            raise ValueError("lump overlaps header/directory")
        lumps.append({"index": i, "name": name(rawname), "raw_name_hex": rawname.hex(),
                      "offset": offset, "size": size, "sha256": sha(data[offset:offset+size]),
                      "data": data[offset:offset+size]})
    maps = []
    for i, lump in enumerate(lumps):
        if not re.fullmatch(r"MAP[0-9]{2}", lump["name"]):
            continue
        if lump["size"] or [x["name"] for x in lumps[i+1:i+11]] != GEOMETRY:
            raise ValueError("unexpected map format")
        maps.append(i)
    return magic, lumps, maps

def repack(magic, lumps):
    payload = bytearray(b"\0"*12)
    directory = []
    for lump in lumps:
        directory.append(struct.pack("<II8s", len(payload), len(lump["data"]), bytes.fromhex(lump["raw_name_hex"])))
        payload.extend(lump["data"])
    offset = len(payload)
    payload.extend(b"".join(directory))
    struct.pack_into("<4sII", payload, 0, magic, len(lumps), offset)
    return bytes(payload)

def public(lump):
    return {k: v for k, v in lump.items() if k != "data"}

def textures(lumps):
    lookup = {x["name"]: x for x in lumps}
    data = lookup["PNAMES"]["data"]
    count = struct.unpack_from("<I", data)[0]
    if len(data) != 4+count*8:
        raise ValueError("unexpected PNAMES length")
    patches = [name(data[4+i*8:12+i*8]) for i in range(count)]
    if len(set(patches)) != len(patches):
        raise ValueError("duplicate PNAMES")
    result = []
    for label in ("TEXTURE1", "TEXTURE2"):
        if label not in lookup:
            continue
        data = lookup[label]["data"]
        count = struct.unpack_from("<I", data)[0]
        if count > (len(data)-4)//4:
            raise ValueError("texture directory out of bounds")
        for i in range(count):
            offset = struct.unpack_from("<I", data, 4+i*4)[0]
            if offset < 4+count*4 or offset+22 > len(data):
                raise ValueError("texture header out of bounds")
            patch_count = struct.unpack_from("<H", data, offset+20)[0]
            end = offset+22+patch_count*10
            if not patch_count or end > len(data):
                raise ValueError("texture patches out of bounds")
            indices = [struct.unpack_from("<H", data, offset+26+j*10)[0] for j in range(patch_count)]
            if any(x >= len(patches) for x in indices):
                raise ValueError("texture PNAMES index out of range")
            result.append({"name": name(data[offset:offset+8]), "table": label, "table_index": i,
                           "raw": data[offset:end], "pnames_indices": indices,
                           "patch_names": [patches[x] for x in indices]})
    if len({x["name"] for x in result}) != len(result):
        raise ValueError("duplicate texture names")
    return patches, result

def texture_table(records):
    result = bytearray(struct.pack("<I", len(records))+b"\0"*(4*len(records)))
    for i, record in enumerate(records):
        struct.pack_into("<I", result, 4+4*i, len(result))
        result.extend(record["raw"])
    return bytes(result)

def analyze(recipe_bytes=None):
    recipe_bytes = RECIPE.read_bytes() if recipe_bytes is None else recipe_bytes
    recipe = load_recipe(recipe_bytes)
    inputs = []
    for entry in recipe["inputs"]:
        rel, size, digest = entry["path"], entry["size"], entry["sha256"]
        path = ROOT / rel
        if path.is_symlink() or not path.is_file():
            raise ValueError(f"input must be a regular file: {path}")
        data = path.read_bytes()
        if len(data) != size or sha(data) != digest:
            raise ValueError(f"pinned content mismatch: {rel}")
        magic, lumps, maps = parse(data)
        inputs.append({"path": rel, "size": size, "sha256": digest, "magic": magic,
                       "lumps": lumps, "maps": maps})
    base = inputs[0]
    lumps = base["lumps"]
    lookup = {x["name"]: x for x in lumps}
    duplicates = {n:c for n,c in collections.Counter(x["name"] for x in lumps).items() if c>1 and n not in GEOMETRY}
    if duplicates:
        raise ValueError("ambiguous global lump precedence")
    pnames, tex = textures(lumps)
    texture_names = [x["name"] for x in tex]
    required = {texture_names[0], "SKY1", "SKY2", "SKY3"}
    required_flats = {"F_SKY1"}
    map_dependencies = []
    for pack, expected in zip(inputs[1:], (5,24)):
        if [pack["lumps"][i]["name"] for i in pack["maps"]] != [f"MAP{i:02}" for i in range(1,expected+1)]:
            raise ValueError("unexpected supported pack map set")
        for marker in pack["maps"]:
            parts = pack["lumps"][marker:marker+11]
            sidedefs, sectors, things = parts[3]["data"], parts[8]["data"], parts[1]["data"]
            if len(sidedefs)%30 or len(sectors)%26 or len(things)%10:
                raise ValueError("unexpected map record width")
            map_textures = {name(sidedefs[i+j:i+j+8]) for i in range(0,len(sidedefs),30) for j in (4,12,20)} - {"-"}
            map_flats = {name(sectors[i+j:i+j+8]) for i in range(0,len(sectors),26) for j in (4,12)}
            required |= map_textures
            required_flats |= map_flats
            map_dependencies.append({"pack": pack["path"], "map": parts[0]["name"],
                                     "textures": sorted(map_textures), "flats": sorted(map_flats),
                                     "thing_types": sorted({struct.unpack_from("<H", things, i+6)[0] for i in range(0,len(things),10)}),
                                     "lumps": [public(x) for x in parts]})
    direct = sorted(required)
    switches = recipe["switch_pairs"]
    if len(switches) != 40:
        raise ValueError("unexpected engine switch count")
    for pair in switches:
        required.update(pair)
    animations = [("true" if item["kind"] == "texture" else "false",
                   item["end"], item["start"]) for item in recipe["animation_intervals"]]
    if len(animations) != 22:
        raise ValueError("unexpected engine animation count")
    animation_intervals = []
    all_names = [x["name"] for x in lumps]
    for kind, end, start in animations:
        names = texture_names if kind == "true" else all_names
        first, last = names.index(start), names.index(end)
        if first >= last:
            raise ValueError("bad animation interval")
        interval = names[first:last+1]
        (required if kind == "true" else required_flats).update(interval)
        animation_intervals.append({"kind": "texture" if kind == "true" else "flat", "start": start, "end": end, "names": interval})
    if animation_intervals != recipe["animation_intervals"]:
        raise ValueError("animation order differs from reviewed recipe")
    # Fixed reviewed literal protection makes generation independent of build
    # timestamps and unrelated source edits. The host test audits current rules.
    literals = {key: ["reviewed recipe v1"] for key in recipe["protected_patch_literals"]}
    if required-set(texture_names):
        raise ValueError("missing required texture")
    kept_tex = [x for x in tex if x["name"] in required]
    if [x["name"] for x in kept_tex] != recipe["retained_texture_names"]:
        raise ValueError("texture closure differs from reviewed recipe")
    all_patch_names = {p for t in tex for p in t["patch_names"]}
    patch_start, patch_end = all_names.index("P_START"), all_names.index("P_END")
    non_patch_namespace_references = {n for n in all_patch_names if not patch_start < lookup[n]["index"] < patch_end}
    kept_patch_names = {p for t in kept_tex for p in t["patch_names"]} | set(literals) | non_patch_namespace_references
    removed_patch_names = all_patch_names-kept_patch_names
    if any(n not in lookup for n in kept_patch_names):
        raise ValueError("missing kept patch")
    map_geometry_indices = {i+j for i in base["maps"] for j in range(1,11)}
    if [lumps[i]["name"] for i in base["maps"]] != [f"MAP{i:02}" for i in range(1,33)]:
        raise ValueError("unexpected base map set")
    candidate_lumps = []
    retained, removed = [], []
    for lump in lumps:
        if lump["index"] in map_geometry_indices or lump["name"] in removed_patch_names:
            removed.append(public(lump))
            continue
        result = dict(lump)
        if lump["name"] in ("TEXTURE1", "TEXTURE2"):
            result["data"] = texture_table([x for x in kept_tex if x["table"] == lump["name"]])
        candidate_lumps.append(result)
        retained.append({"new_index": len(candidate_lumps)-1, "original": public(lump),
                         "new_size": len(result["data"]), "new_sha256": sha(result["data"]),
                         "payload_unchanged": result["data"] == lump["data"]})
    candidate = repack(base["magic"], candidate_lumps)
    expected = recipe["output"]
    if len(candidate) != expected["size_bytes"] or sha(candidate) != expected["sha256"]:
        raise ValueError("derived output differs from pinned compact Arena identity")
    if len(candidate_lumps) != expected["lump_count"]:
        raise ValueError("derived lump count differs from reviewed recipe")
    geom_lumps = [x for x in lumps if x["index"] not in map_geometry_indices]
    geometry_only = repack(base["magic"], geom_lumps)
    categories = collections.defaultdict(lambda: {"count":0,"bytes":0})
    namespace = "other"
    for lump in lumps:
        n = lump["name"]
        if n in ("S_START", "P_START", "F_START"):
            namespace = {"S_START":"sprites", "P_START":"patches", "F_START":"flats"}[n]
        category = "map_geometry" if lump["index"] in map_geometry_indices else namespace
        if category == "other":
            if n.startswith("D_"): category="music"
            elif n.startswith("DS"): category="sound"
            elif n.startswith("DP"): category="pc_sound"
        categories[category]["count"] += 1
        categories[category]["bytes"] += lump["size"]
        if n in ("S_END", "P_END", "F_END"): namespace="other"
    report = {"schema":1,"scope":"deterministic Arena-only derivative; not hardware or PSRAM acceptance",
              "inputs":[{k:v for k,v in x.items() if k not in ("lumps","maps","magic")} | {"lump_count":len(x["lumps"]),"map_count":len(x["maps"])} for x in inputs],
              "categories":dict(categories), "supported_map_dependencies":map_dependencies,
              "texture_zero":texture_names[0], "direct_map_sky_zero_textures":direct,
              "switch_pairs":switches,"animation_intervals":animation_intervals,
              "required_flats":sorted(required_flats),"retained_textures":[{k:v for k,v in t.items() if k!="raw"} | {"record_sha256":sha(t["raw"]),"record_bytes":len(t["raw"])} for t in kept_tex],
              "pnames_unchanged":True,"pnames":pnames,
              "direct_patch_literal_protection":dict(literals),
              "non_patch_namespace_references_preserved":sorted(non_patch_namespace_references),"recipe_sha256":sha(recipe_bytes),
              "required_patch_lumps":[public(lookup[n]) for n in sorted(kept_patch_names)],
              "retained_lumps":retained,"removed_lumps":removed,
              "removed_patch_count":len(removed_patch_names),
              "removed_patch_payload_bytes":sum(lookup[n]["size"] for n in removed_patch_names),
              "geometry_only_bytes":len(geometry_only),"geometry_only_sha256":sha(geometry_only),
              "candidate_bytes":len(candidate),"candidate_sha256":sha(candidate),
              "candidate_lumps":len(candidate_lumps),"savings_bytes":base["size"]-len(candidate),
              "unchanged_payload_contract":"All retained original payload bytes are exact except TEXTURE1/2 metadata; retained texture records exact. All sprites, flats, UI, sounds, music and map markers retained. Packs unchanged.",
              "not_general_iwad":True,
              "open_gates":["Runtime PSRAM high-water and allocation margin","Matching peers and new checkpoint content identity","Physical sound/music/artwork and performance acceptance"]}
    require_recipe_unchanged(recipe_bytes)
    return report, candidate

def install_generated(path, content):
    """Create only under ignored storage, never overwrite different input bytes."""
    path = Path(path).absolute()
    subprocess.run(["git", "check-ignore", "-q", str(path)], cwd=ROOT, check=True)
    if path.is_symlink() or (path.exists() and (not path.is_file() or path.read_bytes() != content)):
        raise ValueError(f"Conflicting existing content: {path}")
    if path.is_file():
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(dir=path.parent, delete=False) as output:
        temporary = Path(output.name)
        output.write(content)
    try:
        os.link(temporary, path)
    finally:
        temporary.unlink()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, help="ignored output path; default is the pinned local Arena path")
    parser.add_argument("--report", type=Path, help="optional ignored JSON preservation/dependency report")
    parser.add_argument("--check", action="store_true", help="regenerate in memory and require the existing exact output")
    args = parser.parse_args()
    recipe_bytes = RECIPE.read_bytes()
    recipe = load_recipe(recipe_bytes)
    output = args.output or ROOT / recipe["output"]["local_path"]
    report, candidate = analyze(recipe_bytes)
    require_recipe_unchanged(recipe_bytes)
    if args.check:
        if output.is_symlink() or not output.is_file() or output.read_bytes() != candidate:
            raise ValueError(f"missing or mismatched compact Arena output: {output}")
    else:
        install_generated(output, candidate)
    if args.report:
        install_generated(args.report, (json.dumps(report, indent=2, sort_keys=True)+"\n").encode())
    print(f"P4_ARENA_COMPACT PASS bytes={len(candidate)} sha256={sha(candidate)} maps=29 campaign=0")


if __name__ == "__main__":
    main()
