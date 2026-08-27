import importlib.util
import pathlib
import tempfile
import unittest
from unittest import mock


CHECKER_PATH = pathlib.Path(__file__).with_name("test_dependency_boundaries.py")
SPEC = importlib.util.spec_from_file_location("dependency_boundaries", CHECKER_PATH)
checker = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(checker)


class DependencyBoundaryTests(unittest.TestCase):
    def test_repository_build_files_include_board_but_not_vendor(self):
        self.assertIn(
            checker.ROOT / "src/platform/board/stm32f103c8/SConscript",
            checker.BUILD_FILES,
        )
        self.assertNotIn(
            checker.ROOT / "vendor/rt-thread/src/SConscript",
            checker.BUILD_FILES,
        )

    def test_define_group_dependencies_fail_closed(self):
        cases = {
            "missing": "DefineGroup('broken', [], [])",
            "opaque": "DefineGroup('broken', [], dependency_list)",
            "empty sentinel": "DefineGroup('broken', [], depend=[''])",
        }

        for label, source in cases.items():
            with self.subTest(dependency=label):
                with tempfile.TemporaryDirectory() as directory:
                    path = pathlib.Path(directory) / "SConscript"
                    path.write_text(source, encoding="utf-8")
                    with mock.patch.object(checker, "BUILD_FILES", [path]):
                        with self.assertRaises(SystemExit):
                            checker.check_empty_dependencies()

    def test_manifest_source_set_includes_board_and_vendor_sources(self):
        _expected_groups, expected_sources = checker.manifest_owned_sources()

        self.assertIn("src/platform/board/stm32f103c8/board.c", expected_sources)
        self.assertIn("vendor/rt-thread/src/thread.c", expected_sources)

        for missing_source in (
            "src/platform/board/stm32f103c8/board.c",
            "vendor/rt-thread/src/thread.c",
        ):
            with self.subTest(missing_source=missing_source):
                with self.assertRaises(SystemExit):
                    checker.check_source_set(
                        expected_sources,
                        expected_sources - {missing_source},
                        "compilation database",
                    )

    def test_command_source_rejects_extra_operand_before_compile_flag(self):
        arguments = checker.command_arguments(
            {"command": "cc unrelated.c -c src/app/main.c"}
        )

        with self.assertRaises(SystemExit):
            checker.check_command_source(
                arguments,
                {"directory": str(checker.ROOT), "file": str(checker.ROOT / "src/app/main.c")},
                "src/app/main.c",
            )

    def test_command_source_matches_cdb_file_not_caller_label(self):
        arguments = checker.command_arguments({"command": "cc -c src/app/main.c"})

        with self.assertRaises(SystemExit):
            checker.check_command_source(
                arguments,
                {
                    "directory": str(checker.ROOT),
                    "file": str(checker.ROOT / "src/kernel/imu/imu_service.c"),
                },
                "src/app/main.c",
            )


if __name__ == "__main__":
    unittest.main()
