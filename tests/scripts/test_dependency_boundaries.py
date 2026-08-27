#!/usr/bin/env python3

import ast
import contextlib
import json
import io
import pathlib
import re
import shlex
import sys
import tempfile


ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

from firmware_manifest import load_manifest, validate_manifest


MANIFEST = ROOT / "project/firmware-manifest.json"
KERNEL_ROOT = ROOT / "src/kernel"
COMPILE_COMMANDS = ROOT / "build/scons/compile_commands.json"
BUILD_FILES = [ROOT / "SConstruct", *sorted((ROOT / "src").rglob("SConscript"))]
CDB_BUILD_INPUTS = [
    MANIFEST,
    ROOT / "rtconfig.py",
    ROOT / "rtconfig.h",
    ROOT / "SConstruct",
    *sorted(ROOT.rglob("SConscript")),
]
OWNED_PREFIXES = {
    "application": "src/app/",
    "kernel": "src/kernel/",
    "modules": "src/modules/",
    "platform": "src/platform/",
}
OWNED_GROUPS = tuple(OWNED_PREFIXES)

GROUP_LAYERS = {
    "Applications": "application",
    "IMU Service": "kernel",
    "IMU Policy": "kernel",
    "Telemetry Service": "kernel",
    "Telemetry Policy": "kernel",
    "IMU Logging": "kernel",
    "Attitude": "module",
    "BMI088": "module",
    "IMU Telemetry": "module",
    "Timing": "module",
    "Transport": "module",
    "STM32 BMI088": "platform",
    "STM32 Monotonic Clock": "platform",
    "STM32 Telemetry UART": "platform",
    "Drivers": "board",
    "Direct HAL": "platform",
    "STM32F1-HAL": "vendor",
}
ALLOWED_LAYER_GRAPH = {
    "application": {"kernel", "platform", "board"},
    "kernel": {"kernel", "module", "platform", "board"},
    "module": {"module"},
    "platform": {"module", "platform", "vendor"},
    "board": {"vendor", "platform"},
    "vendor": set(),
}

SOURCE_SUFFIXES = {".c", ".h", ".cc", ".cpp", ".hpp", ".s", ".S"}
FORBIDDEN_KERNEL_INCLUDES = {
    "board.h",
    "drv_gpio.h",
}
KERNEL_HARDWARE_DEFINES = {
    "STM32F103xB",
    "USE_HAL_DRIVER",
}
MODULE_FORBIDDEN_DEFINES = KERNEL_HARDWARE_DEFINES | {"__RTTHREAD__"}
KERNEL_HARDWARE_INCLUDE_PREFIXES = (
    "src/platform/board/",
    "vendor/cmsis-core/",
    "vendor/stm32f1-cmsis/",
    "vendor/stm32f1-hal/",
    "vendor/rt-thread-stm32-drivers/",
)
APPLICATION_HARDWARE_INCLUDE_PREFIXES = (
    "src/platform/board/",
    "vendor/cmsis-core/",
    "vendor/stm32f1-cmsis/",
    "vendor/stm32f1-hal/",
    "vendor/rt-thread-stm32-drivers/",
)
INCLUDE_OPTIONS = ("-I", "-isystem", "-iquote", "-idirafter")


def fail(message):
    print(f"dependency boundary check failed: {message}", file=sys.stderr)
    raise SystemExit(1)


def relative_path(path):
    try:
        return path.resolve().relative_to(ROOT.resolve()).as_posix()
    except ValueError:
        return path.as_posix()


def source_files(root):
    if not root.is_dir():
        fail(f"missing source directory: {relative_path(root)}")
    return sorted(
        path for path in root.rglob("*") if path.is_file() and path.suffix in SOURCE_SUFFIXES
    )


def is_define_group_call(node):
    return isinstance(node.func, ast.Name) and node.func.id == "DefineGroup"


def include_name(line):
    match = re.match(r'^\s*#\s*include\s*([<"])([^>"]+)[>"]', line)
    return match.group(2) if match else None


def check_kernel_includes():
    violations = []
    for path in source_files(KERNEL_ROOT):
        try:
            lines = path.read_text(encoding="utf-8").splitlines()
        except (OSError, UnicodeDecodeError) as error:
            fail(f"could not read kernel source {relative_path(path)}: {error}")
        for line_number, line in enumerate(lines, 1):
            included = include_name(line)
            if included is None:
                continue
            if (
                included in FORBIDDEN_KERNEL_INCLUDES
                or included.endswith("/board.h")
                or included.endswith("/drv_gpio.h")
                or included.endswith("_stm32.h")
            ):
                violations.append(f"{relative_path(path)}:{line_number}: {included}")
    if violations:
        fail("kernel includes forbidden board or concrete adapter headers:\n  " + "\n  ".join(violations))


def check_empty_dependencies():
    violations = []
    for path in BUILD_FILES:
        if not path.is_file():
            fail(f"missing production build file: {relative_path(path)}")
        try:
            tree = ast.parse(path.read_text(encoding="utf-8"), filename=str(path))
        except (SyntaxError, UnicodeDecodeError) as error:
            fail(f"could not parse production build file {relative_path(path)}: {error}")
        for node in ast.walk(tree):
            if not isinstance(node, ast.Call):
                continue
            if not is_define_group_call(node):
                continue
            group_name = next(
                (node.args[0].value for _ in [0]
                 if node.args and isinstance(node.args[0], ast.Constant)
                 and isinstance(node.args[0].value, str)),
                None,
            )
            if group_name is None:
                fail(
                    f"production DefineGroup has an opaque group name at "
                    f"{relative_path(path)}:{node.lineno}"
                )
            if group_name not in GROUP_LAYERS:
                fail(
                    f"production DefineGroup has an unknown group at "
                    f"{relative_path(path)}:{node.lineno}: {group_name}"
                )
            depend = next(
                (keyword.value for keyword in node.keywords if keyword.arg == "depend"), None
            )
            if depend is None:
                fail(
                    f"production DefineGroup is missing a dependency declaration at "
                    f"{relative_path(path)}:{node.lineno}"
                )
            if not isinstance(depend, (ast.List, ast.Tuple)):
                fail(
                    f"production DefineGroup has an opaque dependency at "
                    f"{relative_path(path)}:{node.lineno}"
                )
            if any(isinstance(item, ast.Constant) and item.value == "" for item in depend.elts):
                violations.append(f"{relative_path(path)}:{node.lineno}")
            direct_dependencies = next(
                (
                    keyword.value
                    for keyword in node.keywords
                    if keyword.arg == "direct_dependencies"
                ),
                None,
            )
            if direct_dependencies is None:
                fail(
                    f"production DefineGroup is missing direct_dependencies at "
                    f"{relative_path(path)}:{node.lineno}"
                )
            if not isinstance(direct_dependencies, (ast.List, ast.Tuple)):
                fail(
                    f"production DefineGroup has opaque direct_dependencies at "
                    f"{relative_path(path)}:{node.lineno}"
                )
            for item in direct_dependencies.elts:
                if not isinstance(item, ast.Constant) or not isinstance(item.value, str):
                    fail(
                        f"production DefineGroup has a non-literal direct dependency at "
                        f"{relative_path(path)}:{node.lineno}"
                    )
                dependency = item.value
                if dependency not in GROUP_LAYERS:
                    fail(
                        f"production DefineGroup has an unknown direct dependency at "
                        f"{relative_path(path)}:{node.lineno}: {dependency}"
                    )
                source_layer = GROUP_LAYERS[group_name]
                target_layer = GROUP_LAYERS[dependency]
                if target_layer not in ALLOWED_LAYER_GRAPH[source_layer]:
                    fail(
                        f"production DefineGroup has a forbidden direct dependency at "
                        f"{relative_path(path)}:{node.lineno}: "
                        f"{group_name} -> {dependency}"
                    )
    if violations:
        fail("production DefineGroup calls contain an empty dependency:\n  " + "\n  ".join(violations))


def check_direct_dependencies_mutation():
    cases = (
        "DefineGroup('Attitude', [], depend=[])",
        "DefineGroup('Attitude', [], depend=[], direct_dependencies=dependency_list)",
        "DefineGroup('Attitude', [], depend=[], direct_dependencies=['Unknown'])",
        "DefineGroup('Attitude', [], depend=[], direct_dependencies=['IMU Service'])",
    )
    original_build_files = BUILD_FILES
    try:
        for source in cases:
            with tempfile.TemporaryDirectory() as directory:
                path = pathlib.Path(directory) / "SConscript"
                path.write_text(source, encoding="utf-8")
                globals()["BUILD_FILES"] = [path]
                try:
                    check_empty_dependencies()
                except SystemExit:
                    continue
                fail("direct dependency mutation was accepted: " + source)
    finally:
        globals()["BUILD_FILES"] = original_build_files


def command_arguments(entry):
    has_arguments = "arguments" in entry
    has_command = "command" in entry
    if has_arguments == has_command:
        fail("compilation database entry must contain exactly one command form")

    if has_arguments:
        arguments = entry["arguments"]
        if not isinstance(arguments, list) or not all(isinstance(item, str) for item in arguments):
            fail("compilation database entry has malformed arguments")
    else:
        command = entry["command"]
        if not isinstance(command, str) or not command.strip():
            fail("compilation database entry has an empty command")
        try:
            arguments = shlex.split(command)
        except ValueError as error:
            fail(f"could not parse compilation command: {error}")

    if not arguments or any(not argument.strip() for argument in arguments):
        fail("compilation database entry has an empty command")
    try:
        compile_index = arguments.index("-c")
    except ValueError:
        fail("compilation database entry is not a compile command")
    if compile_index == len(arguments) - 1:
        fail("compilation database entry has no source after -c")
    return arguments


def option_values(arguments, prefix):
    values = []
    index = 0
    while index < len(arguments):
        argument = arguments[index]
        if argument == prefix:
            index += 1
            if index >= len(arguments):
                fail(f"compilation command ends after {prefix}")
            value = arguments[index]
            if not value:
                fail(f"compilation command has an empty value after {prefix}")
            values.append(value)
        elif argument.startswith(prefix) and len(argument) > len(prefix):
            values.append(argument[len(prefix) :])
        index += 1
    return values


def include_values(arguments):
    return [value for option in INCLUDE_OPTIONS for value in option_values(arguments, option)]


def source_group(source):
    for group, prefix in OWNED_PREFIXES.items():
        if source.startswith(prefix):
            if group == "platform" and source.startswith("src/platform/board/"):
                return None
            return group
    return None


def manifest_owned_sources():
    try:
        manifest = load_manifest(MANIFEST)
        validate_manifest(ROOT, manifest)
    except (OSError, ValueError) as error:
        fail(f"could not validate firmware manifest: {error}")

    expected = {group: set() for group in OWNED_GROUPS}
    complete = set()
    for group in manifest["groups"]:
        for source in group["sources"]:
            complete.add(source)
            source_group_name = source_group(source)
            if source_group_name is None:
                continue
            if source_group_name != group["name"]:
                fail(f"manifest source crosses its ownership boundary: {source}")
            expected[source_group_name].add(source)
    return expected, complete


def entry_source(entry):
    source = entry.get("file")
    if not isinstance(source, str) or not source:
        fail("compilation database entry has no source file")
    directory_value = entry.get("directory", str(ROOT))
    if not isinstance(directory_value, str):
        fail("compilation database entry has a malformed directory")
    directory = pathlib.Path(directory_value)
    if not directory.is_absolute():
        directory = ROOT / directory
    if not directory.is_dir():
        fail(f"compilation database directory does not exist: {directory_value}")
    source_path = pathlib.Path(source)
    if not source_path.is_absolute():
        source_path = directory / source_path
    if not source_path.is_file():
        fail(f"compilation database source does not exist: {source}")
    try:
        return source_path.resolve().relative_to(ROOT.resolve()).as_posix()
    except ValueError:
        fail(f"compilation database source escapes repository root: {source}")


def normalized_include_path(value, directory):
    include_path = pathlib.Path(value)
    directory_path = pathlib.Path(directory)
    if not directory_path.is_absolute():
        directory_path = ROOT / directory_path
    if not include_path.is_absolute():
        include_path = directory_path / include_path
    try:
        return include_path.resolve().relative_to(ROOT.resolve()).as_posix()
    except ValueError:
        return include_path.as_posix()


def path_matches_prefix(path, prefix):
    prefix = prefix.rstrip("/")
    return path == prefix or path.startswith(prefix + "/")


def define_name(define):
    if not define:
        fail("compilation command has an empty define")
    return define.split("=", 1)[0]


def check_source_set(expected, actual, label):
    missing = sorted(expected - actual)
    unexpected = sorted(actual - expected)
    if missing or unexpected:
        details = []
        if missing:
            details.append("missing: " + ", ".join(missing))
        if unexpected:
            details.append("unexpected: " + ", ".join(unexpected))
        fail(f"{label} source set differs from complete manifest (" + "; ".join(details) + ")")


def check_command_source(arguments, entry, source):
    directory = entry.get("directory", str(ROOT))
    if not isinstance(directory, str):
        fail("compilation database entry has a malformed directory")
    directory_path = pathlib.Path(directory)
    if not directory_path.is_absolute():
        directory_path = ROOT / directory_path
    try:
        compile_index = arguments.index("-c")
    except ValueError:
        fail("compilation database entry is not a compile command")
    if compile_index == len(arguments) - 1:
        fail("compilation database entry has no source after -c")

    source_operands = []
    skip_next = False
    value_options = set(INCLUDE_OPTIONS) | {"-D", "-o", "-x"}
    for argument in arguments[1:]:
        if skip_next:
            skip_next = False
            continue
        if argument in value_options:
            skip_next = True
            continue
        if argument == "-c" or argument.startswith("-"):
            continue
        source_operands.append(argument)
    if len(source_operands) != 1:
        fail("compilation database command must have exactly one source file operand")
    source_operand = source_operands[0]
    entry_file = entry.get("file")
    if not isinstance(entry_file, str) or not entry_file:
        fail("compilation database entry has no source file")
    expected = pathlib.Path(entry_file)
    if not expected.is_absolute():
        expected = directory_path / expected
    expected = expected.resolve()
    candidate = pathlib.Path(source_operand)
    if not candidate.is_absolute():
        candidate = directory_path / candidate
    if candidate.resolve() != expected:
        fail(f"compilation database command does not compile its source: {source}")


def check_command_source_mutation():
    arguments = command_arguments({"command": "cc unrelated.c -c src/app/main.c"})
    with contextlib.redirect_stderr(io.StringIO()):
        try:
            check_command_source(
                arguments,
                {"directory": str(ROOT), "file": str(ROOT / "src/app/main.c")},
                "src/app/main.c",
            )
        except SystemExit:
            return
    fail("command source check accepted an unrelated source before -c")


def hardware_defines(defines):
    return sorted(
        name
        for name in (define_name(define) for define in defines)
        if name in KERNEL_HARDWARE_DEFINES
        or name.startswith(("STM32", "BSP_", "SOC_", "PKG_"))
    )


def check_compile_commands():
    if not COMPILE_COMMANDS.is_file():
        fail(f"missing generated compilation database: {relative_path(COMPILE_COMMANDS)}")
    try:
        entries = json.loads(COMPILE_COMMANDS.read_text(encoding="utf-8"))
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as error:
        fail(f"could not read compilation database: {error}")
    if not isinstance(entries, list):
        fail("compilation database root must be a list")

    expected, complete_expected = manifest_owned_sources()
    grouped = {group: [] for group in OWNED_GROUPS}
    seen_sources = set()
    source_paths = []
    for entry in entries:
        if not isinstance(entry, dict):
            fail("compilation database contains a non-object entry")
        source = entry_source(entry)
        arguments = command_arguments(entry)
        check_command_source(arguments, entry, source)
        if source in seen_sources:
            fail(f"compilation database contains duplicate source entry: {source}")
        seen_sources.add(source)
        source_paths.append(ROOT / source)
        group = source_group(source)
        if group is not None:
            directory = entry.get("directory", str(ROOT))
            if not isinstance(directory, str):
                fail("compilation database entry has a malformed directory")
            includes = [
                normalized_include_path(include, directory)
                for include in include_values(arguments)
            ]
            grouped[group].append(
                (source, entry, arguments, includes, option_values(arguments, "-D"))
            )

    check_source_set(complete_expected, seen_sources, "compilation database")

    actual = {group: {item[0] for item in group_entries} for group, group_entries in grouped.items()}
    for group in OWNED_GROUPS:
        missing = sorted(expected[group] - actual[group])
        unexpected = sorted(actual[group] - expected[group])
        if missing or unexpected:
            details = []
            if missing:
                details.append("missing: " + ", ".join(missing))
            if unexpected:
                details.append("unexpected: " + ", ".join(unexpected))
            fail(f"compilation database {group} source set differs from manifest (" + "; ".join(details) + ")")

    for group, group_entries in grouped.items():
        if not group_entries:
            fail(f"compilation database has no {group} entries")

    for source, _entry, _arguments, includes, defines in grouped["modules"]:
        if includes != ["src/modules"]:
            fail(f"module include boundary leaked: {source}: {includes}")
        leaked_defines = sorted(set(hardware_defines(defines)) | {
            name for name in (define_name(define) for define in defines)
            if name in MODULE_FORBIDDEN_DEFINES or name.startswith("RT_")
        })
        if leaked_defines:
            fail(f"module define boundary leaked: {source}: {leaked_defines}")

    for source, _entry, _arguments, includes, defines in grouped["kernel"]:
        leaked_includes = sorted(
            include
            for include in includes
            if any(path_matches_prefix(include, prefix) for prefix in KERNEL_HARDWARE_INCLUDE_PREFIXES)
        )
        if leaked_includes:
            fail(f"kernel hardware include boundary leaked: {source}: {leaked_includes}")
        leaked_defines = hardware_defines(defines)
        if leaked_defines:
            fail(f"kernel hardware define boundary leaked: {source}: {leaked_defines}")

    for source, _entry, _arguments, includes, _defines in grouped["platform"]:
        leaked_includes = sorted(
            include for include in includes if include == "src/kernel" or include.startswith("src/kernel/")
        )
        if leaked_includes:
            fail(f"platform reverse dependency leaked: {source}: {leaked_includes}")

    for source, entry, _arguments, includes, defines in grouped["application"]:
        directory = entry.get("directory", str(ROOT))
        normalized_includes = [normalized_include_path(include, directory) for include in includes]
        leaked_includes = sorted(
            include
            for include in normalized_includes
            if any(
                path_matches_prefix(include, prefix)
                for prefix in APPLICATION_HARDWARE_INCLUDE_PREFIXES
            )
        )
        if leaked_includes:
            fail(f"application hardware include boundary leaked: {source}: {leaked_includes}")
        leaked_defines = hardware_defines(defines)
        if leaked_defines:
            fail(f"application hardware define boundary leaked: {source}: {leaked_defines}")

    relevant_inputs = [path for path in CDB_BUILD_INPUTS if path.is_file()]
    relevant_inputs.extend(source_files(ROOT / "src/app"))
    relevant_inputs.extend(source_files(ROOT / "src/kernel"))
    relevant_inputs.extend(source_files(ROOT / "src/modules"))
    relevant_inputs.extend(
        path
        for path in source_files(ROOT / "src/platform")
        if not relative_path(path).startswith("src/platform/board/")
    )
    relevant_inputs.extend(source_paths)
    newest_input = max(path.stat().st_mtime for path in relevant_inputs)
    if COMPILE_COMMANDS.stat().st_mtime <= newest_input:
        fail(
            "compilation database is stale; regenerate "
            f"{relative_path(COMPILE_COMMANDS)} after its source/build inputs"
        )


def main():
    check_command_source_mutation()
    check_direct_dependencies_mutation()
    check_kernel_includes()
    check_empty_dependencies()
    check_compile_commands()
    print("dependency boundary checks passed")


if __name__ == "__main__":
    main()
