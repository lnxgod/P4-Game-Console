#!/usr/bin/env python3

"""Check Console OS parity and scaffold evidence-first ESP32-P4 board ports."""

from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import re
import sys
from typing import NoReturn


ROOT = pathlib.Path(__file__).resolve().parents[1]
CONTRACT_PATH = ROOT / "hardware/boards/console-os-port-contract.json"
ID_RE = re.compile(r"^[a-z][a-z0-9-]{2,63}$")
COMMIT_RE = re.compile(r"^[0-9a-f]{40}$")
SHA256_RE = re.compile(r"^[0-9a-f]{64}$")
SAFE_TEXT_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9 ._+()/:-]{0,95}$")
INPUTS = {"touch", "gamepad", "keyboard", "mouse"}
TRANSFERS = {"usb-device-msc", "powered-off-removable-media"}


def die(message: str) -> NoReturn:
    raise SystemExit(f"board-port: {message}")


def read_object(path: pathlib.Path, label: str) -> dict:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        die(f"cannot read {label} {path}: {error}")
    if not isinstance(value, dict):
        die(f"{label} must be a JSON object")
    return value


def relative_repository_file(value: object, label: str,
                             *, must_exist: bool = True,
                             allow_directory: bool = False) -> pathlib.Path:
    if not isinstance(value, str) or not value or "\\" in value:
        die(f"{label} must be a repository-relative path")
    relative = pathlib.PurePosixPath(value)
    if relative.is_absolute() or ".." in relative.parts:
        die(f"{label} escapes the repository")
    path = ROOT.joinpath(*relative.parts)
    if must_exist and not (
            path.is_file() or (allow_directory and path.is_dir())):
        die(f"{label} does not exist: {value}")
    return path


def validate_contract(contract: dict) -> dict:
    if contract.get("schema") != 1:
        die("unsupported port-contract schema")
    required = contract.get("required_console_features")
    if (not isinstance(required, list) or not required or
            len(required) != len(set(required)) or
            not all(isinstance(item, str) and item for item in required)):
        die("required feature list is invalid")
    templates = contract.get("adapter_templates")
    targets = contract.get("targets")
    if not isinstance(templates, dict) or not isinstance(targets, dict):
        die("port contract is missing templates or targets")
    for name, template in templates.items():
        if not ID_RE.fullmatch(name) or not isinstance(template, dict):
            die(f"invalid adapter template {name!r}")
        for key in ("board_profile", "sdkconfig_defaults"):
            relative_repository_file(template.get(key),
                                     f"{name}.{key}")
        adapters = template.get("adapters")
        if not isinstance(adapters, dict) or set(adapters) != {
                "display", "storage", "input", "audio", "dependencies"}:
            die(f"{name} adapter map is incomplete")
        for adapter, path in adapters.items():
            relative_repository_file(
                path, f"{name}.{adapter}", allow_directory=True)
    for name, target in targets.items():
        if not ID_RE.fullmatch(name) or not isinstance(target, dict):
            die(f"invalid target {name!r}")
        if target.get("template") not in templates:
            die(f"{name} selects an unknown adapter template")
        features = target.get("features")
        if not isinstance(features, dict) or set(features) != set(required):
            die(f"{name} feature set differs from the parity contract")
        missing = [feature for feature in required
                   if features.get(feature) is not True]
        if missing:
            die(f"{name} lacks required features: {', '.join(missing)}")
        inputs = target.get("inputs")
        if (not isinstance(inputs, list) or not inputs or
                len(inputs) != len(set(inputs)) or
                not set(inputs) <= INPUTS):
            die(f"{name} has no valid navigation input")
        if target.get("touch") is not ("touch" in inputs):
            die(f"{name} touch declaration differs from its input list")
        if target.get("laptop_content_transfer") not in TRANSFERS:
            die(f"{name} has an unsafe content-transfer mode")
        if not isinstance(target.get("exact_target_seen_on_hardware"), bool):
            die(f"{name} exact-target hardware state is not explicit")
        if not isinstance(target.get("full_feature_set_hardware_tested"), bool):
            die(f"{name} full-feature hardware-test state is not explicit")
    return contract


def parity_report(contract: dict) -> dict:
    required = contract["required_console_features"]
    targets = contract["targets"]
    return {
        "result": "console-os-board-parity-valid",
        "required_features": required,
        "touch_is_hardware_optional": True,
        "targets": {
            name: {
                "template": target["template"],
                "inputs": target["inputs"],
                "touch": target["touch"],
                "laptop_content_transfer":
                    target["laptop_content_transfer"],
                "exact_target_seen_on_hardware":
                    target["exact_target_seen_on_hardware"],
                "full_feature_set_hardware_tested":
                    target["full_feature_set_hardware_tested"],
                "feature_count": len(required),
            }
            for name, target in sorted(targets.items())
        },
    }


def validate_text(value: object, label: str) -> str:
    if not isinstance(value, str) or SAFE_TEXT_RE.fullmatch(value) is None:
        die(f"{label} contains unsupported characters or length")
    return value


def validate_spec(spec: dict, contract: dict) -> dict:
    if spec.get("schema") != 1:
        die("unsupported board-port spec schema")
    board_id = spec.get("id")
    if not isinstance(board_id, str) or ID_RE.fullmatch(board_id) is None:
        die("spec id must match [a-z][a-z0-9-]{2,63}")
    normalized = {
        "schema": 1,
        "id": board_id,
        "vendor": validate_text(spec.get("vendor"), "vendor"),
        "product": validate_text(spec.get("product"), "product"),
        "pcb_revision": validate_text(spec.get("pcb_revision"),
                                      "pcb_revision"),
        "soc": validate_text(spec.get("soc"), "soc"),
    }
    memory = spec.get("memory")
    if not isinstance(memory, dict) or set(memory) != {
            "flash_bytes", "psram_bytes"}:
        die("memory must contain only flash_bytes and psram_bytes")
    for key in ("flash_bytes", "psram_bytes"):
        value = memory.get(key)
        if (not isinstance(value, int) or isinstance(value, bool) or
                value <= 0 or value % (1024 * 1024) != 0):
            die(f"memory.{key} must be a positive whole MiB")
    normalized["memory"] = memory
    source = spec.get("official_source")
    if not isinstance(source, dict):
        die("official_source is required")
    repository = source.get("repository")
    if (not isinstance(repository, str) or
            not repository.startswith("https://") or len(repository) > 256):
        die("official_source.repository must be an HTTPS URL")
    if COMMIT_RE.fullmatch(str(source.get("commit"))) is None:
        die("official_source.commit must be a full lowercase Git commit")
    for key in ("manual_sha256", "schematic_sha256"):
        if SHA256_RE.fullmatch(str(source.get(key))) is None:
            die(f"official_source.{key} must be a lowercase SHA-256")
    normalized["official_source"] = source
    template_name = spec.get("adapter_template")
    templates = contract["adapter_templates"]
    if template_name not in templates:
        die("adapter_template must name a supported repository template")
    compatibility = spec.get("backend_compatibility")
    if not isinstance(compatibility, dict):
        die("backend_compatibility must be explicit")
    proven = compatibility.get("proven")
    if not isinstance(proven, bool):
        die("backend_compatibility.proven must be true or false")
    if proven:
        evidence = relative_repository_file(
            compatibility.get("evidence"),
            "backend_compatibility.evidence")
        expected = compatibility.get("evidence_sha256")
        if SHA256_RE.fullmatch(str(expected)) is None:
            die("backend compatibility evidence SHA-256 is invalid")
        actual = hashlib.sha256(evidence.read_bytes()).hexdigest()
        if actual != expected:
            die("backend compatibility evidence SHA-256 differs")
    normalized["adapter_template"] = template_name
    normalized["backend_compatibility"] = compatibility
    interfaces = spec.get("interfaces")
    if not isinstance(interfaces, dict) or not interfaces:
        die("interfaces must be a non-empty hardware fact object")
    normalized["interfaces"] = interfaces
    inputs = spec.get("inputs")
    if (not isinstance(inputs, list) or not inputs or
            len(inputs) != len(set(inputs)) or not set(inputs) <= INPUTS):
        die("inputs must contain unique supported navigation inputs")
    touch = spec.get("touch")
    if touch is not ("touch" in inputs):
        die("touch must exactly match presence in inputs")
    transfer = spec.get("laptop_content_transfer")
    if transfer not in TRANSFERS:
        die("unsupported laptop content-transfer mode")
    normalized["inputs"] = inputs
    normalized["touch"] = touch
    normalized["laptop_content_transfer"] = transfer
    return normalized


def port_plan(spec: dict, contract: dict) -> dict:
    template = contract["adapter_templates"][spec["adapter_template"]]
    proven = spec["backend_compatibility"]["proven"]
    adapters = {
        name: {
            "path": path,
            "action": "reuse-after-focused-hardware-validation"
                      if proven else "review-or-implement",
        }
        for name, path in template["adapters"].items()
    }
    return {
        "result": "board-port-plan",
        "board": spec["id"],
        "adapter_template": spec["adapter_template"],
        "backend_compatibility_proven": proven,
        "console_feature_contract":
            contract["required_console_features"],
        "touch_required": spec["touch"],
        "inputs": spec["inputs"],
        "laptop_content_transfer": spec["laptop_content_transfer"],
        "adapters": adapters,
        "build_ready": proven,
        "flash_authorized": False,
        "mandatory_before_first_write": [
            "complete factory flash backup",
            "backup byte count and SHA-256",
            "hashed live-device identity binding",
            "reviewed exact-board flash authorization",
        ],
    }


def sdkconfig_scaffold(spec: dict) -> str:
    flash_mib = spec["memory"]["flash_bytes"] // (1024 * 1024)
    return f"""# SPDX-License-Identifier: MIT
# Generated by scripts/board-port.py for {spec['id']}.
# Add a dedicated platform_board Kconfig identity before treating this as a
# build target. Never substitute the inherited template's board identity.

CONFIG_ESPTOOLPY_FLASHSIZE_{flash_mib}MB=y
CONFIG_SPIRAM=y

# Adapter template: {spec['adapter_template']}
# Backend compatibility proven: {str(spec['backend_compatibility']['proven']).lower()}
# Flash authorization remains false until exact-device backup and identity binding.
"""


def markdown_plan(spec: dict, plan: dict) -> str:
    rows = "\n".join(
        f"| {name} | `{value['path']}` | {value['action']} |"
        for name, value in sorted(plan["adapters"].items()))
    return f"""# {spec['vendor']} {spec['product']} {spec['pcb_revision']} port

This scaffold was generated deterministically from a source-pinned hardware
spec. It is write-locked. Do not flash this target until the exact device has a
complete factory backup, verified size and SHA-256, and hashed identity binding.

## Reusable adapter plan

| Service | Repository adapter | Action |
|---|---|---|
{rows}

The standard Console OS contract includes the window manager, Program/File/Game
Managers, storage-installed native cartridges, Maze Chase, Space Invaders,
Doom, persistent game storage, atomic OS updates, and audio. Touch is required
only when the hardware provides it. This target declares input through
{', '.join(spec['inputs'])} and laptop content transfer through
`{spec['laptop_content_transfer']}`.

## Deterministic completion sequence

1. Review every interface and compatibility claim against the pinned schematic.
2. Add a unique `platform_board` identity; never reuse the template board name.
3. Reuse only adapters covered by the compatibility evidence. Implement each
   remaining adapter behind the existing platform service API.
4. Add the board defaults, managed-component lock, partition table, and focused
   final-image verifier without changing either existing target.
5. Run component tests once, then one clean target build and parity verification.
6. Back up and bind the exact live device before its first write, then record
   serial and physical acceptance for display, storage, audio, input, games,
   update/rollback, and the window-manager UI.
"""


def write_scaffold(spec: dict, plan: dict, output_root: pathlib.Path) -> dict:
    root = output_root.resolve()
    destination = root / spec["id"]
    if destination.exists():
        die(f"destination already exists: {destination}")
    root.mkdir(parents=True, exist_ok=True)
    destination.mkdir()
    profile = dict(spec)
    profile.update({
        "status": "port-scaffold-hardware-unverified",
        "device_identity": None,
        "factory_backup": None,
        "exact_target_seen_on_hardware": False,
        "full_feature_set_hardware_tested": False,
        "flash_authorized": False,
    })
    files = {
        "board-profile.json": json.dumps(profile, indent=2,
                                         sort_keys=True) + "\n",
        "adapter-plan.json": json.dumps(plan, indent=2,
                                        sort_keys=True) + "\n",
        "sdkconfig.defaults": sdkconfig_scaffold(spec),
        "PORTING.md": markdown_plan(spec, plan),
    }
    for name, content in files.items():
        (destination / name).write_text(content, encoding="utf-8", newline="\n")
    return {
        "result": "board-port-scaffold-created",
        "path": str(destination),
        "files": sorted(files),
        "build_ready": plan["build_ready"],
        "flash_authorized": False,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)
    subparsers.add_parser("check", help="validate the checked-in port contract")
    subparsers.add_parser("matrix", help="print the checked-in feature matrix")
    plan_parser = subparsers.add_parser("plan", help="validate and plan a port spec")
    plan_parser.add_argument("--spec", required=True, type=pathlib.Path)
    scaffold_parser = subparsers.add_parser(
        "scaffold", help="create a write-locked port scaffold")
    scaffold_parser.add_argument("--spec", required=True, type=pathlib.Path)
    scaffold_parser.add_argument("--output-root", required=True,
                                 type=pathlib.Path)
    args = parser.parse_args()

    contract = validate_contract(read_object(CONTRACT_PATH, "port contract"))
    if args.command in {"check", "matrix"}:
        report = parity_report(contract)
        if args.command == "check":
            report = {"result": "board-port-contract-valid",
                      "targets": sorted(contract["targets"])}
    else:
        spec = validate_spec(read_object(args.spec.resolve(), "port spec"),
                             contract)
        plan = port_plan(spec, contract)
        report = plan if args.command == "plan" else write_scaffold(
            spec, plan, args.output_root)
    print(json.dumps(report, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    sys.exit(main())
