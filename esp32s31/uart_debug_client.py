#!/usr/bin/env python3
"""Send deterministic commands to the ESP32-S31 gpSP UART debugger."""

from __future__ import annotations

import argparse
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
        required=True,
        help="command to send; repeat this option to build a sequence",
    )
    args = parser.parse_args()

    with serial.Serial(args.port, args.baud, timeout=0.2) as uart:
        uart.reset_input_buffer()
        for command in args.command:
            if not command.strip() or not run_command(
                uart, command.strip(), args.timeout
            ):
                return 1

    print(
        "result=PASS command=uart_client "
        f"commands={len(args.command)} port={args.port}",
        flush=True,
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
