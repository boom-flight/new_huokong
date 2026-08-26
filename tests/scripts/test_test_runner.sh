#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/rmcs-test-runner.XXXXXX")
trap 'rm -rf -- "$work"' EXIT HUP INT TERM
mkdir -p "$work/bin" "$work/empty"

cat >"$work/bin/scons" <<'EOF'
#!/bin/sh
exit 0
EOF
cat >"$work/bin/sh" <<'EOF'
#!/bin/sh
exit 0
EOF
chmod +x "$work/bin/scons" "$work/bin/sh"

expect_no_c_tests() {
    discovery_dir=$1
    name=$2
    if PATH="$work/bin:$PATH" TEST_DISCOVERY_DIR="$discovery_dir" \
        "$root/tools/test.sh" >"$work/$name-output" 2>&1; then
        echo "expected $name C test discovery to fail" >&2
        exit 1
    fi
    grep -q 'no C test executables found' "$work/$name-output"
}

expect_no_c_tests "$work/empty" empty
: >"$work/empty/test_not_executable"
expect_no_c_tests "$work/empty" non-executable
