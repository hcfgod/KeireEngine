#!/usr/bin/env python3
"""Create and verify a deterministic mixed-content Marketplace upload sample."""

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


PACKAGE_ID = "com.keire.samples.neon-forge-creator-pack"
PACKAGE_VERSION = "1.0.0"
PUBLISHER_ID = "keire-engine"
DISPLAY_NAME = "Neon Forge Creator Pack"
CONTENT_ROOT = pathlib.PurePosixPath("Assets/NeonForgeCreatorPack")

SAMPLE_ASSETS = (
    (
        "Assets/Vfx/ArcaneNova.keirevfx",
        CONTENT_ROOT / "Vfx/ArcaneNova.keirevfx",
    ),
    (
        "Assets/Vfx/ForgeSparks.keirevfx",
        CONTENT_ROOT / "Vfx/ForgeSparks.keirevfx",
    ),
    (
        "Assets/Examples/MaterialLab/ShaderGraphs/01_Foundations/SG_03_NeonPulse.keireshadergraph",
        CONTENT_ROOT / "Shaders/SG_03_NeonPulse.keireshadergraph",
    ),
    (
        "Assets/Examples/MaterialLab/ShaderGraphs/03_Advanced/SG_09_EnergyDissolve.keireshadergraph",
        CONTENT_ROOT / "Shaders/SG_09_EnergyDissolve.keireshadergraph",
    ),
    (
        "Assets/Examples/MaterialLab/MaterialGraphs/01_Foundations/MG_03_NeonPulse.keirematerialgraph",
        CONTENT_ROOT / "Materials/MG_03_NeonPulse.keirematerialgraph",
    ),
    (
        "Assets/Examples/MaterialLab/MaterialGraphs/03_Advanced/MG_09_EnergyDissolve.keirematerialgraph",
        CONTENT_ROOT / "Materials/MG_09_EnergyDissolve.keirematerialgraph",
    ),
)

ASSEMBLY_ID = "03100000-0000-4000-8000-000000000a01"
SCRIPT_ID = "03100000-0000-4000-8000-000000000a02"
MANAGED_ASSEMBLY_TYPE = "4b454952-454d-414e-4147-454441534d01"
TEXT_ASSET_TYPE = "4b454952-4554-4558-5441-535345540001"


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
    for configuration in ("Debug", "Release"):
        candidates = sorted(
            repository.glob(f"Build/Bin/{configuration}-*/KeireAssetTool/{executable}"),
            key=lambda path: path.stat().st_mtime,
            reverse=True,
        )
        if candidates:
            return candidates[0]
    raise RuntimeError("KeireAssetTool is not built. Build that target before creating the sample package.")


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
        raise RuntimeError(f"KeireAssetTool {arguments[0]} failed: {diagnostic}")
    return completed.stdout.strip()


def copy_reviewed_asset(project: pathlib.Path, source_relative: str, destination: pathlib.Path) -> None:
    source = project / pathlib.PurePosixPath(source_relative)
    metadata = pathlib.Path(f"{source}.keiremeta")
    if not source.is_file() or not metadata.is_file():
        raise FileNotFoundError(f"The reviewed sample asset or its metadata is missing: {source_relative}")
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source, destination)
    shutil.copyfile(metadata, pathlib.Path(f"{destination}.keiremeta"))


def write_managed_sample(payload: pathlib.Path) -> None:
    scripts = payload / CONTENT_ROOT / "Scripts"
    runtime = scripts / "Runtime"
    runtime.mkdir(parents=True, exist_ok=True)
    assembly = scripts / "NeonForgeSample.keireasm"
    assembly.write_text(
        json.dumps(
            {
                "classification": "runtime",
                "name": "NeonForgeSample",
                "references": [],
                "rootNamespace": "KeireMarketplaceSamples",
                "schemaVersion": 1,
                "sourceRoots": [f"{CONTENT_ROOT.as_posix()}/Scripts/Runtime"],
            },
            indent=2,
            sort_keys=True,
        )
        + "\n",
        encoding="utf-8",
        newline="\n",
    )
    pathlib.Path(f"{assembly}.keiremeta").write_text(
        json.dumps(
            {
                "dependencies": [],
                "id": ASSEMBLY_ID,
                "importer": "Keire.ManagedAssembly",
                "importerVersion": 2,
                "schemaVersion": 1,
                "subAssets": [],
                "type": MANAGED_ASSEMBLY_TYPE,
            },
            indent=2,
            sort_keys=True,
        )
        + "\n",
        encoding="utf-8",
        newline="\n",
    )
    script = runtime / "NeonForgePulse.cs"
    script.write_text(
        """using Keire;

namespace KeireMarketplaceSamples;

/// <summary>Provides a lightweight presentation motion for the Neon Forge sample assets.</summary>
[StableComponentId("03100000-0000-4000-8000-000000000a10")]
[ExecutionOrder(40)]
public sealed class NeonForgePulse : Behaviour
{
    [SerializeField, StableFieldId("03100000-0000-4000-8000-000000000a11")]
    [Range(-180.0, 180.0), Tooltip("Yaw rotation speed in degrees per second.")]
    private float _rotationSpeed = 24.0f;

    [SerializeField, StableFieldId("03100000-0000-4000-8000-000000000a12")]
    [Range(0.0, 0.5), Tooltip("Vertical pulse distance in metres.")]
    private float _pulseHeight = 0.06f;

    [HotReloadState]
    private float _elapsed;

    private Vector3 _origin;

    protected override void Awake() => _origin = Entity.Transform.LocalPosition;

    protected override void Update()
    {
        float deltaTime = Time.DeltaTime;
        if (deltaTime <= 0.0f)
            return;

        _elapsed += deltaTime;
        TransformHandle transform = Entity.Transform;
        transform.LocalRotation = Quaternion.Euler(0.0f, _elapsed * _rotationSpeed);
        transform.LocalPosition = _origin + (Vector3.Up * (MathF.Sin(_elapsed * 1.5f) * _pulseHeight));
    }
}
""",
        encoding="utf-8",
        newline="\n",
    )
    pathlib.Path(f"{script}.keiremeta").write_text(
        json.dumps(
            {
                "dependencies": [],
                "id": SCRIPT_ID,
                "importer": "Keire.Text",
                "importerVersion": 1,
                "schemaVersion": 1,
                "subAssets": [],
                "type": TEXT_ASSET_TYPE,
            },
            indent=2,
            sort_keys=True,
        )
        + "\n",
        encoding="utf-8",
        newline="\n",
    )


def inventory(payload: pathlib.Path) -> tuple[list[dict[str, object]], int]:
    files: list[dict[str, object]] = []
    total = 0
    for path in sorted((item for item in payload.rglob("*") if item.is_file()), key=lambda item: item.as_posix()):
        relative = path.relative_to(payload).as_posix()
        if not relative.isascii():
            raise RuntimeError(f"Schema-1 package paths must be ASCII portable: {relative}")
        size = path.stat().st_size
        files.append({"mode": 0o644, "path": relative, "sha256": sha256(path), "sizeBytes": size})
        total += size
    return files, total


def asset_inventory(payload: pathlib.Path) -> list[dict[str, object]]:
    assets: list[dict[str, object]] = []
    identities: set[str] = set()
    for metadata in sorted(payload.rglob("*.keiremeta"), key=lambda path: path.as_posix()):
        source = pathlib.Path(str(metadata)[: -len(".keiremeta")])
        document = json.loads(metadata.read_text(encoding="utf-8"))
        identity = document.get("id")
        asset_type = document.get("type")
        dependencies = document.get("dependencies", [])
        if not source.is_file() or not isinstance(identity, str) or not isinstance(asset_type, str):
            raise RuntimeError(f"Asset metadata is incomplete: {metadata}")
        if not isinstance(dependencies, list) or identity in identities:
            raise RuntimeError(f"Asset metadata has invalid dependencies or a duplicate identity: {metadata}")
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
        missing = [dependency for dependency in asset["dependencies"] if dependency not in identities]
        if missing:
            raise RuntimeError(f"{asset['source']} has dependencies outside the sample package: {', '.join(missing)}")
    return sorted(assets, key=lambda asset: str(asset["source"]))


def create_manifest(payload: pathlib.Path) -> dict[str, object]:
    files, installed_size = inventory(payload)
    assets = asset_inventory(payload)
    return {
        "assets": assets,
        "channel": "stable",
        "compatibility": {
            "architectures": ["x86_64"],
            "managedApiVersion": "0.3.1",
            "minimumEngineVersion": "0.3.1",
            "platforms": ["linux", "windows"],
            "rendererCapabilities": ["material-graph", "pbr", "shader-graph", "vfx-graph"],
        },
        "conflicts": [],
        "dependencies": [],
        "displayName": DISPLAY_NAME,
        "entryPoints": [f"{CONTENT_ROOT.as_posix()}/README.md"],
        "files": files,
        "installKind": "assetImport",
        "installedSizeBytes": installed_size,
        "licenses": [{"id": "MIT", "path": "LICENSE.txt"}],
        "managedAssemblies": [
            {
                "definition": f"{CONTENT_ROOT.as_posix()}/Scripts/NeonForgeSample.keireasm",
                "name": "NeonForgeSample",
                "scope": "runtime",
            }
        ],
        "packageId": PACKAGE_ID,
        "publisherId": PUBLISHER_ID,
        "samples": [],
        "schemaVersion": 1,
        "signatureKeyId": "",
        "summary": "A compact mixed-content sample with VFX, Shader Graph, Material Graph, and managed C# assets.",
        "version": PACKAGE_VERSION,
    }


def create_package(
    repository: pathlib.Path,
    project: pathlib.Path,
    output_directory: pathlib.Path,
    asset_tool: pathlib.Path,
) -> pathlib.Path:
    if output_directory.exists():
        raise FileExistsError(f"Refusing to replace existing sample-package output: {output_directory}")
    output_directory.parent.mkdir(parents=True, exist_ok=True)
    staging = output_directory.parent / f".{output_directory.name}-{uuid.uuid4().hex}.tmp"
    payload = staging / "Payload"
    package = staging / f"neon-forge-creator-pack-{PACKAGE_VERSION}.keireassetpackage"
    manifest_path = staging / "manifest.json"
    try:
        payload.mkdir(parents=True)
        for source_relative, destination_relative in SAMPLE_ASSETS:
            copy_reviewed_asset(project, source_relative, payload / destination_relative)
        write_managed_sample(payload)
        readme = payload / CONTENT_ROOT / "README.md"
        readme.write_text(
            "# Neon Forge Creator Pack\n\n"
            "Upload-validation sample containing two VFX graphs, two Shader Graphs, two Material Graphs, "
            "and the NeonForgePulse runtime C# behaviour.\n",
            encoding="utf-8",
            newline="\n",
        )
        shutil.copyfile(repository / "LICENSE.txt", payload / "LICENSE.txt")
        manifest_path.write_text(canonical_json(create_manifest(payload)), encoding="utf-8", newline="\n")
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
        inspected = json.loads(run(asset_tool, ["inspect-asset-package", "--input", str(package)]))
        archive = inspected.get("archive")
        if inspected.get("packageId") != PACKAGE_ID or inspected.get("version") != PACKAGE_VERSION:
            raise RuntimeError("The generated package identity does not match the sample definition.")
        if inspected.get("detachedSignature") is not None:
            raise RuntimeError("The upload sample must remain unsigned until Marketplace publication.")
        if not isinstance(archive, dict) or archive.get("sha256") != sha256(package):
            raise RuntimeError("The generated package hash did not survive independent verification.")
        (staging / "artifact.json").write_text(
            json.dumps(
                {
                    "archiveSha256": archive["sha256"],
                    "archiveSizeBytes": archive["sizeBytes"],
                    "file": package.name,
                    "manifestSha256": archive["manifestSha256"],
                    "packageId": PACKAGE_ID,
                    "signed": False,
                    "version": PACKAGE_VERSION,
                },
                indent=2,
                sort_keys=True,
            )
            + "\n",
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
    parser.add_argument("--asset-tool", type=pathlib.Path)
    parser.add_argument("--project", type=pathlib.Path, default=repository / "Samples" / "KeireSandbox")
    parser.add_argument(
        "--output-directory",
        type=pathlib.Path,
        default=repository / "Build" / "Marketplace" / "UploadSamples" / "NeonForgeCreatorPack" / PACKAGE_VERSION,
    )
    return parser.parse_args()


def main() -> int:
    arguments = parse_arguments()
    repository = pathlib.Path(__file__).resolve().parents[2]
    asset_tool = arguments.asset_tool.resolve() if arguments.asset_tool else locate_asset_tool(repository)
    package = create_package(repository, arguments.project.resolve(), arguments.output_directory.resolve(), asset_tool)
    artifact = json.loads((package.parent / "artifact.json").read_text(encoding="utf-8"))
    print(json.dumps({"artifact": str(package), **artifact}, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as error:
        print(f"create-neon-forge-sample-package: {error}", file=sys.stderr)
        raise SystemExit(1)
