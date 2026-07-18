#!/usr/bin/env python3
"""Capture bounded, machine-readable ESP32-S31 placement benchmarks."""

from __future__ import annotations

import argparse
import sys
import time

import serial


KEEP_COMMANDS = {
    "gpsp_boot",
    "gpsp_status",
    "gpsp_profile",
    "gpsp_profile_part",
    "lcd_init",
    "touch_init",
}


def command_name(line: str) -> str | None:
    for field in line.split():
        if field.startswith("command="):
            return field.removeprefix("command=")
    return None


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", default="/dev/ttyUSB0")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--windows", type=int, default=3)
    parser.add_argument("--timeout", type=float, default=30.0)
    parser.add_argument(
        "--reset",
        action="store_true",
        help="hard-reset through RTS before collecting a deterministic run",
    )
    args = parser.parse_args()

    deadline = time.monotonic() + args.timeout
    profile_windows = 0
    saw_boot = False
    with serial.Serial(args.port, args.baud, timeout=0.25) as uart:
        if args.reset:
            uart.dtr = False
            uart.rts = False
            uart.reset_input_buffer()
            uart.rts = True
            time.sleep(0.1)
            uart.rts = False

        while time.monotonic() < deadline:
            raw = uart.readline()
            if not raw:
                continue
            line = raw.decode("utf-8", errors="replace").strip()
            command = command_name(line)
            if command not in KEEP_COMMANDS:
                continue
            print(line, flush=True)
            if command == "gpsp_boot" and "result=PASS" in line:
                saw_boot = True
            elif command == "lcd_init" and "result=FAIL" in line:
                return 2
            elif command == "gpsp_profile":
                profile_windows += 1
                if profile_windows >= args.windows:
                    # Keep reading this window's part lines until the next
                    # status line, or until one second of UART silence.
                    quiet_deadline = time.monotonic() + 1.0
                    while time.monotonic() < quiet_deadline:
                        raw = uart.readline()
                        if not raw:
                            continue
                        tail = raw.decode("utf-8", errors="replace").strip()
                        tail_command = command_name(tail)
                        if tail_command == "gpsp_profile_part":
                            print(tail, flush=True)
                            quiet_deadline = time.monotonic() + 0.25
                    print(
                        "result=PASS command=placement_benchmark "
                        f"windows={profile_windows}",
                        flush=True,
                    )
                    return 0

    reason = "profile_timeout" if saw_boot else "boot_timeout"
    print(f"result=FAIL command=placement_benchmark reason={reason}")
    return 1


if __name__ == "__main__":
    sys.exit(main())
