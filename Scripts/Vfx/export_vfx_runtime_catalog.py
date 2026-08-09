#!/usr/bin/env python3
"""Export Kéire's runtime VFX node catalog contract as canonical JSON."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

from runtime_vfx_catalog import (
    DEFAULT_CONTRACT,
    export_runtime_catalog,
    load_runtime_catalog,
)


def arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--contract",
        type=Path,
        default=DEFAULT_CONTRACT,
        help="Runtime catalog contract to export.",
    )
    parser.add_argument(
        "--output",
        type=Path,
        help="Optional JSON destination; stdout is used when omitted.",
    )
    parser.add_argument(
        "--check",
        action="store_true",
        help="Fail instead of rewriting a stale --output file.",
    )
    return parser.parse_args()


def main() -> int:
    options = arguments()
    if options.check and options.output is None:
        print("--check requires --output", file=sys.stderr)
        return 2
    try:
        encoded = json.dumps(
            export_runtime_catalog(load_runtime_catalog(options.contract)),
            ensure_ascii=False,
            indent=2,
        )
        encoded += "\n"
        if options.output is None:
            sys.stdout.write(encoded)
        elif options.check:
            if (
                not options.output.is_file()
                or options.output.read_text(encoding="utf-8") != encoded
            ):
                print(
                    f"Kéire VFX runtime catalog export is stale: {options.output}",
                    file=sys.stderr,
                )
                return 1
            print(f"Kéire VFX runtime catalog export is current: {options.output}")
        else:
            options.output.parent.mkdir(parents=True, exist_ok=True)
            with options.output.open("w", encoding="utf-8", newline="\n") as stream:
                stream.write(encoded)
            print(f"Wrote {options.output}")
        return 0
    except (OSError, ValueError) as error:
        print(f"Kéire VFX runtime catalog export failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
