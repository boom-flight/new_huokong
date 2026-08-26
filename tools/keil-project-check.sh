#!/usr/bin/env sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$root"

python3 -c '
from pathlib import Path
from tools.firmware_manifest import load_manifest, validate_manifest
root = Path(".")
manifest = load_manifest(root / "project/firmware-manifest.json")
validate_manifest(root, manifest)
'

python3 tools/generate-keil-project.py \
    --root . \
    --manifest project/firmware-manifest.json \
    --output project/keil/huokong.uvprojx \
    --check

scatter=project/keil/stm32f103c8.sct
python3 - "$scatter" <<'PY'
import pathlib
import sys

lines = []
for raw_line in pathlib.Path(sys.argv[1]).read_text(encoding="utf-8").splitlines():
    active = raw_line.split(";", 1)[0].strip()
    if active:
        lines.append(active)


def region_body(name):
    for index, line in enumerate(lines):
        if not line.startswith(name + " "):
            continue
        if index + 1 >= len(lines) or lines[index + 1] != "{":
            raise SystemExit(f"{name} is missing an opening brace")
        depth = 1
        body = []
        for nested in lines[index + 2:]:
            if nested == "{":
                depth += 1
            elif nested == "}":
                depth -= 1
                if depth == 0:
                    return body
            elif depth > 0:
                body.append(nested)
        raise SystemExit(f"{name} has unbalanced braces")
    raise SystemExit(f"missing {name}")


load = region_body("LR_IROM1")
if "ER_IROM1 0x08000000 0x10000" not in load:
    raise SystemExit("LR_IROM1 has the wrong ER_IROM1 range")
execute = region_body("ER_IROM1")
if "*.o (RESET, +First)" not in execute or ".ANY (+RO)" not in execute:
    raise SystemExit("ER_IROM1 does not place reset and read-only sections")
ram = region_body("RW_IRAM1")
if ".ANY (+RW +ZI)" not in ram:
    raise SystemExit("RW_IRAM1 does not place read-write and zero sections")
if "ARM_LIB_STACK 0x20004C00 EMPTY 0x400" not in load:
    raise SystemExit("missing 0x400-byte ARM_LIB_STACK reservation")
PY

project=project/keil/huokong.uvprojx
grep -Fq 'build\keil\stm32f103c8\Debug' "$project" || {
    printf 'Keil project output is outside build/keil\n' >&2
    exit 1
}
if grep -Eq 'build\\scons|tests/|tests\\\\|SConscript|link[.]lds' "$project"; then
    printf 'Keil project contains a forbidden source or output path\n' >&2
    exit 1
fi

printf 'Keil project static checks passed\n'
