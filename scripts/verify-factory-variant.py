#!/usr/bin/env python3

"""Verify the Elecrow factory-firmware variant without committing image assets."""

from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import re
import tempfile
import urllib.request
import zipfile


ROOT = pathlib.Path(__file__).resolve().parents[1]
EVIDENCE_PATH = ROOT / "hardware/evidence/elecrow-factory-firmware-variant.json"
PROFILE_PATH = ROOT / "hardware/board-profile.json"
MANIFEST_PATH = ROOT / "hardware/backups/manifest.json"
SHA256_RE = re.compile(r"^[0-9a-f]{64}$")
LVGL_BRANCH_START = "#if LV_COLOR_DEPTH == 16 && LV_COLOR_16_SWAP == 0"
LVGL_BRANCH_END = "#endif"
HEX_BYTE_RE = re.compile(r"0x([0-9a-fA-F]{2})")


def fail(message: str) -> None:
    raise SystemExit(f"factory-variant verification failed: {message}")


def require(condition: bool, message: str) -> None:
    if not condition:
        fail(message)


def load_object(path: pathlib.Path) -> dict:
    with path.open(encoding="utf-8") as source:
        value = json.load(source)
    require(isinstance(value, dict), f"expected a JSON object: {path}")
    return value


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def sha256_file(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def require_sha256(value: object, label: str) -> str:
    require(isinstance(value, str), f"{label} SHA-256 is not a string")
    require(SHA256_RE.fullmatch(value) is not None, f"invalid {label} SHA-256")
    return value


def parse_offset(value: object, label: str) -> int:
    require(isinstance(value, str), f"{label} must be a hexadecimal string")
    require(re.fullmatch(r"0x[0-9a-f]+", value) is not None, f"invalid {label}")
    return int(value, 16)


def variant_map(evidence: dict) -> dict[str, dict]:
    variants = evidence.get("reference_variants")
    require(isinstance(variants, list), "reference_variants must be an array")
    result: dict[str, dict] = {}
    for variant in variants:
        require(isinstance(variant, dict), "reference variant must be an object")
        variant_id = variant.get("id")
        require(isinstance(variant_id, str) and variant_id, "reference variant has no ID")
        require(variant_id not in result, f"duplicate reference variant ID: {variant_id}")
        result[variant_id] = variant
    require(set(result) == {"inch7", "inch9", "inch10_1"}, "unexpected candidate set")
    return result


def validate_metadata(evidence: dict, profile: dict, manifest: dict) -> tuple[dict[str, dict], dict]:
    require(evidence.get("schema") == 1, "unsupported evidence schema")
    require(evidence.get("classification") == "factory-firmware-variant", "wrong classification")
    require(evidence.get("result") == "pass", "variant evidence is not a pass")

    variants = variant_map(evidence)
    method = evidence.get("method")
    require(isinstance(method, dict), "method must be an object")
    require(method.get("asset_bytes") == 1843200, "unexpected compiled asset length")
    require(method.get("width_pixels") == 1024, "unexpected asset width")
    require(method.get("height_pixels") == 600, "unexpected asset height")
    require(method.get("bytes_per_pixel") == 3, "unexpected asset bytes per pixel")
    require(
        method["width_pixels"] * method["height_pixels"] * method["bytes_per_pixel"]
        == method["asset_bytes"],
        "asset geometry does not equal its byte count",
    )
    require(
        method.get("lvgl_source_condition") == "LV_COLOR_DEPTH == 16 && LV_COLOR_16_SWAP == 0",
        "unexpected LVGL source condition",
    )

    backup_evidence = evidence.get("factory_backup")
    require(isinstance(backup_evidence, dict), "factory_backup must be an object")
    backup_manifest = manifest.get("backup")
    require(isinstance(backup_manifest, dict), "backup manifest has no backup object")
    for field in ("file", "bytes", "sha256"):
        require(
            backup_evidence.get(field) == backup_manifest.get(field),
            f"evidence backup {field} differs from manifest",
        )
    require_sha256(backup_evidence.get("sha256"), "factory backup")

    conclusion = evidence.get("conclusion")
    require(isinstance(conclusion, dict), "conclusion must be an object")
    require(conclusion.get("scope") == "factory_firmware_variant", "conclusion scope is too broad")
    require(conclusion.get("screen_inches") == 10.1, "unexpected concluded screen size")
    require(conclusion.get("sku") == "DHE04310D", "unexpected concluded SKU")
    require(conclusion.get("confidence") == "very_high", "unexpected confidence")
    require(conclusion.get("physical_screen_sku_confirmed") is False, "physical SKU must remain unconfirmed")
    require(conclusion.get("pcb_revision") is None, "evidence must not infer a PCB revision")
    require(conclusion.get("pin_map_authorized") is False, "evidence must not authorize pins")

    identification = profile.get("identification")
    require(isinstance(identification, dict), "board profile identification must be an object")
    require(profile.get("screen_inches") == conclusion["screen_inches"], "profile screen differs from evidence")
    require(profile.get("sku") == conclusion["sku"], "profile SKU differs from evidence")
    require(profile.get("pcb_revision") is None, "profile PCB revision must remain unconfirmed")
    require(profile.get("pin_map_authorized") is False, "profile pin map must remain unauthorized")
    require(
        identification.get("screen_sku_scope") == conclusion["scope"],
        "profile screen/SKU scope differs from evidence",
    )
    require(
        identification.get("physical_screen_sku_confirmed") is False,
        "profile must not claim physical screen/SKU confirmation",
    )
    require(
        identification.get("evidence") == EVIDENCE_PATH.relative_to(ROOT).as_posix(),
        "profile points to the wrong evidence file",
    )

    for variant_id, variant in variants.items():
        archive = variant.get("factory_source_archive")
        selector = variant.get("selector")
        asset = variant.get("asset")
        offsets = variant.get("backup_full_asset_occurrence_offsets")
        require(isinstance(archive, dict), f"{variant_id} archive metadata must be an object")
        require(isinstance(selector, dict), f"{variant_id} selector metadata must be an object")
        require(isinstance(asset, dict), f"{variant_id} asset metadata must be an object")
        require(isinstance(offsets, list), f"{variant_id} occurrence offsets must be an array")
        require_sha256(archive.get("sha256"), f"{variant_id} archive")
        require_sha256(selector.get("source_sha256"), f"{variant_id} selector source")
        require_sha256(asset.get("source_sha256"), f"{variant_id} asset source")
        require_sha256(asset.get("sha256"), f"{variant_id} compiled asset")
        require(asset.get("bytes") == method["asset_bytes"], f"{variant_id} asset length mismatch")
        for offset in offsets:
            parse_offset(offset, f"{variant_id} occurrence offset")

    require(variants["inch7"]["backup_full_asset_occurrence_offsets"] == [], "7-inch negative changed")
    require(variants["inch9"]["backup_full_asset_occurrence_offsets"] == [], "9-inch negative changed")
    require(
        variants["inch10_1"]["backup_full_asset_occurrence_offsets"] == ["0x71004"],
        "10.1-inch occurrence changed",
    )
    return variants, conclusion


def validate_local_backup(evidence: dict, variants: dict[str, dict]) -> bytes | None:
    backup = evidence["factory_backup"]
    path = (ROOT / backup["file"]).resolve()
    backup_root = (ROOT / "hardware/backups").resolve()
    require(path.is_relative_to(backup_root), "backup path leaves hardware/backups")
    if not path.exists():
        return None

    require(path.stat().st_size == backup["bytes"], "local backup byte count differs from evidence")
    require(sha256_file(path) == backup["sha256"], "local backup SHA-256 differs from evidence")
    flash = path.read_bytes()

    selected = variants["inch10_1"]
    asset = selected["asset"]
    for encoded_offset in selected["backup_full_asset_occurrence_offsets"]:
        offset = parse_offset(encoded_offset, "selected occurrence offset")
        end = offset + asset["bytes"]
        require(end <= len(flash), "selected occurrence leaves the backup")
        require(
            sha256_bytes(flash[offset:end]) == asset["sha256"],
            "selected backup slice does not match the pinned 10.1-inch asset hash",
        )
    return flash


def extract_compiled_asset(archive_path: pathlib.Path, variant: dict) -> bytes:
    archive_metadata = variant["factory_source_archive"]
    variant_id = variant["id"]
    require(archive_path.is_file(), f"missing {variant_id} archive: {archive_path}")
    require(archive_path.stat().st_size == archive_metadata["bytes"], f"{variant_id} archive length mismatch")
    require(sha256_file(archive_path) == archive_metadata["sha256"], f"{variant_id} archive SHA-256 mismatch")

    with zipfile.ZipFile(archive_path) as archive:
        selector_source = archive.read(variant["selector"]["source_path"])
        require(
            sha256_bytes(selector_source) == variant["selector"]["source_sha256"],
            f"{variant_id} selector source SHA-256 mismatch",
        )
        require(
            variant["selector"]["expected_statement"] in selector_source.decode("utf-8"),
            f"{variant_id} selector does not choose its named background",
        )

        asset_source = archive.read(variant["asset"]["source_path"])
        require(
            sha256_bytes(asset_source) == variant["asset"]["source_sha256"],
            f"{variant_id} asset source SHA-256 mismatch",
        )

    source_text = asset_source.decode("utf-8")
    require(source_text.count(LVGL_BRANCH_START) == 1, f"{variant_id} LVGL branch is ambiguous")
    branch_tail = source_text.split(LVGL_BRANCH_START, 1)[1]
    require(LVGL_BRANCH_END in branch_tail, f"{variant_id} LVGL branch has no end")
    branch = branch_tail.split(LVGL_BRANCH_END, 1)[0]
    compiled_asset = bytes(int(value, 16) for value in HEX_BYTE_RE.findall(branch))
    require(len(compiled_asset) == variant["asset"]["bytes"], f"{variant_id} compiled asset length mismatch")
    require(
        sha256_bytes(compiled_asset) == variant["asset"]["sha256"],
        f"{variant_id} compiled asset SHA-256 mismatch",
    )
    return compiled_asset


def exact_occurrence_offsets(haystack: bytes, needle: bytes) -> list[str]:
    offsets: list[str] = []
    search_from = 0
    while True:
        offset = haystack.find(needle, search_from)
        if offset < 0:
            return offsets
        offsets.append(hex(offset))
        search_from = offset + 1


def parse_archive_arguments(values: list[str], expected_ids: set[str]) -> dict[str, pathlib.Path]:
    result: dict[str, pathlib.Path] = {}
    for value in values:
        variant_id, separator, encoded_path = value.partition("=")
        require(separator == "=" and variant_id and encoded_path, f"invalid --reference-archive: {value}")
        require(variant_id in expected_ids, f"unknown reference variant: {variant_id}")
        require(variant_id not in result, f"duplicate reference archive: {variant_id}")
        result[variant_id] = pathlib.Path(encoded_path).expanduser().resolve()
    if result:
        require(set(result) == expected_ids, "provide all three reference archives or none")
    return result


def download_references(variants: dict[str, dict], destination: pathlib.Path) -> dict[str, pathlib.Path]:
    result: dict[str, pathlib.Path] = {}
    for variant_id, variant in variants.items():
        path = destination / f"{variant_id}.zip"
        request = urllib.request.Request(
            variant["factory_source_archive"]["url"],
            headers={"User-Agent": "esp32-p4-factory-variant-verifier/1"},
        )
        with urllib.request.urlopen(request, timeout=60) as response, path.open("wb") as output:
            while chunk := response.read(1024 * 1024):
                output.write(chunk)
        result[variant_id] = path
    return result


def run_full_reference_search(
    flash: bytes | None,
    variants: dict[str, dict],
    archives: dict[str, pathlib.Path],
) -> None:
    require(flash is not None, "the ignored local factory backup is required for full reference search")
    for variant_id, variant in variants.items():
        compiled_asset = extract_compiled_asset(archives[variant_id], variant)
        actual_offsets = exact_occurrence_offsets(flash, compiled_asset)
        require(
            actual_offsets == variant["backup_full_asset_occurrence_offsets"],
            f"{variant_id} full-asset occurrence result differs from evidence",
        )


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--reference-archive",
        action="append",
        default=[],
        metavar="ID=PATH",
        help="verify an exact local source archive; supply inch7, inch9, and inch10_1 together",
    )
    parser.add_argument(
        "--fetch-references",
        action="store_true",
        help="download all three commit-pinned official source archives into a temporary directory",
    )
    args = parser.parse_args()
    require(not (args.fetch_references and args.reference_archive), "choose fetched or local references, not both")

    evidence = load_object(EVIDENCE_PATH)
    profile = load_object(PROFILE_PATH)
    manifest = load_object(MANIFEST_PATH)
    variants, conclusion = validate_metadata(evidence, profile, manifest)
    flash = validate_local_backup(evidence, variants)
    archives = parse_archive_arguments(args.reference_archive, set(variants))

    if args.fetch_references:
        with tempfile.TemporaryDirectory(prefix="p4-elecrow-reference-") as temporary_directory:
            fetched = download_references(variants, pathlib.Path(temporary_directory))
            run_full_reference_search(flash, variants, fetched)
        mode = "full-fetched-reference-search"
    elif archives:
        run_full_reference_search(flash, variants, archives)
        mode = "full-local-reference-search"
    elif flash is not None:
        mode = "local-selected-slice"
    else:
        mode = "metadata-only-backup-absent"

    print(
        "factory firmware variant: PASS "
        f"mode={mode} screen={conclusion['screen_inches']}in sku={conclusion['sku']} "
        "scope=factory_firmware_variant pcb_revision=unconfirmed pins=unauthorized"
    )


if __name__ == "__main__":
    main()
