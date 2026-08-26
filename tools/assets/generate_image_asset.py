#!/usr/bin/env python3
"""Convert a raster image to a BRICK C image asset."""

from __future__ import annotations

import argparse
from pathlib import Path
import re
import sys

from PIL import Image


FORMATS = {
    "rgb565": ("BRICK_IMAGE_FORMAT_RGB565_LE", 2),
    "rgba8888": ("BRICK_IMAGE_FORMAT_RGBA8888", 4),
}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", required=True, type=Path)
    parser.add_argument("--output", type=Path, help="Generated .c file")
    parser.add_argument("--symbol", required=True, help="C identifier for the asset")
    parser.add_argument("--format", choices=FORMATS, default="rgb565")
    parser.add_argument("--header", type=Path, help="Generated declaration header")
    parser.add_argument("--binary", type=Path, help="Optional raw pixel output")
    parser.add_argument("--resize", help="Resize to WIDTHxHEIGHT before conversion")
    parser.add_argument("--progmem", action="store_true", help="Place generated C data in ESP8266 PROGMEM")
    return parser.parse_args()


def valid_symbol(value: str) -> bool:
    return re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", value) is not None


def bytes_for_image(image: Image.Image, image_format: str) -> bytes:
    if image_format == "rgb565":
        rgb = image.convert("RGB")
        result = bytearray()
        for red, green, blue in rgb.getdata():
            value = ((red >> 3) << 11) | ((green >> 2) << 5) | (blue >> 3)
            result.extend((value & 0xFF, value >> 8))
        return bytes(result)
    return image.convert("RGBA").tobytes()


def format_bytes(data: bytes) -> str:
    rows = []
    for offset in range(0, len(data), 16):
        rows.append("    " + ", ".join(f"0x{byte:02X}" for byte in data[offset : offset + 16]) + ",")
    return "\n".join(rows)


def main() -> int:
    args = parse_args()
    if not args.input.is_file():
        print(f"error: input does not exist: {args.input}", file=sys.stderr)
        return 2
    if not valid_symbol(args.symbol):
        print(f"error: invalid C symbol: {args.symbol}", file=sys.stderr)
        return 2
    if args.output is None and args.binary is None:
        print("error: provide --output, --binary, or both", file=sys.stderr)
        return 2

    format_name, bytes_per_pixel = FORMATS[args.format]
    with Image.open(args.input) as source:
        image = source.copy()
    if args.resize:
        match = re.fullmatch(r"(\d+)x(\d+)", args.resize)
        if match is None or int(match.group(1)) <= 0 or int(match.group(2)) <= 0:
            print("error: --resize must be WIDTHxHEIGHT with positive values", file=sys.stderr)
            return 2
        image = image.resize((int(match.group(1)), int(match.group(2))), Image.Resampling.LANCZOS)
    data = bytes_for_image(image, args.format)
    output = args.output.resolve() if args.output else None
    header = (args.header or output.with_suffix(".h")).resolve() if output else None
    if output:
        output.parent.mkdir(parents=True, exist_ok=True)
        header.parent.mkdir(parents=True, exist_ok=True)
        header.write_text(
        f"""#pragma once

#include <stdint.h>

#ifndef BRICK_IMAGE_ASSET_TYPES_DEFINED
#define BRICK_IMAGE_ASSET_TYPES_DEFINED

typedef enum {{
    BRICK_IMAGE_FORMAT_RGB565_LE = 1,
    BRICK_IMAGE_FORMAT_RGBA8888 = 2,
}} brick_image_pixel_format_t;

typedef struct {{
    uint16_t width;
    uint16_t height;
    uint8_t format;
    uint8_t bytes_per_pixel;
    uint32_t data_size;
    const uint8_t* data;
}} brick_image_asset_t;

#endif

extern const brick_image_asset_t {args.symbol};
""",
            encoding="utf-8",
        )
        storage_include = '#include <pgmspace.h>\n' if args.progmem else ''
        storage = 'PROGMEM ' if args.progmem else ''
        source = (
            f'#include "{header.name}"\n{storage_include}\n'
            f"static const uint8_t {storage}{args.symbol}_data[] = {{\n{format_bytes(data)}\n}};\n\n"
            f"const brick_image_asset_t {args.symbol} = {{\n"
            f"    {image.width}, {image.height}, {format_name}, {bytes_per_pixel}, "
            f"{len(data)}, {args.symbol}_data\n}};\n"
        )
        output.write_text(source, encoding="utf-8")
    if args.binary:
        binary = args.binary.resolve()
        binary.parent.mkdir(parents=True, exist_ok=True)
        binary.write_bytes(data)
    print(f"Generated {image.width}x{image.height}, {len(data)} bytes")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
