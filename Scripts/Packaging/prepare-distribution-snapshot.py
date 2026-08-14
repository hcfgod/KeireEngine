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
from typing import NamedTuple


SHA256 = re.compile(r"^[0-9a-f]{64}$")
KEY_ID = re.compile(r"^ed25519-[0-9a-f]{32}$")
IDENTITY = re.compile(r"^[a-z0-9][a-z0-9._-]{0,63}$")


class PackageInput(NamedTuple):
    manifest: dict[str, object]
    manifest_path: Path
    manifest_size: int
    manifest_sha256: str
    package: Path
    channel: str
    platform: str
    architecture: str


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
    descriptor, temporary_name = tempfile.mkstemp(
        prefix=f".{path.name}.", suffix=".tmp", dir=path.parent
    )
    try:
        with os.fdopen(descriptor, "wb") as stream:
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary_name, path)
    except BaseException:
        Path(temporary_name).unlink(missing_ok=True)
        raise


def validate_package(manifest_path: Path, package: Path, key_id: str) -> PackageInput:
    manifest = read_json(manifest_path, 8 * 1024 * 1024)
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
        or manifest["type"] not in {"editor", "hubInstaller"}
        or not isinstance(artifact, dict)
        or set(artifact) != {"sizeBytes", "sha256"}
        or not isinstance(manifest["files"], list)
        or not manifest["files"]
        or not isinstance(manifest["installedSizeBytes"], int)
        or isinstance(manifest["installedSizeBytes"], bool)
        or manifest["installedSizeBytes"] < 1
    ):
        raise ValueError(
            f"Package manifest is not a complete supported schema-1 manifest: {manifest_path}"
        )
    if manifest["signatureKeyId"] != key_id:
        raise ValueError(
            "Catalog key ID does not match a package manifest signing key ID."
        )
    channel = manifest["channel"]
    platform = manifest["platform"]
    architecture = manifest["architecture"]
    if (
        not isinstance(manifest["packageId"], str)
        or not IDENTITY.fullmatch(manifest["packageId"])
        or not isinstance(manifest["version"], str)
        or not isinstance(channel, str)
        or not isinstance(platform, str)
        or not isinstance(architecture, str)
        or not IDENTITY.fullmatch(channel)
        or platform not in {"windows", "macos", "linux"}
        or architecture not in {"x86_64", "arm64"}
    ):
        raise ValueError(
            "Package identity, channel, platform, or architecture is invalid."
        )
    if (
        not package.is_file()
        or package.is_symlink()
        or not isinstance(artifact["sizeBytes"], int)
        or isinstance(artifact["sizeBytes"], bool)
        or package.stat().st_size != artifact["sizeBytes"]
        or not SHA256.fullmatch(str(artifact["sha256"]))
        or sha256_file(package) != artifact["sha256"]
    ):
        raise ValueError(
            "Package bytes do not match the package manifest artifact identity."
        )
    return PackageInput(
        manifest,
        manifest_path,
        manifest_path.stat().st_size,
        sha256_file(manifest_path),
        package,
        channel,
        platform,
        architecture,
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package-manifest", required=True, action="append", type=Path)
    parser.add_argument("--package", required=True, action="append", type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--key-id", required=True)
    parser.add_argument("--sequence", required=True, type=int)
    parser.add_argument("--expires-at", required=True, type=parse_expiry)
    args = parser.parse_args()

    if args.sequence < 1 or args.sequence > 2**63 - 1:
        raise ValueError("Catalog sequence must be between 1 and 2^63-1.")
    if not KEY_ID.fullmatch(args.key_id):
        raise ValueError("Catalog key ID is invalid.")
    if len(args.package_manifest) != len(args.package):
        raise ValueError("Every package manifest requires one package artifact.")
    packages = [
        validate_package(manifest, package, args.key_id)
        for manifest, package in zip(args.package_manifest, args.package, strict=True)
    ]

    legacy_catalogs: dict[tuple[str, str, str], list[dict[str, object]]] = {}
    compact_catalogs: dict[tuple[str, str, str], list[dict[str, object]]] = {}
    identities: set[tuple[str, str, str, str, str]] = set()
    for item in packages:
        identity = (
            item.channel,
            item.platform,
            item.architecture,
            str(item.manifest["packageId"]),
            str(item.manifest["version"]),
        )
        if identity in identities:
            raise ValueError(
                "The prepared snapshot contains a duplicate package identity."
            )
        identities.add(identity)
        catalog_key = (item.channel, item.platform, item.architecture)
        legacy_catalogs.setdefault(catalog_key, []).append(dict(item.manifest))
        compact_descriptor = dict(item.manifest)
        compact_descriptor.pop("files")
        compact_descriptor["manifest"] = {
            "sizeBytes": item.manifest_size,
            "sha256": item.manifest_sha256,
        }
        compact_catalogs.setdefault(catalog_key, []).append(compact_descriptor)

    output = args.output.resolve()
    if output.exists():
        raise ValueError(f"Prepared snapshot output already exists: {output}")
    parent = output.parent
    parent.mkdir(parents=True, exist_ok=True)
    staging = Path(
        tempfile.mkdtemp(prefix=f".{output.name}.", suffix=".tmp", dir=parent)
    )
    try:
        for catalog_root, schema_version, catalogs in (
            ("catalogs", 1, legacy_catalogs),
            ("catalogs-v2", 2, compact_catalogs),
        ):
            for (channel, platform, architecture), manifests in sorted(
                catalogs.items()
            ):
                catalog_path = (
                    staging / catalog_root / channel / platform / f"{architecture}.json"
                )
                manifests.sort(
                    key=lambda value: (
                        str(value["packageId"]),
                        str(value["version"]),
                    )
                )
                write_atomic(
                    catalog_path,
                    {
                        "schemaVersion": schema_version,
                        "keyId": args.key_id,
                        "sequence": args.sequence,
                        "expiresAt": args.expires_at,
                        "channel": channel,
                        "platform": platform,
                        "architecture": architecture,
                        "packages": manifests,
                    },
                )
        manifest_root = staging / "manifests"
        manifest_root.mkdir(parents=True, exist_ok=True)
        for item in packages:
            manifest_path = manifest_root / f"{item.manifest_sha256}.json"
            if manifest_path.exists():
                if sha256_file(manifest_path) != item.manifest_sha256:
                    raise ValueError("Prepared manifest digest collision was detected.")
                continue
            shutil.copyfile(item.manifest_path, manifest_path)
            if (
                manifest_path.stat().st_size != item.manifest_size
                or sha256_file(manifest_path) != item.manifest_sha256
            ):
                raise ValueError("Prepared package manifest copy failed verification.")

        package_root = staging / "packages"
        package_root.mkdir(parents=True, exist_ok=True)
        for item in packages:
            artifact = item.manifest["artifact"]
            assert isinstance(artifact, dict)
            digest = str(artifact["sha256"])
            package_path = package_root / digest
            if package_path.exists():
                if sha256_file(package_path) != digest:
                    raise ValueError("Prepared package digest collision was detected.")
                continue
            shutil.copyfile(item.package, package_path)
            if sha256_file(package_path) != digest:
                raise ValueError("Prepared package copy failed digest verification.")
        os.replace(staging, output)
    except BaseException:
        shutil.rmtree(staging, ignore_errors=True)
        raise
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
