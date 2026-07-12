#!/usr/bin/env python3
"""Codebase audit helpers for Simple cleanup work.

This script reports drift/duplication signals. It is intentionally conservative:
it reports findings but does not rewrite files.
"""

from __future__ import annotations

import argparse
import collections
import pathlib
import re
import sys
from typing import Iterable

ROOT_DIRS = ["Lang", "VM", "Byte", "Library", "LSP", "CLI", "Tests"]
SOURCE_SUFFIXES = {".cpp", ".h", ".hpp", ".cc", ".cxx"}


def iter_files(root: pathlib.Path, suffixes: set[str]) -> Iterable[pathlib.Path]:
    for name in ROOT_DIRS:
        base = root / name
        if not base.exists():
            continue
        for path in base.rglob("*"):
            if path.is_file() and path.suffix in suffixes:
                parts = set(path.parts)
                if "build" in parts or ".git" in parts:
                    continue
                yield path


def read_text(path: pathlib.Path) -> str:
    return path.read_text(encoding="utf-8", errors="ignore")


def report_largest_files(root: pathlib.Path, limit: int) -> None:
    rows = []
    for path in iter_files(root, SOURCE_SUFFIXES):
        rows.append((len(read_text(path).splitlines()), path))
    print("\nlargest files")
    for count, path in sorted(rows, reverse=True)[:limit]:
        print(f"  {count:6d} {path.as_posix()}")


def _mask_cpp_non_code(text: str) -> str:
    """Preserve newlines/columns while masking C++ comments and quoted literals."""
    chars = list(text)
    i = 0
    while i < len(chars):
        if text.startswith("//", i):
            end = text.find("\n", i + 2)
            end = len(text) if end < 0 else end
        elif text.startswith("/*", i):
            close = text.find("*/", i + 2)
            end = len(text) if close < 0 else close + 2
        elif chars[i] in {'"', "'"}:
            quote = chars[i]
            end = i + 1
            while end < len(chars):
                if chars[end] == "\\":
                    end += 2
                    continue
                end += 1
                if chars[end - 1] == quote:
                    break
        else:
            i += 1
            continue
        for index in range(i, end):
            if chars[index] != "\n":
                chars[index] = " "
        i = end
    return "".join(chars)


def report_largest_functions(root: pathlib.Path, limit: int) -> None:
    rows: list[tuple[int, pathlib.Path, int, str]] = []
    control_names = {"if", "for", "while", "switch", "catch"}
    name_re = re.compile(r"((?:[A-Za-z_]\w*::)*(?:operator\s*[^\s(]+|~?[A-Za-z_]\w*))\s*\(")
    for path in iter_files(root, {".cpp", ".cc", ".cxx"}):
        masked = _mask_cpp_non_code(read_text(path))
        lines = masked.splitlines(keepends=True)
        offset = 0
        covered_until = 0
        for line_no, line in enumerate(lines, 1):
            brace_column = line.find("{")
            if offset < covered_until or brace_column < 0 or ";" in line[:brace_column]:
                offset += len(line)
                continue
            signature = "".join(lines[max(0, line_no - 6):line_no])
            matches = list(name_re.finditer(signature))
            if not matches:
                offset += len(line)
                continue
            name = matches[-1].group(1).split("::")[-1]
            if name in control_names:
                offset += len(line)
                continue
            brace = offset + brace_column
            depth = 0
            end = brace
            for end in range(brace, len(masked)):
                if masked[end] == "{":
                    depth += 1
                elif masked[end] == "}":
                    depth -= 1
                    if depth == 0:
                        break
            if depth == 0:
                line_count = masked.count("\n", brace, end) + 1
                rows.append((line_count, path, line_no, name))
                covered_until = end + 1
            offset += len(line)
    print("\nlargest functions")
    for count, path, line, name in sorted(rows, key=lambda row: row[0], reverse=True)[:limit]:
        print(f"  {count:6d} {path.as_posix()}:{line} {name}")


def report_duplicate_blocks(root: pathlib.Path, limit: int, block_lines: int = 8) -> None:
    occurrences: dict[tuple[str, ...], list[tuple[pathlib.Path, int]]] = collections.defaultdict(list)
    for path in iter_files(root, SOURCE_SUFFIXES):
        masked_lines = _mask_cpp_non_code(read_text(path)).splitlines()
        normalized = [re.sub(r"\s+", " ", line).strip() for line in masked_lines]
        for index in range(0, len(normalized) - block_lines + 1):
            block = tuple(normalized[index:index + block_lines])
            meaningful = [line for line in block if line not in {"", "{", "}", "};"}]
            if len(meaningful) < block_lines // 2:
                continue
            occurrences[block].append((path, index + 1))

    rows: list[tuple[int, tuple[str, ...], list[tuple[pathlib.Path, int]]]] = []
    for block, locations in occurrences.items():
        distinct: list[tuple[pathlib.Path, int]] = []
        for location in locations:
            if all(location[0] != prior[0] or abs(location[1] - prior[1]) >= block_lines
                   for prior in distinct):
                distinct.append(location)
        if len(distinct) > 1:
            rows.append((len(distinct), block, distinct))

    print(f"\nduplicate {block_lines}-line blocks")
    if not rows:
        print("  none")
        return
    displayed = 0
    seen_families: set[tuple[tuple[str, int], ...]] = set()
    for count, block, locations in sorted(rows, key=lambda row: row[0], reverse=True):
        first_line = locations[0][1]
        family = tuple((path.as_posix(), line - first_line) for path, line in locations[:4])
        if family in seen_families:
            continue
        seen_families.add(family)
        first_path = locations[0][0]
        sample = next((line for line in block if line not in {"", "{", "}", "};"}), "")
        print(f"  {count:5d} first={first_path.as_posix()}:{first_line} sample={sample[:100]}")
        for path, line in locations[1:4]:
            print(f"        also={path.as_posix()}:{line}")
        displayed += 1
        if displayed == limit:
            break


def report_repeated_strings(root: pathlib.Path, limit: int) -> None:
    counts: collections.Counter[str] = collections.Counter()
    locations: dict[str, pathlib.Path] = {}
    string_re = re.compile(r'"([^"\\\n]|\\.){3,}"')
    for path in iter_files(root, SOURCE_SUFFIXES):
        for match in string_re.finditer(read_text(path)):
            literal = match.group(0)
            counts[literal] += 1
            locations.setdefault(literal, path)
    print("\nrepeated string literals")
    for literal, count in counts.most_common(limit):
        if count < 2:
            break
        print(f"  {count:5d} {literal[:100]}  first={locations[literal].as_posix()}")


def report_repeated_numbers(root: pathlib.Path, limit: int) -> None:
    counts: collections.Counter[str] = collections.Counter()
    locations: dict[str, pathlib.Path] = {}
    number_re = re.compile(r"\b[0-9]{2,}\b|0x[0-9A-Fa-f]{2,}")
    for path in iter_files(root, SOURCE_SUFFIXES):
        for match in number_re.finditer(read_text(path)):
            literal = match.group(0)
            counts[literal] += 1
            locations.setdefault(literal, path)
    print("\nrepeated numeric literals")
    for literal, count in counts.most_common(limit):
        if count < 2:
            break
        print(f"  {count:5d} {literal:>12}  first={locations[literal].as_posix()}")


def report_duplicate_test_names(root: pathlib.Path) -> bool:
    ok = True
    array_re = re.compile(r"static\s+const\s+TestCase\s+(\w+)\s*\[\]\s*=\s*\{")
    entry_re = re.compile(r'\{\s*"([^"]+)"\s*,')
    print("\nduplicate test names")
    for path in (root / "Tests" / "tests").rglob("*.cpp"):
        active = None
        seen: dict[str, int] = {}
        for line_no, line in enumerate(read_text(path).splitlines(), 1):
            if active is None:
                match = array_re.search(line)
                if match:
                    active = match.group(1)
                    seen = {}
                continue
            if "};" in line:
                active = None
                seen = {}
                continue
            match = entry_re.search(line)
            if not match:
                continue
            name = match.group(1)
            if name in seen:
                print(f"  {path.as_posix()}:{line_no}: {active}: {name} first={seen[name]}")
                ok = False
            else:
                seen[name] = line_no
    if ok:
        print("  none")
    return ok


def report_fixture_drift(root: pathlib.Path) -> bool:
    test_sources = "\n".join(read_text(path) for path in (root / "Tests" / "tests").rglob("*.cpp"))
    ok = True
    print("\nunreferenced fixtures")
    for folder in [root / "Tests" / "simple", root / "Tests" / "simple_bad"]:
        if not folder.exists():
            continue
        for path in sorted(folder.rglob("*.simple")):
            rel = path.relative_to(root).as_posix()
            if rel not in test_sources:
                print(f"  {rel}")
                ok = False
    if ok:
        print("  none")
    return ok


def report_stale_diagnostics(root: pathlib.Path) -> bool:
    stale_re = re.compile(r"(?<!\.)\bIO\.print|(?<!\.)\bIO\.buffer|(?<!\.)\bDL\.open|(?<!\.)\bFile\.(open|close|write)")
    ok = True
    print("\nstale diagnostic/library substrings")
    for path in iter_files(root, SOURCE_SUFFIXES):
        if "Tests/tests/test_audit.cpp" in path.as_posix():
            continue
        for line_no, line in enumerate(read_text(path).splitlines(), 1):
            if stale_re.search(line):
                print(f"  {path.relative_to(root).as_posix()}:{line_no}: {line.strip()[:160]}")
                ok = False
    if ok:
        print("  none")
    return ok


def report_legacy_imports(root: pathlib.Path) -> bool:
    legacy_re = re.compile(r"^\s*import\s+(IO|FS|DL|Time|Buffer|Channel)\b")
    ok = True
    print("\nlegacy public alias imports in fixtures")
    for path in (root / "Tests").rglob("*.simple"):
        for line_no, line in enumerate(read_text(path).splitlines(), 1):
            if legacy_re.search(line):
                print(f"  {path.relative_to(root).as_posix()}:{line_no}: {line.strip()}")
                ok = False
    if ok:
        print("  none")
    return ok


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", default=".")
    parser.add_argument("--limit", type=int, default=25)
    parser.add_argument("--fail-on-drift", action="store_true")
    args = parser.parse_args()

    root = pathlib.Path(args.root).resolve()
    report_largest_files(root, args.limit)
    report_largest_functions(root, args.limit)
    report_duplicate_blocks(root, args.limit)
    report_repeated_strings(root, args.limit)
    report_repeated_numbers(root, args.limit)
    ok = True
    ok &= report_duplicate_test_names(root)
    ok &= report_fixture_drift(root)
    ok &= report_legacy_imports(root)
    ok &= report_stale_diagnostics(root)
    return 1 if args.fail_on_drift and not ok else 0


if __name__ == "__main__":
    sys.exit(main())
