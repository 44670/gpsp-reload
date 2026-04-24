#!/usr/bin/env python3
import argparse
import base64
import os
import re
import select
import shlex
import signal
import struct
import subprocess
import sys
import time
import zlib
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
IDF_APP = ROOT / "tests" / "esp32s3" / "idf-app"


def png_chunk(kind, payload):
    body = kind + payload
    return (
        struct.pack(">I", len(payload)) +
        body +
        struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)
    )


def rgb565_to_png(rgb565, width, height):
    expected = width * height * 2
    if len(rgb565) != expected:
        raise ValueError(f"frame has {len(rgb565)} bytes, expected {expected}")

    rows = bytearray()
    offset = 0
    for _ in range(height):
        rows.append(0)
        for _ in range(width):
            value = rgb565[offset] | (rgb565[offset + 1] << 8)
            offset += 2
            r = (value >> 11) & 0x1F
            g = (value >> 5) & 0x3F
            b = value & 0x1F
            rows.extend(((r << 3) | (r >> 2),
                         (g << 2) | (g >> 4),
                         (b << 3) | (b >> 2)))

    return b"".join((
        b"\x89PNG\r\n\x1a\n",
        png_chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)),
        png_chunk(b"IDAT", zlib.compress(bytes(rows), 9)),
        png_chunk(b"IEND", b""),
    ))


def run_idf(args, action):
    idf_path = Path(args.idf_path).expanduser()
    command = [
        "idf.py",
        "-B", args.build_dir,
        "-D", f"GPSP_TEST_BACKEND={args.backend}",
        "-D", "GPSP_TEST_MODE=frames",
        "-D", f"GPSP_TEST_FRAMES={args.frames}",
        "-D", f"GPSP_TEST_ROM={args.rom}",
        "-D", f"GPSP_TEST_EXPECT_FB_HASH={args.expect_fb_hash}",
        "-D", "GPSP_TEST_DUMP_FRAME=1",
        action,
    ]
    shell_command = (
        f"source {shlex.quote(str(idf_path / 'export.sh'))} >/dev/null 2>&1 && " +
        " ".join(shlex.quote(part) for part in command)
    )
    return subprocess.Popen(
        ["bash", "-lc", shell_command],
        cwd=IDF_APP,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        errors="replace",
        preexec_fn=os.setsid,
    )


def wait_build(args):
    proc = run_idf(args, "build")
    assert proc.stdout is not None
    for line in proc.stdout:
        sys.stdout.write(line)
    rc = proc.wait()
    if rc != 0:
        raise SystemExit(rc)


def run_qemu_capture(args):
    proc = run_idf(args, "qemu")
    assert proc.stdout is not None

    begin_re = re.compile(
        r"framebuffer_rgb565_base64_begin width=(\d+) height=(\d+) bytes=(\d+) "
        r"hash=(0x[0-9a-fA-F]+)"
    )
    width = height = byte_count = None
    frame_hash = None
    result_line = None
    capture = False
    b64_lines = []
    payload_lines = 0
    payload_chars = 0
    deadline = time.monotonic() + args.timeout

    try:
        while True:
            if time.monotonic() > deadline:
                raise TimeoutError(f"QEMU timed out after {args.timeout:g}s")

            readable, _, _ = select.select([proc.stdout], [], [], 0.1)
            if not readable:
                if proc.poll() is not None:
                    break
                continue

            line = proc.stdout.readline()
            if not line:
                if proc.poll() is not None:
                    break
                continue

            stripped = line.strip()

            match = begin_re.search(stripped)
            if match:
                sys.stdout.write(line)
                width = int(match.group(1))
                height = int(match.group(2))
                byte_count = int(match.group(3))
                frame_hash = match.group(4)
                capture = True
                b64_lines.clear()
                payload_lines = 0
                payload_chars = 0
                continue

            if capture:
                if stripped == "framebuffer_rgb565_base64_end":
                    capture = False
                    if not args.show_frame_dump:
                        print("framebuffer_rgb565_base64_payload_elided "
                              f"lines={payload_lines} chars={payload_chars}")
                    sys.stdout.write(line)
                    if result_line is not None:
                        break
                    continue
                if args.show_frame_dump:
                    sys.stdout.write(line)
                b64_lines.append(stripped)
                payload_lines += 1
                payload_chars += len(stripped)
                continue

            sys.stdout.write(line)

            if stripped.startswith("result="):
                result_line = stripped
                if b64_lines and not capture:
                    break
    finally:
        try:
            os.killpg(proc.pid, signal.SIGTERM)
        except ProcessLookupError:
            pass
        proc.wait(timeout=10)

    if width is None or height is None or byte_count is None:
        raise RuntimeError("QEMU output did not contain a framebuffer dump")
    if not result_line:
        raise RuntimeError("QEMU output did not contain a result line")

    rgb565 = base64.b64decode("".join(b64_lines), validate=True)
    if len(rgb565) != byte_count:
        raise RuntimeError(f"decoded {len(rgb565)} bytes, expected {byte_count}")

    output = Path(args.output).expanduser()
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(rgb565_to_png(rgb565, width, height))
    print(f"wrote {output} width={width} height={height} hash={frame_hash}")

    if "result=PASS" not in result_line:
        raise RuntimeError(result_line)


def main():
    parser = argparse.ArgumentParser(
        description="Run the ESP32-S3 QEMU frame test and write the last frame as PNG."
    )
    parser.add_argument("--rom", required=True, help="GBA ROM path to embed")
    parser.add_argument("--output", default=str(ROOT / "tests/esp32s3/out/qemu_frame.png"))
    parser.add_argument("--backend", default="dynarec", choices=("dynarec", "interp"))
    parser.add_argument("--frames", type=int, default=60)
    parser.add_argument("--expect-fb-hash", default="0",
                        help="expected framebuffer hash, e.g. 0x10ce4667")
    parser.add_argument("--build-dir", default="build-v6.0-esp32s3-capture")
    parser.add_argument("--idf-path", default="~/esp-idf")
    parser.add_argument("--no-build", action="store_true")
    parser.add_argument("--timeout", type=float, default=180.0,
                        help="seconds before terminating QEMU capture")
    parser.add_argument("--show-frame-dump", action="store_true",
                        help="echo the raw base64 framebuffer UART payload")
    args = parser.parse_args()

    if not args.no_build:
        wait_build(args)
    run_qemu_capture(args)


if __name__ == "__main__":
    main()
