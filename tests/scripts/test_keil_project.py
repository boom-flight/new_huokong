#!/usr/bin/env python3

import json
import pathlib
import subprocess
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET


REPOSITORY_ROOT = pathlib.Path(__file__).resolve().parents[2]
GENERATOR = REPOSITORY_ROOT / "tools/generate-keil-project.py"


class KeilProjectTests(unittest.TestCase):
    def setUp(self):
        self.temp_dir = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp_dir.cleanup)
        self.tmp_path = pathlib.Path(self.temp_dir.name)

    def manifest(self):
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
                    "sources": ["src/app/main.c"],
                    "include_paths": ["src", "vendor/include"],
                    "defines": ["STM32F103xB", "USE_HAL_DRIVER"],
                },
                {
                    "name": "startup",
                    "sources": ["vendor/startup_gcc.s"],
                    "keil_sources": ["vendor/startup.s"],
                    "include_paths": ["vendor/include"],
                    "defines": ["__RTTHREAD__"],
                },
            ],
        }

    def write_fixture_manifest(self):
        manifest_path = self.tmp_path / "project/firmware-manifest.json"
        manifest_path.parent.mkdir(parents=True)
        (self.tmp_path / "src/app").mkdir(parents=True)
        (self.tmp_path / "vendor/include").mkdir(parents=True)
        (self.tmp_path / "vendor/startup.s").write_text("", encoding="utf-8")
        (self.tmp_path / "vendor/startup_gcc.s").write_text("", encoding="utf-8")
        (self.tmp_path / "src/app/main.c").write_text("", encoding="utf-8")
        manifest_path.write_text(
            json.dumps(self.manifest(), indent=2) + "\n", encoding="utf-8"
        )
        return manifest_path

    def run_generator(self, root, output, *flags):
        return subprocess.run(
            [
                sys.executable,
                str(GENERATOR),
                "--root",
                str(root),
                "--manifest",
                str(root / "project/firmware-manifest.json"),
                "--output",
                str(output),
                *flags,
            ],
            capture_output=True,
            text=True,
        )

    def generate_fixture_project(self, root=None):
        root = root or self.tmp_path
        self.write_fixture_manifest()
        output = root / "project/keil/huokong.uvprojx"
        result = self.run_generator(root, output)
        self.assertEqual(result.returncode, 0, result.stderr)
        return output

    @staticmethod
    def project_files(xml):
        return {
            path.replace("/", "\\")
            for path in (element.text or "" for element in xml.findall(".//FilePath"))
        }

    @staticmethod
    def project_output_options(xml):
        target = xml.find(".//TargetOption")
        return " ".join(target.itertext()) if target is not None else ""

    @staticmethod
    def replace_one_source_with_unlisted_file(output):
        tree = ET.parse(output)
        file_path = tree.find(".//FilePath")
        file_path.text = r"..\..\src\app\unlisted.c"
        tree.write(output, encoding="utf-8", xml_declaration=True)

    def test_generates_relative_sources_and_separate_output(self):
        output = self.generate_fixture_project()
        xml = ET.parse(output)
        self.assertEqual(xml.findtext(".//TargetName"), "huokong")
        self.assertIn(r"..\..\src\app\main.c", self.project_files(xml))
        self.assertIn(r"..\..\vendor\startup.s", self.project_files(xml))
        self.assertNotIn(r"..\..\vendor\startup_gcc.s", self.project_files(xml))
        self.assertIn(
            r"..\..\build\keil\stm32f103c8\Debug",
            self.project_output_options(xml),
        )
        self.assertNotIn("build\\scons", output.read_text(encoding="utf-8"))

    def test_check_rejects_project_source_drift(self):
        output = self.generate_fixture_project()
        self.replace_one_source_with_unlisted_file(output)
        result = self.run_generator(self.tmp_path, output, "--check")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("source", result.stderr.lower())

    def test_rejects_missing_source(self):
        self.write_fixture_manifest()
        manifest_path = self.tmp_path / "project/firmware-manifest.json"
        manifest = self.manifest()
        manifest["groups"][0]["sources"] = ["src/app/missing.c"]
        manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
        output = self.tmp_path / "project/keil/huokong.uvprojx"
        result = self.run_generator(self.tmp_path, output)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("missing", result.stderr.lower())
        self.assertFalse(output.exists())

    def test_rejects_output_outside_approved_roots(self):
        self.write_fixture_manifest()
        output = self.tmp_path / "huokong.uvprojx"
        result = self.run_generator(self.tmp_path, output)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("output", result.stderr.lower())

    def test_rejects_unsupported_extension(self):
        self.write_fixture_manifest()
        manifest_path = self.tmp_path / "project/firmware-manifest.json"
        manifest = self.manifest()
        manifest["groups"][0]["sources"] = ["src/app/main.txt"]
        (self.tmp_path / "src/app/main.txt").write_text("", encoding="utf-8")
        manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
        result = self.run_generator(
            self.tmp_path, self.tmp_path / "project/keil/huokong.uvprojx"
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("extension", result.stderr.lower())


if __name__ == "__main__":
    unittest.main()
