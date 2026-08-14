#!/usr/bin/env python3
"""Build and inspect the five deterministic first-party Kéire Marketplace packages."""

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
from dataclasses import dataclass


VERSION = "0.3.1"
PUBLISHER_ID = "keire-engine"


@dataclass(frozen=True)
class PackageDefinition:
    slug: str
    package_id: str
    display_name: str
    summary: str
    install_kind: str
    sources: tuple[str, ...]
    entry_points: tuple[str, ...]
    renderer_capabilities: tuple[str, ...] = ()


PACKAGES = (
    PackageDefinition(
        "keire-sandbox-content-pack",
        "com.keire.official.sandbox-content",
        "Kéire Sandbox Content Pack",
        "The maintained Kéire Sandbox project, assets, settings, scripts, and production examples.",
        "completeProject",
        ("Assets", "ProjectSettings", "README.md"),
        ("Assets/Scenes/SandboxShowcase.keirescene",),
        ("pbr", "shader-graph", "material-graph", "vfx-graph"),
    ),
    PackageDefinition(
        "shader-material-graph-showcase",
        "com.keire.official.shader-material-showcase",
        "Shader and Material Graph Showcase",
        "Foundational through advanced Shader Graph and Material Graph examples with their required shader sources.",
        "assetImport",
        ("Assets/Examples/MaterialLab", "Assets/Shaders"),
        ("Assets/Examples/MaterialLab/README.md",),
        ("pbr", "shader-graph", "material-graph"),
    ),
    PackageDefinition(
        "vfx-starter-pack",
        "com.keire.official.vfx-starter",
        "VFX Starter Pack",
        "Reusable deterministic VFX Graph examples, materials, meshes, textures, and diagnostics scenarios.",
        "assetImport",
        ("Assets/Vfx", "Assets/Shaders"),
        ("Assets/Vfx/README.md",),
        ("pbr", "vfx-graph"),
    ),
    PackageDefinition(
        "gameplay-csharp-samples",
        "com.keire.official.gameplay-csharp-samples",
        "Gameplay and C# Samples",
        "Managed gameplay examples with an explicit runtime assembly boundary and marketplace code validation metadata.",
        "assetImport",
        ("Assets/Scripts",),
        ("Assets/Scripts/Gameplay.keireasm",),
    ),
    PackageDefinition(
        "starter-ui-input-assets",
        "com.keire.official.starter-ui-input",
        "Starter UI and Input Assets",
        "Portable input-action foundations for keyboard, pointer, controller, and accessible interface workflows.",
        "assetImport",
        ("Assets/Input",),
        ("Assets/Input/DefaultInput.keireinput",),
    ),
)


def canonical_json(value: object) -> str:
    return json.dumps(value, ensure_ascii=False, separators=(",", ":"), sort_keys=True)


def sha256(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def locate_asset_tool(repository: pathlib.Path) -> pathlib.Path:
    executable = "KeireAssetTool.exe" if os.name == "nt" else "KeireAssetTool"
    candidates = sorted(
        repository.glob(f"Build/Bin/Debug-*/KeireAssetTool/{executable}"),
        key=lambda path: path.stat().st_mtime,
        reverse=True,
    )
    if not candidates:
        candidates = sorted(
            repository.glob(f"Build/Bin/Release-*/KeireAssetTool/{executable}"),
            key=lambda path: path.stat().st_mtime,
            reverse=True,
        )
    if not candidates:
        raise RuntimeError(
            "KeireAssetTool is not built. Build that target before preparing official packages."
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
        diagnostic = (
            completed.stderr.strip()
            or completed.stdout.strip()
            or "no diagnostic was produced"
        )
        raise RuntimeError(f"KeireAssetTool {arguments[0]} failed: {diagnostic}")
    return completed.stdout.strip()


def tracked_project_files(
    repository: pathlib.Path, project: pathlib.Path
) -> set[pathlib.Path]:
    try:
        project_relative = (
            project.resolve().relative_to(repository.resolve()).as_posix()
        )
    except ValueError as error:
        raise RuntimeError(
            "Official package sources must be inside the Kéire repository."
        ) from error
    completed = subprocess.run(
        ["git", "-C", str(repository), "ls-files", "-z", "--", project_relative],
        check=False,
        capture_output=True,
    )
    if completed.returncode != 0:
        diagnostic = completed.stderr.decode("utf-8", errors="replace").strip()
        raise RuntimeError(
            f"Could not enumerate reviewed official source files: {diagnostic}"
        )
    tracked = {
        (repository / pathlib.PurePosixPath(relative.decode("utf-8"))).resolve()
        for relative in completed.stdout.split(b"\0")
        if relative
    }
    if not tracked:
        raise RuntimeError("The official package source project has no tracked files.")
    return tracked


def copy_source(
    project: pathlib.Path,
    relative: str,
    payload: pathlib.Path,
    tracked_files: set[pathlib.Path] | None = None,
) -> None:
    source = project / pathlib.PurePosixPath(relative)
    destination = payload / pathlib.PurePosixPath(relative)
    if source.is_symlink():
        raise RuntimeError(f"Official package source may not be a link: {source}")
    if source.is_file():
        if tracked_files is not None and source.resolve() not in tracked_files:
            raise RuntimeError(f"Official package source is not tracked: {source}")
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, destination)
        return
    if not source.is_dir():
        raise FileNotFoundError(f"Official package source is missing: {source}")
    for item in sorted(source.rglob("*"), key=lambda path: path.as_posix()):
        if item.is_symlink():
            raise RuntimeError(
                f"Official package source may not contain a link: {item}"
            )
        if item.is_file():
            if tracked_files is not None and item.resolve() not in tracked_files:
                continue
            target = payload / item.relative_to(project)
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(item, target)


def inventory(payload: pathlib.Path) -> tuple[list[dict[str, object]], int]:
    files: list[dict[str, object]] = []
    total = 0
    for path in sorted(
        (item for item in payload.rglob("*") if item.is_file()),
        key=lambda item: item.as_posix(),
    ):
        relative = path.relative_to(payload).as_posix()
        if not relative.isascii():
            raise RuntimeError(
                f"Schema-1 package paths must be ASCII portable: {relative}"
            )
        size = path.stat().st_size
        files.append(
            {"mode": 0o644, "path": relative, "sha256": sha256(path), "sizeBytes": size}
        )
        total += size
    return files, total


def asset_inventory(payload: pathlib.Path) -> list[dict[str, object]]:
    assets: list[dict[str, object]] = []
    identities: set[str] = set()
    for metadata in sorted(
        payload.rglob("*.keiremeta"), key=lambda path: path.as_posix()
    ):
        source = pathlib.Path(str(metadata)[: -len(".keiremeta")])
        if not source.is_file():
            raise RuntimeError(f"Asset metadata has no matching source: {metadata}")
        document = json.loads(metadata.read_text(encoding="utf-8"))
        identity = document.get("id")
        asset_type = document.get("type")
        dependencies = document.get("dependencies")
        if (
            not isinstance(identity, str)
            or not isinstance(asset_type, str)
            or not isinstance(dependencies, list)
        ):
            raise RuntimeError(f"Asset metadata is incomplete: {metadata}")
        if identity in identities:
            raise RuntimeError(
                f"Duplicate asset identity in official package: {identity}"
            )
        identities.add(identity)
        assets.append(
            {
                "dependencies": sorted(dependencies),
                "id": identity,
                "metadata": metadata.relative_to(payload).as_posix(),
                "source": source.relative_to(payload).as_posix(),
                "type": asset_type,
            }
        )
    for asset in assets:
        missing = [
            dependency
            for dependency in asset["dependencies"]
            if dependency not in identities
        ]
        if missing:
            raise RuntimeError(
                f"{asset['source']} has dependencies outside its official package: {', '.join(missing)}"
            )
    return sorted(assets, key=lambda asset: str(asset["source"]))


def managed_assemblies(
    payload: pathlib.Path, assets: list[dict[str, object]]
) -> list[dict[str, str]]:
    asset_sources = {str(asset["source"]) for asset in assets}
    result: list[dict[str, str]] = []
    scope_by_classification = {
        "runtime": "runtime",
        "editor": "editor",
        "tests": "test",
    }
    for definition in sorted(
        payload.rglob("*.keireasm"), key=lambda path: path.as_posix()
    ):
        relative = definition.relative_to(payload).as_posix()
        if relative not in asset_sources:
            raise RuntimeError(
                f"Managed assembly definition is not an inventoried asset: {relative}"
            )
        document = json.loads(definition.read_text(encoding="utf-8"))
        name = document.get("name")
        classification = document.get("classification", "runtime")
        if not isinstance(name, str) or classification not in scope_by_classification:
            raise RuntimeError(f"Managed assembly definition is invalid: {relative}")
        result.append(
            {
                "definition": relative,
                "name": name,
                "scope": scope_by_classification[classification],
            }
        )
    return result


def create_manifest(
    definition: PackageDefinition, payload: pathlib.Path
) -> dict[str, object]:
    files, installed_size = inventory(payload)
    assets = asset_inventory(payload)
    return {
        "assets": assets,
        "channel": "stable",
        "compatibility": {
            "architectures": ["x86_64"],
            "managedApiVersion": VERSION,
            "minimumEngineVersion": VERSION,
            "platforms": ["linux", "windows"],
            "rendererCapabilities": list(definition.renderer_capabilities),
        },
        "conflicts": [],
        "dependencies": [],
        "displayName": definition.display_name,
        "entryPoints": list(definition.entry_points),
        "files": files,
        "installKind": definition.install_kind,
        "installedSizeBytes": installed_size,
        "licenses": [{"id": "MIT", "path": "LICENSE.txt"}],
        "managedAssemblies": managed_assemblies(payload, assets),
        "packageId": definition.package_id,
        "publisherId": PUBLISHER_ID,
        "samples": [],
        "schemaVersion": 1,
        "signatureKeyId": "",
        "summary": definition.summary,
        "version": VERSION,
    }


def create_package(
    repository: pathlib.Path,
    project: pathlib.Path,
    output_root: pathlib.Path,
    asset_tool: pathlib.Path,
    tracked_files: set[pathlib.Path],
    definition: PackageDefinition,
) -> dict[str, object]:
    destination = output_root / definition.slug
    if destination.exists():
        raise FileExistsError(
            f"Refusing to replace existing official-package output: {destination}"
        )
    staging = output_root / f".{definition.slug}-{uuid.uuid4().hex}.tmp"
    payload = staging / "Payload"
    package = staging / f"{definition.slug}-{VERSION}.keireassetpackage"
    manifest_path = staging / "manifest.json"
    try:
        payload.mkdir(parents=True)
        for source in definition.sources:
            copy_source(project, source, payload, tracked_files)
        shutil.copyfile(repository / "LICENSE.txt", payload / "LICENSE.txt")
        manifest_path.write_text(
            canonical_json(create_manifest(definition, payload)),
            encoding="utf-8",
            newline="\n",
        )
        run(
            asset_tool,
            [
                "create-asset-package",
                "--manifest",
                str(manifest_path),
                "--input",
                str(payload),
                "--output",
                str(package),
                "--compression-level",
                "9",
            ],
        )
        inspected = json.loads(
            run(asset_tool, ["inspect-asset-package", "--input", str(package)])
        )
        archive = inspected.get("archive")
        if (
            inspected.get("packageId") != definition.package_id
            or inspected.get("version") != VERSION
        ):
            raise RuntimeError(f"Generated identity does not match {definition.slug}.")
        if inspected.get("detachedSignature") is not None:
            raise RuntimeError(
                "Quarantine packages must remain unsigned until marketplace publication."
            )
        if not isinstance(archive, dict) or archive.get("sha256") != sha256(package):
            raise RuntimeError(
                f"Independent archive verification failed for {definition.slug}."
            )
        artifact = {
            "archiveSha256": archive["sha256"],
            "archiveSizeBytes": archive["sizeBytes"],
            "file": package.name,
            "installKind": definition.install_kind,
            "manifestSha256": archive["manifestSha256"],
            "packageId": definition.package_id,
            "productSlug": definition.slug,
            "signed": False,
            "version": VERSION,
        }
        (staging / "artifact.json").write_text(
            json.dumps(artifact, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
            newline="\n",
        )
        shutil.move(str(staging), str(destination))
        return {**artifact, "path": str(destination / package.name)}
    except Exception:
        shutil.rmtree(staging, ignore_errors=True)
        raise


def parse_arguments() -> argparse.Namespace:
    repository = pathlib.Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--asset-tool", type=pathlib.Path)
    parser.add_argument(
        "--project", type=pathlib.Path, default=repository / "Samples" / "KeireSandbox"
    )
    parser.add_argument(
        "--output-directory",
        type=pathlib.Path,
        default=repository / "Build" / "Marketplace" / "Official" / VERSION,
    )
    return parser.parse_args()


def main() -> int:
    arguments = parse_arguments()
    repository = pathlib.Path(__file__).resolve().parents[2]
    project = arguments.project.resolve()
    output = arguments.output_directory.resolve()
    asset_tool = (
        arguments.asset_tool.resolve()
        if arguments.asset_tool
        else locate_asset_tool(repository)
    )
    tracked_files = tracked_project_files(repository, project)
    if output.exists():
        raise FileExistsError(
            f"Refusing to replace existing official release set: {output}"
        )
    output.parent.mkdir(parents=True, exist_ok=True)
    output.mkdir()
    try:
        artifacts = [
            create_package(
                repository, project, output, asset_tool, tracked_files, definition
            )
            for definition in PACKAGES
        ]
        index = {
            "artifacts": artifacts,
            "publisherId": PUBLISHER_ID,
            "schemaVersion": 1,
            "version": VERSION,
        }
        (output / "release-index.json").write_text(
            json.dumps(index, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
            newline="\n",
        )
        print(json.dumps(index, indent=2, sort_keys=True))
        return 0
    except Exception:
        shutil.rmtree(output, ignore_errors=True)
        raise


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as error:
        print(f"create-official-marketplace-packages: {error}", file=sys.stderr)
        raise SystemExit(1)
