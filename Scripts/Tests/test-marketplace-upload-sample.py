#!/usr/bin/env python3
"""Regression checks for the mixed-content Marketplace upload sample."""

from __future__ import annotations

import importlib.util
import json
import pathlib
import tempfile


ROOT = pathlib.Path(__file__).resolve().parents[2]
GENERATOR = ROOT / "Scripts/Marketplace/create-neon-forge-sample-package.py"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


spec = importlib.util.spec_from_file_location("marketplace_upload_sample", GENERATOR)
require(
    spec is not None and spec.loader is not None,
    "Could not load the upload-sample generator.",
)
generator = importlib.util.module_from_spec(spec)
spec.loader.exec_module(generator)

require(
    generator.PACKAGE_ID == "com.keire.samples.neon-forge-creator-pack",
    "The sample identity changed.",
)
require(generator.PACKAGE_VERSION == "1.0.0", "The sample version changed.")
require(generator.ENGINE_VERSION == "0.4.2", "The first-party sample must follow the current engine version.")
require(
    len(generator.SAMPLE_ASSETS) == 6,
    "The upload sample must select six authored graph assets.",
)
source_extensions = [
    pathlib.PurePosixPath(source).suffix for source, _ in generator.SAMPLE_ASSETS
]
require(
    source_extensions.count(".keirevfx") == 2,
    "The upload sample must contain two VFX graphs.",
)
require(
    source_extensions.count(".keireshadergraph") == 2,
    "The upload sample must contain two Shader Graphs.",
)
require(
    source_extensions.count(".keirematerialgraph") == 2,
    "The upload sample must contain two Material Graphs.",
)

with tempfile.TemporaryDirectory(
    prefix="keire-marketplace-upload-sample-"
) as temporary:
    payload = pathlib.Path(temporary)
    generator.write_managed_sample(payload)
    readme = payload / generator.CONTENT_ROOT / "README.md"
    generator.write_utf8_lf(readme, "sample\n")
    generator.write_utf8_lf(payload / "LICENSE.txt", "MIT\n")
    manifest = generator.create_manifest(payload)
    encoded = generator.canonical_json(manifest)
    require(
        encoded
        == json.dumps(
            manifest, ensure_ascii=False, separators=(",", ":"), sort_keys=True
        ),
        "The sample manifest is not canonical JSON.",
    )
    require(
        len(manifest) == 19,
        "The sample manifest must contain the complete schema-v1 field set.",
    )
    require(
        manifest["signatureKeyId"] == "",
        "The quarantine upload sample must remain unsigned.",
    )
    require(
        manifest["installKind"] == "assetImport",
        "The sample must use selective asset import.",
    )
    require(
        manifest["compatibility"]["minimumEngineVersion"] == "0.4.2"
        and manifest["compatibility"]["managedApiVersion"] == "0.4.2",
        "The first-party sample compatibility metadata is stale.",
    )
    require(
        manifest["managedAssemblies"]
        == [
            {
                "definition": "Assets/NeonForgeCreatorPack/Scripts/NeonForgeSample.keireasm",
                "name": "NeonForgeSample",
                "scope": "runtime",
            }
        ],
        "The runtime assembly declaration is incomplete.",
    )
    sources = {asset["source"] for asset in manifest["assets"]}
    require(
        "Assets/NeonForgeCreatorPack/Scripts/Runtime/NeonForgePulse.cs" in sources,
        "The upload sample must inventory its C# behaviour.",
    )
    require(
        manifest["installedSizeBytes"]
        == sum(item["sizeBytes"] for item in manifest["files"]),
        "The sample installed-size accounting is incorrect.",
    )

print("Marketplace mixed-content upload-sample validation passed.")
