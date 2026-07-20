#!/usr/bin/env python3
"""Pack a raw GBA ROM into the ESP32-S31 direct-mmap page container."""

from __future__ import annotations

import argparse
import binascii
import struct
import sys
from pathlib import Path


MAGIC = b"GPSPGBAP"
VERSION = 1
HEADER_BYTES = 0x8000
PAGE_BYTES = 0x8000
MAX_PAGES = 1024
TABLE_OFFSET = 0x100
FLAGS_DEDUP = 0x00000001

OFF_VERSION = 0x08
OFF_HEADER_BYTES = 0x0C
OFF_PAGE_BYTES = 0x10
OFF_ROM_BYTES = 0x14
OFF_PAGE_COUNT = 0x18
OFF_STORED_PAGE_COUNT = 0x1C
OFF_DATA_OFFSET = 0x20
OFF_IMAGE_BYTES = 0x24
OFF_TABLE_OFFSET = 0x28
OFF_TABLE_ENTRY_BYTES = 0x2C
OFF_ROM_CRC32 = 0x30
OFF_HEADER_CRC32 = 0x34
OFF_FLAGS = 0x38


def integer(text: str) -> int:
    return int(text, 0)


def pack_rom(raw: bytes) -> tuple[bytearray, list[bytes], dict[str, int]]:
    if not raw:
        raise ValueError("ROM is empty")
    if len(raw) > MAX_PAGES * PAGE_BYTES:
        raise ValueError("ROM exceeds the 32 MiB GBA address space")

    page_count = (len(raw) + PAGE_BYTES - 1) // PAGE_BYTES
    unique_pages: list[bytes] = []
    unique_index: dict[bytes, int] = {}
    logical_to_physical: list[int] = []

    for logical_page in range(page_count):
        begin = logical_page * PAGE_BYTES
        page = raw[begin : begin + PAGE_BYTES]
        if len(page) < PAGE_BYTES:
            page += b"\xff" * (PAGE_BYTES - len(page))

        physical_page = unique_index.get(page)
        if physical_page is None:
            physical_page = len(unique_pages)
            unique_index[page] = physical_page
            unique_pages.append(page)
        logical_to_physical.append(physical_page)

    image_bytes = HEADER_BYTES + len(unique_pages) * PAGE_BYTES
    header = bytearray(HEADER_BYTES)
    header[: len(MAGIC)] = MAGIC
    fields = (
        (OFF_VERSION, VERSION),
        (OFF_HEADER_BYTES, HEADER_BYTES),
        (OFF_PAGE_BYTES, PAGE_BYTES),
        (OFF_ROM_BYTES, len(raw)),
        (OFF_PAGE_COUNT, page_count),
        (OFF_STORED_PAGE_COUNT, len(unique_pages)),
        (OFF_DATA_OFFSET, HEADER_BYTES),
        (OFF_IMAGE_BYTES, image_bytes),
        (OFF_TABLE_OFFSET, TABLE_OFFSET),
        (OFF_TABLE_ENTRY_BYTES, 4),
        (OFF_ROM_CRC32, binascii.crc32(raw) & 0xFFFFFFFF),
        (OFF_HEADER_CRC32, 0),
        (OFF_FLAGS, FLAGS_DEDUP),
    )
    for offset, value in fields:
        struct.pack_into("<I", header, offset, value)

    for logical_page, physical_page in enumerate(logical_to_physical):
        physical_offset = HEADER_BYTES + physical_page * PAGE_BYTES
        struct.pack_into(
            "<I", header, TABLE_OFFSET + logical_page * 4, physical_offset
        )

    struct.pack_into(
        "<I", header, OFF_HEADER_CRC32, binascii.crc32(header) & 0xFFFFFFFF
    )
    stats = {
        "rom_bytes": len(raw),
        "page_count": page_count,
        "stored_page_count": len(unique_pages),
        "duplicate_page_count": page_count - len(unique_pages),
        "image_bytes": image_bytes,
        "rom_crc32": binascii.crc32(raw) & 0xFFFFFFFF,
    }
    return header, unique_pages, stats


def verify_image(image_path: Path, raw: bytes, stats: dict[str, int]) -> None:
    image = image_path.read_bytes()
    if len(image) != stats["image_bytes"]:
        raise ValueError("packed image length mismatch")
    header = bytearray(image[:HEADER_BYTES])
    expected_crc = struct.unpack_from("<I", header, OFF_HEADER_CRC32)[0]
    struct.pack_into("<I", header, OFF_HEADER_CRC32, 0)
    if binascii.crc32(header) & 0xFFFFFFFF != expected_crc:
        raise ValueError("packed header CRC mismatch")

    reconstructed = bytearray()
    for page in range(stats["page_count"]):
        offset = struct.unpack_from("<I", image, TABLE_OFFSET + page * 4)[0]
        reconstructed += image[offset : offset + PAGE_BYTES]
    if reconstructed[: len(raw)] != raw:
        raise ValueError("packed page table does not reconstruct the ROM")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("input", type=Path, help="raw .gba input")
    parser.add_argument("output", type=Path, help="packed output image")
    parser.add_argument(
        "--max-bytes",
        type=integer,
        default=None,
        help="fail if the packed image exceeds this many bytes",
    )
    args = parser.parse_args()

    try:
        raw = args.input.read_bytes()
        header, pages, stats = pack_rom(raw)
        if args.max_bytes is not None and stats["image_bytes"] > args.max_bytes:
            raise ValueError(
                f"packed image is {stats['image_bytes']} bytes, "
                f"limit is {args.max_bytes} bytes"
            )
        with args.output.open("wb") as output:
            output.write(header)
            for page in pages:
                output.write(page)
        verify_image(args.output, raw, stats)
    except (OSError, ValueError) as error:
        print(f"pack_gba: {error}", file=sys.stderr)
        return 1

    print(
        "result=PASS command=pack_gba format=gpsp-page-v1 "
        + " ".join(f"{key}={value}" for key, value in stats.items())
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
