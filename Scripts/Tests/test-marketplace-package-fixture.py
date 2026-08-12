#!/usr/bin/env python3
"""Regression checks for the deterministic harmless validator package fixture."""

from __future__ import annotations

import hashlib
import importlib.util
import json
import pathlib
import tempfile


ROOT = pathlib.Path(__file__).resolve().parents[2]
GENERATOR = ROOT / "Scripts/Marketplace/create-validator-smoke-package.py"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


spec = importlib.util.spec_from_file_location("validator_smoke_package", GENERATOR)
require(spec is not None and spec.loader is not None, "Could not load the smoke-package generator.")
generator = importlib.util.module_from_spec(spec)
spec.loader.exec_module(generator)

with tempfile.TemporaryDirectory(prefix="keire-validator-smoke-test-") as temporary:
    payload = pathlib.Path(temporary)
    assets = payload / "Assets"
    assets.mkdir()
    source = assets / "ValidatorSmoke.txt"
    metadata = assets / "ValidatorSmoke.txt.keiremeta"
    license_path = payload / "LICENSE.txt"
    source.write_text("harmless\n", encoding="utf-8", newline="\n")
    metadata.write_text(
        generator.canonical_json({"id": generator.ASSET_ID, "type": generator.TEXT_ASSET_TYPE}),
        encoding="utf-8",
        newline="\n",
    )
    license_path.write_text("MIT\n", encoding="utf-8", newline="\n")

    manifest = generator.manifest(payload)
    encoded = generator.canonical_json(manifest)
    require(encoded == json.dumps(manifest, ensure_ascii=False, separators=(",", ":"), sort_keys=True),
            "The fixture manifest is not canonical JSON.")
    require(len(manifest) == 19, "The fixture manifest must contain the complete schema-v1 field set.")
    require(manifest["packageId"] == "com.keire.internal.validator-smoke",
            "The fixture package identity changed unexpectedly.")
    require(manifest["version"] == "0.0.1" and manifest["signatureKeyId"] == "",
            "The quarantine fixture must remain versioned and unsigned.")
    require(manifest["managedAssemblies"] == [], "The harmless fixture must not contain managed code.")
    require([item["path"] for item in manifest["files"]] == [
        "Assets/ValidatorSmoke.txt",
        "Assets/ValidatorSmoke.txt.keiremeta",
        "LICENSE.txt",
    ], "The fixture inventory must be stable and portable.")
    require(manifest["installedSizeBytes"] == sum(path.stat().st_size for path in (source, metadata, license_path)),
            "The fixture installed-size accounting is incorrect.")
    for item in manifest["files"]:
        expected = hashlib.sha256((payload / item["path"]).read_bytes()).hexdigest()
        require(item["sha256"] == expected, f"The fixture digest is incorrect for {item['path']}.")

print("Marketplace package-fixture validation passed.")
