#!/bin/sh

set -eu

P4_SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
P4_INSTALL_ROOT=$(CDPATH= cd -- "$P4_SCRIPT_DIR/.." && pwd)
P4_INSTALL_ENV="$P4_INSTALL_ROOT/.tools/install-python"
P4_INSTALL_REQUIREMENTS="$P4_INSTALL_ROOT/requirements-install.txt"
P4_INSTALL_LOCK="$P4_INSTALL_ROOT/.tools/.install-python.setup.lock"
P4_INSTALL_STAGE=''

case "${1-}" in
    --help|-h)
        printf 'Usage: %s\n\n' "$0"
        printf 'Install pinned esptool/pyserial in .tools/install-python without ESP-IDF.\n'
        printf 'A matching environment is reused without downloading anything.\n'
        exit 0
        ;;
    '') ;;
    *) printf 'Unexpected argument: %s\n' "$1" >&2; exit 2 ;;
esac
if [ "$#" -gt 1 ]; then
    printf 'This command takes no arguments.\n' >&2
    exit 2
fi

p4_install_env_matches() {
    "$1/bin/python" -I - "$1" "$P4_INSTALL_REQUIREMENTS" <<'PY'
import importlib.metadata
from pathlib import Path
import sys

expected = {"esptool": "4.12.0", "pyserial": "3.5"}
requirements = [line.strip() for line in Path(sys.argv[2]).read_text().splitlines()
                if line.strip() and not line.lstrip().startswith("#")]
if requirements != [f"{name}=={version}" for name, version in expected.items()]:
    raise SystemExit("Installation requirements do not match the supported pins")
prefix = Path(sys.argv[1]).resolve()
if Path(sys.prefix).resolve() != prefix or sys.prefix == sys.base_prefix:
    raise SystemExit("Installation Python is not the requested virtual environment")
import esptool
import serial
for name, version in expected.items():
    if importlib.metadata.version(name) != version:
        raise SystemExit(f"Installation tool version mismatch: {name}")
for module in (esptool, serial):
    try:
        Path(module.__file__).resolve().relative_to(prefix)
    except ValueError:
        raise SystemExit("Installation tools were imported from outside the environment")
PY
}

p4_install_ready() {
    printf 'Installation tools ready: %s\n' "$P4_INSTALL_ENV/bin/python"
    printf 'Activate with: . "%s/bin/activate"\n' "$P4_INSTALL_ENV"
}

if [ -e "$P4_INSTALL_ENV" ] || [ -L "$P4_INSTALL_ENV" ]; then
    if [ ! -L "$P4_INSTALL_ENV" ] && [ -x "$P4_INSTALL_ENV/bin/python" ] && \
       p4_install_env_matches "$P4_INSTALL_ENV"; then
        p4_install_ready
        exit 0
    fi
    printf 'Refusing to replace existing incomplete or mismatched environment: %s\n' \
        "$P4_INSTALL_ENV" >&2
    printf 'Move that directory aside before retrying; SDK environments are unaffected.\n' >&2
    exit 1
fi

command -v python3 >/dev/null 2>&1 || {
    printf 'Python 3 is required to prepare installation tools.\n' >&2
    exit 1
}
mkdir -p "$P4_INSTALL_ROOT/.tools"
if ! mkdir "$P4_INSTALL_LOCK" 2>/dev/null; then
    printf 'Another setup may be running; refusing to replace setup lock: %s\n' \
        "$P4_INSTALL_LOCK" >&2
    exit 1
fi
p4_install_cleanup() {
    if [ -n "$P4_INSTALL_STAGE" ] && [ -d "$P4_INSTALL_STAGE" ]; then
        rm -rf -- "$P4_INSTALL_STAGE"
    fi
    rmdir "$P4_INSTALL_LOCK" 2>/dev/null || :
}
trap p4_install_cleanup EXIT
trap 'exit 1' HUP INT TERM

P4_INSTALL_STAGE=$(mktemp -d "$P4_INSTALL_ROOT/.tools/.install-python.tmp.XXXXXX")
python3 -m venv --prompt install-python "$P4_INSTALL_STAGE"
"$P4_INSTALL_STAGE/bin/python" -I -m pip install --disable-pip-version-check \
    --no-input --require-virtualenv --requirement "$P4_INSTALL_REQUIREMENTS"
p4_install_env_matches "$P4_INSTALL_STAGE"

# Venv activation and console scripts contain their creation path. Relocate
# only those generated text files before publishing the complete environment.
python3 -I - "$P4_INSTALL_STAGE" "$P4_INSTALL_ENV" <<'PY'
import os
from pathlib import Path
import sys

source, destination = map(Path, sys.argv[1:])
if os.path.lexists(destination):
    raise SystemExit(f"Refusing to replace an environment created during setup: {destination}")
for path in [source / "pyvenv.cfg", *(source / "bin").iterdir()]:
    if path.is_symlink() or not path.is_file():
        continue
    data = path.read_bytes()
    try:
        data.decode("utf-8")
    except UnicodeDecodeError:
        continue
    # Both paths share the same parent. Replacing the unique stage basename
    # also handles shell-quoted paths whose parent contains an apostrophe.
    rewritten = data.replace(os.fsencode(source.name), os.fsencode(destination.name))
    if rewritten != data:
        path.write_bytes(rewritten)
os.rename(source, destination)
PY
P4_INSTALL_STAGE=''
p4_install_env_matches "$P4_INSTALL_ENV"
p4_install_ready
