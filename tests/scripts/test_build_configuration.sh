#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/rmcs-build-config.XXXXXX")
missing_output="$work/missing-output"
external_output="$work/external-output"
layout_output="$work/layout-output"
root_artifact=
source_artifact=
build_artifact=
saved_elf="$work/huokong.elf"
saved_map="$work/huokong.map"

cleanup() {
    rm -f -- "$root_artifact" "$source_artifact" "$build_artifact"
    if test -f "$saved_elf"; then
        mv -- "$saved_elf" build/scons/firmware/huokong.elf
    fi
    if test -f "$saved_map"; then
        mv -- "$saved_map" build/scons/firmware/huokong.map
    fi
    rm -rf -- "$work"
}
trap cleanup EXIT HUP INT TERM

fail() {
    printf 'build configuration check failed: %s\n' "$1" >&2
    exit 1
}

cd "$root"

grep -Fq "REPOSITORY_ROOT = os.path.abspath(Dir('#').abspath)" SConstruct \
    || fail 'SCons repository root is not fixed to the invocation root'
grep -Fq "RTT_ROOT = os.path.join(REPOSITORY_ROOT, 'vendor', 'rt-thread')" \
    SConstruct \
    || fail 'SCons accepts an external RTT_ROOT'
if grep -Fq "os.getenv('RTT_ROOT')" SConstruct; then
    fail 'SConstruct resolves RT-Thread from the environment'
fi
grep -Fq 'export RTT_ROOT="$PROJECT_ROOT/vendor/rt-thread"' tools/env.sh \
    || fail 'public scripts do not export the vendored RTT_ROOT'

if grep -Eq "manifest_include_paths|AppendUnique\(CCFLAGS" SConstruct; then
    fail 'SCons root environment globally injects manifest paths or flags'
fi
if grep -Fq "AppendUnique(CPPPATH=['drivers'])" SConstruct; then
    fail 'SCons root environment globally injects driver include paths'
fi

grep -Fq 'python3 tests/scripts/test_link_owners.py' tools/test.sh \
    || fail 'public test runner omits link ownership verification'
grep -Fq 'tools/build.sh' tools/test.sh \
    || fail 'public test runner does not build firmware before link ownership'
if grep -Fq 'sh tests/scripts/test_build_configuration.sh' tools/test.sh; then
    fail 'public test runner recursively invokes its configuration test'
fi
grep -Fq 'TEST_FIRMWARE_BUILD_SCRIPT' tools/test.sh \
    || fail 'public test runner has no controlled build-script test hook'
grep -Fq 'build/scons/firmware/huokong.elf' tools/test.sh \
    || fail 'link ownership verification does not name the firmware ELF'
grep -Fq 'build/scons/firmware/huokong.map' tools/test.sh \
    || fail 'link ownership verification does not name the firmware Map'

test -x tests/scripts/test_manifest_boundaries.py \
    || fail 'manifest boundary verifier is missing'
python3 tests/scripts/test_manifest_boundaries.py \
    || fail 'manifest boundary verifier rejected the current source boundaries'

if python3 tests/scripts/test_link_owners.py \
    build/scons/firmware/missing.elf build/scons/firmware/missing.map \
    >"$missing_output" 2>&1; then
    fail 'link ownership checker accepted missing artifacts'
fi
grep -Fq 'requires existing ELF and Map files' "$missing_output" \
    || fail 'missing link artifacts do not fail closed'

if grep -Eq '^CompilationDatabase:' .clangd; then
    fail 'clangd project config uses unsupported CompilationDatabase key'
fi
grep -Fq -- '--compile-commands-dir=${workspaceFolder}/build/scons' \
    .devcontainer/devcontainer.json \
    || fail 'VS Code clangd is not pointed at the SCons compilation database'
grep -Fq -- '--query-driver=/usr/bin/arm-none-eabi-*' \
    .devcontainer/devcontainer.json \
    || fail 'VS Code clangd is not allowed to query the ARM GCC driver'
grep -Fq -- '--compile-commands-dir=${workspaceFolder}/build/scons' \
    .vscode/settings.json \
    || fail 'workspace VS Code clangd is not pointed at the SCons compilation database'
grep -Fq -- '--query-driver=/usr/bin/arm-none-eabi-*' \
    .vscode/settings.json \
    || fail 'workspace VS Code clangd is not allowed to query the ARM GCC driver'
grep -Fq "build', 'scons', 'compile_commands.json'" SConstruct \
    || fail 'SCons compilation database output is not under build/scons'

external_rtt=$(mktemp -u)
if ! RTT_ROOT="$external_rtt" scons -n -Q >"$external_output" 2>&1; then
    fail 'SCons dry-run failed with an external RTT_ROOT'
fi
if grep -Fq "$external_rtt" "$external_output"; then
    fail 'external RTT_ROOT changed the SCons source paths'
fi

for extension in elf axf bin hex map dblite d dep i lib out; do
    grep -Fq "*.$extension" tests/scripts/test_repository_layout.sh \
        || fail "layout gate does not scan .$extension artifacts"
done
grep -Fq 'compile_commands.json' tests/scripts/test_repository_layout.sh \
    || fail 'layout gate does not scan compile_commands.json'

scons --cdb >"$work/cdb-output" 2>&1 \
    || fail 'SCons could not generate the compilation database'
rm -rf -- build/kernel
python3 - <<'PY'
import json
import shlex

with open("build/scons/compile_commands.json", encoding="utf-8") as stream:
    entries = json.load(stream)
with open("project/firmware-manifest.json", encoding="utf-8") as stream:
    manifest = json.load(stream)
forbidden_defines = {
    define.split("=", 1)[0]
    for group in manifest["groups"]
    if group["name"] != "modules"
    for define in group["defines"]
}
module_entries = [entry for entry in entries if entry["file"].startswith("src/modules/")]
if not module_entries:
    raise SystemExit("compilation database has no module entries")
for entry in module_entries:
    includes = [argument for argument in shlex.split(entry["command"]) if argument.startswith("-I")]
    if includes != ["-Isrc/modules"]:
        raise SystemExit(f"module include boundary leaked: {entry['file']}: {includes}")
    defines = [
        argument[2:].split("=", 1)[0]
        for argument in shlex.split(entry["command"])
        if argument.startswith("-D")
    ]
    leaked_defines = sorted(set(defines) & forbidden_defines)
    if leaked_defines:
        raise SystemExit(
            f"module define boundary leaked: {entry['file']}: {leaked_defines}"
        )
PY

expect_layout_rejection() {
    location=$1
    artifact=$2
    : >"$artifact"
    if sh tests/scripts/test_repository_layout.sh >"$layout_output" 2>&1; then
        fail "layout gate accepted $location artifact"
    fi
    grep -Fq 'generated artifact is outside root build' "$layout_output" \
        || fail "$location artifact failure was not explicit"
    rm -f -- "$artifact"
}

for extension in elf axf bin hex map dblite; do
    root_artifact="task5-layout-gate.$extension"
    source_artifact="src/app/task5-layout-gate.$extension"
    expect_layout_rejection root "$root_artifact"
expect_layout_rejection source "$source_artifact"
done
for extension in d dep i lib out; do
    root_artifact="task6-layout-gate.$extension"
    source_artifact="src/app/task6-layout-gate.$extension"
    expect_layout_rejection root "$root_artifact"
    expect_layout_rejection source "$source_artifact"
done
root_artifact=compile_commands.json
source_artifact=src/app/compile_commands.json
expect_layout_rejection root "$root_artifact"
expect_layout_rejection source "$source_artifact"

build_artifact=build/scons/task5-layout-gate.elf
: >"$build_artifact"
sh tests/scripts/test_repository_layout.sh >"$layout_output" 2>&1 \
    || fail 'layout gate rejected a legal build artifact'
rm -f -- "$build_artifact"

for extension in d dep i lib out; do
    build_artifact="build/scons/task6-layout-gate.$extension"
    : >"$build_artifact"
    sh tests/scripts/test_repository_layout.sh >"$layout_output" 2>&1 \
        || fail "layout gate rejected a legal build .$extension artifact"
    rm -f -- "$build_artifact"
done

mkdir -p "$work/bin" "$work/discovery"
cat >"$work/bin/scons" <<'EOF'
#!/bin/sh
exit 0
EOF
cat >"$work/discovery/test_fixture" <<'EOF'
#!/bin/sh
exit 0
EOF
cat >"$work/no-build.sh" <<'EOF'
#!/bin/sh
exit 0
EOF
chmod +x "$work/bin/scons" "$work/discovery/test_fixture" "$work/no-build.sh"
if test -f build/scons/firmware/huokong.elf; then
    mv -- build/scons/firmware/huokong.elf "$saved_elf"
fi
if test -f build/scons/firmware/huokong.map; then
    mv -- build/scons/firmware/huokong.map "$saved_map"
fi
if TEST_FIRMWARE_BUILD_SCRIPT="$work/no-build.sh" \
    TEST_DISCOVERY_DIR="$work/discovery" PATH="$work/bin:$PATH" \
    tools/test.sh >"$missing_output" 2>&1; then
    fail 'tools/test.sh accepted missing firmware artifacts after a no-op build'
fi
grep -Fq 'link owner check requires existing ELF and Map files' "$missing_output" \
    || fail 'tools/test.sh missing-artifact failure was not explicit'

mkdir -p "$work/src/modules" "$work/project/keil"
cat >"$work/src/modules/fixture.c" <<'EOF'
int fixture(void) { return 0; }
EOF
cat >"$work/manifest.json" <<'EOF'
{
  "version": 1,
  "target": {
    "name": "STM32F103C8",
    "device": "STM32F103xB",
    "cpu": "Cortex-M3",
    "flash": {"origin": "0x08000000", "length": 65536},
    "ram": {"origin": "0x20000000", "length": 20480},
    "stack_size": 1024
  },
  "groups": [{
    "name": "modules",
    "sources": ["src/modules/fixture.c"],
    "include_paths": ["src/modules", "src/modules/fixture_include"],
    "defines": ["FIXTURE_DEFINE"]
  }]
}
EOF
python3 tools/generate-keil-project.py \
    --root "$work" --manifest "$work/manifest.json" \
    --output "$work/project/keil/fixture.uvprojx"
grep -Fq 'FIXTURE_DEFINE' "$work/project/keil/fixture.uvprojx" \
    || fail 'fixture define did not reach Keil configuration'
grep -Fq 'fixture_include' "$work/project/keil/fixture.uvprojx" \
    || fail 'fixture include did not reach Keil configuration'
PYTHONPATH=tools python3 - "$work/manifest.json" <<'PY'
import sys
from firmware_manifest import load_manifest, manifest_group_settings

settings = manifest_group_settings(load_manifest(sys.argv[1]), "modules")
assert settings["defines"] == ["FIXTURE_DEFINE"]
assert settings["include_paths"] == ["src/modules", "src/modules/fixture_include"]
PY

printf 'build configuration checks passed\n'
