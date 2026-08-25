#!/bin/sh
set -eu

elf=${1:-rt-thread.elf}
size_tool=${SIZE_TOOL:-arm-none-eabi-size}
objdump_tool=${OBJDUMP_TOOL:-arm-none-eabi-objdump}
linker_script=${LINKER_SCRIPT:-board/linker_scripts/link.lds}
map_file=${MAP_FILE:-rt-thread.map}

if ! test -f "$elf"; then
    echo "ELF file not found: $elf" >&2
    exit 1
fi
if ! test -f "$linker_script"; then
    echo "linker script not found: $linker_script" >&2
    exit 1
fi
if ! test -f "$map_file"; then
    echo "map file not found: $map_file" >&2
    exit 1
fi
if ! size_output=$("$size_tool" "$elf"); then
    echo "size tool failed: $size_tool" >&2
    exit 1
fi
set -- $(printf '%s\n' "$size_output" | tail -n 1)
if test "$#" -lt 3; then
    echo "malformed size output" >&2
    exit 1
fi
text=$1
data=$2
bss=$3
case "$text:$data:$bss" in
    *[!0-9:]*) echo "malformed size output" >&2; exit 1 ;;
esac
if test "${#text}" -gt 8 || test "${#data}" -gt 8 ||
    test "${#bss}" -gt 8; then
    echo "size field out of range" >&2
    exit 1
fi

flash=$((text + data))
static_ram=$((data + bss))
printf 'Flash: %s bytes; static SRAM: %s bytes\n' "$flash" "$static_ram"

if test "$flash" -gt 53248; then
    echo "Flash design limit exceeded: $flash > 53248" >&2
    exit 1
fi
if test "$static_ram" -gt 16384; then
    echo "SRAM design limit exceeded: $static_ram > 16384" >&2
    exit 1
fi

if ! sections=$("$objdump_tool" -h "$elf"); then
    echo "objdump tool failed: $objdump_tool" >&2
    exit 1
fi
if ! section_error=$(printf '%s\n' "$sections" | awk '
function hex_to_dec(value, result, index_value, digit) {
    value = toupper(value)
    if (length(value) != 8 || value !~ /^[0-9A-F]+$/) {
        return -1
    }
    result = 0
    for (index_value = 1; index_value <= length(value); ++index_value) {
        digit = index("0123456789ABCDEF", substr(value, index_value, 1)) - 1
        result = result * 16 + digit
    }
    return result
}
function fail(message) {
    if (!failed) {
        print message
    }
    failed = 1
}
BEGIN {
    rom_start = 134217728
    rom_end = 134283264
    ram_start = 536870912
    ram_end = 536891392
}
/^[[:space:]]*[0-9]+[[:space:]]/ {
    name = $2
    size = hex_to_dec($3)
    vma = hex_to_dec($4)
    lma = hex_to_dec($5)
    if (size < 0 || vma < 0 || lma < 0) {
        fail("malformed section row for " name)
        next
    }
    if ((getline flags) <= 0) {
        fail("missing flags for " name)
        next
    }
    allocated = index(flags, "ALLOC") != 0
    loadable = index(flags, "LOAD") != 0

    if (name == ".text") {
        ++text_count
        if (vma != rom_start || lma != rom_start || !allocated || !loadable) {
            fail(".text must be allocated and loaded at 08000000")
        }
    }
    if (name == ".data") {
        ++data_count
        if (vma != ram_start || lma < rom_start || lma >= rom_end ||
            !allocated || !loadable) {
            fail(".data must be allocated at 20000000 with a ROM LMA")
        }
    }

    if (allocated) {
        if (vma >= rom_start && vma < rom_end) {
            if (vma + size > rom_end) {
                fail(name " exceeds ROM VMA range")
            }
        } else if (vma >= ram_start && vma < ram_end) {
            if (vma + size > ram_end) {
                fail(name " exceeds RAM VMA range")
            }
        } else {
            fail(name " allocated VMA is outside ROM/RAM")
        }
    }
    if (loadable) {
        if (lma >= rom_start && lma < rom_end) {
            if (lma + size > rom_end) {
                fail(name " exceeds ROM LMA range")
            }
        } else if (lma >= ram_start && lma < ram_end) {
            if (lma + size > ram_end) {
                fail(name " exceeds RAM LMA range")
            }
        } else {
            fail(name " loadable LMA is outside ROM/RAM")
        }
    }
}
END {
    if (text_count != 1) {
        fail("expected exactly one .text section")
    }
    if (data_count != 1) {
        fail("expected exactly one .data section")
    }
    if (failed) {
        exit 1
    }
}') ; then
    echo "ELF section layout invalid: $section_error" >&2
    exit 1
fi

if ! linker_error=$(awk '
function uncomment(line, output, start, finish) {
    output = ""
    while (length(line) != 0) {
        if (in_comment) {
            finish = index(line, "*/")
            if (finish == 0) {
                return output
            }
            line = substr(line, finish + 2)
            in_comment = 0
        } else {
            start = index(line, "/*")
            if (start == 0) {
                return output line
            }
            output = output substr(line, 1, start - 1)
            line = substr(line, start + 2)
            in_comment = 1
        }
    }
    return output
}
{
    line = uncomment($0)
    slash = index(line, "//")
    hash = index(line, "#")
    if (slash != 0 && (hash == 0 || slash < hash)) {
        line = substr(line, 1, slash - 1)
    } else if (hash != 0) {
        line = substr(line, 1, hash - 1)
    }
    sub(/^[[:space:]]+/, "", line)
    sub(/[[:space:]]+$/, "", line)
    if (!in_memory && line ~ /^MEMORY[[:space:]]*\{$/) {
        ++memory_count
        in_memory = 1
        next
    }
    if (!in_memory && line == "MEMORY") {
        awaiting_brace = 1
        next
    }
    if (awaiting_brace && line == "{") {
        ++memory_count
        in_memory = 1
        awaiting_brace = 0
        next
    }
    if (awaiting_brace && line != "") {
        awaiting_brace = 0
    }
    if (in_memory && line == "}") {
        in_memory = 0
        next
    }
    if (in_memory && line ~ /^ROM([[:space:]]|\(|:)/) {
        ++rom_count
        if (line ~ /^ROM[[:space:]]*\(rx\)[[:space:]]*:[[:space:]]*ORIGIN[[:space:]]*=[[:space:]]*0x08000000,[[:space:]]*LENGTH[[:space:]]*=[[:space:]]*64K[[:space:]]*;?$/) {
            ++exact_rom_count
        }
    }
    if (in_memory && line ~ /^RAM([[:space:]]|\(|:)/) {
        ++ram_count
        if (line ~ /^RAM[[:space:]]*\(rwx\)[[:space:]]*:[[:space:]]*ORIGIN[[:space:]]*=[[:space:]]*0x20000000,[[:space:]]*LENGTH[[:space:]]*=[[:space:]]*20K[[:space:]]*;?$/) {
            ++exact_ram_count
        }
    }
}
END {
    if (memory_count != 1 || rom_count != 1 || ram_count != 1 ||
        exact_rom_count != 1 || exact_ram_count != 1) {
        print "expected exactly one active ROM and RAM declaration in MEMORY"
        exit 1
    }
}' "$linker_script"); then
    echo "linker script memory declaration invalid: $linker_error" >&2
    exit 1
fi

if ! map_error=$(awk '
function trim(line) {
    sub(/^[[:space:]]+/, "", line)
    sub(/[[:space:]]+$/, "", line)
    return line
}
{
    line = trim($0)
    if (line == "Memory Configuration") {
        ++table_count
        in_table = 1
        next
    }
    if (line == "Linker script and memory map") {
        if (in_table) {
            table_closed = 1
        }
        in_table = 0
        next
    }
    if (!in_table) {
        next
    }
    if ($1 == "ROM") {
        ++rom_count
        if ($2 == "0x08000000" && $3 == "0x00010000") {
            ++exact_rom_count
        }
    }
    if ($1 == "RAM") {
        ++ram_count
        if ($2 == "0x20000000" && $3 == "0x00005000") {
            ++exact_ram_count
        }
    }
}
END {
    if (table_count != 1 || !table_closed || rom_count != 1 ||
        ram_count != 1 || exact_rom_count != 1 || exact_ram_count != 1) {
        print "expected exact ROM and RAM origin/length rows"
        exit 1
    }
}' "$map_file"); then
    echo "map memory declaration invalid: $map_error" >&2
    exit 1
fi
