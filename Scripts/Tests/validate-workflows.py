#!/usr/bin/env python3
"""Parse every GitHub Actions workflow and reject duplicate YAML mapping keys."""

from __future__ import annotations

import sys
from pathlib import Path

try:
    import yaml
except ImportError:
    print("PyYAML is required to validate GitHub Actions workflows.", file=sys.stderr)
    raise SystemExit(2)


class UniqueKeyLoader(yaml.SafeLoader):
    pass


def construct_unique_mapping(
    loader: UniqueKeyLoader, node: yaml.MappingNode, deep: bool = False
) -> dict:
    loader.flatten_mapping(node)
    result = {}
    for key_node, value_node in node.value:
        key = loader.construct_object(key_node, deep=deep)
        if key in result:
            raise yaml.constructor.ConstructorError(
                "while constructing a mapping",
                node.start_mark,
                f"found duplicate key {key!r}",
                key_node.start_mark,
            )
        result[key] = loader.construct_object(value_node, deep=deep)
    return result


UniqueKeyLoader.add_constructor(
    yaml.resolver.BaseResolver.DEFAULT_MAPPING_TAG, construct_unique_mapping
)


def main() -> int:
    failures: list[str] = []
    workflows = sorted(Path(".github/workflows").glob("*.y*ml"))
    if not workflows:
        failures.append(".github/workflows: no workflow files found")
    for path in workflows:
        try:
            document = yaml.load(
                path.read_text(encoding="utf-8"), Loader=UniqueKeyLoader
            )
            if not isinstance(document, dict) or not isinstance(
                document.get("jobs"), dict
            ):
                failures.append(
                    f"{path.as_posix()}: workflow root and jobs must be mappings"
                )
        except (OSError, UnicodeError, yaml.YAMLError) as error:
            failures.append(f"{path.as_posix()}: {error}")

    if failures:
        print("Workflow validation failed:", file=sys.stderr)
        for failure in failures:
            print(f"  {failure}", file=sys.stderr)
        return 1
    print(f"Parsed {len(workflows)} GitHub Actions workflows successfully.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
