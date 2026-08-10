#!/usr/bin/env python3
"""Synchronize the packaged Sandbox template with the canonical Sandbox authoring project."""

from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import sys
from pathlib import Path


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
SAMPLE_ROOT = REPOSITORY_ROOT / "Samples" / "KeireSandbox"
TEMPLATE_ROOT = REPOSITORY_ROOT / "KeireHubContent" / "Templates"
PAYLOAD_ROOT = TEMPLATE_ROOT / "Payloads" / "Sandbox"
CATALOG_PATH = TEMPLATE_ROOT / "catalog.json"

EXCLUDED_ASSET_PREFIXES = (Path("Assets/Generated"),)
EXCLUDED_PROJECT_SETTINGS = {
    Path("ProjectSettings/BuildProfiles.keiresettings"),
    Path("ProjectSettings/Player.keiresettings"),
    Path("ProjectSettings/Project.keireproject"),
}
EXCLUDED_ROOT_FILES = {Path("SandboxGameplay.csproj")}


def _is_within(path: Path, root: Path) -> bool:
    try:
        path.resolve().relative_to(root.resolve())
        return True
    except ValueError:
        return False


def _is_temporary(path: Path) -> bool:
    return path.name.endswith(".TMP") or "~RF" in path.name


def _source_files() -> dict[Path, Path]:
    files: dict[Path, Path] = {}
    candidates = [SAMPLE_ROOT / ".gitignore", SAMPLE_ROOT / "README.md"]
    candidates.extend((SAMPLE_ROOT / "Assets").rglob("*"))
    candidates.extend((SAMPLE_ROOT / "ProjectSettings").glob("*.keiresettings"))
    for source in candidates:
        if not source.is_file() or source.is_symlink():
            continue
        relative = source.relative_to(SAMPLE_ROOT)
        if relative in EXCLUDED_ROOT_FILES or relative in EXCLUDED_PROJECT_SETTINGS or _is_temporary(relative):
            continue
        if any(relative == prefix or prefix in relative.parents for prefix in EXCLUDED_ASSET_PREFIXES):
            continue
        files[relative] = source
    return dict(sorted(files.items(), key=lambda item: item[0].as_posix()))


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def _payload_manifest(files: dict[Path, Path]) -> list[dict[str, object]]:
    return [
        {"path": relative.as_posix(), "sizeBytes": source.stat().st_size, "sha256": _sha256(source)}
        for relative, source in files.items()
    ]


def _catalog() -> dict[str, object]:
    with CATALOG_PATH.open("r", encoding="utf-8") as stream:
        return json.load(stream)


def _sandbox_manifest(catalog: dict[str, object]) -> dict[str, object]:
    templates = catalog.get("templates")
    if not isinstance(templates, list):
        raise RuntimeError("Template catalog has no template list.")
    for template in templates:
        if isinstance(template, dict) and template.get("id") == "keire.sandbox":
            return template
    raise RuntimeError("Template catalog has no keire.sandbox manifest.")


def _expected_manifest(files: dict[Path, Path]) -> dict[str, object]:
    payload_files = _payload_manifest(files)
    return {
        "version": "1.1.0",
        "compatibleEditors": ">=0.2.0 <2.0.0",
        "description": (
            "The complete Kéire production Sandbox with Shader Graph, Material Graph, VFX, scripting, rendering, "
            "physics, UI, audio, meshes, textures, and staged example scenes."
        ),
        "tags": ["Sample", "Learning", "Shader Graph", "Material Graph", "VFX", "Scripting"],
        "estimatedSizeBytes": sum(int(entry["sizeBytes"]) for entry in payload_files),
        "payloadFiles": payload_files,
        "defaultProjectConfiguration": {
            "startupScene": "a1aa0000-0000-4000-8000-000000000001",
            "defaultInput": "97b38693-6dc3-4f06-a228-44ba5786e8d1",
        },
        "starterContent": [
            "Assets/Scenes/ShaderMaterialShowcase.keirescene",
            "Assets/Scenes/SampleScene.keirescene",
            "Assets/Materials/MaterialGraphs/01_BasicPaint_Shader.keireshadergraph",
            "Assets/Materials/MaterialGraphs/09_HolographicVoronoi_Shader.keireshadergraph",
            "Assets/Vfx/ArcaneNova.keirevfx",
        ],
    }


def _payload_files() -> dict[Path, Path]:
    if not PAYLOAD_ROOT.is_dir():
        return {}
    result: dict[Path, Path] = {}
    for path in PAYLOAD_ROOT.rglob("*"):
        if path.is_symlink():
            raise RuntimeError(f"Sandbox template contains a symbolic link: {path}")
        if path.is_file():
            result[path.relative_to(PAYLOAD_ROOT)] = path
    return result


def _check(files: dict[Path, Path]) -> list[str]:
    errors: list[str] = []
    payload = _payload_files()
    expected_paths = set(files)
    payload_paths = set(payload)
    for missing in sorted(expected_paths - payload_paths):
        errors.append(f"missing payload file: {missing.as_posix()}")
    for extra in sorted(payload_paths - expected_paths):
        errors.append(f"unexpected payload file: {extra.as_posix()}")
    for relative in sorted(expected_paths & payload_paths):
        if files[relative].read_bytes() != payload[relative].read_bytes():
            errors.append(f"payload differs from Sandbox: {relative.as_posix()}")

    catalog = _catalog()
    manifest = _sandbox_manifest(catalog)
    for key, expected in _expected_manifest(files).items():
        if manifest.get(key) != expected:
            errors.append(f"catalog field is stale: {key}")
    return errors


def _sync(files: dict[Path, Path]) -> None:
    if not _is_within(PAYLOAD_ROOT, REPOSITORY_ROOT):
        raise RuntimeError("Sandbox template payload escapes the repository.")
    PAYLOAD_ROOT.mkdir(parents=True, exist_ok=True)
    for relative, destination in _payload_files().items():
        if relative not in files:
            destination.unlink()
    for relative, source in files.items():
        destination = PAYLOAD_ROOT / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, destination)
    for directory in sorted((path for path in PAYLOAD_ROOT.rglob("*") if path.is_dir()), reverse=True):
        if not any(directory.iterdir()):
            directory.rmdir()

    catalog = _catalog()
    manifest = _sandbox_manifest(catalog)
    manifest.update(_expected_manifest(files))
    with CATALOG_PATH.open("w", encoding="utf-8", newline="\n") as stream:
        json.dump(catalog, stream, ensure_ascii=False, indent=2)
        stream.write("\n")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="Report drift without modifying the repository.")
    arguments = parser.parse_args()
    files = _source_files()
    if not files:
        raise RuntimeError("The canonical Sandbox authoring projection is empty.")
    if arguments.check:
        errors = _check(files)
        if errors:
            print("Sandbox template drift detected:", file=sys.stderr)
            for error in errors:
                print(f"  - {error}", file=sys.stderr)
            return 1
        print(f"Sandbox template is synchronized ({len(files)} files).")
        return 0
    _sync(files)
    errors = _check(files)
    if errors:
        raise RuntimeError("Sandbox template synchronization did not converge: " + "; ".join(errors))
    print(f"Synchronized Sandbox template ({len(files)} files).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
