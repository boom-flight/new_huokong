#!/usr/bin/env python3

import argparse
import os
import pathlib
import tempfile
import xml.etree.ElementTree as ET

from firmware_manifest import (
    ManifestError,
    load_manifest,
    manifest_group_settings,
    validate_manifest,
)


class KeilProjectError(ValueError):
    """Raised when a Keil project cannot be generated or checked."""


def windows_path(path):
    return str(path).replace(os.sep, "\\")


def relative_windows_path(root, base, path):
    return windows_path(pathlib.Path(os.path.relpath(path, base)))


def approved_output(root, output):
    try:
        relative = output.relative_to(root)
    except ValueError as error:
        raise KeilProjectError("output must be inside the repository") from error
    if relative.suffix.lower() != ".uvprojx":
        raise KeilProjectError("output must be a .uvprojx file")
    if relative.parts[:2] not in (("project", "keil"), ("build", "keil")):
        raise KeilProjectError(
            "output must be under project/keil or build/keil"
        )


def file_type(path):
    suffix = pathlib.Path(path).suffix
    if suffix == ".c":
        return "1"
    if suffix in {".s", ".S"}:
        return "2"
    if suffix == ".h":
        return "5"
    raise KeilProjectError(f"unsupported project file extension: {path}")


def group_sources(group):
    return group.get("keil_sources", group["sources"])


def add_text(parent, tag, text):
    ET.SubElement(parent, tag).text = str(text)


def project_tree(root, manifest, output):
    project_dir = output.parent
    build_dir = root / "build/keil/stm32f103c8/Debug"
    scatter_file = root / "project/keil/stm32f103c8.sct"
    output_dir = relative_windows_path(root, project_dir, build_dir) + "\\"
    listing_dir = output_dir + "Listings\\"
    objects_dir = output_dir + "Objects\\"
    database_dir = output_dir + "Database\\"
    scatter_path = relative_windows_path(root, project_dir, scatter_file)

    target = manifest["target"]
    project = ET.Element("Project")
    add_text(project, "SchemaVersion", "2.1")
    add_text(project, "Header", "### uVision Project, (C) Keil Software")
    targets = ET.SubElement(project, "Targets")
    target_node = ET.SubElement(targets, "Target")
    add_text(target_node, "TargetName", "huokong")
    add_text(target_node, "ToolsetNumber", "0x0")
    add_text(target_node, "ToolsetName", "ARM-CLANG")

    target_option = ET.SubElement(target_node, "TargetOption")
    common = ET.SubElement(target_option, "TargetCommonOption")
    add_text(common, "Device", target["name"])
    add_text(common, "Vendor", "STMicroelectronics")
    add_text(
        common,
        "Cpu",
        f'IROM({target["flash"]["origin"]},{target["flash"]["length"]:#x}) '
        f'IRAM({target["ram"]["origin"]},{target["ram"]["length"]:#x}) '
        f'CPUTYPE("{target["cpu"]}")',
    )
    add_text(common, "OutputDirectory", output_dir)
    add_text(common, "ListingPath", listing_dir)
    add_text(common, "OutputName", "huokong")
    add_text(common, "CreateExecutable", "1")
    add_text(common, "CreateHexFile", "1")
    add_text(common, "CreateBinFile", "1")
    add_text(common, "CreateBatchFile", "0")
    add_text(common, "DebugInformation", "1")
    add_text(common, "MapFile", output_dir + "huokong.map")
    add_text(common, "ObjectsPath", objects_dir)
    add_text(common, "DatabasePath", database_dir)

    common_property = ET.SubElement(target_option, "CommonProperty")
    add_text(common_property, "UseCPPCompiler", "0")

    arm_ads = ET.SubElement(target_option, "TargetArmAds")
    arm_misc = ET.SubElement(arm_ads, "ArmAdsMisc")
    add_text(arm_misc, "UseScatterFile", "1")
    add_text(arm_misc, "ScatterFile", scatter_path)
    add_text(arm_misc, "StackSize", hex(target["stack_size"]))

    cads = ET.SubElement(arm_ads, "Cads")
    controls = ET.SubElement(cads, "VariousControls")
    defines = []
    include_paths = []
    for group in manifest["groups"]:
        settings = manifest_group_settings(manifest, group["name"])
        for define in settings["defines"]:
            if define not in defines:
                defines.append(define)
        for include_path in settings["include_paths"]:
            if include_path not in include_paths:
                include_paths.append(include_path)
    add_text(controls, "Define", ",".join(defines))
    add_text(
        controls,
        "IncludePath",
        ";".join(
            relative_windows_path(root, project_dir, root / include_path)
            for include_path in include_paths
        ),
    )
    add_text(controls, "LanguageC", "1")
    add_text(controls, "CompilerVersion", "6")
    add_text(
        controls,
        "MiscControls",
        "--target=arm-arm-none-eabi -mcpu=cortex-m3 -mthumb --std=c11 -Oz -g",
    )
    add_text(controls, "RvctClang", "1")

    ldads = ET.SubElement(arm_ads, "LDads")
    add_text(ldads, "ScatterFile", scatter_path)
    add_text(ldads, "Misc", "--map --list " + listing_dir + "huokong.map")

    groups = ET.SubElement(target_node, "Groups")
    seen_file_names = set()
    for group in manifest["groups"]:
        group_node = ET.SubElement(groups, "Group")
        add_text(group_node, "GroupName", group["name"])
        files = ET.SubElement(group_node, "Files")
        for source in group_sources(group):
            file_name = pathlib.Path(source).name
            if file_name in seen_file_names:
                raise KeilProjectError(f"duplicate project object name: {file_name}")
            seen_file_names.add(file_name)
            file_node = ET.SubElement(files, "File")
            add_text(file_node, "FileName", file_name)
            add_text(file_node, "FileType", file_type(source))
            add_text(
                file_node,
                "FilePath",
                relative_windows_path(root, project_dir, root / source),
            )
    ET.indent(project, space="  ")
    return ET.ElementTree(project)


def rendered_tree(root, manifest, output):
    tree = project_tree(root, manifest, output)
    return ET.tostring(tree.getroot(), encoding="utf-8", xml_declaration=True) + b"\n"


def check_project(root, manifest, output):
    if not output.is_file():
        raise KeilProjectError(f"project file not found: {output}")
    try:
        actual = ET.parse(output)
    except (OSError, ET.ParseError) as error:
        raise KeilProjectError(f"cannot parse project file: {error}") from error
    expected = ET.fromstring(rendered_tree(root, manifest, output))
    actual_sources = {
        element.text or "" for element in actual.getroot().findall(".//FilePath")
    }
    expected_sources = {
        element.text or "" for element in expected.findall(".//FilePath")
    }
    if actual_sources != expected_sources:
        raise KeilProjectError("project source set differs from manifest")
    if ET.tostring(actual.getroot()) != ET.tostring(expected):
        raise KeilProjectError("project configuration differs from manifest")


def write_project(output, content):
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(
            mode="wb", dir=output.parent, prefix=output.name + ".", suffix=".tmp", delete=False
        ) as stream:
            temporary = pathlib.Path(stream.name)
            stream.write(content)
        temporary.replace(output)
    finally:
        if temporary is not None and temporary.exists():
            temporary.unlink()


def parse_args():
    parser = argparse.ArgumentParser(description="Generate the Huokong Keil project")
    parser.add_argument("--root", type=pathlib.Path, default=pathlib.Path("."))
    parser.add_argument("--manifest", type=pathlib.Path, required=True)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    parser.add_argument("--check", action="store_true")
    return parser.parse_args()


def main():
    args = parse_args()
    root = args.root.resolve()
    manifest_path = args.manifest
    if not manifest_path.is_absolute():
        manifest_path = root / manifest_path
    output = args.output.resolve()
    approved_output(root, output)
    manifest = load_manifest(manifest_path)
    validate_manifest(root, manifest)
    if args.check:
        check_project(root, manifest, output)
        print(f"Keil project is up to date: {output}")
    else:
        write_project(output, rendered_tree(root, manifest, output))
        print(f"Generated Keil project: {output}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (ManifestError, KeilProjectError) as error:
        print(f"error: {error}", file=os.sys.stderr)
        raise SystemExit(1)
