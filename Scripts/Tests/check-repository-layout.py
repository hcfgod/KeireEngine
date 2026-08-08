#!/usr/bin/env python3
"""Enforce the repository's first-party directory and C++ file layout."""

from __future__ import annotations

import os
from pathlib import Path


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
EXCLUDED_DIRECTORIES = {
    ".astro",
    ".codex-remote-attachments",
    ".git",
    "Artifacts",
    "Build",
    "Library",
    "Logs",
    "Temp",
    "Tools",
    "Vendor",
    "bin",
    "node_modules",
    "obj",
}
HEADER_EXTENSIONS = {".h", ".hh", ".hpp", ".hxx"}
SOURCE_EXTENSIONS = {".c", ".cc", ".cpp", ".cxx", ".m", ".mm"}
SOURCE_LAYOUT_EXCEPTIONS = {
    # Premake compiles this intentionally empty anchor from its policy directory.
    "Scripts/Premake/ManagedBuildAnchor.cpp",
}
EXPECTED_SOURCE_DIRECTORIES = {
    "Services/KeireDistributionService/Source",
    "Services/KeireDistributionService/DocumentationSite/Source",
}


def has_exact_child(parent: Path, name: str) -> bool:
    return any(child.name == name for child in parent.iterdir())


def main() -> int:
    failures: list[str] = []
    header_count = 0
    source_count = 0

    if not has_exact_child(REPOSITORY_ROOT, "Docs"):
        failures.append(
            "The canonical repository documentation directory must be named exactly 'Docs'."
        )
    if has_exact_child(REPOSITORY_ROOT, "docs"):
        failures.append(
            "A lowercase repository-root 'docs' directory is not allowed; use 'Docs'."
        )

    for expected in sorted(EXPECTED_SOURCE_DIRECTORIES):
        absolute = REPOSITORY_ROOT / expected
        if not absolute.is_dir() or not has_exact_child(absolute.parent, "Source"):
            failures.append(
                f"Required source directory is missing or has incorrect case: {expected}"
            )

    for current, directories, files in os.walk(REPOSITORY_ROOT):
        current_path = Path(current)
        relative_directory = current_path.relative_to(REPOSITORY_ROOT)

        kept_directories: list[str] = []
        for directory in directories:
            if directory in EXCLUDED_DIRECTORIES:
                continue
            relative = relative_directory / directory
            if directory == "src":
                failures.append(
                    f"Lowercase source directory must be named 'Source': {relative.as_posix()}"
                )
            kept_directories.append(directory)
        directories[:] = kept_directories

        for filename in files:
            relative = relative_directory / filename
            relative_path = relative.as_posix()
            suffix = Path(filename).suffix.lower()

            if suffix in HEADER_EXTENSIONS:
                header_count += 1
                if "Include" not in relative.parts:
                    failures.append(
                        f"First-party header must live under an Include directory: {relative_path}"
                    )
                if "Source" in relative.parts:
                    failures.append(
                        f"Header may not live under a Source directory: {relative_path}"
                    )

            if suffix in SOURCE_EXTENSIONS:
                source_count += 1
                if (
                    "Source" not in relative.parts
                    and relative_path not in SOURCE_LAYOUT_EXCEPTIONS
                ):
                    failures.append(
                        f"First-party implementation file must live under a Source directory: {relative_path}"
                    )

    if failures:
        print("Repository layout validation failed:")
        for failure in failures:
            print(f"  - {failure}")
        return 1

    print(
        "Repository layout validation passed: Docs casing, Source directory casing, "
        f"{header_count} headers under Include, and {source_count} implementation files under Source."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
