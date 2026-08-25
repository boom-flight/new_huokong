#!/usr/bin/env python3

import pathlib
import re
import subprocess
import sys


EXPECTED_OWNERS = {
    "EXTI15_10_IRQHandler": "bmi088_stm32",
    "TIM2_IRQHandler": "monotonic_clock_stm32",
    "HAL_TIM_PeriodElapsedCallback": "monotonic_clock_stm32",
    "DMA1_Channel7_IRQHandler": "telemetry_uart_stm32",
    "USART2_IRQHandler": "telemetry_uart_stm32",
    "HAL_UART_TxCpltCallback": "telemetry_uart_stm32",
    "HAL_UART_ErrorCallback": "telemetry_uart_stm32",
}


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
        if symbol in definitions and symbol_type.isupper() and symbol_type not in {
            "U",
            "V",
            "W",
        }:
            definitions[symbol].append((owner, symbol_type))
    return definitions


def map_owners(map_path):
    owners = {symbol: [] for symbol in EXPECTED_OWNERS}
    object_pattern = re.compile(r"([^\s()]+\.o)(?:\)|\s|$)")
    symbol_pattern = re.compile(
        r"^\s*0x[0-9A-Fa-f]+\s+(" + "|".join(
            re.escape(symbol) for symbol in EXPECTED_OWNERS
        ) + r")\s*$"
    )
    current_object = None

    for line in map_path.read_text(encoding="utf-8", errors="replace").splitlines():
        object_matches = object_pattern.findall(line)
        if object_matches:
            current_object = object_matches[-1]
        symbol_match = symbol_pattern.match(line)
        if symbol_match is not None:
            owners[symbol_match.group(1)].append(current_object)
    return owners


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
    owners = map_owners(map_path)
    failures = []

    for symbol, expected_owner in EXPECTED_OWNERS.items():
        symbol_definitions = definitions[symbol]
        if len(symbol_definitions) != 1:
            failures.append(
                f"{symbol}: expected exactly one strong definition, "
                f"found {len(symbol_definitions)}"
            )
            continue

        symbol_owners = owners[symbol]
        if len(symbol_owners) != 1:
            failures.append(
                f"{symbol}: expected exactly one Map owner, found "
                f"{len(symbol_owners)} ({symbol_owners})"
            )
            continue
        actual_owner = symbol_owners[0]
        if actual_owner is None or pathlib.Path(actual_owner).stem != expected_owner:
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
