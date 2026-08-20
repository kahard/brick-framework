#!/usr/bin/env python3
"""Pack raw asset binaries and emit a C++ manifest with offsets."""

from __future__ import annotations

import argparse
import re
from pathlib import Path


def identifier(value: str) -> str:
    result = re.sub(r"[^A-Za-z0-9_]", "_", value)
    if not result or result[0].isdigit():
        result = "asset_" + result
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--header", type=Path, required=True)
    parser.add_argument(
        "--asset",
        action="append",
        required=True,
        metavar="NAME=PATH,WIDTHxHEIGHT,FORMAT",
        help="asset definition, e.g. joy_tears=joy.bin,480x480,rgb565",
    )
    args = parser.parse_args()

    records: list[tuple[str, Path, int, int, str, int]] = []
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
        data = path.read_bytes()
        bytes_per_pixel = {"rgb565": 2, "rgba8888": 4}.get(format_name)
        if bytes_per_pixel is None or width <= 0 or height <= 0:
            raise SystemExit(f"invalid asset metadata: {definition}")
        expected = width * height * bytes_per_pixel
        if len(data) != expected:
            raise SystemExit(f"{path}: expected {expected} bytes, got {len(data)}")
        blob.extend(data)
        records.append((identifier(name), path, width, height, format_name, len(data)))

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(blob)
    args.header.parent.mkdir(parents=True, exist_ok=True)
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
        "inline constexpr brick::interfaces::display::AssetDescriptor entries[] = {",
    ]
    offset = 0
    for name, _, width, height, format_name, size in records:
        format_cpp = {
            "rgb565": "brick::interfaces::display::PixelFormat::rgb565",
            "rgba8888": "brick::interfaces::display::PixelFormat::rgba8888",
        }[format_name]
        lines.append(
            f"    {{static_cast<std::uint32_t>(Id::{name}), {offset}, {size}, "
            f"{width}, {height}, {width * (2 if format_name == 'rgb565' else 4)}, {format_cpp}}},"
        )
        offset += size
    lines += [
        "};",
        "inline constexpr std::size_t entry_count = sizeof(entries) / sizeof(entries[0]);",
        "}  // namespace generated_assets",
        "",
    ]
    args.header.write_text("\n".join(lines), encoding="utf-8")
    print(f"Generated {args.output} ({len(blob)} bytes) and {args.header}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
