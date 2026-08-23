#!/usr/bin/env python3
"""Repair known Premake Ninja compiler-rule and PCH-path defects."""

from __future__ import annotations

import argparse
import re
from pathlib import Path


MSVC_PCH_PATH = re.compile(r"/Fp(?:\.\./)+Build/")


def patch_rule_block(block: list[str], source: Path) -> int:
    if not any(line.strip() == "deps = gcc" for line in block):
        return 0
    if not any(line.strip() == "depfile = $out.d" for line in block):
        raise ValueError(f"{source}: GCC-style dependency rule has no '$out.d' depfile")

    command_index = next(
        (
            index
            for index, line in enumerate(block)
            if line.lstrip().startswith("command = ")
        ),
        None,
    )
    if command_index is None:
        raise ValueError(f"{source}: GCC-style dependency rule has no command")
    command = block[command_index]
    if "-MF $out.d" in command:
        return 0
    compile_suffix = " -c $in -o $out"
    if compile_suffix not in command:
        raise ValueError(
            f"{source}: cannot add dependency output to compiler rule: {command.strip()}"
        )
    block[command_index] = command.replace(
        compile_suffix, " -MD -MF $out.d" + compile_suffix, 1
    )
    return 1


def patch_project(path: Path) -> int:
    lines = path.read_text(encoding="utf-8").splitlines(keepends=True)
    patched = 0
    rule_start: int | None = None
    for index, line in enumerate(lines + ["rule __end__\n"]):
        if not line.startswith("rule "):
            continue
        if rule_start is not None:
            block = lines[rule_start:index]
            patched += patch_rule_block(block, path)
            lines[rule_start:index] = block
        rule_start = index
    for index, line in enumerate(lines):
        repaired = MSVC_PCH_PATH.sub("/FpBuild/", line)
        if repaired != line:
            lines[index] = repaired
            patched += 1
    if patched:
        with path.open("w", encoding="utf-8", newline="") as stream:
            stream.write("".join(lines))
    return patched


def project_files(root: Path) -> list[Path]:
    workspace = root / "build.ninja"
    if not workspace.is_file():
        raise ValueError(f"Ninja workspace was not found: {workspace}")
    projects: list[Path] = []
    for line in workspace.read_text(encoding="utf-8").splitlines():
        if line.startswith("subninja "):
            project = root / line.removeprefix("subninja ").strip()
            if not project.is_file():
                raise ValueError(f"Ninja subproject was not found: {project}")
            projects.append(project)
    if not projects:
        raise ValueError(f"Ninja workspace declares no subprojects: {workspace}")
    return projects


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("root", type=Path)
    arguments = parser.parse_args()
    root = arguments.root.resolve()
    projects = project_files(root)
    patched = sum(patch_project(project) for project in projects)
    print(
        f"Validated Ninja dependency and PCH paths in {len(projects)} subprojects; patched {patched} entries."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
