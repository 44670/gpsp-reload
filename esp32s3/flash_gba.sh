#!/usr/bin/env bash
set -euo pipefail

APP_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
HW_BUILD_DIR="$APP_DIR/build"
QEMU_BUILD_DIR="$APP_DIR/build-qemu"
BUILD_DIR="$HW_BUILD_DIR"
IDF_PATH="${IDF_PATH:-$HOME/esp-idf}"
PARTTOOL="$IDF_PATH/components/partition_table/parttool.py"
PARTITION_BIN="$BUILD_DIR/partition_table/partition-table.bin"
GAMEPAK_PARTITION="gamepak"
BAUD="460800"
PORT=""
FLASH_IMAGE=""
ROM=""

usage() {
  cat <<EOF
Usage:
  $(basename "$0") -p PORT path/to/game.gba
  $(basename "$0") --image build-qemu/qemu_flash_gba.bin path/to/game.gba

The gamepak partition contains only the raw .gba bytes. This script does not
write a header, metadata block, manifest, or sidecar partition.

Options:
  -p, --port PORT       Serial port for real CoreS3 SE flashing.
  -b, --baud BAUD       Flash baud rate. Default: $BAUD.
  --image PATH          Patch a QEMU flash image instead of a device.
  --idf-path PATH       ESP-IDF path. Default: $IDF_PATH.
  -h, --help            Show this help.
EOF
}

while [ "$#" -gt 0 ]; do
  case "$1" in
    -p|--port)
      PORT="${2:?missing port}"
      shift 2
      ;;
    -b|--baud)
      BAUD="${2:?missing baud}"
      shift 2
      ;;
    --image)
      FLASH_IMAGE="${2:?missing image path}"
      shift 2
      ;;
    --idf-path)
      IDF_PATH="${2:?missing ESP-IDF path}"
      PARTTOOL="$IDF_PATH/components/partition_table/parttool.py"
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    -*)
      echo "unknown option: $1" >&2
      usage >&2
      exit 2
      ;;
    *)
      if [ -n "$ROM" ]; then
        echo "only one .gba file may be specified" >&2
        exit 2
      fi
      ROM="$1"
      shift
      ;;
  esac
done

if [ -z "$ROM" ]; then
  usage >&2
  exit 2
fi

if [ -z "$FLASH_IMAGE" ] && [ -z "$PORT" ]; then
  echo "provide either --port PORT or --image PATH" >&2
  usage >&2
  exit 2
fi

if [ -n "$FLASH_IMAGE" ] && [ -n "$PORT" ]; then
  echo "--image and --port are mutually exclusive" >&2
  exit 2
fi

if [ ! -f "$ROM" ]; then
  echo "GBA ROM not found: $ROM" >&2
  exit 1
fi

ROM="$(realpath "$ROM")"
if [ -n "$FLASH_IMAGE" ]; then
  FLASH_IMAGE="$(realpath -m "$FLASH_IMAGE")"
  BUILD_DIR="$QEMU_BUILD_DIR"
else
  BUILD_DIR="$HW_BUILD_DIR"
fi
PARTITION_BIN="$BUILD_DIR/partition_table/partition-table.bin"

. "$IDF_PATH/export.sh" >/dev/null

if [ -n "$FLASH_IMAGE" ]; then
  (cd "$APP_DIR" && idf.py -B build-qemu/ -D USE_QEMU=1 partition-table >/dev/null)
else
  (cd "$APP_DIR" && idf.py -B build/ partition-table >/dev/null)
fi

if [ ! -f "$PARTITION_BIN" ]; then
  echo "partition table not found: $PARTITION_BIN" >&2
  exit 1
fi

part_info() {
  python3 "$PARTTOOL" -q --partition-table-file "$PARTITION_BIN" \
    get_partition_info --partition-name "$1" --info "$2"
}

GAMEPAK_OFFSET="$(part_info "$GAMEPAK_PARTITION" offset)"
GAMEPAK_SIZE="$(part_info "$GAMEPAK_PARTITION" size)"
ROM_SIZE="$(stat -c %s "$ROM")"

if [ "$ROM_SIZE" -le 0 ]; then
  echo "GBA ROM is empty: $ROM" >&2
  exit 1
fi

if [ "$ROM_SIZE" -gt $((GAMEPAK_SIZE)) ]; then
  printf 'GBA ROM is too large: %s bytes, partition %s is %d bytes\n' \
    "$ROM_SIZE" "$GAMEPAK_PARTITION" "$((GAMEPAK_SIZE))" >&2
  exit 1
fi

if [ -n "$FLASH_IMAGE" ]; then
  mkdir -p "$(dirname "$FLASH_IMAGE")"
  (cd "$BUILD_DIR" && python3 -m esptool --chip=esp32s3 merge-bin \
    --output "$FLASH_IMAGE" --pad-to-size=8MB @flash_args >/dev/null)

  python3 - "$FLASH_IMAGE" "$GAMEPAK_OFFSET" "$GAMEPAK_SIZE" "$ROM" <<'PY'
import pathlib
import sys

image_path = pathlib.Path(sys.argv[1])
gamepak_offset = int(sys.argv[2], 0)
gamepak_size = int(sys.argv[3], 0)
rom_path = pathlib.Path(sys.argv[4])

with image_path.open("r+b") as image:
    image.seek(gamepak_offset)
    remaining = gamepak_size
    chunk = b"\xFF" * 65536
    while remaining:
        n = min(remaining, len(chunk))
        image.write(chunk[:n])
        remaining -= n
    image.seek(gamepak_offset)
    with rom_path.open("rb") as rom:
        while True:
            data = rom.read(1024 * 1024)
            if not data:
                break
            image.write(data)
PY
  printf 'patched %s: raw %s bytes at partition %s offset %s\n' \
    "$FLASH_IMAGE" "$ROM_SIZE" "$GAMEPAK_PARTITION" "$GAMEPAK_OFFSET"
else
  python3 "$PARTTOOL" --partition-table-file "$PARTITION_BIN" \
    --port "$PORT" --baud "$BAUD" \
    erase_partition --partition-name "$GAMEPAK_PARTITION"
  python3 "$PARTTOOL" --partition-table-file "$PARTITION_BIN" \
    --port "$PORT" --baud "$BAUD" \
    write_partition --partition-name "$GAMEPAK_PARTITION" --input "$ROM"
  printf 'flashed %s: raw %s bytes to partition %s\n' \
    "$ROM" "$ROM_SIZE" "$GAMEPAK_PARTITION"
fi
