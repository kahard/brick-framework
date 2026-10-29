#!/usr/bin/env bash
set -euo pipefail

EXPECTED_VERSION=17
MODE="${1:-format}"
if ! command -v clang-format >/dev/null 2>&1; then echo "Error: clang-format is not installed" >&2; exit 1; fi
LOCAL_VERSION=$(clang-format --version | grep -oE '[0-9]+' | head -n1 || true)
if [[ "$LOCAL_VERSION" != "$EXPECTED_VERSION" ]]; then echo "Error: clang-format $EXPECTED_VERSION is required, found ${LOCAL_VERSION:-unknown}" >&2; exit 1; fi
case "$MODE" in
  format) FLAGS=(-i --verbose) ;;
  check|format-check) FLAGS=(--dry-run --Werror --verbose) ;;
  *) echo "Usage: $0 [format|format-check]" >&2; exit 1 ;;
esac
index=0
while IFS= read -r -d '' file; do
  index=$((index + 1))
  echo "Formatting [$index] $file"
  clang-format "${FLAGS[@]}" "$file"
done < <(find . \( -path './.git' -o -path './.git/*' \) -prune -o -type f \( -name '*.c' -o -name '*.cc' -o -name '*.cpp' -o -name '*.cxx' -o -name '*.h' \) -print0)
