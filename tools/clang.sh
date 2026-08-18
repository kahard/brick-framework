#!/usr/bin/env bash
set -euo pipefail

EXPECTED_VERSION=17
FORMAT_FLAGS_FORMAT=(-i --verbose)
FORMAT_FLAGS_CHECK=(--dry-run --Werror --verbose)
MODE="${1:-format}"

if ! command -v clang-format >/dev/null 2>&1; then
    echo "Error: clang-format is not installed or not available in PATH" >&2
    exit 1
fi

LOCAL_VERSION=$(clang-format --version | grep -oE '[0-9]+' | head -n1 || true)
if [[ "$LOCAL_VERSION" != "$EXPECTED_VERSION" ]]; then
    echo "Error: clang-format $EXPECTED_VERSION is required, found ${LOCAL_VERSION:-unknown}" >&2
    exit 1
fi

case "$MODE" in
    format)
        FORMAT_FLAGS=("${FORMAT_FLAGS_FORMAT[@]}")
        ;;
    check|format-check)
        FORMAT_FLAGS=("${FORMAT_FLAGS_CHECK[@]}")
        ;;
    *)
        echo "Usage: $0 [format|format-check]" >&2
        exit 1
        ;;
esac

find . \
    \( -path './.git' -o -path './.git/*' -o -path './build' -o -path './build/*' \) -prune -o \
    -type f \( -name '*.c' -o -name '*.cc' -o -name '*.cpp' -o -name '*.cxx' -o -name '*.h' \) -print0 \
    | xargs -0 clang-format "${FORMAT_FLAGS[@]}"
