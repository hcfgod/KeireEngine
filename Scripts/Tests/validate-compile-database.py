#!/usr/bin/env python3
"""Require every Premake-owned first-party translation unit in compile_commands.json."""

from __future__ import annotations

import json
import sys
from pathlib import Path


DATABASE = Path("compile_commands.json")
ROOTS = (
    "AssetTool",
    "KeireAssetWorker",
    "KeireClient",
    "KeireCore",
    "KeireEditorTests",
    "KeireHub",
    "KeireHubPackagePublisher",
    "KeireHubRuntime",
    "KeireHubTests",
    "KeireHubWorker",
    "KeireRenderTests",
    "KeireRuntime",
    "KeireTests",
    "Scripts/Premake",
    "SourceModules",
)


def command_path(entry: dict[str, object]) -> Path:
    source = Path(str(entry["file"]))
    if not source.is_absolute():
        source = Path(str(entry["directory"])) / source
    return source.resolve()


def main() -> int:
    try:
        entries = json.loads(DATABASE.read_text(encoding="utf-8-sig"))
        if not isinstance(entries, list):
            raise ValueError("root must be an array")
        compiled = {command_path(entry) for entry in entries if isinstance(entry, dict)}
    except (OSError, UnicodeError, ValueError, KeyError, TypeError) as error:
        print(f"{DATABASE}: invalid compilation database: {error}", file=sys.stderr)
        return 1

    expected = {
        path.resolve()
        for root_name in ROOTS
        for path in Path(root_name).rglob("*.cpp")
        if path.is_file()
    }
    missing = sorted(expected - compiled)
    if missing:
        print(
            "Compilation database omits first-party translation units:", file=sys.stderr
        )
        for path in missing:
            print(f"  {path.relative_to(Path.cwd())}", file=sys.stderr)
        return 1
    print(
        f"Compilation database covers all {len(expected)} Premake-owned translation units."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
