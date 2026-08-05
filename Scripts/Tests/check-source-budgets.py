#!/usr/bin/env python3
"""Enforce first-party source-file line budgets and non-growth ceilings."""

from __future__ import annotations

import json
import sys
from pathlib import Path


CONFIGURATION = Path("Config/SourceFileBudgets.json")


def main() -> int:
    configuration = json.loads(CONFIGURATION.read_text(encoding="utf-8"))
    if configuration.get("schemaVersion") != 1:
        print(f"{CONFIGURATION}: unsupported schema version", file=sys.stderr)
        return 1

    extensions = set(configuration["extensions"])
    test_roots = set(configuration["testRoots"])
    exceptions = configuration["exceptions"]
    observed: set[str] = set()
    failures: list[str] = []
    checked = 0

    for root_name in configuration["roots"]:
        root = Path(root_name)
        if not root.is_dir():
            failures.append(f"{root_name}: configured source root is missing")
            continue
        default = (
            configuration["testMaximumLines"]
            if root_name in test_roots
            else configuration["defaultMaximumLines"]
        )
        for path in root.rglob("*"):
            if not path.is_file() or path.suffix.lower() not in extensions:
                continue
            checked += 1
            portable = path.as_posix()
            lines = len(path.read_text(encoding="utf-8").splitlines())
            limit = exceptions.get(portable, default)
            if portable in exceptions:
                observed.add(portable)
                if lines <= default:
                    failures.append(
                        f"{portable}: remove the stale exception now that the file is within the {default}-line budget"
                    )
                elif lines < limit:
                    failures.append(
                        f"{portable}: lower its non-growth ceiling from {limit} to the observed {lines} lines"
                    )
            if lines > limit:
                failures.append(
                    f"{portable}: {lines} lines exceeds its {limit}-line budget"
                )

    for path in sorted(set(exceptions) - observed):
        failures.append(
            f"{path}: configured source-budget exception is missing or no longer scanned"
        )

    if failures:
        print("Source-file budget validation failed:", file=sys.stderr)
        for failure in failures:
            print(f"  {failure}", file=sys.stderr)
        return 1
    print(f"Checked {checked} first-party source files against enforced line budgets.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
