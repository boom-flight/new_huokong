# Linux 与 Keil 双构建隔离实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在保留 Linux/SCons + GNU Arm 构建的同时，增加可由 Keil MDK5 ARMCLANG 原生编译的 uVision 工程，并将两套构建产物完全隔离。

**Architecture:** 使用 `project/firmware-manifest.json` 作为两个固件后端共享的源码边界。SCons 输出迁移到 `build/scons/`，Keil 工程固定输出到 `build/keil/stm32f103c8/Debug/`；GCC linker script 与 ARMCLANG scatter file 分开维护，源码和第三方快照不复制。

**Tech Stack:** C11, Python 3 standard library, SCons, GNU Arm Embedded Toolchain, Keil MDK5 ARMCLANG, uVision `.uvprojx`, ARM scatter file, POSIX shell.

**Spec:** `docs/superpowers/specs/2026-08-26-dual-build-keil-design.md`

## Global Constraints

- 目标固定为 `STM32F103xB / Cortex-M3`，Flash 为 `64 KiB`，SRAM 为 `20 KiB`。
- 两个后端共享 `src/` 和 `vendor/`，不复制自研源码或第三方源码。
- `tests/` 只用于主机测试，不进入 Keil 固件工程。
- 固件构建产物只能进入 `build/scons/` 或 `build/keil/`；`.uvoptx/.uvguix` 是被忽略的用户级 IDE 元数据例外。
- GCC 使用 `src/platform/board/stm32f103c8/linker_scripts/link.lds`；Keil 使用 `project/keil/stm32f103c8.sct`。
- 不修改、格式化或升级 RT-Thread、CMSIS、HAL 和 STM32 驱动快照。
- 不改变协议、线程优先级、引脚、中断所有权或运行时行为。
- 不使用目录扫描、递归 Glob 或隐式发现生产源码；清单中的路径必须显式且稳定。
- 不创建 Git commit；每个任务结束只运行检查并保留工作树变更，除非用户另外明确要求提交。

---

### Task 1: 建立共享固件清单和验证器

**Files:**
- Create: `project/firmware-manifest.json`
- Create: `tools/firmware_manifest.py`
- Create: `tests/scripts/test_firmware_manifest.py`
- Modify: `tests/scripts/test_repository_layout.sh`

**Interfaces:**
- `tools/firmware_manifest.py` 提供 `ManifestError(ValueError)`。
- `load_manifest(path: pathlib.Path) -> dict` 读取 JSON，并拒绝非法 JSON 或缺少根字段。
- `validate_manifest(root: pathlib.Path, manifest: dict) -> None` 校验目标、路径、分组和重复项；失败时抛出包含相对路径的 `ManifestError`。
- `manifest_sources(manifest: dict) -> list[str]` 按 JSON 顺序返回去重后的生产源码路径。
- manifest 根字段固定为 `version`、`target`、`groups`；`target` 包含 `name`、`device`、`cpu`、`flash`、`ram`、`stack_size`；每个 group 包含 `name`、`sources`、`include_paths`、`defines`。

- [ ] **Step 1: 写清单验证失败测试**

在 `tests/scripts/test_firmware_manifest.py` 中使用 `unittest.TestCase` 和 `tempfile.TemporaryDirectory` 构造以下测试。测试文件定义 `valid_manifest(sources)` helper，并通过 `from tools.firmware_manifest import ManifestError, manifest_sources, validate_manifest` 使用生产接口：

```python
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
```

Add a small `valid_manifest()` fixture helper in the same test file so tests do not depend on the production repository.

- [ ] **Step 2: 运行测试确认 RED**

Run from `new_huokong/`:

```sh
python3 -m unittest tests/scripts/test_firmware_manifest.py -v
```

Expected: FAIL because `tools/firmware_manifest.py` does not yet exist.

- [ ] **Step 3: 写最小验证器实现**

Implement the three interfaces using only `json`, `pathlib`, and standard exceptions. Reject:

- `version != 1`;
- target values other than `STM32F103xB`, `Cortex-M3`, `0x08000000`, `65536`, `0x20000000`, `20480`, and `1024`;
- empty group names or duplicate group names;
- absolute paths, `..` path components, paths outside `src/` or `vendor/`, non-C/H/assembly source extensions, missing files, and duplicate source paths;
- include paths or defines that are not strings.

- [ ] **Step 4: 创建当前固定目标的 manifest**

Populate `project/firmware-manifest.json` with explicit groups for the current production build. Include all self-owned files already named by `SConscript`, the board startup/system files, the active HAL files from `vendor/stm32f1-hal/SConscript`, the active STM32 driver files from `vendor/rt-thread-stm32-drivers`, the CMSIS device startup/system files, and the RT-Thread files selected by the checked-in `.config`.

Use repository-relative POSIX paths. Do not use `*`, directory roots, or generated files. Keep group order and source order stable. Include the current board include directories and public defines, including `STM32F103xB`, `USE_HAL_DRIVER`, `__RTTHREAD__`, and the `.config`-derived defines.

- [ ] **Step 5: 运行 GREEN 测试和 manifest layout checks**

Run:

```sh
python3 -m unittest tests/scripts/test_firmware_manifest.py -v
python3 -c 'from pathlib import Path; from tools.firmware_manifest import load_manifest, validate_manifest; root=Path("."); m=load_manifest(root / "project/firmware-manifest.json"); validate_manifest(root, m); print(len(m["groups"]), "groups")'
```

Expected: all tests pass and validation reports the number of current groups.

- [ ] **Step 6: 加入仓库布局门禁**

Update `tests/scripts/test_repository_layout.sh` to require `project/firmware-manifest.json` and invoke the validator. The shell check must fail closed if Python cannot load or validate the manifest. Run:

```sh
sh tests/scripts/test_repository_layout.sh
git diff --check
```

Expected: the manifest is present, valid, and no whitespace errors are reported.

---

### Task 2: 迁移 SCons 和主机测试输出目录

**Files:**
- Modify: `SConstruct`
- Modify: `SConscript`
- Modify: `rtconfig.py`
- Modify: `src/kernel/imu/SConscript`
- Modify: `src/kernel/telemetry/SConscript`
- Modify: `src/platform/devices/SConscript`
- Modify: `src/platform/time/SConscript`
- Modify: `src/platform/transport/SConscript`
- Modify: `tests/SConstruct`
- Modify: `tools/build.sh`
- Modify: `tools/clean.sh`
- Modify: `tools/check-size.sh`
- Modify: `tools/flash.sh`
- Modify: `tools/debug.sh`
- Modify: `tools/test.sh`
- Modify: `tests/scripts/test_repository_layout.sh`
- Modify: `.clangd`
- Modify: `docs/development/build-test-debug.md`
- Modify: `README.md`

**Interfaces:**
- Firmware ELF path becomes `build/scons/firmware/huokong.elf`.
- Firmware BIN and MAP become `build/scons/firmware/huokong.bin` and `build/scons/firmware/huokong.map`.
- Host test executables become `build/scons/host-tests/<name>`.
- Compile database becomes `build/scons/compile_commands.json`.
- `tools/check-size.sh` keeps its current argument interface and defaults to the new SCons ELF, MAP, and GNU linker script.
- `tools/flash.sh` and `tools/debug.sh` default to the new SCons ELF/BIN and reject a Keil AXF path.

- [ ] **Step 1: 写输出路径门禁的失败断言**

Before editing build files, extend `tests/scripts/test_repository_layout.sh` with exact assertions for `build/scons/firmware`, `build/scons/host-tests`, and the absence of active `build/firmware` and `build/host-tests` references. Run:

```sh
sh tests/scripts/test_repository_layout.sh
```

Expected: FAIL on the old output paths.

- [ ] **Step 2: 迁移 SConstruct 的 firmware root**

Change `FIRMWARE_DIR`, `TARGET`, `BIN_TARGET`, `MAP_TARGET`, `SConsignFile`, and `centralize_object_target()` so every firmware object and side effect is under `build/scons/firmware/`. Preserve `DefaultEnvironment`, `PrepareBuilding`, vendor source selection, the `--cdb` behavior, and the existing map name. Export the validated manifest from the root SCons environment and use it to verify all declared production source paths before building.

- [ ] **Step 3: 迁移 explicit object targets**

Replace every `#build/firmware/...` target in the kernel and platform SConscripts with `#build/scons/firmware/...`. Keep each existing group-specific include path and compiler define unchanged. Do not alter vendor snapshot SConscripts.

- [ ] **Step 4: 迁移 rtconfig and size checker defaults**

Change the map argument in `rtconfig.py` to `build/scons/firmware/huokong.map`. Change `tools/check-size.sh` defaults to:

```sh
elf=${1:-build/scons/firmware/huokong.elf}
map_file=${MAP_FILE:-build/scons/firmware/huokong.map}
```

Keep the memory ranges, Flash/SRAM limits, section checks, and GNU linker script path unchanged.

- [ ] **Step 5: 迁移主机测试 output root**

Change `tests/SConstruct` object and program targets from `build/host-tests` to `build/scons/host-tests`, and change `tools/test.sh` discovery and focused-test paths to match. Keep the existing aliases and test ordering.

- [ ] **Step 6: 更新工具、文档和尺寸测试引用**

Update `tools/build.sh`, `tools/clean.sh`, `tools/flash.sh`, and `tools/debug.sh` to use only SCons paths. Update all active documentation and tests that refer to the retired paths. `tools/clean.sh` must remove or clean only `build/scons/`; it must never remove `build/keil/`.

- [ ] **Step 7: 运行 SCons regression**

Run:

```sh
tools/test.sh
tools/build.sh --cdb
tools/check-size.sh build/scons/firmware/huokong.elf
sh tests/scripts/test_repository_layout.sh
```

Expected: host tests, layout checks, SCons firmware build, size checks, and existing IRQ owner checks pass; no file is written under `build/keil/`.

---

### Task 3: 实现 Keil 工程生成器和 scatter file

**Files:**
- Create: `tools/generate-keil-project.py`
- Create: `project/keil/stm32f103c8.sct`
- Create: `tests/scripts/test_keil_project.py`
- Create: `project/keil/huokong.uvprojx`
- Modify: `project/firmware-manifest.json`

**Interfaces:**
- `generate-keil-project.py` accepts `--manifest`, `--output`, `--root`, and `--check`.
- Normal mode validates the manifest and atomically writes the complete deterministic `.uvprojx` to `--output`.
- `--check` validates the existing XML against the manifest and exits nonzero on source, include, define, target, or output path drift without writing files.
- `project/keil/stm32f103c8.sct` defines `LR_IROM1 0x08000000 0x10000`, `ER_IROM1`, `RW_IRAM1 0x20000000 0x5000`, and a `0x400` stack reservation.

- [ ] **Step 1: 写 XML generator 的失败测试**

In `tests/scripts/test_keil_project.py`, use `unittest.TestCase` and a temporary root containing a small manifest and source files. `setUp()` creates `self.tmp_path` with `tempfile.TemporaryDirectory()` and registers cleanup. Define these test helpers: `run_generator(root, output, *flags) -> subprocess.CompletedProcess`, `generate_fixture_project(root) -> pathlib.Path`, `project_files(xml) -> set[str]`, `project_output_options(xml) -> str`, and `replace_one_source_with_unlisted_file(output) -> None`. Test that the generator:

```python
class KeilProjectTests(unittest.TestCase):
    def test_generates_relative_sources_and_separate_output(self):
        output = self.tmp_path / "project/keil/huokong.uvprojx"
        run_generator(self.tmp_path, output)
        xml = ET.parse(output)
        self.assertEqual(xml.findtext(".//TargetName"), "huokong")
        self.assertIn("..\\..\\src\\app\\main.c", project_files(xml))
        self.assertIn("build\\keil\\stm32f103c8\\Debug", project_output_options(xml))
        self.assertNotIn("build\\scons", output.read_text(encoding="utf-8"))


    def test_check_rejects_project_source_drift(self):
        output = generate_fixture_project(self.tmp_path)
        replace_one_source_with_unlisted_file(output)
        result = run_generator(self.tmp_path, output, "--check")
        self.assertNotEqual(result.returncode, 0)
```

Also test missing source, duplicate source, unsupported extension, and output path outside `build/keil/` all fail with a nonzero result and an actionable error.

- [ ] **Step 2: 运行 generator tests 确认 RED**

Run:

```sh
python3 -m unittest tests/scripts/test_keil_project.py -v
```

Expected: FAIL because the generator and project XML do not yet exist.

- [ ] **Step 3: 实现 manifest-to-uVision XML generator**

Use `xml.etree.ElementTree` and stable insertion order. Generate groups for each manifest group, source entries relative to `project/keil/`, include paths and defines in ARMCLANG target settings, and target output settings that point to `build/keil/stm32f103c8/Debug/`. Emit `.axf`, `.hex`, `.bin`, `.map`, Objects, Listings, and Database paths below that directory. Convert relative paths to Windows separators only in XML; keep manifest paths POSIX.

Write through a sibling temporary file and `Path.replace()` so validation errors cannot leave a partial checked-in project. In `--check` mode parse XML, normalize separators, compare source sets and configuration fields, and do not modify the file.

- [ ] **Step 4: 配置 ARMCLANG project settings and compatibility only when required**

Configure the project for ARM Compiler 6, C11, `STM32F103xB`, `USE_HAL_DRIVER`, `__RTTHREAD__`, the checked-in RT-Thread configuration, CMSIS ARMCLANG headers, and the ARM startup file. Do not include `tests/`, `.lds`, SConscripts, or generated files. If ARMCLANG diagnostics identify a self-owned compatibility gap, create `project/keil/compiler_compat.h` with only that definition and wire it through the project settings; otherwise do not create the header. Do not add module-level `#ifdef KEIL`.

- [ ] **Step 5: 写并验证 scatter file**

Create the scatter file with explicit ROM/RAM regions and the existing RT-Thread section requirements. Keep the vector table in read-only code at Flash origin, place `.data` load data in Flash with runtime address in SRAM, reserve the 0x400 stack, and place zero-initialized data in SRAM. Add comments identifying the matching GNU linker memory ranges.

- [ ] **Step 6: 生成并检查 committed `.uvprojx`**

Run:

```sh
python3 tools/generate-keil-project.py \
  --root . \
  --manifest project/firmware-manifest.json \
  --output project/keil/huokong.uvprojx
python3 tools/generate-keil-project.py \
  --root . \
  --manifest project/firmware-manifest.json \
  --output project/keil/huokong.uvprojx \
  --check
python3 -m unittest tests/scripts/test_keil_project.py -v
```

Expected: deterministic XML is generated, `--check` passes, and all generator tests pass.

---

### Task 4: 完成 Keil 兼容门禁和构建文档

**Files:**
- Modify: `.gitignore`
- Modify: `tests/scripts/test_repository_layout.sh`
- Modify: `tests/scripts/test_layout_fail_closed.sh`
- Modify: `README.md`
- Modify: `docs/development/build-test-debug.md`
- Create: `tools/keil-project-check.sh`

**Interfaces:**
- `tools/keil-project-check.sh` runs manifest validation, generator `--check`, scatter memory assertions, and forbidden source/output path checks without requiring MDK.
- The repository layout test invokes this check fail-closed and reports which Keil path or artifact violated the boundary.

- [ ] **Step 1: 写 Keil layout RED 断言**

Extend the repository test to fail when any of these are missing or wrong: `project/firmware-manifest.json`, `project/keil/huokong.uvprojx`, `project/keil/stm32f103c8.sct`, `tools/generate-keil-project.py`, ARM startup path, `build/keil` output references, or manifest/XML source mismatch. Add a temporary fake `python3` regression in `test_layout_fail_closed.sh` that makes the layout check fail with a clear manifest validation error.

- [ ] **Step 2: 更新 ignore 规则**

Add exact rules for `build/scons/`, `build/keil/`, Keil `Objects/`, `Listings/`, `Database/`, `RTE/`, `DebugConfig/`, `.uvoptx`, `.uvguix`, `JLinkLog.txt`, `JLinkSettings.ini`, and other MDK user caches. Do not ignore `project/keil/huokong.uvprojx`, `project/keil/stm32f103c8.sct`, `project/firmware-manifest.json`, or the generator.

- [ ] **Step 3: 更新文档入口**

Document both workflows in `README.md` and `docs/development/build-test-debug.md`:

```text
Linux/SCons: tools/test.sh, tools/build.sh, tools/flash.sh, tools/debug.sh
Keil/MDK5: open project/keil/huokong.uvprojx and build the Debug target
SCons outputs: build/scons/
Keil outputs: build/keil/stm32f103c8/Debug/
```

Explain that Keil MDK5/ARMCLANG is Windows-only in the current validation environment, that `.uvprojx` is committed, and that `.uvoptx/.uvguix` are local ignored metadata.

- [ ] **Step 4: 运行静态 Keil gate**

Run:

```sh
tools/keil-project-check.sh
sh tests/scripts/test_repository_layout.sh
python3 -m unittest tests/scripts/test_firmware_manifest.py tests/scripts/test_keil_project.py -v
```

Expected: all static checks pass without invoking MDK.

---

### Task 5: 完成跨后端验证和最终审查

**Files:**
- Modify only files required by failed checks from Tasks 1-4.
- Test: `tests/scripts/test_firmware_manifest.py`, `tests/scripts/test_keil_project.py`, `tests/scripts/test_repository_layout.sh`, all existing host tests and firmware checks.

**Interfaces:**
- Linux verification proves SCons output is self-contained under `build/scons/`.
- Keil static verification proves the committed uVision project is self-contained and points only to `build/keil/`.
- Windows verification, when available, proves ARMCLANG can build the committed project and produce AXF/HEX/BIN/MAP.

- [ ] **Step 1: 清理当前 SCons output only**

Remove only the current worktree's `build/scons/` directory if it exists. Do not remove `build/keil/`, user files, or unrelated worktree changes. Run `git status --short` before and after to confirm no unrelated file was altered.

- [ ] **Step 2: 运行完整 Linux verification**

Run:

```sh
tools/test.sh
tools/build.sh --cdb
tools/check-size.sh build/scons/firmware/huokong.elf
python3 tests/scripts/test_link_owners.py \
  build/scons/firmware/huokong.elf \
  build/scons/firmware/huokong.map
sh tests/scripts/test_repository_layout.sh
git diff --check
```

Expected: all existing host, size, map, IRQ owner, repository layout, and fail-closed checks pass; ELF/BIN/MAP/CDB are under `build/scons/` only.

- [ ] **Step 3: 运行 Keil static verification**

Run:

```sh
tools/keil-project-check.sh
python3 tools/generate-keil-project.py \
  --root . \
  --manifest project/firmware-manifest.json \
  --output project/keil/huokong.uvprojx \
  --check
```

Expected: manifest, XML, scatter, source boundaries, output paths, and ignore rules pass.

- [ ] **Step 4: 在 Windows/MDK 环境执行原生构建（如环境可用）**

From the repository root in a Developer Command Prompt:

```text
UV4.exe -b project\keil\huokong.uvprojx -j0
```

Verify `build\keil\stm32f103c8\Debug\huokong.axf`, `.hex`, `.bin`, and `.map`; inspect the map for `0x08000000` Flash origin, `0x20000000` SRAM origin, and no duplicate strong definitions for the existing IRQ/HAL callback owners. If MDK or hardware is unavailable, record those checks as not executed rather than claiming success.

- [ ] **Step 5: 检查最终 diff 和工作树边界**

Run:

```sh
git status --short
git diff --stat
git diff --check
git diff -- .gitignore SConstruct SConscript rtconfig.py project tools tests docs README.md
```

Confirm only the requested dual-build files changed, third-party snapshots are untouched, no user changes were reverted, and no Git commit is created unless explicitly requested.

## Execution Record (2026-08-26)

- Manifest validation, SCons output migration, host-test isolation, deterministic
  Keil project generation, ARMASM scatter validation, layout gates, full Linux
  tests, SCons firmware build, size check, and IRQ-owner verification are complete.
- SCons outputs are under `build/scons/`; the committed Keil project targets
  `build/keil/stm32f103c8/Debug/`.
- Native MDK/uVision build in Task 5 Step 4 was not executed because this Linux
  environment has no Windows Keil toolchain. Hardware validation remains pending.
