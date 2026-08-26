#!/usr/bin/env bash
set -euo pipefail

normalize_probe()
{
    case "${1,,}" in
    cmsis-dap|cmsis_dap|dap|daplink|firedap)
        printf 'cmsis-dap\n'
        ;;
    st-link|stlink)
        printf 'stlink\n'
        ;;
    j-link|jlink)
        printf 'jlink\n'
        ;;
    *)
        printf 'unsupported HUOKONG_PROBE value: %s\n' "$1" >&2
        return 2
        ;;
    esac
}

detect_probe()
{
    local requested=${HUOKONG_PROBE:-auto}
    local devices line lower
    local -a probes=()

    if [[ "${requested,,}" != auto ]]; then
        normalize_probe "$requested"
        return
    fi

    if [[ -v HUOKONG_USB_DEVICES ]]; then
        devices=$HUOKONG_USB_DEVICES
    else
        if ! command -v lsusb >/dev/null; then
            printf 'lsusb is required for automatic debug probe detection\n' >&2
            return 2
        fi
        devices=$(lsusb)
    fi

    while IFS= read -r line; do
        lower=${line,,}
        if [[ "$lower" == *cmsis-dap* || "$lower" == *cmsis_dap* ||
              "$lower" == *daplink* || "$lower" == *firedap* ||
              "$lower" =~ id[[:space:]]+0d28:0204([[:space:]]|$) ]]; then
            probes+=(cmsis-dap)
        elif [[ "$lower" =~ id[[:space:]]+0483:(3744|3748|374b|374d|374e|374f|3752|3753|3754|3755|3757)([[:space:]]|$) ]]; then
            probes+=(stlink)
        elif [[ "$lower" == *j-link* || "$lower" == *jlink* ||
                "$lower" == *"id 1366:"* ]]; then
            probes+=(jlink)
        fi
    done <<<"$devices"

    if (( ${#probes[@]} == 0 )); then
        printf '%s\n' \
            'no supported debug probe detected; supported: CMSIS-DAP, ST-Link, J-Link' \
            'set HUOKONG_PROBE to select one manually' >&2
        return 1
    fi
    if (( ${#probes[@]} > 1 )); then
        printf 'multiple supported debug probes detected: %s\n' "${probes[*]}" >&2
        printf 'set HUOKONG_PROBE to select one manually\n' >&2
        return 1
    fi

    printf '%s\n' "${probes[0]}"
}

probe=$(detect_probe)
if [[ ${1:-} == --detect ]]; then
    if (( $# != 1 )); then
        printf 'usage: %s --detect\n' "$0" >&2
        exit 2
    fi
    printf '%s\n' "$probe"
    exit 0
fi

if ! command -v openocd >/dev/null; then
    printf 'openocd is not installed or not available in PATH\n' >&2
    exit 127
fi

printf 'Using debug probe: %s\n' "$probe" >&2
case "$probe" in
cmsis-dap)
    exec openocd -f interface/cmsis-dap.cfg "$@"
    ;;
stlink)
    exec openocd -f interface/stlink.cfg "$@"
    ;;
jlink)
    exec openocd -f interface/jlink.cfg -c 'transport select swd' "$@"
    ;;
esac
