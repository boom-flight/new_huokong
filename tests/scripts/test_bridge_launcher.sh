#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/bridge-launcher.XXXXXX")
trap 'rm -rf -- "$work"' EXIT HUP INT TERM

mkdir -p "$work/bin"
cat >"$work/bin/python3" <<'EOF'
#!/usr/bin/env bash
printf '%s\n' "$@" >"$BRIDGE_LAUNCHER_ARGS"
EOF
chmod +x "$work/bin/python3"

BRIDGE_LAUNCHER_ARGS="$work/args" PATH="$work/bin:$PATH" \
    FOXGLOVE_DEVICE=/dev/serial/by-id/usb-debug \
    "$root/tools/bridge.sh" --port 9000

grep -Fxq -- '--device' "$work/args"
grep -Fxq -- '/dev/serial/by-id/usb-debug' "$work/args"
grep -Fxq -- '--port' "$work/args"
grep -Fxq -- '9000' "$work/args"

printf 'Bridge launcher checks passed\n'
