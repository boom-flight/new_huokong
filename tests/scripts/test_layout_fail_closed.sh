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

cat >"$work/bin/python3" <<'EOF'
#!/bin/sh
exit 7
EOF
chmod +x "$work/bin/python3"

if PATH="$work/bin:$PATH" sh "$root/tests/scripts/test_repository_layout.sh" \
    >"$work/python-output" 2>&1; then
    echo 'expected repository layout check to reject Python validation failure' >&2
    exit 1
fi
grep -Fq 'Keil project check failed' "$work/python-output"
