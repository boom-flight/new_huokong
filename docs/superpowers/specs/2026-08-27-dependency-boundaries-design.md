# Dependency Boundaries Design

## Goal

Strengthen the one-way dependency architecture under `src` without changing firmware behavior: kernel code may use stable platform contracts and pure modules, while concrete board, RT-Thread, HAL, and CMSIS details remain below the platform boundary.

## Design

1. Move status LED ownership behind a platform-owned contract. `app` initializes the platform LED; `kernel/imu` only requests LED states and does not include `board.h`, `drv_gpio.h`, or use `GET_PIN`.
2. Replace kernel includes of `*_stm32.h` service adapters with generic platform contracts. The STM32 implementations keep their existing hardware ownership; app-level composition supplies the selected implementation.
3. Make SCons group dependencies explicit and keep group include paths local. Existing source ownership and manifest source lists remain explicit.
4. Add static checks for forbidden kernel hardware includes, concrete adapter leakage, empty dependency declarations, and compile-command include boundaries.

## Constraints

- `modules` must remain buildable with only `-Isrc/modules` and no RT-Thread, HAL, CMSIS, platform, or vendor headers.
- `platform` must not depend on `kernel`.
- Kernel public headers must not expose RT-Thread, HAL, or CMSIS types.
- Runtime callbacks may notify upward but must not create reverse source dependencies.
- No behavior or protocol changes are intended.

## Verification

Run the focused architecture checks, host tests, `scons --cdb`, and the firmware dry-run/build where the toolchain is available. Inspect `build/scons/compile_commands.json` to confirm group-specific include boundaries.
