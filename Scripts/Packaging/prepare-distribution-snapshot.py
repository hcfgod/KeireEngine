#!/usr/bin/env python3
"""Prepare exact package-catalog bytes and a content-addressed package for offline signing."""

from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import tempfile


SHA256 = re.compile(r"^[0-9a-f]{64}$")
KEY_ID = re.compile(r"^ed25519-[0-9a-f]{32}$")
IDENTITY = re.compile(r"^[a-z0-9][a-z0-9._-]{0,63}$")


def read_json(path: Path, maximum: int) -> dict[str, object]:
    if not path.is_file() or path.is_symlink() or path.stat().st_size > maximum:
        raise ValueError(f"JSON input is missing, unsafe, or oversized: {path}")
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise ValueError(f"JSON input must be an object: {path}")
    return value


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def parse_expiry(value: str) -> str:
    if not value.endswith("Z"):
        raise ValueError("Catalog expiry must be a UTC timestamp ending in Z.")
    parsed = datetime.fromisoformat(value[:-1] + "+00:00")
    if parsed.tzinfo != timezone.utc or parsed <= datetime.now(timezone.utc):
        raise ValueError("Catalog expiry must be a future UTC timestamp.")
    return value


def write_atomic(path: Path, value: dict[str, object]) -> None:
    data = (json.dumps(value, ensure_ascii=False, indent=2) + "\n").encode("utf-8")
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary_name = tempfile.mkstemp(prefix=f".{path.name}.", suffix=".tmp", dir=path.parent)
    try:
        with os.fdopen(descriptor, "wb") as stream:
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary_name, path)
    except BaseException:
        Path(temporary_name).unlink(missing_ok=True)
        raise


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package-manifest", required=True, type=Path)
    parser.add_argument("--package", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--key-id", required=True)
    parser.add_argument("--sequence", required=True, type=int)
    parser.add_argument("--expires-at", required=True, type=parse_expiry)
    args = parser.parse_args()

    manifest = read_json(args.package_manifest, 8 * 1024 * 1024)
    required = {
        "schemaVersion",
        "packageId",
        "version",
        "type",
        "displayName",
        "channel",
        "platform",
        "architecture",
        "artifact",
        "installedSizeBytes",
        "files",
        "signatureKeyId",
    }
    optional = {"engineCompatibility", "dependencies", "conflicts", "licenses"}
    artifact = manifest.get("artifact")
    if (
        not required.issubset(manifest)
        or not set(manifest).issubset(required | optional)
        or manifest["schemaVersion"] != 1
        or manifest["type"] != "editor"
        or not isinstance(artifact, dict)
        or set(artifact) != {"sizeBytes", "sha256"}
        or not isinstance(manifest["files"], list)
        or not manifest["files"]
        or not isinstance(manifest["installedSizeBytes"], int)
        or isinstance(manifest["installedSizeBytes"], bool)
        or manifest["installedSizeBytes"] < 1
    ):
        raise ValueError("Package manifest is not a complete schema-1 editor manifest.")
    if args.sequence < 1 or args.sequence > 2**63 - 1:
        raise ValueError("Catalog sequence must be between 1 and 2^63-1.")
    if not KEY_ID.fullmatch(args.key_id) or manifest["signatureKeyId"] != args.key_id:
        raise ValueError("Catalog key ID does not match the package manifest signing key ID.")
    channel = str(manifest["channel"])
    platform = str(manifest["platform"])
    architecture = str(manifest["architecture"])
    if (
        not isinstance(manifest["packageId"], str)
        or not IDENTITY.fullmatch(manifest["packageId"])
        or not all(IDENTITY.fullmatch(value) for value in (channel, platform, architecture))
    ):
        raise ValueError("Package channel, platform, or architecture is invalid.")
    if (
        not args.package.is_file()
        or args.package.is_symlink()
        or not isinstance(artifact["sizeBytes"], int)
        or isinstance(artifact["sizeBytes"], bool)
        or args.package.stat().st_size != artifact["sizeBytes"]
        or not SHA256.fullmatch(str(artifact["sha256"]))
        or sha256_file(args.package) != artifact["sha256"]
    ):
        raise ValueError("Package bytes do not match the package manifest artifact identity.")

    output = args.output.resolve()
    if output.exists():
        raise ValueError(f"Prepared snapshot output already exists: {output}")
    parent = output.parent
    parent.mkdir(parents=True, exist_ok=True)
    staging = Path(tempfile.mkdtemp(prefix=f".{output.name}.", suffix=".tmp", dir=parent))
    try:
        catalog_path = staging / "catalogs" / channel / platform / f"{architecture}.json"
        package_path = staging / "packages" / str(artifact["sha256"])
        write_atomic(
            catalog_path,
            {
                "schemaVersion": 1,
                "keyId": args.key_id,
                "sequence": args.sequence,
                "expiresAt": args.expires_at,
                "channel": channel,
                "platform": platform,
                "architecture": architecture,
                "packages": [manifest],
            },
        )
        package_path.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(args.package, package_path)
        if sha256_file(package_path) != artifact["sha256"]:
            raise ValueError("Prepared package copy failed digest verification.")
        os.replace(staging, output)
    except BaseException:
        shutil.rmtree(staging, ignore_errors=True)
        raise
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
