# Dependency Boundaries Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Enforce one-way source and build dependencies while keeping board-specific implementations below kernel contracts.

**Architecture:** Kernel services consume platform-owned contracts and modules. The STM32 board implements those contracts and owns all hardware headers, handles, IRQs, and GPIO details. SCons compiles each group from a private cloned environment and declares direct group dependencies explicitly.

**Tech Stack:** C11, SCons, RT-Thread, STM32 HAL/CMSIS, POSIX shell and Python static checks.

**Spec:** `docs/superpowers/specs/2026-08-27-dependency-boundaries-design.md`

## Global Constraints

- `modules` must remain buildable with only `-Isrc/modules` and no RT-Thread, HAL, CMSIS, platform, or vendor headers.
- `platform` must not depend on `kernel`.
- Kernel public headers must not expose RT-Thread, HAL, or CMSIS types.
- Runtime callbacks may notify upward but must not create reverse source dependencies.
- No behavior or protocol changes are intended.
- Do not commit changes; the controller will review and integrate the shared worktree changes.

---

### Task 1: IMU Hardware Boundary

**Files:**
- Create: `src/platform/indicators/status_led.h`
- Modify: `src/platform/board/stm32f103c8/board.c`
- Modify: `src/platform/board/stm32f103c8/SConscript`
- Modify: `project/firmware-manifest.json`
- Modify: `src/app/main.c`
- Modify: `src/kernel/imu/imu_service.c`

**Interfaces:**
- Produces `status_led_init(void)` and `status_led_set(bool)` in `status_led.h`.
- The board implementation owns `GET_PIN(B, 6)` and RT-Thread GPIO calls.
- `main.c` calls `status_led_init()`, and IMU runtime calls `status_led_set(bool)`.

- [ ] Add the platform LED contract and board implementation with the existing LED behavior.
- [ ] Remove board/GPIO includes and pin macros from `imu_service.c` and use the contract.
- [ ] Update explicit board source/manifest ownership if a new source file is needed.
- [ ] Add or update a static boundary test for forbidden kernel board/GPIO includes.
- [ ] Run the focused host/static checks and firmware dry-run.

### Task 2: Generic Kernel Platform Contracts

**Files:**
- Create or modify: `src/platform/devices/bmi088.h`
- Create or modify: `src/platform/time/monotonic_clock.h`
- Create or modify: `src/platform/transport/telemetry_uart.h`
- Modify: `src/platform/devices/bmi088_stm32.c` and its header as needed for delegation
- Modify: `src/platform/time/monotonic_clock_stm32.c` and its header as needed for delegation
- Modify: `src/platform/transport/telemetry_uart_stm32.c` and its header as needed for delegation
- Modify: `src/kernel/imu/imu_service.c`
- Modify: `src/kernel/telemetry/telemetry_service.c`
- Modify: `src/app/main.c` only if composition is required by the chosen contract

**Interfaces:**
- Kernel consumers must include generic platform contracts, not headers whose public names end in `_stm32.h`.
- Existing runtime semantics and function signatures may be preserved behind generic declarations where possible.
- Concrete STM32 implementations remain the only code that includes `main.h`, HAL, or CMSIS.

- [ ] Add failing source-boundary checks for kernel concrete-adapter includes.
- [ ] Define generic contracts and connect the existing STM32 implementations without changing runtime behavior.
- [ ] Switch kernel includes/calls to the generic contracts.
- [ ] Run focused tests, compile database generation, and firmware dry-run.

### Task 3: SCons Dependencies and Local Environments

**Files:**
- Modify: `src/app/SConscript`
- Modify: `src/kernel/imu/SConscript`
- Modify: `src/kernel/telemetry/SConscript`
- Modify: `src/kernel/logging/SConscript`
- Modify: all `src/modules/**/SConscript` files
- Modify: `src/platform/devices/SConscript`
- Modify: `src/platform/time/SConscript`
- Modify: `src/platform/transport/SConscript`
- Modify: `SConstruct` or project build helpers only if required to keep environments local

**Interfaces:**
- Every group declares only its direct dependencies.
- Every group compiles with its own cloned environment and does not leak ordinary `CPPPATH` or `CPPDEFINES` to later groups.
- Task 1 owns any board `SConscript` edits; this task must not overwrite those edits.

- [ ] Add a failing check that rejects empty `depend=['']` in production SConscripts.
- [ ] Convert group construction to local environments while preserving explicit source lists and object targets.
- [ ] Declare direct dependencies for app, kernel, modules, platform, and board groups.
- [ ] Verify compile commands for app/kernel/platform/modules have only intended include paths and defines.
- [ ] Run SCons dry-run and compilation database checks.

### Task 4: Architecture Regression Gates

**Files:**
- Create or modify: `tests/scripts/test_dependency_boundaries.py`
- Modify: `tests/scripts/test_build_configuration.sh` only if needed to invoke the new check

**Interfaces:**
- The check must fail closed when kernel includes board/GPIO or concrete `_stm32` adapter headers.
- The check must reject empty production `depend` declarations.
- The check must inspect the generated compilation database for module isolation and kernel/app/platform boundary leakage.

- [ ] Write failing static checks against the current tree and verify they fail for the known violations.
- [ ] Implement the minimal production changes needed for the checks to pass after Tasks 1-3.
- [ ] Run the new check independently and through the standard build configuration check.

## Integration Verification

- [ ] Run `python3 tests/scripts/test_manifest_boundaries.py`.
- [ ] Run `python3 tests/scripts/test_dependency_boundaries.py`.
- [ ] Run host tests through `tools/test.sh` or the available focused test runner.
- [ ] Run `scons --cdb` and inspect `build/scons/compile_commands.json`.
- [ ] Run `scons -n -Q` and report any unavailable hardware/toolchain checks without masking failures.
