#!/usr/bin/env python3

from __future__ import annotations

import json
import pathlib
import subprocess
import tempfile


ROOT = pathlib.Path(__file__).resolve().parents[2]
REGISTRY = ROOT / "scripts/p4cart_seed_registry.py"


def run(*arguments: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        ["python3", str(REGISTRY), *arguments], cwd=ROOT, check=False,
        text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
    )


def write_template(root: pathlib.Path, slug: str) -> None:
    template = root / slug
    template.mkdir(parents=True)
    (template / "p4.json").write_text(json.dumps({
        "format": "p4-cart-source-v1",
        "game": {"slug": slug},
    }), encoding="utf-8")


def main() -> None:
    actual = run("--check")
    assert actual.returncode == 0, actual.stderr
    report = json.loads(actual.stdout)
    assert report["seed_carts"] == []

    with tempfile.TemporaryDirectory() as temporary:
        root = pathlib.Path(temporary)
        templates = root / "templates"
        write_template(templates, "first-game")
        write_template(templates, "second-game")
        metadata = root / "metadata.json"
        metadata.write_text(json.dumps({
            "legacy_p4cart": {
                "seed_cart": "P4/GAMES/FIRST-GAME.P4CART",
                "seed_carts": [
                    "P4/GAMES/FIRST-GAME.P4CART",
                    "P4/GAMES/SECOND-GAME.P4CART",
                ],
            },
        }), encoding="utf-8")
        generated = root / "seeds.cmake"
        result = run(
            "--metadata", str(metadata),
            "--templates-root", str(templates),
            "--output-cmake", str(generated),
        )
        assert result.returncode == 0, result.stderr
        assert generated.read_text(encoding="utf-8") == (
            "# Generated; do not edit.\n"
            'p4_add_script_cart_seed("FIRST-GAME.P4CART" "first-game")\n'
            'p4_add_script_cart_seed("SECOND-GAME.P4CART" "second-game")\n'
        )

        write_template(templates, "undeclared-game")
        undeclared = run(
            "--metadata", str(metadata),
            "--templates-root", str(templates),
            "--check",
        )
        assert undeclared.returncode == 0, undeclared.stderr
        metadata.write_text(json.dumps({"legacy_p4cart": {"seed_carts": []}}))
        empty = run("--metadata", str(metadata), "--templates-root", str(templates), "--check")
        assert empty.returncode == 0, empty.stderr
        (templates / "undeclared-game/p4.json").unlink()
        (templates / "undeclared-game").rmdir()

        invalid_values = (
            ["P4/GAMES/FIRST-GAME.P4CART"] * 2,
            ["../FIRST-GAME.P4CART"],
            ["P4/GAMES/MISSING.P4CART"],
        )
        for values in invalid_values:
            metadata.write_text(json.dumps({
                "legacy_p4cart": {"seed_carts": values},
            }), encoding="utf-8")
            invalid = run(
                "--metadata", str(metadata),
                "--templates-root", str(templates),
                "--check",
            )
            assert invalid.returncode != 0, values

    print("P4 Cart seed registry tests passed")


if __name__ == "__main__":
    main()
