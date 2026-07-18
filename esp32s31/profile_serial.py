#!/usr/bin/env python3
"""Capture ESP32-S31 gpSP PC samples and aggregate them by ELF symbol."""

from __future__ import annotations

import argparse
from collections import Counter
from pathlib import Path
import re
import shutil
import subprocess
import sys

import serial


PC_LINE = re.compile(r"command=gpsp_profile_pc index=\d+ pcs=([0-9a-fA-F,]+)")


def symbolize(elf: Path, addresses: list[int], addr2line: str) -> dict[int, tuple[str, str]]:
    unique = list(dict.fromkeys(addresses))
    payload = "".join(f"0x{address:08x}\n" for address in unique)
    result = subprocess.run(
        [addr2line, "-f", "-C", "-e", str(elf)],
        input=payload,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=True,
    )
    lines = result.stdout.splitlines()
    if len(lines) != len(unique) * 2:
        raise RuntimeError("unexpected addr2line output")
    return {
        address: (lines[index * 2], lines[index * 2 + 1])
        for index, address in enumerate(unique)
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", default="/dev/ttyUSB0")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--elf", type=Path, default=Path("build/gpsp_esp32s31.elf"))
    parser.add_argument("--addr2line", default="riscv32-esp-elf-addr2line")
    parser.add_argument("--top", type=int, default=30)
    args = parser.parse_args()

    if not args.elf.is_file():
        parser.error(f"ELF not found: {args.elf}")
    addr2line = shutil.which(args.addr2line)
    if addr2line is None:
        parser.error(f"addr2line not found: {args.addr2line}")

    samples: list[int] = []
    with serial.Serial(args.port, args.baud, timeout=2) as uart:
        while True:
            raw = uart.readline()
            if not raw:
                continue
            line = raw.decode("utf-8", errors="replace").rstrip()
            print(line, flush=True)
            match = PC_LINE.search(line)
            if match:
                samples.extend(int(value, 16) for value in match.group(1).split(","))
            if "command=gpsp_profile_pc_done" in line:
                break

    symbols = symbolize(args.elf, samples, addr2line)
    function_counts = Counter(symbols[address][0] for address in samples)
    location_counts = Counter(symbols[address] for address in samples)
    print(f"result=PASS command=gpsp_profile_symbols samples={len(samples)}")
    for rank, (function, count) in enumerate(function_counts.most_common(args.top), 1):
        percentage = count * 100.0 / len(samples)
        print(
            f"kind=function rank={rank} samples={count} pct={percentage:.2f} "
            f"function={function}"
        )
    for rank, ((function, location), count) in enumerate(
        location_counts.most_common(args.top), 1
    ):
        percentage = count * 100.0 / len(samples)
        print(
            f"kind=location rank={rank} samples={count} pct={percentage:.2f} "
            f"function={function} location={location}"
        )
    return 0


if __name__ == "__main__":
    sys.exit(main())
