#!/usr/bin/env bash
set -euo pipefail

APP_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$APP_DIR/build"
IDF_PATH="${IDF_PATH:-/home/john/esp-idf}"
PARTTOOL="$IDF_PATH/components/partition_table/parttool.py"
PARTITION_BIN="$BUILD_DIR/partition_table/partition-table.bin"
GAMEPAK_PARTITION="gamepak"
PACKER="$APP_DIR/pack_gba.py"
BAUD="460800"
PORT=""
ROM=""

usage() {
  printf '%s\n' \
    "Usage: flash_gba.sh -p PORT path/to/game.gba" \
    "" \
    "The .gba is packed into the direct-mmap 32 KiB page container." \
    "" \
    "Options:" \
    "  -p, --port PORT       Serial port for ESP32-S31 flashing." \
    "  -b, --baud BAUD       Flash baud rate. Default: 460800." \
    "  --idf-path PATH       ESP-IDF path. Default: /home/john/esp-idf." \
    "  -h, --help            Show this help."
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

if [ -z "$PORT" ] || [ -z "$ROM" ]; then
  usage >&2
  exit 2
fi
if [ ! -f "$ROM" ]; then
  echo "GBA ROM not found: $ROM" >&2
  exit 1
fi

ROM="$(realpath "$ROM")"
. "$IDF_PATH/export.sh" >/dev/null

(cd "$APP_DIR" && idf.py --preview -B build partition-table >/dev/null)
if [ ! -f "$PARTITION_BIN" ]; then
  echo "partition table not found: $PARTITION_BIN" >&2
  exit 1
fi

part_info() {
  python3 "$PARTTOOL" -q --partition-table-file "$PARTITION_BIN" \
    get_partition_info --partition-name "$1" --info "$2"
}

GAMEPAK_SIZE="$(part_info "$GAMEPAK_PARTITION" size)"
ROM_SIZE="$(stat -c %s "$ROM")"
if [ "$ROM_SIZE" -le 0 ]; then
  echo "GBA ROM is empty: $ROM" >&2
  exit 1
fi

PACK_DIR="$(mktemp -d /tmp/gpsp-gamepak-XXXXXX)"
trap 'rm -rf "$PACK_DIR"' EXIT
PACKED_ROM="$PACK_DIR/gamepak.bin"
python3 "$PACKER" "$ROM" "$PACKED_ROM" --max-bytes "$GAMEPAK_SIZE"
PACKED_SIZE="$(stat -c %s "$PACKED_ROM")"

python3 "$PARTTOOL" --partition-table-file "$PARTITION_BIN" \
  --port "$PORT" --baud "$BAUD" \
  erase_partition --partition-name "$GAMEPAK_PARTITION"
python3 "$PARTTOOL" --partition-table-file "$PARTITION_BIN" \
  --port "$PORT" --baud "$BAUD" \
  write_partition --partition-name "$GAMEPAK_PARTITION" --input "$PACKED_ROM"
printf 'flashed %s: raw_bytes=%s packed_bytes=%s partition=%s format=gpsp-page-v1\n' \
  "$ROM" "$ROM_SIZE" "$PACKED_SIZE" "$GAMEPAK_PARTITION"
