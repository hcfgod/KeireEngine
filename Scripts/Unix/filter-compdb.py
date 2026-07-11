#!/usr/bin/env python3
import json
import pathlib
import sys


def main() -> int:
    source = pathlib.Path(sys.argv[1])
    destination = pathlib.Path(sys.argv[2])
    commands = json.loads(source.read_text(encoding="utf-8"))
    debug_commands = [
        command
        for command in commands
        if "/Debug-" in command["output"].replace("\\", "/")
    ]
    destination.write_text(json.dumps(debug_commands, indent=2) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
