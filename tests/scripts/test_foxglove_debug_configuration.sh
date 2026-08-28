#!/usr/bin/env bash
set -euo pipefail

work=$(mktemp -d "${TMPDIR:-/tmp}/foxglove-debug-config.XXXXXX")
trap 'rm -rf -- "$work"' EXIT HUP INT TERM

fail() {
    printf 'Foxglove debug configuration check failed: %s\n' "$1" >&2
    exit 1
}

if rg -n '^CONFIG_HUOKONG_FOXGLOVE_DEBUG=y$' .config; then
    fail 'default .config enables Foxglove debug'
fi
if rg -n '^#define HUOKONG_FOXGLOVE_DEBUG\b' rtconfig.h; then
    fail 'default rtconfig.h enables Foxglove debug'
fi

if ! awk '
    /#if defined\(HUOKONG_FOXGLOVE_DEBUG\)/ { debug_block = 1 }
    /foxglove_debug_service_init\(/ && !debug_block { found = 1 }
    /#endif/ { debug_block = 0 }
    END { exit found }
' src/app/main.c; then
    fail 'default application source calls debug service init'
fi

if ! rg -Fq "if GetDepend('HUOKONG_FOXGLOVE_DEBUG'):" src/debug/SConscript; then
    fail 'debug SConscript does not gate source inclusion on the profile'
fi
if rg -n "foxglove_debug_frame\.c" build/scons/compile_commands.json 2>/dev/null; then
    fail 'disabled compilation database contains the debug frame source'
fi
if compgen -G 'build/scons/firmware/objects/debug/*' >/dev/null 2>&1; then
    fail 'disabled firmware build contains debug objects'
fi

if rg -n '\bUSART1_IRQHandler\b|HAL_UART_(TxCplt|Error)Callback|HAL_UART_Msp(Init|DeInit)' \
    src/platform/transport/foxglove_debug_uart_stm32.c; then
    fail 'debug-specific USART1 IRQ or HAL callback owner exists'
fi

compile_flags=(
    -mcpu=cortex-m3 -mthumb -ffunction-sections -fdata-sections
    -Dgcc -std=gnu11 -Os -gdwarf-2 -g
    -I. -Isrc/debug -Isrc/kernel -Isrc/modules -Isrc/platform
    -Ivendor/rt-thread/include -Ivendor/rt-thread/components/drivers/include
)
guarded_sources=(
    'src/debug/foxglove_debug.c'
    'src/debug/foxglove_debug_console.c'
    'src/debug/foxglove_debug_frame.c'
    'src/platform/transport/foxglove_debug_uart_stm32.c'
)
for source in "${guarded_sources[@]}"; do
    object_name=${source//\//_}
    arm-none-eabi-gcc "${compile_flags[@]}" -c "$source" -o "$work/disabled-$object_name.o"
    arm-none-eabi-gcc "${compile_flags[@]}" -DHUOKONG_FOXGLOVE_DEBUG \
        -c "$source" -o "$work/enabled-$object_name.o"
done

if arm-none-eabi-nm -g "$work/disabled-src_debug_foxglove_debug.c.o" \
    | rg -q 'foxglove_debug_(service_init|service_deinit|drop_count)'; then
    fail 'disabled debug service object exports runtime symbols'
fi
if arm-none-eabi-nm -g "$work/disabled-src_debug_foxglove_debug_console.c.o" \
    | rg -q '(^| )rt_kprintf$'; then
    fail 'disabled debug console object exports rt_kprintf'
fi
if arm-none-eabi-nm -g "$work/disabled-src_platform_transport_foxglove_debug_uart_stm32.c.o" \
    | rg -q 'foxglove_debug_uart_(init|deinit|send)'; then
    fail 'disabled debug UART object exports runtime symbols'
fi
if arm-none-eabi-nm -g "$work/disabled-src_debug_foxglove_debug_frame.c.o" \
    | rg -q 'foxglove_debug_(crc16_ccitt_false|frame_encode)'; then
    fail 'disabled debug frame object exports runtime symbols'
fi

arm-none-eabi-nm -g "$work/enabled-src_debug_foxglove_debug.c.o" \
    | rg -q ' T foxglove_debug_service_init$' \
    || fail 'enabled debug service object does not export service init'
arm-none-eabi-nm -g "$work/enabled-src_debug_foxglove_debug_console.c.o" \
    | rg -q ' T rt_kprintf$' \
    || fail 'enabled debug console object does not export rt_kprintf'
arm-none-eabi-nm -g "$work/enabled-src_platform_transport_foxglove_debug_uart_stm32.c.o" \
    | rg -q ' T foxglove_debug_uart_init$' \
    || fail 'enabled debug UART object does not export UART init'
arm-none-eabi-nm -g "$work/enabled-src_debug_foxglove_debug_frame.c.o" \
    | rg -q ' T foxglove_debug_frame_encode$' \
    || fail 'enabled debug frame object does not export frame encoder'

printf 'Foxglove debug configuration checks passed\n'
