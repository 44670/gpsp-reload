#!/usr/bin/env python3
import argparse
import os
import select
import shlex
import subprocess
import sys
import time
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
IDF_APP = ROOT / "tests" / "esp32s3" / "idf-app"
BUILD_DIR = "build"
QEMU_EXTRA_ARGS = "-m 8M"


def qemu_flash_image():
    return IDF_APP / BUILD_DIR / "qemu_flash_gba.bin"


def flash_gba_image(args):
    subprocess.check_call([
        str(IDF_APP / "flash_gba.sh"),
        "--image", str(qemu_flash_image()),
        str(Path(args.rom).expanduser()),
    ], cwd=ROOT)


def run_idf(args, action, stdin_data=None):
    idf_path = Path(args.idf_path).expanduser()
    command = [
        "idf.py",
        "-B", BUILD_DIR,
        "-D", f"GPSP_TEST_BACKEND={args.backend}",
        "-D", "GPSP_TEST_MODE=debug",
        "-D", "GPSP_TEST_DUMP_FRAME=1",
        action,
    ]
    if action == "qemu":
        command.extend([
            "--flash-file", str(qemu_flash_image()),
            "--qemu-extra-args", QEMU_EXTRA_ARGS,
        ])
    shell_command = (
        f"source {shlex.quote(str(idf_path / 'export.sh'))} >/dev/null 2>&1 && " +
        " ".join(shlex.quote(part) for part in command)
    )
    if stdin_data is None or action != "qemu":
        return subprocess.call(["bash", "-lc", shell_command], cwd=IDF_APP)

    process = subprocess.Popen(
        ["bash", "-lc", shell_command],
        cwd=IDF_APP,
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        bufsize=1,
    )
    assert process.stdin is not None
    assert process.stdout is not None

    process.stdin.write(stdin_data)
    process.stdin.flush()

    result_rc = None
    deadline = time.monotonic() + args.timeout
    while True:
        if process.poll() is not None:
            break

        if time.monotonic() > deadline:
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
            return 124

        readable, _, _ = select.select([process.stdout], [], [], 0.1)
        if not readable:
            continue

        line = process.stdout.readline()
        if not line:
            continue

        sys.stdout.write(line)
        sys.stdout.flush()

        if "result=PASS" in line or "result=FAIL" in line:
            result_rc = 0 if "result=PASS" in line else 1
            process.stdin.write("\x01x")
            process.stdin.flush()

    for line in process.stdout:
        sys.stdout.write(line)

    rc = process.wait()
    if result_rc is not None:
        return result_rc
    return rc


def main():
    parser = argparse.ArgumentParser(
        description="Build and run the ESP32-S3 serial debugger in QEMU."
    )
    parser.add_argument("--rom", required=True,
                        help="GBA ROM path to flash into the gamepak partition")
    parser.add_argument("--backend", default="interp", choices=("interp",))
    parser.add_argument("--idf-path", default="~/esp-idf")
    parser.add_argument("--no-build", action="store_true")
    parser.add_argument("--cmd", action="append", default=[],
                        help="Debugger command to send after QEMU starts; repeatable")
    parser.add_argument("--script", type=Path,
                        help="File containing debugger commands to send to QEMU")
    parser.add_argument("--timeout", type=float, default=180.0,
                        help="Seconds before killing scripted QEMU sessions")
    args = parser.parse_args()

    if not args.no_build:
        rc = run_idf(args, "build")
        if rc != 0:
            raise SystemExit(rc)
    flash_gba_image(args)

    stdin_data = None
    if args.script or args.cmd:
        commands = []
        if args.script:
            commands.extend(args.script.read_text().splitlines())
        commands.extend(args.cmd)
        stdin_data = "\n".join(commands) + "\n"

    raise SystemExit(run_idf(args, "qemu", stdin_data=stdin_data))


if __name__ == "__main__":
    main()
