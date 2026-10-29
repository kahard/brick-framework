#!/usr/bin/env python3
"""Format the framework sources with the pinned clang-format 17.

Usage: python tools/format.py [format|check]

Uses clang-format from .venv (`make setup`) and falls back to the one on PATH
only when it is version 17.
"""

from __future__ import annotations

import os
import shutil
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
EXPECTED_MAJOR = "17"
SOURCE_SUFFIXES = {".c", ".cc", ".cpp", ".cxx", ".h"}
SKIPPED_DIRS = {".git", "build", ".venv"}


def find_clang_format() -> str:
    venv = REPO_ROOT / ".venv" / ("Scripts/clang-format.exe" if os.name == "nt" else "bin/clang-format")
    candidate = str(venv) if venv.exists() else shutil.which("clang-format")
    if candidate is None:
        sys.exit("error: clang-format not found; run `make setup`")
    version = subprocess.run([candidate, "--version"], capture_output=True, text=True).stdout
    if f"version {EXPECTED_MAJOR}." not in version:
        sys.exit(f"error: clang-format {EXPECTED_MAJOR} is required, found: {version.strip()}; run `make setup`")
    return candidate


def sources() -> list[str]:
    files = []
    for directory, subdirs, names in os.walk(REPO_ROOT):
        subdirs[:] = sorted(d for d in subdirs if d not in SKIPPED_DIRS)
        files += [os.path.join(directory, n) for n in sorted(names) if os.path.splitext(n)[1] in SOURCE_SUFFIXES]
    return files


def main() -> int:
    mode = sys.argv[1] if len(sys.argv) > 1 else "format"
    if mode not in ("format", "check"):
        sys.exit(__doc__)
    clang_format = find_clang_format()
    flags = ["-i"] if mode == "format" else ["--dry-run", "--Werror"]
    files = sources()
    failed = 0
    # Keep command lines short enough for Windows.
    for start in range(0, len(files), 50):
        failed += subprocess.call([clang_format, *flags, *files[start : start + 50]], cwd=REPO_ROOT) != 0
    print(f"{mode}: {len(files)} files, {'OK' if failed == 0 else 'FAILED'}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
