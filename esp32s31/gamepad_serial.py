#!/usr/bin/env python3
"""Monitor ESP32-S31 USB HID connection and changed gamepad reports."""

from __future__ import annotations

import argparse
from pathlib import Path
import re
import sys
import time

import serial


COMMAND_RE = re.compile(r"(?:^| )command=([^ ]+)")
DATA_RE = re.compile(r"(?:^| )data=([0-9a-fA-F]*)")
SEQUENCE_RE = re.compile(r"(?:^| )offset=(\d+)")
KEEP_COMMANDS = {
    "usb_gamepad_init",
    "usb_hid_connect",
    "usb_hid_descriptor",
    "usb_hid_descriptor_chunk",
    "usb_hid_report",
    "usb_hid_skip",
    "usb_interface",
    "usb_xinput_connect",
    "usb_gamepad_event",
}


def command_name(line: str) -> str | None:
    match = COMMAND_RE.search(line)
    return match.group(1) if match else None


def describe_report(line: str, previous: bytes | None) -> tuple[str, bytes] | None:
    data_match = DATA_RE.search(line)
    if data_match is None:
        return None
    report = bytes.fromhex(data_match.group(1))
    sequence_match = SEQUENCE_RE.search(line)
    sequence = sequence_match.group(1) if sequence_match else "?"

    if previous is None:
        changes = "initial"
    else:
        width = max(len(previous), len(report))
        fields = []
        for index in range(width):
            old = previous[index] if index < len(previous) else 0
            new = report[index] if index < len(report) else 0
            if old != new:
                fields.append(f"b{index}:{old:02x}->{new:02x}/xor={old ^ new:02x}")
        changes = ",".join(fields) if fields else "none"

    return (
        f"event=gamepad_report seq={sequence} bytes={len(report)} "
        f"data={report.hex()} changed={changes}",
        report,
    )


def hard_reset(uart: serial.Serial) -> None:
    uart.dtr = False
    uart.rts = False
    uart.reset_input_buffer()
    uart.rts = True
    time.sleep(0.1)
    uart.rts = False


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Print changed USB gamepad reports from Korvo-1 firmware"
    )
    parser.add_argument("--port", default="/dev/ttyUSB0")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument(
        "--reset", action="store_true", help="reset the board before monitoring"
    )
    parser.add_argument(
        "--timeout",
        type=float,
        default=0.0,
        help="stop after this many seconds; zero waits until Ctrl-C",
    )
    parser.add_argument(
        "--output", type=Path, help="also save original machine-readable lines"
    )
    parser.add_argument(
        "--no-descriptor",
        action="store_true",
        help="hide report-descriptor chunks",
    )
    args = parser.parse_args()

    deadline = time.monotonic() + args.timeout if args.timeout > 0 else None
    previous: bytes | None = None
    output = args.output.open("w", encoding="utf-8") if args.output else None

    try:
        with serial.Serial(args.port, args.baud, timeout=0.25) as uart:
            if args.reset:
                hard_reset(uart)

            print(
                f"event=monitor_ready port={args.port} baud={args.baud} "
                "action=press_gamepad_buttons",
                flush=True,
            )
            while deadline is None or time.monotonic() < deadline:
                raw = uart.readline()
                if not raw:
                    continue
                line = raw.decode("utf-8", errors="replace").strip()
                command = command_name(line)
                if command not in KEEP_COMMANDS:
                    continue
                if output is not None:
                    output.write(line + "\n")
                    output.flush()
                if command == "usb_hid_report":
                    described = describe_report(line, previous)
                    if described is not None:
                        pretty, previous = described
                        print(pretty, flush=True)
                elif command == "usb_hid_descriptor_chunk" and args.no_descriptor:
                    continue
                else:
                    print(line, flush=True)
    except KeyboardInterrupt:
        print("event=monitor_stopped reason=keyboard_interrupt", flush=True)
    finally:
        if output is not None:
            output.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
