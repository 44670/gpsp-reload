#!/usr/bin/env python3
"""Summarize ESP32-S3 linker map RAM usage.

The default input is the active hardware build map:
    esp32s3/build/gpsp_esp32s3.map
"""

from __future__ import annotations

import argparse
import collections
import os
import re
from dataclasses import dataclass
from pathlib import Path


DEFAULT_SECTIONS = (
    ".dram0.bss",
    ".dram0.data",
    ".noinit",
    ".ext_ram.bss",
)


MEMORY_RE = re.compile(
    r"^(?P<name>\S+)\s+"
    r"(?P<origin>0x[0-9a-fA-F]+)\s+"
    r"(?P<length>0x[0-9a-fA-F]+)"
    r"(?:\s+(?P<attrs>\S+))?$"
)
OUTPUT_SECTION_RE = re.compile(
    r"^(?P<name>\.[A-Za-z0-9_.$-]+)\s+"
    r"(?P<addr>0x[0-9a-fA-F]+)\s+"
    r"(?P<size>0x[0-9a-fA-F]+|\d+)"
)
OUTPUT_SECTION_NAME_RE = re.compile(r"^(?P<name>\.[A-Za-z0-9_.$-]+)\s*$")
OUTPUT_SECTION_CONT_RE = re.compile(
    r"^\s+(?P<addr>0x[0-9a-fA-F]+)\s+"
    r"(?P<size>0x[0-9a-fA-F]+|\d+)\s*$"
)
INPUT_SECTION_RE = re.compile(
    r"^\s+(?P<name>\.[A-Za-z0-9_.$*+-]+|\*fill\*)\s+"
    r"(?P<addr>0x[0-9a-fA-F]+)\s+"
    r"(?P<size>0x[0-9a-fA-F]+|\d+)"
    r"(?:\s+(?P<object>.*?))?\s*$"
)
INPUT_SECTION_NAME_RE = re.compile(
    r"^\s+(?P<name>\.[A-Za-z0-9_.$*+-]+|\*fill\*)\s*$"
)
INPUT_SECTION_CONT_RE = re.compile(
    r"^\s+(?P<addr>0x[0-9a-fA-F]+)\s+"
    r"(?P<size>0x[0-9a-fA-F]+|\d+)"
    r"(?:\s+(?P<object>.*?))?\s*$"
)
SYMBOL_ASSIGN_RE = re.compile(r"^\s+0x[0-9a-fA-F]+\s+(?P<name>[_A-Za-z]\w*)\s*=\s*\.?\s*$")
SYMBOL_RE = re.compile(r"^\s+0x[0-9a-fA-F]+\s+(?P<name>[_A-Za-z]\w*)\s*$")


@dataclass(frozen=True)
class MemoryRegion:
    name: str
    origin: int
    length: int
    attrs: str


@dataclass(frozen=True)
class OutputSection:
    name: str
    addr: int
    size: int


@dataclass
class InputEntry:
    output_section: str
    input_section: str
    addr: int
    size: int
    obj: str
    symbol: str | None = None


def parse_int(value: str) -> int:
    return int(value, 0)


def fmt_size(size: int) -> str:
    if size >= 1024 * 1024:
        return f"{size / (1024 * 1024):7.2f} MiB"
    if size >= 1024:
        return f"{size / 1024:7.1f} KiB"
    return f"{size:7d} B  "


def default_map_path() -> Path:
    return Path(__file__).resolve().parent / "build" / "gpsp_esp32s3.map"


def short_obj(obj: str) -> str:
    if not obj:
        return ""
    if obj.startswith("esp-idf/"):
        return obj
    return os.path.basename(obj)


def likely_symbol(input_section: str) -> str:
    prefixes = (
        ".bss.",
        ".data.",
        ".rodata.",
        ".dram1.",
        ".dram0.",
        ".iram1.",
        ".iram0.",
        ".ext_ram.bss.",
        ".ext_ram.data.",
    )
    for prefix in prefixes:
        if input_section.startswith(prefix):
            name = input_section[len(prefix) :]
            return input_section if name.isdigit() else name
    return input_section


def parse_map(path: Path) -> tuple[list[MemoryRegion], list[OutputSection], list[InputEntry]]:
    regions: list[MemoryRegion] = []
    output_sections: list[OutputSection] = []
    entries: list[InputEntry] = []

    in_memory_config = False
    seen_memory_row = False
    current_output: str | None = None
    pending_output_name: str | None = None
    pending_input_name: str | None = None
    last_entry: InputEntry | None = None

    for raw_line in path.read_text(errors="replace").splitlines():
        line = raw_line.rstrip()

        if line == "Memory Configuration":
            in_memory_config = True
            seen_memory_row = False
            continue

        if in_memory_config:
            match = MEMORY_RE.match(line)
            if match:
                regions.append(
                    MemoryRegion(
                        name=match.group("name"),
                        origin=parse_int(match.group("origin")),
                        length=parse_int(match.group("length")),
                        attrs=match.group("attrs") or "",
                    )
                )
                seen_memory_row = True
                continue
            if seen_memory_row and not line:
                in_memory_config = False
            continue

        match = OUTPUT_SECTION_RE.match(line)
        if match and not raw_line.startswith(" "):
            current_output = match.group("name")
            output_sections.append(
                OutputSection(
                    name=current_output,
                    addr=parse_int(match.group("addr")),
                    size=parse_int(match.group("size")),
                )
            )
            pending_input_name = None
            pending_output_name = None
            last_entry = None
            continue

        if not raw_line.startswith(" "):
            match = OUTPUT_SECTION_NAME_RE.match(line)
            if match:
                pending_output_name = match.group("name")
                pending_input_name = None
                last_entry = None
                continue
            pending_output_name = None

        if pending_output_name:
            match = OUTPUT_SECTION_CONT_RE.match(line)
            if match:
                current_output = pending_output_name
                output_sections.append(
                    OutputSection(
                        name=current_output,
                        addr=parse_int(match.group("addr")),
                        size=parse_int(match.group("size")),
                    )
                )
                pending_output_name = None
                pending_input_name = None
                last_entry = None
                continue

        if current_output is None:
            continue

        match = INPUT_SECTION_RE.match(line)
        if match:
            entry = InputEntry(
                output_section=current_output,
                input_section=match.group("name"),
                addr=parse_int(match.group("addr")),
                size=parse_int(match.group("size")),
                obj=match.group("object") or "",
            )
            entries.append(entry)
            pending_input_name = None
            last_entry = entry
            continue

        match = INPUT_SECTION_NAME_RE.match(line)
        if match:
            pending_input_name = match.group("name")
            last_entry = None
            continue

        if pending_input_name:
            match = INPUT_SECTION_CONT_RE.match(line)
            if match:
                entry = InputEntry(
                    output_section=current_output,
                    input_section=pending_input_name,
                    addr=parse_int(match.group("addr")),
                    size=parse_int(match.group("size")),
                    obj=match.group("object") or "",
                )
                entries.append(entry)
                pending_input_name = None
                last_entry = entry
                continue

        if last_entry is not None:
            match = SYMBOL_RE.match(line) or SYMBOL_ASSIGN_RE.match(line)
            if match and last_entry.symbol is None:
                last_entry.symbol = match.group("name")

    return regions, output_sections, entries


def print_memory_regions(regions: list[MemoryRegion]) -> None:
    print("Memory regions")
    for region in regions:
        end = region.origin + region.length
        print(
            f"  {region.name:16} "
            f"0x{region.origin:08x}-0x{end:08x} "
            f"{fmt_size(region.length)} {region.attrs}"
        )


def print_output_sections(output_sections: list[OutputSection]) -> None:
    interesting_prefixes = (
        ".dram",
        ".iram",
        ".rtc",
        ".ext_ram",
        ".flash.text",
        ".flash.rodata",
        ".noinit",
    )
    print("\nOutput sections")
    for section in output_sections:
        if section.name.startswith(interesting_prefixes):
            print(f"  {section.name:20} 0x{section.addr:08x} {fmt_size(section.size)}")


def print_heap_hint(
    regions: list[MemoryRegion], output_sections: list[OutputSection]
) -> None:
    dram_region = next((r for r in regions if r.name == "dram0_0_seg"), None)
    heap_start = next((s.addr for s in output_sections if s.name == ".dram0.heap_start"), None)
    if not dram_region or heap_start is None:
        return

    region_end = dram_region.origin + dram_region.length
    linked_bytes = max(0, heap_start - dram_region.origin)
    free_bytes = max(0, region_end - heap_start)
    print("\nInternal DRAM linker pressure")
    print(f"  dram0_0_seg size:       {fmt_size(dram_region.length)}")
    print(f"  linked before heap:     {fmt_size(linked_bytes)}")
    print(f"  heap starts at:         0x{heap_start:08x}")
    print(f"  free inside dram0_0_seg:{fmt_size(free_bytes)}")


def print_top_entries(
    entries: list[InputEntry],
    sections: tuple[str, ...],
    limit: int,
    include_fill: bool,
) -> None:
    for section in sections:
        section_entries = [
            entry
            for entry in entries
            if entry.output_section == section
            and entry.size
            and (include_fill or entry.input_section != "*fill*")
        ]
        if not section_entries:
            continue

        section_entries.sort(key=lambda entry: entry.size, reverse=True)
        total = sum(entry.size for entry in section_entries)
        print(f"\nTop entries in {section} ({fmt_size(total).strip()} shown)")
        for entry in section_entries[:limit]:
            symbol = entry.symbol or likely_symbol(entry.input_section)
            print(
                f"  {fmt_size(entry.size)} 0x{entry.addr:08x} "
                f"{symbol:36} {entry.input_section:24} {short_obj(entry.obj)}"
            )


def print_top_objects(
    entries: list[InputEntry],
    sections: tuple[str, ...],
    limit: int,
    include_fill: bool,
) -> None:
    for section in sections:
        totals: dict[str, int] = collections.defaultdict(int)
        for entry in entries:
            if entry.output_section != section or not entry.size:
                continue
            if not include_fill and entry.input_section == "*fill*":
                continue
            totals[short_obj(entry.obj) or entry.input_section] += entry.size

        if not totals:
            continue

        print(f"\nTop objects in {section}")
        for obj, size in sorted(totals.items(), key=lambda item: item[1], reverse=True)[:limit]:
            print(f"  {fmt_size(size)} {obj}")


def parse_sections(value: str) -> tuple[str, ...]:
    return tuple(section.strip() for section in value.split(",") if section.strip())


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "map",
        nargs="?",
        type=Path,
        default=default_map_path(),
        help="linker map path, default: esp32s3/build/gpsp_esp32s3.map",
    )
    parser.add_argument(
        "--sections",
        default=",".join(DEFAULT_SECTIONS),
        help="comma-separated output sections to rank",
    )
    parser.add_argument("--top", type=int, default=20, help="rows per ranking")
    parser.add_argument(
        "--include-fill",
        action="store_true",
        help="include linker fill/alignment entries in rankings",
    )
    args = parser.parse_args()

    map_path = args.map.resolve()
    if not map_path.exists():
        parser.error(f"map file not found: {map_path}")

    sections = parse_sections(args.sections)
    regions, output_sections, entries = parse_map(map_path)

    print(f"Map: {map_path}")
    print_memory_regions(regions)
    print_output_sections(output_sections)
    print_heap_hint(regions, output_sections)
    print_top_entries(entries, sections, args.top, args.include_fill)
    print_top_objects(entries, sections, args.top, args.include_fill)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
