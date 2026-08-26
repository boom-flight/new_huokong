#!/usr/bin/env python3

import pathlib
import tempfile
import unittest

from tools.firmware_manifest import (
    ManifestError,
    load_manifest,
    manifest_sources,
    validate_manifest,
)


def valid_manifest(sources):
    return {
        "version": 1,
        "target": {
            "name": "STM32F103C8",
            "device": "STM32F103xB",
            "cpu": "Cortex-M3",
            "flash": {"origin": "0x08000000", "length": 65536},
            "ram": {"origin": "0x20000000", "length": 20480},
            "stack_size": 1024,
        },
        "groups": [
            {
                "name": "application",
                "sources": sources,
                "include_paths": ["src"],
                "defines": ["STM32F103xB"],
            }
        ],
    }


class ManifestTests(unittest.TestCase):
    def setUp(self):
        self.temp_dir = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp_dir.cleanup)
        self.tmp_path = pathlib.Path(self.temp_dir.name)

    def test_rejects_source_outside_src_or_vendor(self):
        manifest = valid_manifest(["tests/fixtures/not-firmware.c"])
        with self.assertRaisesRegex(ManifestError, "tests/fixtures/not-firmware.c"):
            validate_manifest(self.tmp_path, manifest)

    def test_rejects_missing_source(self):
        manifest = valid_manifest(["src/app/missing.c"])
        with self.assertRaisesRegex(ManifestError, "src/app/missing.c"):
            validate_manifest(self.tmp_path, manifest)

    def test_rejects_duplicate_source(self):
        manifest = valid_manifest(["src/app/main.c", "src/app/main.c"])
        app_dir = self.tmp_path / "src/app"
        app_dir.mkdir(parents=True)
        (app_dir / "main.c").write_text("", encoding="utf-8")
        with self.assertRaisesRegex(ManifestError, "duplicate"):
            validate_manifest(self.tmp_path, manifest)

    def test_accepts_current_target_and_returns_stable_sources(self):
        manifest = valid_manifest(["src/app/main.c", "vendor/rt-thread/src/thread.c"])
        (self.tmp_path / "src/app").mkdir(parents=True)
        (self.tmp_path / "src/app/main.c").write_text("", encoding="utf-8")
        (self.tmp_path / "vendor/rt-thread/src").mkdir(parents=True)
        (self.tmp_path / "vendor/rt-thread/src/thread.c").write_text("", encoding="utf-8")
        validate_manifest(self.tmp_path, manifest)
        self.assertEqual(
            manifest_sources(manifest),
            ["src/app/main.c", "vendor/rt-thread/src/thread.c"],
        )

    def test_rejects_invalid_target_and_source_extension(self):
        manifest = valid_manifest(["src/app/main.txt"])
        manifest["target"]["flash"]["length"] = 1
        app_dir = self.tmp_path / "src/app"
        app_dir.mkdir(parents=True)
        (app_dir / "main.txt").write_text("", encoding="utf-8")
        with self.assertRaisesRegex(ManifestError, "target"):
            validate_manifest(self.tmp_path, manifest)

        manifest["target"]["flash"]["length"] = 65536
        with self.assertRaisesRegex(ManifestError, "main.txt"):
            validate_manifest(self.tmp_path, manifest)

    def test_rejects_invalid_include_and_define_values(self):
        manifest = valid_manifest(["src/app/main.c"])
        (self.tmp_path / "src/app").mkdir(parents=True)
        (self.tmp_path / "src/app/main.c").write_text("", encoding="utf-8")
        manifest["groups"][0]["include_paths"] = ["src", 1]
        with self.assertRaisesRegex(ManifestError, "include_paths"):
            validate_manifest(self.tmp_path, manifest)

        manifest["groups"][0]["include_paths"] = ["src"]
        manifest["groups"][0]["defines"] = ["STM32F103xB", None]
        with self.assertRaisesRegex(ManifestError, "defines"):
            validate_manifest(self.tmp_path, manifest)

    def test_load_manifest_accepts_path_like_input(self):
        manifest_path = self.tmp_path / "manifest.json"
        manifest_path.write_text("{}", encoding="utf-8")
        self.assertEqual(load_manifest(str(manifest_path)), {})

    def test_accepts_independent_keil_source_override(self):
        manifest = valid_manifest(["src/app/main.c"])
        manifest["groups"][0]["keil_sources"] = [
            "src/app/main.c",
            "vendor/startup.s",
        ]
        (self.tmp_path / "src/app").mkdir(parents=True)
        (self.tmp_path / "src/app/main.c").write_text("", encoding="utf-8")
        (self.tmp_path / "vendor").mkdir(parents=True)
        (self.tmp_path / "vendor/startup.s").write_text("", encoding="utf-8")
        validate_manifest(self.tmp_path, manifest)


if __name__ == "__main__":
    unittest.main()
