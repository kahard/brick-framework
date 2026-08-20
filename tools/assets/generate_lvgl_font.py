#!/usr/bin/env python3
"""Generate an LVGL C font asset using lv_font_conv.

The generated font is intentionally uncompressed and unprefiltered. This is
the format used by the LVGL fonts shipped with the PlatformIO package and is a
safe default for embedded targets.
"""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path


DEFAULT_RANGES = ("0x20-0x7F", "0xA0-0xFF", "0x0100-0x017F")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--font", required=True, type=Path, help="Input TTF/OTF/WOFF font")
    parser.add_argument("--output", required=True, type=Path, help="Output LVGL .c file")
    parser.add_argument("--name", required=True, help="C identifier for the generated font")
    parser.add_argument("--size", required=True, type=int, help="Font size in pixels")
    parser.add_argument("--bpp", choices=("1", "2", "3", "4", "8"), default="4")
    parser.add_argument(
        "--range",
        dest="ranges",
        action="append",
        default=None,
        help="Unicode range, repeatable; defaults to Latin-1 and Latin Extended-A",
    )
    parser.add_argument("--lv-include", default="lvgl.h", help="LVGL include used by generated C")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if not args.font.is_file():
        print(f"error: font file does not exist: {args.font}", file=sys.stderr)
        return 2
    if args.size <= 0:
        print("error: --size must be positive", file=sys.stderr)
        return 2

    output = args.output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    npx = shutil.which("npx.cmd" if os.name == "nt" else "npx")
    if npx is None:
        print("error: npx was not found; install Node.js and npm", file=sys.stderr)
        return 2

    command = [
        npx,
        "--yes",
        "lv_font_conv",
        "--font",
        str(args.font.resolve()),
        "--size",
        str(args.size),
        "--bpp",
        args.bpp,
    ]
    for unicode_range in args.ranges or DEFAULT_RANGES:
        command.extend(("--range", unicode_range))
    command.extend(
        (
            "--format",
            "lvgl",
            "-o",
            str(output),
            "--lv-font-name",
            args.name,
            "--lv-include",
            args.lv_include,
            "--no-compress",
            "--no-prefilter",
            "--force-fast-kern-format",
        )
    )

    print("Generating LVGL font:")
    print(" ".join(command))
    return subprocess.run(command, check=False).returncode


if __name__ == "__main__":
    raise SystemExit(main())
