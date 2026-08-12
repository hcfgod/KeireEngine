#!/usr/bin/env python3
"""Validate first-party marketplace package selection and manifest evidence."""

from __future__ import annotations

import importlib.util
import pathlib
import sys
import tempfile


ROOT = pathlib.Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "Scripts/Marketplace/create-official-marketplace-packages.py"
spec = importlib.util.spec_from_file_location("keire_official_packages", SCRIPT)
if spec is None or spec.loader is None:
    raise RuntimeError("Could not load the official marketplace package builder.")
module = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = module
spec.loader.exec_module(module)


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


definitions = module.PACKAGES
require(len(definitions) == 5, "The official release set must contain exactly five launch products.")
require(len({definition.slug for definition in definitions}) == len(definitions), "Product slugs must be unique.")
require(len({definition.package_id for definition in definitions}) == len(definitions), "Package IDs must be unique.")
require(all(definition.package_id.startswith("com.keire.official.") for definition in definitions),
        "Official packages must use the first-party package namespace.")

project = ROOT / "Samples/KeireSandbox"
tracked_files = module.tracked_project_files(ROOT, project)
with tempfile.TemporaryDirectory(prefix="keire-official-package-test-") as temporary:
    temporary_root = pathlib.Path(temporary)
    for definition in definitions:
        payload = temporary_root / definition.slug
        payload.mkdir()
        for source in definition.sources:
            module.copy_source(project, source, payload, tracked_files)
        (payload / "LICENSE.txt").write_bytes((ROOT / "LICENSE.txt").read_bytes())
        manifest = module.create_manifest(definition, payload)
        require(manifest["files"], f"{definition.slug} has no file inventory.")
        require(manifest["assets"], f"{definition.slug} has no asset inventory.")
        require(manifest["entryPoints"] == list(definition.entry_points),
                f"{definition.slug} changed its reviewed entry points.")
        require(all((payload / entry).is_file() for entry in manifest["entryPoints"]),
                f"{definition.slug} declares a missing entry point.")
        csharp = list(payload.rglob("*.cs"))
        require(bool(csharp) == bool(manifest["managedAssemblies"]),
                f"{definition.slug} must explicitly classify all managed code.")
        require(not any(path.suffix.lower() in {".csproj", ".dll", ".exe", ".ps1", ".sh"}
                        for path in payload.rglob("*") if path.is_file()),
                f"{definition.slug} contains a prohibited marketplace payload type.")

print("Official marketplace package selection validation passed for five deterministic products.")
