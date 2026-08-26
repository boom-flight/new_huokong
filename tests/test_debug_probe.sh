#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
script="$root/tools/openocd.sh"
work=$(mktemp -d "${TMPDIR:-/tmp}/huokong-probe-test.XXXXXX")
trap 'rm -rf -- "$work"' EXIT HUP INT TERM

fail()
{
    printf '%s\n' "$1" >&2
    exit 1
}

expect_detect()
{
    expected=$1
    devices=$2
    actual=$(HUOKONG_USB_DEVICES="$devices" "$script" --detect)
    [[ "$actual" == "$expected" ]] ||
        fail "expected $expected, got $actual"
}

expect_failure()
{
    expected=$1
    devices=$2
    if HUOKONG_USB_DEVICES="$devices" "$script" --detect \
        >"$work/output" 2>&1; then
        fail "expected probe detection to fail"
    fi
    grep -Fq "$expected" "$work/output" ||
        fail "missing error text: $expected"
}

expect_openocd_args()
{
    expected=$1
    devices=$2
    command=${3:-shutdown}
    output=$(PATH="$work/bin:$PATH" HUOKONG_USB_DEVICES="$devices" \
        "$script" -f target/stm32f1x.cfg -c "$command")
    [[ "$output" == "$expected" ]] ||
        fail "unexpected OpenOCD arguments: $output"
}

mkdir -p "$work/bin"
cat >"$work/bin/openocd" <<'EOF'
#!/usr/bin/env bash
for argument in "$@"; do
    printf '<%s>\n' "$argument"
done
EOF
chmod +x "$work/bin/openocd"

cmsis_dap='Bus 001 Device 016: ID 0416:5021 FIRE FireDAP CMSIS-DAP'
daplink='Bus 001 Device 007: ID 0d28:0204 NXP ARM mbed'
st_link='Bus 001 Device 004: ID 0483:374b STMicroelectronics ST-LINK/V2.1'
j_link='Bus 001 Device 005: ID 1366:0105 SEGGER J-Link'

expect_detect cmsis-dap "$cmsis_dap"
expect_detect cmsis-dap "$daplink"
expect_detect stlink "$st_link"
expect_detect jlink "$j_link"

actual=$(HUOKONG_PROBE=j-link HUOKONG_USB_DEVICES='' "$script" --detect)
[[ "$actual" == jlink ]] || fail "manual override was not normalized"

expect_failure 'no supported debug probe detected' \
    'Bus 001 Device 001: ID 1d6b:0002 Linux Foundation root hub'
expect_failure 'no supported debug probe detected' \
    'Bus 001 Device 006: ID 0483:3747 STMicroelectronics ST-LINK unsupported'
expect_failure 'multiple supported debug probes detected' \
    "$cmsis_dap
$st_link"

expect_openocd_args \
    $'<-f>\n<interface/cmsis-dap.cfg>\n<-f>\n<target/stm32f1x.cfg>\n<-c>\n<program firmware.bin 0x08000000 verify reset exit>' \
    "$cmsis_dap" 'program firmware.bin 0x08000000 verify reset exit'
expect_openocd_args \
    $'<-f>\n<interface/stlink.cfg>\n<-f>\n<target/stm32f1x.cfg>\n<-c>\n<shutdown>' \
    "$st_link"
expect_openocd_args \
    $'<-f>\n<interface/jlink.cfg>\n<-c>\n<transport select swd>\n<-f>\n<target/stm32f1x.cfg>\n<-c>\n<shutdown>' \
    "$j_link"
