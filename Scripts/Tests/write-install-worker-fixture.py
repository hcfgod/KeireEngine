#!/usr/bin/env python3
"""Write the minimal schema-2 package manifest used by Windows installer fault tests."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def encode(document: dict[str, object]) -> bytes:
    return (json.dumps(document, ensure_ascii=False, indent=2) + "\n").encode("utf-8")


def fingerprint(document: dict[str, object]) -> str:
    payload = {
        key: value
        for key, value in document.items()
        if key not in ("manifestFingerprint", "installedSizeBytes")
    }
    canonical = json.dumps(
        payload, ensure_ascii=False, sort_keys=True, separators=(",", ":")
    ).encode("utf-8")
    return hashlib.sha256(canonical).hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--stage", required=True)
    parser.add_argument("--artifact", choices=("editor", "hub"), required=True)
    parser.add_argument("--version", required=True)
    arguments = parser.parse_args()

    stage = Path(arguments.stage).resolve(strict=True)
    manifest_name = f"{arguments.artifact}-package.json"
    manifest_path = stage / manifest_name
    manifest_path.unlink(missing_ok=True)
    installer_generated = {
        manifest_name,
        "Uninstall.exe",
        f".keire-{arguments.artifact}-install",
    }
    files: list[dict[str, object]] = []
    for root, directories, names in os.walk(stage, followlinks=False):
        root_path = Path(root)
        if any((root_path / directory).is_symlink() for directory in directories):
            raise RuntimeError("fixture package contains a linked directory")
        for name in names:
            path = root_path / name
            if path.is_symlink() or not path.is_file():
                raise RuntimeError("fixture package contains a non-regular file")
            relative = path.relative_to(stage).as_posix()
            if relative in installer_generated:
                continue
            files.append(
                {
                    "path": relative,
                    "sizeBytes": path.stat().st_size,
                    "sha256": sha256_file(path),
                }
            )
    files.sort(key=lambda item: str(item["path"]))
    document: dict[str, object] = {
        "schemaVersion": 2,
        "artifact": arguments.artifact,
        "packageId": f"KeireFixture.{arguments.artifact}",
        "version": arguments.version,
        "commit": "installer-fixture",
        "inventoryExcludes": [manifest_name],
        "files": files,
        "installedSizeBytes": sum(int(item["sizeBytes"]) for item in files),
    }
    document["manifestFingerprint"] = fingerprint(document)
    for _ in range(8):
        payload = encode(document)
        installed_size = sum(int(item["sizeBytes"]) for item in files) + len(payload)
        if document["installedSizeBytes"] == installed_size:
            break
        document["installedSizeBytes"] = installed_size
    else:
        raise RuntimeError("fixture manifest size did not converge")
    manifest_path.write_bytes(encode(document))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
