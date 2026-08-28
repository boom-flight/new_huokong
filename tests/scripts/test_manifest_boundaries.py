#!/usr/bin/env python3

import ast
import json
import pathlib
import re
import sys


ROOT = pathlib.Path(__file__).resolve().parents[2]
MANIFEST = ROOT / "project/firmware-manifest.json"

GROUP_SCRIPTS = {
    "application": ["src/app/SConscript"],
    "board": ["src/platform/board/stm32f103c8/SConscript"],
    "kernel": [
        "src/kernel/imu/SConscript",
        "src/kernel/logging/SConscript",
        "src/kernel/telemetry/SConscript",
    ],
    "modules": [
        "src/modules/attitude/SConscript",
        "src/modules/devices/bmi088/SConscript",
        "src/modules/protocols/imu_telemetry/SConscript",
        "src/modules/timing/SConscript",
        "src/modules/transport/SConscript",
    ],
    "platform": [
        "src/platform/devices/SConscript",
        "src/platform/time/SConscript",
        "src/platform/transport/SConscript",
    ],
    "debug": ["src/debug/SConscript"],
}
SOURCE_PREFIXES = {
    "application": "src/app/",
    "board": "src/platform/board/",
    "kernel": "src/kernel/",
    "modules": "src/modules/",
    "platform": "src/platform/",
    "debug": "src/debug/",
}


def fail(message):
    print(f"manifest boundary check failed: {message}", file=sys.stderr)
    raise SystemExit(1)


def settings_group(script):
    tree = ast.parse(script.read_text(encoding="utf-8"), filename=str(script))
    groups = []
    for node in ast.walk(tree):
        if not isinstance(node, ast.Call):
            continue
        if not isinstance(node.func, ast.Name):
            continue
        if node.func.id != "manifest_group_settings" or len(node.args) < 2:
            continue
        if not isinstance(node.args[0], ast.Name) or node.args[0].id != "manifest":
            continue
        if isinstance(node.args[1], ast.Constant) and isinstance(
            node.args[1].value, str
        ):
            groups.append(node.args[1].value)
    return groups


def header_defines(header):
    defines = {}
    pattern = re.compile(r"^\s*#define\s+(\w+)(?:\s+(.*?))?\s*$")
    for line in header.read_text(encoding="utf-8").splitlines():
        match = pattern.match(line)
        if match:
            defines[match.group(1)] = (match.group(2) or "").strip()
    return defines


def manifest_define_name(define):
    return define.split("=", 1)[0]


def main():
    manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
    groups = {group["name"]: group for group in manifest["groups"]}

    modules = groups.get("modules")
    if modules is None:
        fail("manifest has no modules group")
    if modules["include_paths"] != ["src/modules"]:
        fail("modules include boundary is not exactly src/modules")
    if modules["defines"]:
        fail("modules inherit manifest defines")
    for source in modules["sources"]:
        if not source.startswith("src/modules/"):
            fail(f"module source escapes src/modules: {source}")
        if not (ROOT / source).is_file():
            fail(f"module source is missing: {source}")

    for name, relative_scripts in GROUP_SCRIPTS.items():
        group = groups.get(name)
        if group is None:
            fail(f"manifest has no {name} group")
        for source in group["sources"]:
            if not source.startswith(SOURCE_PREFIXES[name]):
                fail(f"{name} source crosses its source-tree boundary: {source}")
        for relative_script in relative_scripts:
            script = ROOT / relative_script
            if not script.is_file():
                fail(f"missing explicit {name} SConscript: {relative_script}")
            if settings_group(script) != [name]:
                fail(f"{relative_script} does not use only the {name} settings")
            text = script.read_text(encoding="utf-8")
            if "CPPPATH=settings[\"include_paths\"]" not in text and \
                    "CPPPATH=settings['include_paths']" not in text:
                fail(f"{relative_script} does not own its include paths explicitly")
            if "CPPDEFINES=settings[\"defines\"]" not in text and \
                    "CPPDEFINES=settings['defines']" not in text:
                fail(f"{relative_script} does not own its defines explicitly")
    for source in modules["sources"]:
        scripts = [ROOT / relative_script for relative_script in GROUP_SCRIPTS["modules"]]
        if not any(
            pathlib.Path(source).name in script.read_text(encoding="utf-8")
            for script in scripts
        ):
            fail(f"module source is not represented by an explicit SConscript: {source}")
        script = next(
            script for script in scripts
            if pathlib.Path(source).name in script.read_text(encoding="utf-8")
        )
        text = script.read_text(encoding="utf-8")
        if re.search(r"(?:vendor/|src/kernel|src/platform)", text):
            fail(f"module boundary leaks platform, kernel, or vendor paths: {script}")
        if re.search(r"CPP(?:PATH|DEFINES)\s*=\s*(?!settings\[)", text):
            fail(
                f"module boundary has a non-manifest include or define assignment: {script}"
            )

    sconstruct = (ROOT / "SConstruct").read_text(encoding="utf-8")
    if re.search(r"AppendUnique\((?:CPPPATH|CCFLAGS)", sconstruct):
        fail("SConstruct globally injects include paths or compile flags")

    rtconfig = (ROOT / "rtconfig.py").read_text(encoding="utf-8")
    if re.search(r"CPPPATH|CPPDEFINES|(?:^|\s)-I", rtconfig):
        fail("rtconfig.py carries SCons group include paths or defines")
    defines = header_defines(ROOT / "rtconfig.h")
    for define in groups["rt-thread"]["defines"]:
        name = manifest_define_name(define)
        if name == "__RTTHREAD__":
            rtthread_sconscript = (ROOT / "vendor/rt-thread/src/SConscript").read_text(
                encoding="utf-8"
            )
            if name not in rtthread_sconscript:
                fail(f"RT-Thread source boundary does not own define: {name}")
            continue
        if name not in defines:
            fail(f"rtconfig.h does not contain rt-thread configuration define: {name}")

    print("manifest boundary checks passed")


if __name__ == "__main__":
    main()
