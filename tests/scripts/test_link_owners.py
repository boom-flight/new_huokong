#!/usr/bin/env python3

import pathlib
import re
import subprocess
import sys


EXPECTED_OWNERS = {
    "EXTI15_10_IRQHandler": (
        "build/scons/firmware/objects/platform/devices/bmi088_stm32.o"
    ),
    "TIM2_IRQHandler": (
        "build/scons/firmware/objects/platform/time/monotonic_clock_stm32.o"
    ),
    "HAL_TIM_PeriodElapsedCallback": (
        "build/scons/firmware/objects/platform/time/monotonic_clock_stm32.o"
    ),
    "DMA1_Channel7_IRQHandler": (
        "build/scons/firmware/objects/platform/transport/telemetry_uart_stm32.o"
    ),
    "USART2_IRQHandler": (
        "build/scons/firmware/objects/platform/transport/telemetry_uart_stm32.o"
    ),
    "HAL_UART_TxCpltCallback": (
        "build/scons/firmware/objects/platform/transport/telemetry_uart_stm32.o"
    ),
    "HAL_UART_ErrorCallback": (
        "build/scons/firmware/objects/platform/transport/telemetry_uart_stm32.o"
    ),
}
REPOSITORY_ROOT = pathlib.Path(__file__).resolve().parents[2]


def strong_definitions(elf_path):
    result = subprocess.run(
        ["arm-none-eabi-nm", "-A", "--defined-only", str(elf_path)],
        check=True,
        capture_output=True,
        text=True,
    )
    definitions = {symbol: [] for symbol in EXPECTED_OWNERS}
    pattern = re.compile(r"^(.*):[0-9A-Fa-f]+\s+(\S)\s+(\S+)$")

    for line in result.stdout.splitlines():
        match = pattern.match(line)
        if match is None:
            continue
        owner, symbol_type, symbol = match.groups()
        if symbol in definitions:
            definitions[symbol].append((owner, symbol_type))
    return definitions


def normalize_map_owner(owner):
    path = pathlib.Path(owner)
    if path.is_absolute():
        try:
            path = path.relative_to(REPOSITORY_ROOT)
        except ValueError:
            return path.as_posix()
    return path.as_posix()


def map_blocks(map_path):
    blocks = {symbol: [] for symbol in EXPECTED_OWNERS}
    target_sections = re.compile(
        r"^\s+\.text\.(" + "|".join(
            re.escape(symbol) for symbol in EXPECTED_OWNERS
        ) + r")\s*$"
    )
    input_section = re.compile(
        r"^\s+0x[0-9A-Fa-f]+\s+0x[0-9A-Fa-f]+\s+(\S+\.o)\s*$"
    )
    any_section = re.compile(r"^\s+\.\S+")
    lines = map_path.read_text(
        encoding="utf-8", errors="replace"
    ).splitlines()
    linked_map_start = next(
        index for index, line in enumerate(lines)
        if line.strip() == "Linker script and memory map"
    )

    for index in range(linked_map_start + 1, len(lines)):
        line = lines[index]
        section_match = target_sections.match(line)
        if section_match is None:
            continue

        symbol = section_match.group(1)
        symbol_pattern = re.compile(
            rf"^\s+0x[0-9A-Fa-f]+\s+{re.escape(symbol)}\s*$"
        )
        object_records = []
        symbol_definitions = 0
        for block_line in lines[index + 1:]:
            if any_section.match(block_line):
                break
            object_match = input_section.match(block_line)
            if object_match is not None:
                object_records.append(normalize_map_owner(object_match.group(1)))
            if symbol_pattern.match(block_line):
                symbol_definitions += 1

        blocks[symbol].append((object_records, symbol_definitions))
    return blocks


def main():
    if len(sys.argv) != 3:
        print(f"usage: {sys.argv[0]} ELF MAP", file=sys.stderr)
        return 2

    elf_path = pathlib.Path(sys.argv[1])
    map_path = pathlib.Path(sys.argv[2])
    if not elf_path.is_file() or not map_path.is_file():
        print("link owner check requires existing ELF and Map files", file=sys.stderr)
        return 2

    definitions = strong_definitions(elf_path)
    blocks = map_blocks(map_path)
    failures = []

    for symbol, expected_owner in EXPECTED_OWNERS.items():
        symbol_definitions = definitions[symbol]
        strong_symbol_definitions = [
            definition
            for definition in symbol_definitions
            if definition[1].isupper()
            and definition[1] not in {"U", "V", "W"}
        ]
        if len(symbol_definitions) != 1 or len(strong_symbol_definitions) != 1:
            failures.append(
                f"{symbol}: expected exactly one strong definition, "
                f"found {len(strong_symbol_definitions)} strong and "
                f"{len(symbol_definitions) - len(strong_symbol_definitions)} "
                "weak/other"
            )
            continue

        valid_owners = []
        for block_index, (object_records, map_definitions) in enumerate(
            blocks[symbol], start=1
        ):
            if len(object_records) != 1 or map_definitions != 1:
                failures.append(
                    f"{symbol}: malformed Map block {block_index}: expected "
                    "exactly one object record and one symbol definition, "
                    f"found object_records={len(object_records)}, "
                    f"symbol_definitions={map_definitions}"
                )
                continue
            valid_owners.append(object_records[0])

        if len(valid_owners) != 1:
            failures.append(
                f"{symbol}: expected exactly one valid Map block, found "
                f"{len(valid_owners)}"
            )
            continue
        actual_owner = valid_owners[0]
        if actual_owner != expected_owner:
            failures.append(
                f"{symbol}: expected owner {expected_owner}, found "
                f"{actual_owner or 'unknown'}"
            )

    if failures:
        for failure in failures:
            print(f"link owner check failed: {failure}", file=sys.stderr)
        return 1

    for symbol, expected_owner in EXPECTED_OWNERS.items():
        print(f"link owner check passed: {symbol} -> {expected_owner}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
