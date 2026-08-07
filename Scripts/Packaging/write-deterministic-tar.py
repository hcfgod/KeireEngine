#!/usr/bin/env python3
"""Write a deterministic gzip-compressed tar archive with explicit POSIX modes."""

from __future__ import annotations

import argparse
import gzip
from pathlib import Path, PurePosixPath
import sys
import tarfile


def normalize_relative(value: str) -> str:
    normalized = value.replace("\\", "/")
    parts = normalized.split("/")
    path = PurePosixPath(normalized)
    if (
        not normalized
        or path.is_absolute()
        or any(part in ("", ".", "..") for part in parts)
        or ":" in parts[0]
    ):
        raise ValueError(f"archive path is not confined: {value}")
    return path.as_posix()


def collect_entries(source: Path) -> list[tuple[str, Path, bool]]:
    entries: list[tuple[str, Path, bool]] = []
    casefolded: set[str] = set()
    for path in source.rglob("*"):
        if path.is_symlink():
            raise ValueError(f"archive source contains a symbolic link: {path}")
        relative = path.relative_to(source).as_posix()
        folded = relative.casefold()
        if folded in casefolded:
            raise ValueError(
                f"archive source contains a portable case collision: {relative}"
            )
        casefolded.add(folded)
        if path.is_dir():
            entries.append((relative, path, True))
        elif path.is_file():
            entries.append((relative, path, False))
        else:
            raise ValueError(f"archive source contains a non-regular entry: {path}")
    return sorted(entries, key=lambda entry: entry[0])


def configure_entry(info: tarfile.TarInfo, mode: int) -> None:
    info.mode = mode
    info.mtime = 0
    info.uid = 0
    info.gid = 0
    info.uname = ""
    info.gname = ""


def write_archive(source: Path, output: Path, executables: set[str]) -> None:
    source = source.resolve(strict=True)
    output = output.resolve(strict=False)
    if not source.is_dir() or source.is_symlink():
        raise ValueError(f"archive source is not a regular directory: {source}")
    if output == source or output.is_relative_to(source):
        raise ValueError("archive output must remain outside the source directory")
    if output.exists():
        raise ValueError(f"archive output already exists: {output}")

    entries = collect_entries(source)
    files = {relative for relative, _, is_directory in entries if not is_directory}
    missing_executables = sorted(executables - files)
    if missing_executables:
        raise ValueError(
            "declared executable is missing from the archive source: "
            + ", ".join(missing_executables)
        )

    output.parent.mkdir(parents=True, exist_ok=True)
    try:
        with output.open("xb") as raw_stream:
            with gzip.GzipFile(
                filename="",
                mode="wb",
                compresslevel=9,
                fileobj=raw_stream,
                mtime=0,
            ) as gzip_stream:
                with tarfile.open(
                    fileobj=gzip_stream,
                    mode="w",
                    format=tarfile.PAX_FORMAT,
                ) as archive:
                    root = tarfile.TarInfo(f"{source.name}/")
                    root.type = tarfile.DIRTYPE
                    configure_entry(root, 0o755)
                    archive.addfile(root)

                    for relative, path, is_directory in entries:
                        name = f"{source.name}/{relative}"
                        info = tarfile.TarInfo(f"{name}/" if is_directory else name)
                        if is_directory:
                            info.type = tarfile.DIRTYPE
                            configure_entry(info, 0o755)
                            archive.addfile(info)
                            continue

                        stat = path.stat()
                        info.type = tarfile.REGTYPE
                        info.size = stat.st_size
                        configure_entry(
                            info, 0o755 if relative in executables else 0o644
                        )
                        with path.open("rb") as input_stream:
                            archive.addfile(info, input_stream)
    except BaseException:
        output.unlink(missing_ok=True)
        raise


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--executable", action="append", default=[])
    args = parser.parse_args()

    try:
        executables = {normalize_relative(value) for value in args.executable}
        if len(executables) != len(args.executable):
            raise ValueError("archive executable declarations must be unique")
        write_archive(args.source, args.output, executables)
    except (OSError, ValueError, tarfile.TarError) as error:
        print(f"deterministic tar error: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
