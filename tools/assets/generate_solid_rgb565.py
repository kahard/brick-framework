#!/usr/bin/env python3
"""Generate a solid RGB565 image asset."""

from __future__ import annotations

import argparse
from pathlib import Path


def parse_color(value: str) -> tuple[int, int, int]:
    value = value.removeprefix("#")
    if len(value) != 6:
        raise argparse.ArgumentTypeError("color must be RRGGBB or #RRGGBB")
    try:
        return int(value[0:2], 16), int(value[2:4], 16), int(value[4:6], 16)
    except ValueError as error:
        raise argparse.ArgumentTypeError("color must be RRGGBB or #RRGGBB") from error


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--width", type=int, required=True)
    parser.add_argument("--height", type=int, required=True)
    parser.add_argument("--color", type=parse_color, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.width <= 0 or args.height <= 0:
        parser.error("width and height must be positive")

    red, green, blue = args.color
    value = ((red >> 3) << 11) | ((green >> 2) << 5) | (blue >> 3)
    pixel = bytes((value & 0xFF, value >> 8))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(pixel * (args.width * args.height))
    print(f"Generated {args.width}x{args.height}, {args.width * args.height * 2} bytes")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
