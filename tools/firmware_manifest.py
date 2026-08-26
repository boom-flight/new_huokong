#!/usr/bin/env python3

import json
from pathlib import Path


class ManifestError(ValueError):
    """Raised when a firmware manifest is malformed or unsafe."""


EXPECTED_TARGET = {
    "name": "STM32F103C8",
    "device": "STM32F103xB",
    "cpu": "Cortex-M3",
    "flash": {"origin": "0x08000000", "length": 65536},
    "ram": {"origin": "0x20000000", "length": 20480},
    "stack_size": 1024,
}
SOURCE_SUFFIXES = {".c", ".h", ".s", ".S"}


def load_manifest(path):
    path = Path(path)
    try:
        manifest = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as error:
        raise ManifestError(f"cannot load manifest {path}: {error}") from error
    if not isinstance(manifest, dict):
        raise ManifestError("manifest root must be an object")
    return manifest


def validate_manifest(root, manifest):
    if not isinstance(manifest, dict):
        raise ManifestError("manifest root must be an object")
    if manifest.get("version") != 1:
        raise ManifestError("manifest version must be 1")
    if manifest.get("target") != EXPECTED_TARGET:
        raise ManifestError("target does not match the STM32F103C8 profile")

    groups = manifest.get("groups")
    if not isinstance(groups, list) or not groups:
        raise ManifestError("groups must be a non-empty list")

    seen_groups = set()
    seen_sources = set()
    seen_keil_sources = set()
    for group in groups:
        if not isinstance(group, dict):
            raise ManifestError("each group must be an object")
        name = group.get("name")
        if not isinstance(name, str) or not name:
            raise ManifestError("group name must be a non-empty string")
        if name in seen_groups:
            raise ManifestError(f"duplicate group name: {name}")
        seen_groups.add(name)

        sources = group.get("sources")
        if not isinstance(sources, list):
            raise ManifestError(f"{name}: sources must be a list")

        def validate_sources(values, field, seen):
            for source in values:
                if not isinstance(source, str):
                    raise ManifestError(f"{name}: {field} path must be a string")
                source_path = Path(source)
                if (
                    source_path.is_absolute()
                    or ".." in source_path.parts
                    or source_path.parts[:1] not in (("src",), ("vendor",))
                ):
                    raise ManifestError(f"invalid source path: {source}")
                if source_path.suffix not in SOURCE_SUFFIXES:
                    raise ManifestError(f"unsupported source extension: {source}")
                if source in seen:
                    raise ManifestError(f"duplicate source path: {source}")
                seen.add(source)
                if not (Path(root) / source_path).is_file():
                    raise ManifestError(f"missing source path: {source}")

        validate_sources(sources, "source", seen_sources)
        keil_sources = group.get("keil_sources")
        if keil_sources is not None and not isinstance(keil_sources, list):
            raise ManifestError(f"{name}: keil_sources must be a list")
        if keil_sources is not None:
            validate_sources(keil_sources, "Keil source", seen_keil_sources)

        for field in ("include_paths", "defines"):
            values = group.get(field)
            if not isinstance(values, list) or not all(
                isinstance(value, str) for value in values
            ):
                raise ManifestError(f"{name}: {field} must contain strings")
            for value in values:
                value_path = Path(value)
                if field == "include_paths" and (
                    value_path.is_absolute() or ".." in value_path.parts
                ):
                    raise ManifestError(f"invalid include path: {value}")


def manifest_sources(manifest):
    return [source for group in manifest["groups"] for source in group["sources"]]


def manifest_group_settings(manifest, name, root=None):
    for group in manifest["groups"]:
        if group["name"] == name:
            include_paths = list(group["include_paths"])
            if root is not None:
                include_paths = [str(Path(root) / path) for path in include_paths]
            return {
                "include_paths": include_paths,
                "defines": list(group["defines"]),
            }
    raise ManifestError(f"missing manifest group: {name}")
