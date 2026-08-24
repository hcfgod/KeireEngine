#!/usr/bin/env python3
from __future__ import annotations

import argparse
import re
from pathlib import Path


COMPILE_RULE = re.compile(r"^(cc|cxx|pch)_[A-Za-z0-9_-]+$")
COMMAND = re.compile(r"^(\s*command\s*=\s*)(.*)$")
COMPILER = re.compile(r"^(cl|clang-cl|gcc|g\+\+|clang|clang\+\+)(\s|$)", re.IGNORECASE)


def patch_file(path: Path, cache: str) -> int:
    original = path.read_text(encoding="utf-8")
    lines = original.splitlines(keepends=True)
    rule = ""
    patched = 0
    for index, line in enumerate(lines):
        stripped = line.strip()
        if stripped.startswith("rule "):
            rule = stripped[5:]
            continue
        match = COMMAND.match(line.rstrip("\r\n"))
        if not match or not COMPILE_RULE.match(rule):
            continue
        command = match.group(2)
        if cache == "sccache" and command.startswith("sccache "):
            patched += 1
        elif cache == "sccache" and COMPILER.match(command):
            ending = line[len(line.rstrip("\r\n")) :]
            lines[index] = f"{match.group(1)}sccache {command}{ending}"
            patched += 1
    updated = "".join(lines)
    if updated != original:
        with path.open("w", encoding="utf-8", newline="") as output:
            output.write(updated)
    return patched


def main() -> int:
    parser = argparse.ArgumentParser(description="Configure sccache as the generated Ninja compiler launcher.")
    parser.add_argument("root", type=Path)
    parser.add_argument("--cache", choices=("off", "sccache"), required=True)
    arguments = parser.parse_args()
    if arguments.cache == "off":
        return 0

    patched = 0
    for path in sorted(arguments.root.rglob("*.ninja")):
        if any(part in {"Dependencies", "Vendor", "Tools"} for part in path.parts):
            continue
        patched += patch_file(path, arguments.cache)
    if patched == 0:
        raise RuntimeError("No generated Ninja compile rules accepted the requested compiler cache.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
