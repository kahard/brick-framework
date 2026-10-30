#!/usr/bin/env python3
"""Check BRICK embedded coding rules (docs/embedded-coding-rules.md).

Usage:
  python libs/rules/brick_rules.py [--strict] [--summary] PATH...

Every C/C++ source under PATH is classified by its location:

  host     platforms/pc, libs/mocks, tests, tools, pc/   -> not checked
  app      .../src/app/...                                -> all rules
  target   everything else (core, boards, platforms, app entry points)
                                                          -> all rules except
                                                             layer-include

Applications that are not yet split into src/app and src/target are treated
as `app` for every file except main.cpp, Types.h and ApplicationConfig.h.

A finding is suppressed by `// brick-rules: allow(<rule-id>) <reason>` on the
same line or on the line above; the reason is mandatory.
Without --strict the script only reports and exits 0.
"""

from __future__ import annotations

import argparse
import os
import re
import sys
from collections import Counter
from dataclasses import dataclass
from pathlib import Path

SOURCE_SUFFIXES = {".c", ".cc", ".cpp", ".cxx", ".h", ".hpp"}
SKIPPED_DIRS = {".git", ".pio", ".venv", "build", "managed_components", "generated", "node_modules", "fonts"}
HOST_PARTS = (("platforms", "pc"), ("libs", "mocks"), ("tests",), ("tools",), ("pc",))
ENTRY_FILES = {"main.cpp", "Types.h", "ApplicationConfig.h"}

APP_FORBIDDEN_INCLUDE = re.compile(
    r"^(brick/platform/|brick/boards/|esp_|driver/|freertos/|hal/|soc/|rom/|Arduino\.h|TFT_eSPI|stm32|"
    r"sdkconfig\.h|windows\.h|unistd\.h|pthread\.h)"
)


@dataclass(frozen=True)
class Rule:
    rule_id: str
    pattern: re.Pattern
    message: str


RULES = (
    Rule("lambda", re.compile(r"(?<![\w\]\)])\[[&=]?\s*(?:[&=]?\s*\w+\s*,?\s*)*\]\s*(?:\([^;{}]*\)\s*)?(?:mutable\s*)?(?:->\s*[\w:<>]+\s*)?\{"),
         "lambda (rule L1): use a named function, a small class or a C callback with context"),
    Rule("new-delete", re.compile(r"\bnew\s+[\w:]|\bnew\s*\(|\bdelete\s*(\[\s*\])?\s*\w"),
         "new/delete (rule M1): allocate during initialization through owned storage or IMemoryProvider"),
    Rule("malloc", re.compile(r"\b(malloc|calloc|realloc|free|heap_caps_malloc|heap_caps_calloc|heap_caps_free)\s*\("),
         "heap call (rule M1): allocate once during initialization through IMemoryProvider"),
    Rule("std-container", re.compile(r"\bstd::(vector|map|unordered_map|set|unordered_set|list|deque|queue|stack)\b"),
         "allocating container (rule M2): use std::array or a caller-provided buffer"),
    Rule("std-string", re.compile(r"\bstd::(string|wstring|ostringstream|stringstream)\b"),
         "std::string (rule M2): use const char* and fixed char buffers"),
    Rule("std-function", re.compile(r"\bstd::(function|shared_ptr|make_shared|make_unique|bind)\b"),
         "std::function / shared ownership (rule M2): use an interface or a C callback with context"),
    Rule("exceptions", re.compile(r"\b(throw|try|catch)\b|\b(dynamic_cast|typeid)\b"),
         "exceptions / RTTI (rule L2): report errors through return values"),
    Rule("double", re.compile(r"\bdouble\b"),
         "double (rule L3): use integers, fixed point or float"),
    Rule("static-local", re.compile(r"^[ \t]+static\s+(?!constexpr\b|const\b|inline\b)[\w:<>, ]+?[\s*&]+\w+\s*(?:\[[^\]]*\]\s*)?[;={]"),
         "function-local static (rule L4): make the object a member of its owner"),
)
# Rules that only make sense inside a function body.
FUNCTION_SCOPE_RULES = {"static-local"}
TYPE_SCOPE = re.compile(r"\b(class|struct|union|namespace|enum)\b[^;(){}]*$|extern\s+\"C\"\s*$")


def function_scopes(code_lines: list[str]) -> list[bool]:
    """For every line, whether it starts inside a function (or other code) block."""
    stack: list[bool] = []
    pending = ""
    result = []
    for line in code_lines:
        result.append(bool(stack) and stack[-1])
        for c in line:
            if c == "{":
                in_code = bool(stack) and stack[-1]
                stack.append(in_code or not TYPE_SCOPE.search(pending.strip()))
                pending = ""
            elif c == "}":
                if stack:
                    stack.pop()
                pending = ""
            elif c == ";":
                pending = ""
            else:
                pending += c
        pending += " "
    return result
LAYER_RULE_ID = "layer-include"
SUPPRESS = re.compile(r"brick-rules:\s*allow\(([\w,\s-]+)\)\s*(\S.*)?")
INCLUDE = re.compile(r'^\s*#\s*include\s*[<"]([^">]+)[">]')


def strip_code(text: str) -> list[str]:
    """Blank out comments and string/char literals, keeping line structure."""
    out, i, n = [], 0, len(text)
    state = "code"
    buf = []
    while i < n:
        c = text[i]
        nxt = text[i + 1] if i + 1 < n else ""
        if state == "code":
            if c == "/" and nxt == "/":
                state = "line"
                i += 2
                continue
            if c == "/" and nxt == "*":
                state = "block"
                i += 2
                continue
            if c == '"' or (c == "'" and not (buf and (buf[-1].isalnum()))):
                state = c
                buf.append(c)
                i += 1
                continue
            buf.append(c)
        elif state == "line":
            if c == "\n":
                state = "code"
                buf.append(c)
        elif state == "block":
            if c == "*" and nxt == "/":
                state = "code"
                i += 2
                continue
            if c == "\n":
                buf.append(c)
        else:  # inside a literal
            if c == "\\":
                i += 2
                continue
            if c == state:
                state = "code"
                buf.append(c)
            elif c == "\n":
                state = "code"
                buf.append(c)
        i += 1
    out = "".join(buf).split("\n")
    return out


def classify(path: Path) -> str:
    parts = path.parts
    for host in HOST_PARTS:
        for index in range(len(parts) - len(host) + 1):
            if tuple(parts[index : index + len(host)]) == host:
                return "host"
    if "src" not in parts:
        return "target"
    src_index = len(parts) - 1 - parts[::-1].index("src")
    below = parts[src_index + 1 :]
    project = Path(*parts[:src_index])
    if below[0] == "app":
        return "app"
    if below[0] == "target" or (project / "src" / "app").is_dir():
        return "target"
    if (project / "platformio.ini").exists():
        # Application not yet split into src/app and src/target.
        return "target" if path.name in ENTRY_FILES else "app"
    return "target"


def suppressed(raw_lines: list[str], index: int, rule_id: str) -> bool:
    for line in (raw_lines[index], raw_lines[index - 1] if index > 0 else ""):
        match = SUPPRESS.search(line)
        if match and match.group(2) and rule_id in [r.strip() for r in match.group(1).split(",")]:
            return True
    return False


def check_file(path: Path):
    layer = classify(path)
    if layer == "host":
        return layer, []
    raw = path.read_text(encoding="utf-8", errors="replace")
    raw_lines = raw.split("\n")
    code_lines = strip_code(raw)
    in_function = function_scopes(code_lines)
    findings = []
    for index, line in enumerate(code_lines):
        if layer == "app":
            include = INCLUDE.match(raw_lines[index])
            if include and APP_FORBIDDEN_INCLUDE.match(include.group(1)) and not suppressed(raw_lines, index, LAYER_RULE_ID):
                findings.append((index + 1, LAYER_RULE_ID,
                                 f"application layer includes <{include.group(1)}> (rule A1): take an interface instead"))
        if line.lstrip().startswith("#"):
            continue
        for rule in RULES:
            if rule.rule_id in FUNCTION_SCOPE_RULES and not in_function[index]:
                continue
            # Rule L5: a static object inside the entry function is the accepted way
            # to give the application static storage.
            if rule.rule_id == "static-local" and path.name == "main.cpp":
                continue
            if rule.pattern.search(line) and not suppressed(raw_lines, index, rule.rule_id):
                findings.append((index + 1, rule.rule_id, rule.message))
    return layer, findings


def sources(paths):
    for root in paths:
        root = Path(root)
        if root.is_file():
            yield root
            continue
        for directory, subdirs, files in os.walk(root):
            subdirs[:] = sorted(d for d in subdirs if d not in SKIPPED_DIRS)
            for name in sorted(files):
                if Path(name).suffix in SOURCE_SUFFIXES and not name.startswith("generated"):
                    yield Path(directory) / name


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("paths", nargs="+")
    parser.add_argument("--strict", action="store_true", help="exit 1 when any rule is violated")
    parser.add_argument("--summary", action="store_true", help="print only the per-rule totals")
    args = parser.parse_args()

    totals = Counter()
    files_checked = 0
    for path in sources(args.paths):
        layer, findings = check_file(path)
        if layer == "host":
            continue
        files_checked += 1
        for line, rule_id, message in findings:
            totals[rule_id] += 1
            if not args.summary:
                print(f"{path.as_posix()}:{line}: [{rule_id}] {message}")
    print(f"\nbrick-rules: {files_checked} files, {sum(totals.values())} findings")
    for rule_id, count in totals.most_common():
        print(f"  {rule_id:15} {count}")
    return 1 if args.strict and totals else 0


if __name__ == "__main__":
    sys.exit(main())
