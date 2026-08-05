#!/usr/bin/env python3
"""Reject invalid UTF-8 and common mojibake in versioned first-party text files."""

from __future__ import annotations

import subprocess
import sys
from pathlib import Path


BINARY_SUFFIXES = {".exe", ".fbx", ".gz", ".obj", ".png", ".wav", ".zip"}
EXCLUDED_PREFIXES = ("Build/", "Vendor/", "Tools/")
SUSPICIOUS_SEQUENCES = ("\u00c3", "\u00c2", "\u00e2\u20ac", "\ufffd")


def versioned_paths() -> list[Path]:
    output = subprocess.check_output(
        ["git", "ls-files", "-z", "--cached", "--others", "--exclude-standard"]
    )
    return [Path(value.decode("utf-8")) for value in output.split(b"\0") if value]


def main() -> int:
    failures: list[str] = []
    for path in versioned_paths():
        portable = path.as_posix()
        if (
            portable.startswith(EXCLUDED_PREFIXES)
            or path.suffix.lower() in BINARY_SUFFIXES
            or not path.is_file()
        ):
            continue
        raw = path.read_bytes()
        try:
            text = raw.decode("utf-8")
        except UnicodeDecodeError as error:
            failures.append(f"{portable}: invalid UTF-8 at byte {error.start}")
            continue
        for sequence in SUSPICIOUS_SEQUENCES:
            if sequence in text:
                failures.append(
                    f"{portable}: suspicious mojibake sequence {sequence!r}"
                )

    if failures:
        print("Tracked text integrity check failed:", file=sys.stderr)
        for failure in failures:
            print(f"  {failure}", file=sys.stderr)
        return 1
    print("Versioned first-party text is valid UTF-8 with no recognized mojibake.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
