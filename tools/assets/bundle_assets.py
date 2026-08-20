#!/usr/bin/env python3
"""Pack raw asset binaries and emit a C++ manifest with offsets."""

from __future__ import annotations

import argparse
import re
from pathlib import Path
from typing import TextIO


def identifier(value: str) -> str:
    result = re.sub(r"[^A-Za-z0-9_]", "_", value)
    if not result or result[0].isdigit():
        result = "asset_" + result
    return result


def write_manifest(stream: TextIO, records: list[tuple[str, Path, int, int, str, int]],
                   offsets: list[int], total_size: int) -> None:
    stream.write("# name\toffset\tsize\twidth\theight\tformat\n")
    for record, offset in zip(records, offsets):
        name, _, width, height, format_name, size = record
        stream.write(f"{name}\t{offset}\t{size}\t{width}\t{height}\t{format_name}\n")
    stream.write(f"# total_size\t{total_size}\n")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--header", type=Path, required=True)
    parser.add_argument(
        "--manifest",
        type=Path,
        help="optional tab-separated text catalog with names, offsets and metadata",
    )
    parser.add_argument(
        "--asset",
        action="append",
        required=True,
        metavar="NAME=PATH,WIDTHxHEIGHT,FORMAT",
        help="asset definition, e.g. joy_tears=joy.bin,480x480,rgb565",
    )
    args = parser.parse_args()

    records: list[tuple[str, Path, int, int, str, int]] = []
    names: set[str] = set()
    blob = bytearray()
    for definition in args.asset:
        try:
            name_path, dimensions, format_name = definition.split(",", 2)
            name, path_text = name_path.split("=", 1)
            width_text, height_text = dimensions.lower().split("x", 1)
            width, height = int(width_text), int(height_text)
        except ValueError as error:
            raise SystemExit(f"invalid --asset: {definition}") from error
        path = Path(path_text)
        raw_name = name.strip()
        name = identifier(raw_name)
        if not raw_name or name in names:
            raise SystemExit(f"duplicate or empty asset name: {name_path}")
        data = path.read_bytes()
        bytes_per_pixel = {"rgb565": 2, "rgba8888": 4}.get(format_name)
        if bytes_per_pixel is None or width <= 0 or height <= 0:
            raise SystemExit(f"invalid asset metadata: {definition}")
        expected = width * height * bytes_per_pixel
        if len(data) != expected:
            raise SystemExit(f"{path}: expected {expected} bytes, got {len(data)}")
        if len(blob) > 0xFFFFFFFF or len(data) > 0xFFFFFFFF - len(blob):
            raise SystemExit("asset bundle exceeds 32-bit offset/size range")
        blob.extend(data)
        names.add(name)
        records.append((name, path, width, height, format_name, len(data)))

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(blob)
    args.header.parent.mkdir(parents=True, exist_ok=True)
    offsets: list[int] = []
    offset = 0
    for _, _, _, _, _, size in records:
        offsets.append(offset)
        offset += size

    lines = [
        "#pragma once",
        "",
        "#include <cstddef>",
        "#include <cstdint>",
        '#include "brick/core/image/AssetBundle.h"',
        "",
        "namespace generated_assets {",
        "enum class Id : std::uint32_t {",
    ]
    for index, record in enumerate(records, start=1):
        lines.append(f"    {record[0]} = {index},")
    lines += [
        "};",
        "",
        f"inline constexpr std::size_t bundle_size = {len(blob)};",
        "inline constexpr brick::interfaces::display::AssetDescriptor entries[] = {",
    ]
    for (name, _, width, height, format_name, size), offset in zip(records, offsets):
        format_cpp = {
            "rgb565": "brick::interfaces::display::PixelFormat::rgb565",
            "rgba8888": "brick::interfaces::display::PixelFormat::rgba8888",
        }[format_name]
        lines.append(
            f"    {{static_cast<std::uint32_t>(Id::{name}), {offset}, {size}, "
            f"{width}, {height}, {width * (2 if format_name == 'rgb565' else 4)}, {format_cpp}}},"
        )
    lines += [
        "};",
        "inline constexpr std::size_t entry_count = sizeof(entries) / sizeof(entries[0]);",
        "inline const brick::interfaces::display::AssetDescriptor* find(Id id) {",
        "    for (std::size_t index = 0; index < entry_count; ++index)",
        "        if (entries[index].id == static_cast<std::uint32_t>(id)) return &entries[index];",
        "    return nullptr;",
        "}",
        "}  // namespace generated_assets",
        "",
    ]
    args.header.write_text("\n".join(lines), encoding="utf-8")
    if args.manifest is not None:
        args.manifest.parent.mkdir(parents=True, exist_ok=True)
        with args.manifest.open("w", encoding="utf-8", newline="") as manifest:
            write_manifest(manifest, records, offsets, len(blob))
    print(f"Generated {args.output} ({len(blob)} bytes) and {args.header}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
