#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/rmcs-layout.XXXXXX")
trap 'rm -rf -- "$work"' EXIT HUP INT TERM
mkdir -p "$work/bin"
cat >"$work/bin/find" <<'EOF'
#!/bin/sh
exit 7
EOF
chmod +x "$work/bin/find"

if PATH="$work/bin:$PATH" sh "$root/tests/scripts/test_repository_layout.sh" \
    >"$work/output" 2>&1; then
    echo 'expected repository layout check to reject find failure' >&2
    exit 1
fi
grep -Fq 'cannot scan generated objects' "$work/output"
