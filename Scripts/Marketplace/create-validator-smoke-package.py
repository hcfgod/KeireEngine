#!/usr/bin/env python3
"""Create and verify the harmless marketplace-validator smoke package."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import pathlib
import shutil
import subprocess
import sys
import uuid


PACKAGE_ID = "com.keire.internal.validator-smoke"
PACKAGE_VERSION = "0.0.1"
PUBLISHER_ID = "keire-engine"
ASSET_ID = "03100000-0000-4000-8000-000000000901"
TEXT_ASSET_TYPE = "4b454952-4554-4558-5441-535345540001"


def sha256(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def inventory(payload: pathlib.Path) -> tuple[list[dict[str, object]], int]:
    files: list[dict[str, object]] = []
    total = 0
    for path in sorted((item for item in payload.rglob("*") if item.is_file()), key=lambda item: item.as_posix()):
        relative = path.relative_to(payload).as_posix()
        size = path.stat().st_size
        files.append({"mode": 0o644, "path": relative, "sha256": sha256(path), "sizeBytes": size})
        total += size
    return files, total


def manifest(payload: pathlib.Path) -> dict[str, object]:
    files, total = inventory(payload)
    return {
        "assets": [{
            "dependencies": [],
            "id": ASSET_ID,
            "metadata": "Assets/ValidatorSmoke.txt.keiremeta",
            "source": "Assets/ValidatorSmoke.txt",
            "type": TEXT_ASSET_TYPE,
        }],
        "channel": "stable",
        "compatibility": {
            "architectures": ["x86_64"],
            "managedApiVersion": "0.3.1",
            "minimumEngineVersion": "0.3.1",
            "platforms": ["linux", "windows"],
            "rendererCapabilities": [],
        },
        "conflicts": [],
        "dependencies": [],
        "displayName": "Kéire Validator Smoke Test",
        "entryPoints": ["Assets/ValidatorSmoke.txt"],
        "files": files,
        "installKind": "assetImport",
        "installedSizeBytes": total,
        "licenses": [{"id": "MIT", "path": "LICENSE.txt"}],
        "managedAssemblies": [],
        "packageId": PACKAGE_ID,
        "publisherId": PUBLISHER_ID,
        "samples": [],
        "schemaVersion": 1,
        "signatureKeyId": "",
        "summary": "A harmless deterministic package for validating Kéire Marketplace quarantine and scanning.",
        "version": PACKAGE_VERSION,
    }


def canonical_json(value: object) -> str:
    return json.dumps(value, ensure_ascii=False, separators=(",", ":"), sort_keys=True)


def locate_asset_tool(repository: pathlib.Path) -> pathlib.Path:
    executable = "KeireAssetTool.exe" if os.name == "nt" else "KeireAssetTool"
    candidates = sorted(
        repository.glob(f"Build/Bin/Debug-*/KeireAssetTool/{executable}"),
        key=lambda path: path.stat().st_mtime,
        reverse=True,
    )
    if not candidates:
        raise RuntimeError(
            "KeireAssetTool is not built. Build target KeireAssetTool before creating the smoke package."
        )
    return candidates[0]


def run(asset_tool: pathlib.Path, arguments: list[str]) -> str:
    completed = subprocess.run(
        [str(asset_tool), *arguments],
        check=False,
        capture_output=True,
        encoding="utf-8",
        errors="strict",
    )
    if completed.returncode != 0:
        diagnostic = completed.stderr.strip() or completed.stdout.strip() or "no diagnostic was produced"
        raise RuntimeError(f"KeireAssetTool {' '.join(arguments[:1])} failed: {diagnostic}")
    return completed.stdout.strip()


def create_package(repository: pathlib.Path, output_directory: pathlib.Path, asset_tool: pathlib.Path) -> pathlib.Path:
    if output_directory.exists():
        raise FileExistsError(f"Refusing to replace existing smoke-package output: {output_directory}")
    output_directory.parent.mkdir(parents=True, exist_ok=True)
    staging = output_directory.parent / f".{output_directory.name}-{uuid.uuid4().hex}.tmp"
    payload = staging / "Payload"
    assets = payload / "Assets"
    package = staging / f"keire-validator-smoke-{PACKAGE_VERSION}.keireassetpackage"
    manifest_path = staging / "manifest.json"
    try:
        assets.mkdir(parents=True)
        (assets / "ValidatorSmoke.txt").write_text(
            "Kéire Marketplace validator smoke package.\nThis file contains no executable content.\n",
            encoding="utf-8",
            newline="\n",
        )
        (assets / "ValidatorSmoke.txt.keiremeta").write_text(
            canonical_json({"id": ASSET_ID, "type": TEXT_ASSET_TYPE}),
            encoding="utf-8",
            newline="\n",
        )
        license_text = (repository / "LICENSE.txt").read_text(encoding="utf-8").replace("\r\n", "\n")
        (payload / "LICENSE.txt").write_text(license_text, encoding="utf-8", newline="\n")
        manifest_path.write_text(canonical_json(manifest(payload)), encoding="utf-8", newline="\n")

        run(asset_tool, [
            "create-asset-package",
            "--manifest", str(manifest_path),
            "--input", str(payload),
            "--output", str(package),
        ])
        inspected = json.loads(run(asset_tool, ["inspect-asset-package", "--input", str(package)]))
        if inspected.get("packageId") != PACKAGE_ID or inspected.get("version") != PACKAGE_VERSION:
            raise RuntimeError("The generated package identity does not match the smoke fixture.")
        if inspected.get("detachedSignature") is not None:
            raise RuntimeError("The quarantine smoke package must remain unsigned.")
        archive = inspected.get("archive")
        if not isinstance(archive, dict) or archive.get("sha256") != sha256(package):
            raise RuntimeError("The generated package hash did not survive independent verification.")
        (staging / "artifact.json").write_text(
            json.dumps({
                "archiveSha256": archive["sha256"],
                "archiveSizeBytes": archive["sizeBytes"],
                "manifestSha256": archive["manifestSha256"],
                "packageId": PACKAGE_ID,
                "signed": False,
                "version": PACKAGE_VERSION,
            }, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
            newline="\n",
        )
        shutil.move(str(staging), str(output_directory))
        return output_directory / package.name
    except Exception:
        shutil.rmtree(staging, ignore_errors=True)
        raise


def parse_arguments() -> argparse.Namespace:
    repository = pathlib.Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--asset-tool", type=pathlib.Path, default=None)
    parser.add_argument(
        "--output-directory",
        type=pathlib.Path,
        default=repository / "Build" / "Marketplace" / "ValidatorSmoke" / PACKAGE_VERSION,
    )
    return parser.parse_args()


def main() -> int:
    arguments = parse_arguments()
    repository = pathlib.Path(__file__).resolve().parents[2]
    asset_tool = arguments.asset_tool.resolve() if arguments.asset_tool else locate_asset_tool(repository)
    package = create_package(repository, arguments.output_directory.resolve(), asset_tool)
    artifact = json.loads((package.parent / "artifact.json").read_text(encoding="utf-8"))
    print(json.dumps({"artifact": str(package), **artifact}, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as error:
        print(f"create-validator-smoke-package: {error}", file=sys.stderr)
        raise SystemExit(1)
