#!/usr/bin/env python3
"""Send deterministic commands to the ESP32-S31 gpSP UART debugger."""

from __future__ import annotations

import argparse
from pathlib import Path
import re
import sys
import time

import serial


COMMAND_RE = re.compile(r"(?:^| )command=([^ ]+)")
RESULT_RE = re.compile(r"(?:^| )result=([^ ]+)")


def command_name(line: str) -> str | None:
    match = COMMAND_RE.search(line)
    return match.group(1) if match else None


def result_name(line: str) -> str | None:
    match = RESULT_RE.search(line)
    return match.group(1) if match else None


def response_completes(command: str, line: str) -> bool:
    name = command.split(maxsplit=1)[0]
    response_name = {"run": "stepf", "continue": "cont", "?": "help"}.get(
        name, name
    )
    if command_name(line) != response_name:
        return False
    if response_name == "stepf":
        return "action=complete" in line
    return True


def run_command(
    uart: serial.Serial, command: str, timeout: float
) -> bool:
    uart.write((command + "\n").encode("utf-8"))
    uart.flush()
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        raw = uart.readline()
        if not raw:
            continue
        line = raw.decode("utf-8", errors="replace").strip()
        if not response_completes(command, line):
            continue
        print(line, flush=True)
        return result_name(line) == "PASS"
    print(
        "result=FAIL command=uart_client reason=response_timeout "
        f"sent={command!r} timeout={timeout}",
        flush=True,
    )
    return False


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Run a sequence of gpSP UART debugger commands"
    )
    parser.add_argument("--port", default="/dev/ttyUSB0")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=30.0)
    parser.add_argument(
        "--command",
        action="append",
        default=[],
        help="command to send; repeat this option to build a sequence",
    )
    parser.add_argument(
        "--commands-file",
        type=Path,
        help="read additional commands one per line; use /dev/stdin for a pipe",
    )
    args = parser.parse_args()

    commands = list(args.command)
    if args.commands_file is not None:
        commands.extend(
            line.strip()
            for line in args.commands_file.read_text(encoding="utf-8").splitlines()
            if line.strip() and not line.lstrip().startswith("#")
        )
    if not commands:
        parser.error("at least one --command or --commands-file is required")

    with serial.Serial(args.port, args.baud, timeout=0.2) as uart:
        uart.reset_input_buffer()
        for command in commands:
            if not command.strip() or not run_command(
                uart, command.strip(), args.timeout
            ):
                return 1

    print(
        "result=PASS command=uart_client "
        f"commands={len(commands)} port={args.port}",
        flush=True,
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
