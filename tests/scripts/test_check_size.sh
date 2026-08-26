#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
work="$root/build/scons/test-fixtures/check-size-test"
default_linker=src/platform/board/stm32f103c8/linker_scripts/link.lds

grep -Fq "linker_script=\${LINKER_SCRIPT:-$default_linker}" \
    "$root/tools/check-size.sh" || {
    echo "check-size default linker path is not $default_linker" >&2
    exit 1
}
if grep -Fq 'board/linker_scripts/link.lds' "$root/tools/check-size.sh"; then
    echo 'check-size still references the old board linker path' >&2
    exit 1
fi

rm -rf -- "$work"
mkdir -p "$work"
trap 'rm -rf -- "$work"' EXIT HUP INT TERM

write_size_tool() {
    tool=$1
    text=$2
    data=$3
    bss=$4
    cat >"$tool" <<EOF
#!/bin/sh
printf 'text data bss dec hex filename\n'
printf '$text $data $bss 0 0 firmware.elf\n'
EOF
    chmod +x "$tool"
}

write_size_tool "$work/size-pass" 53247 1 16383
write_size_tool "$work/size-flash-fail" 53249 0 0
write_size_tool "$work/size-ram-fail" 1 16384 1
write_size_tool "$work/size-oversized" 9999999999999999999 0 0

cat >"$work/size-malformed" <<'EOF'
#!/bin/sh
printf 'text data bss dec hex filename\n'
printf 'not-a-size-row\n'
EOF
cat >"$work/size-fail" <<'EOF'
#!/bin/sh
exit 7
EOF
cat >"$work/objdump-fail" <<'EOF'
#!/bin/sh
exit 8
EOF
chmod +x "$work/size-malformed" "$work/size-fail" "$work/objdump-fail"

write_output_tool() {
    tool=$1
    fixture=$2
    cat >"$tool" <<EOF
#!/bin/sh
cat "$fixture"
EOF
    chmod +x "$tool"
}

cat >"$work/objdump-pass.txt" <<'EOF'
Sections:
Idx Name          Size      VMA       LMA       File off  Algn
  0 .text         00000010  08000000  08000000  00001000  2**2
                  CONTENTS, ALLOC, LOAD, READONLY, CODE
  1 .data         00000008  20000000  08000010  00002000  2**2
                  CONTENTS, ALLOC, LOAD, DATA
  2 .bss          00000008  20000008  20000008  00002008  2**2
                  ALLOC
EOF
write_output_tool "$work/objdump-pass" "$work/objdump-pass.txt"

cat >"$work/objdump-misplaced.txt" <<'EOF'
Sections:
Idx Name          Size      VMA       LMA       File off  Algn
  0 .text         00000010  08000010  08000010  00001000  2**2
                  CONTENTS, ALLOC, LOAD, READONLY, CODE
  1 .rom-marker   00000001  08000000  08000000  00001010  2**0
                  CONTENTS, ALLOC, LOAD, READONLY
  2 .data         00000008  20000010  08000020  00002000  2**2
                  CONTENTS, ALLOC, LOAD, DATA
  3 .ram-marker   00000001  20000000  08000030  00002008  2**0
                  CONTENTS, ALLOC, LOAD, DATA
EOF
write_output_tool "$work/objdump-misplaced" "$work/objdump-misplaced.txt"

cat >"$work/objdump-missing-text.txt" <<'EOF'
Sections:
Idx Name          Size      VMA       LMA       File off  Algn
  0 .rom-marker   00000010  08000000  08000000  00001000  2**2
                  CONTENTS, ALLOC, LOAD, READONLY
  1 .data         00000008  20000000  08000010  00002000  2**2
                  CONTENTS, ALLOC, LOAD, DATA
EOF
write_output_tool "$work/objdump-missing-text" "$work/objdump-missing-text.txt"

cat >"$work/objdump-missing-data.txt" <<'EOF'
Sections:
Idx Name          Size      VMA       LMA       File off  Algn
  0 .text         00000010  08000000  08000000  00001000  2**2
                  CONTENTS, ALLOC, LOAD, READONLY, CODE
  1 .ram-marker   00000008  20000000  08000010  00002000  2**2
                  CONTENTS, ALLOC, LOAD, DATA
EOF
write_output_tool "$work/objdump-missing-data" "$work/objdump-missing-data.txt"

cat >"$work/objdump-rom-overflow.txt" <<'EOF'
Sections:
Idx Name          Size      VMA       LMA       File off  Algn
  0 .text         00010001  08000000  08000000  00001000  2**2
                  CONTENTS, ALLOC, LOAD, READONLY, CODE
  1 .data         00000008  20000000  08000010  00002000  2**2
                  CONTENTS, ALLOC, LOAD, DATA
EOF
write_output_tool "$work/objdump-rom-overflow" "$work/objdump-rom-overflow.txt"

cat >"$work/objdump-ram-overflow.txt" <<'EOF'
Sections:
Idx Name          Size      VMA       LMA       File off  Algn
  0 .text         00000010  08000000  08000000  00001000  2**2
                  CONTENTS, ALLOC, LOAD, READONLY, CODE
  1 .data         00000008  20000000  08000010  00002000  2**2
                  CONTENTS, ALLOC, LOAD, DATA
  2 .bss          00000020  20004ff0  20004ff0  00002008  2**2
                  ALLOC
EOF
write_output_tool "$work/objdump-ram-overflow" "$work/objdump-ram-overflow.txt"

cat >"$work/objdump-data-lma-outside.txt" <<'EOF'
Sections:
Idx Name          Size      VMA       LMA       File off  Algn
  0 .text         00000010  08000000  08000000  00001000  2**2
                  CONTENTS, ALLOC, LOAD, READONLY, CODE
  1 .data         00000008  20000000  08010000  00002000  2**2
                  CONTENTS, ALLOC, LOAD, DATA
EOF
write_output_tool "$work/objdump-data-lma-outside" "$work/objdump-data-lma-outside.txt"

cat >"$work/link-valid.lds" <<'EOF'
MEMORY
{
    /* ROM (rx)  : ORIGIN = 0x08000000, LENGTH = 64K */
    // RAM (rwx) : ORIGIN = 0x20000000, LENGTH = 20K
    # ROM (rx)  : ORIGIN = 0x08000000, LENGTH = 64K
    ROM (rx)  : ORIGIN = 0x08000000, LENGTH = 64K
    RAM (rwx) : ORIGIN = 0x20000000, LENGTH = 20K
}
EOF
cat >"$work/link-bad.lds" <<'EOF'
MEMORY
{
    ROM (rx)  : ORIGIN = 0x08000000, LENGTH = 65K
    RAM (rwx) : ORIGIN = 0x20000000, LENGTH = 20K
}
EOF
cat >"$work/link-commented.lds" <<'EOF'
/*
MEMORY
{
    ROM (rx)  : ORIGIN = 0x08000000, LENGTH = 64K
    RAM (rwx) : ORIGIN = 0x20000000, LENGTH = 20K
}
*/
MEMORY
{
    ROM (rx)  : ORIGIN = 0x08000000, LENGTH = 65K
    RAM (rwx) : ORIGIN = 0x20000000, LENGTH = 20K
}
EOF
cat >"$work/link-duplicate.lds" <<'EOF'
MEMORY
{
    ROM (rx)  : ORIGIN = 0x08000000, LENGTH = 64K
    ROM (rx)  : ORIGIN = 0x08000000, LENGTH = 64K
    RAM (rwx) : ORIGIN = 0x20000000, LENGTH = 20K
}
EOF
cat >"$work/link-conflicting-duplicate.lds" <<'EOF'
MEMORY
{
    /* ROM (rx)  : ORIGIN = 0x08000000, LENGTH = 64K */
    // ROM (rx)  : ORIGIN = 0x08000000, LENGTH = 64K
    # RAM (rwx) : ORIGIN = 0x20000000, LENGTH = 20K
    ROM (rx)  : ORIGIN = 0x08000000, LENGTH = 64K
    ROM (rx)  : ORIGIN = 0x08000000, LENGTH = 65K
    RAM (rwx) : ORIGIN = 0x20000000, LENGTH = 20K
}
EOF
cat >"$work/link-attribute-less-duplicate.lds" <<'EOF'
MEMORY
{
    ROM (rx)  : ORIGIN = 0x08000000, LENGTH = 64K
    ROM: ORIGIN = 0x08001000, LENGTH = 60K
    RAM (rwx) : ORIGIN = 0x20000000, LENGTH = 20K
    RAM: ORIGIN = 0x20000100, LENGTH = 19K
}
EOF

cat >"$work/map-valid" <<'EOF'
Memory Configuration
Name             Origin             Length             Attributes
ROM              0x08000000         0x00010000         xr
RAM              0x20000000         0x00005000         xrw

Linker script and memory map
EOF
cat >"$work/map-bad" <<'EOF'
Memory Configuration
Name             Origin             Length             Attributes
ROM              0x08000000         0x00010000         xr
RAM              0x20000000         0x00008000         xrw

Linker script and memory map
EOF
cat >"$work/map-incidental-outside" <<'EOF'
ROM              0x08000000         0x00010000         incidental
RAM              0x20000000         0x00005000         incidental

Memory Configuration
Name             Origin             Length             Attributes
ROM              0x08001000         0x00010000         xr
RAM              0x20000100         0x00005000         xrw

Linker script and memory map
ROM              0x08000000         0x00010000         incidental
RAM              0x20000000         0x00005000         incidental
EOF
cat >"$work/map-conflicting-duplicate" <<'EOF'
Memory Configuration
Name             Origin             Length             Attributes
ROM              0x08000000         0x00010000         xr
ROM              0x08001000         0x0000f000         xr
RAM              0x20000000         0x00005000         xrw

Linker script and memory map
EOF
: >"$work/firmware.elf"

run_check() {
    SIZE_TOOL=$1 OBJDUMP_TOOL=$2 LINKER_SCRIPT=$3 MAP_FILE=$4 \
        "$root/tools/check-size.sh" "$work/firmware.elf"
}

expect_failure() {
    name=$1
    diagnostic=$2
    shift 2
    if run_check "$@" >"$work/$name.out" 2>&1; then
        echo "expected $name failure" >&2
        exit 1
    fi
    grep -q "$diagnostic" "$work/$name.out"
}

run_check "$work/size-pass" "$work/objdump-pass" \
    "$work/link-valid.lds" "$work/map-valid"

expect_failure flash-limit 'Flash design limit exceeded: 53249 > 53248' \
    "$work/size-flash-fail" "$work/objdump-pass" \
    "$work/link-valid.lds" "$work/map-valid"
expect_failure ram-limit 'SRAM design limit exceeded: 16385 > 16384' \
    "$work/size-ram-fail" "$work/objdump-pass" \
    "$work/link-valid.lds" "$work/map-valid"

expect_failure misplaced-sections 'ELF section layout invalid' \
    "$work/size-pass" "$work/objdump-misplaced" \
    "$work/link-valid.lds" "$work/map-valid"
expect_failure missing-text 'ELF section layout invalid' \
    "$work/size-pass" "$work/objdump-missing-text" \
    "$work/link-valid.lds" "$work/map-valid"
expect_failure missing-data 'ELF section layout invalid' \
    "$work/size-pass" "$work/objdump-missing-data" \
    "$work/link-valid.lds" "$work/map-valid"
expect_failure rom-overflow 'ELF section layout invalid' \
    "$work/size-pass" "$work/objdump-rom-overflow" \
    "$work/link-valid.lds" "$work/map-valid"
expect_failure ram-overflow 'ELF section layout invalid' \
    "$work/size-pass" "$work/objdump-ram-overflow" \
    "$work/link-valid.lds" "$work/map-valid"
expect_failure data-lma-outside 'ELF section layout invalid' \
    "$work/size-pass" "$work/objdump-data-lma-outside" \
    "$work/link-valid.lds" "$work/map-valid"

expect_failure bad-linker 'linker script memory declaration invalid' \
    "$work/size-pass" "$work/objdump-pass" \
    "$work/link-bad.lds" "$work/map-valid"
expect_failure commented-linker 'linker script memory declaration invalid' \
    "$work/size-pass" "$work/objdump-pass" \
    "$work/link-commented.lds" "$work/map-valid"
expect_failure duplicate-linker 'linker script memory declaration invalid' \
    "$work/size-pass" "$work/objdump-pass" \
    "$work/link-duplicate.lds" "$work/map-valid"
expect_failure conflicting-duplicate-linker \
    'linker script memory declaration invalid' \
    "$work/size-pass" "$work/objdump-pass" \
    "$work/link-conflicting-duplicate.lds" "$work/map-valid"
expect_failure attribute-less-duplicate-linker \
    'linker script memory declaration invalid' \
    "$work/size-pass" "$work/objdump-pass" \
    "$work/link-attribute-less-duplicate.lds" "$work/map-valid"
expect_failure bad-map 'map memory declaration invalid' \
    "$work/size-pass" "$work/objdump-pass" \
    "$work/link-valid.lds" "$work/map-bad"
expect_failure incidental-map-text 'map memory declaration invalid' \
    "$work/size-pass" "$work/objdump-pass" \
    "$work/link-valid.lds" "$work/map-incidental-outside"
expect_failure conflicting-map-row 'map memory declaration invalid' \
    "$work/size-pass" "$work/objdump-pass" \
    "$work/link-valid.lds" "$work/map-conflicting-duplicate"

expect_failure malformed-size 'malformed size output' \
    "$work/size-malformed" "$work/objdump-pass" \
    "$work/link-valid.lds" "$work/map-valid"
expect_failure oversized-size 'size field out of range' \
    "$work/size-oversized" "$work/objdump-pass" \
    "$work/link-valid.lds" "$work/map-valid"
expect_failure size-tool-failure 'size tool failed' \
    "$work/size-fail" "$work/objdump-pass" \
    "$work/link-valid.lds" "$work/map-valid"
expect_failure objdump-tool-failure 'objdump tool failed' \
    "$work/size-pass" "$work/objdump-fail" \
    "$work/link-valid.lds" "$work/map-valid"
