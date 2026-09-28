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
require(module.VERSION == "0.4.4", "Official packages must follow the current project version.")
require(
    len(definitions) == 6,
    "The official release set must contain six first-party products.",
)
require(
    len({definition.slug for definition in definitions}) == len(definitions),
    "Product slugs must be unique.",
)
require(
    len({definition.package_id for definition in definitions}) == len(definitions),
    "Package IDs must be unique.",
)
require(
    all(
        definition.package_id.startswith("com.keire.official.")
        for definition in definitions
    ),
    "Official packages must use the first-party package namespace.",
)

project = ROOT / "Samples/KeireSandbox"
tracked_files = module.tracked_project_files(ROOT, project)
with tempfile.TemporaryDirectory(prefix="keire-official-package-test-") as temporary:
    temporary_root = pathlib.Path(temporary)
    for definition in definitions:
        payload = temporary_root / definition.slug
        payload.mkdir()
        for source in definition.sources:
            module.copy_source(project, source, payload, None if definition.slug == "first-person-controller" else tracked_files)
        (payload / "LICENSE.txt").write_bytes((ROOT / "LICENSE.txt").read_bytes())
        manifest = module.create_manifest(definition, payload)
        require(manifest["files"], f"{definition.slug} has no file inventory.")
        require(manifest["assets"], f"{definition.slug} has no asset inventory.")
        require(
            manifest["entryPoints"] == list(definition.entry_points),
            f"{definition.slug} changed its reviewed entry points.",
        )
        require(
            manifest["version"] == "0.4.4"
            and manifest["compatibility"]["minimumEngineVersion"] == "0.4.4"
            and manifest["compatibility"]["managedApiVersion"] == "0.4.4",
            f"{definition.slug} does not identify the current first-party source release.",
        )
        require(
            all((payload / entry).is_file() for entry in manifest["entryPoints"]),
            f"{definition.slug} declares a missing entry point.",
        )
        if definition.slug == "first-person-controller":
            import json
            prefab = json.loads((payload / definition.entry_points[0]).read_text(encoding="utf-8"))
            body, camera = prefab["entities"]
            require(camera["parent"] == body["id"], "FPS camera must remain parented to its motor.")
            require(any(c["type"] == "4b454952-4543-4841-5241-435445520001" for c in body["components"]), "FPS prefab needs a character motor.")
            require(any(c["type"] == "d7e0f0a1-bfe2-46d2-bc47-4083f77a8100" for c in camera["components"]), "FPS prefab needs the packaged behaviour.")
            input_asset = json.loads((payload / "Assets/FirstPersonController/FirstPersonInput.keireinput").read_text(encoding="utf-8"))
            actions = {a["name"]: a["id"] for a in input_asset["actionMaps"][0]["actions"]}
            require(set(actions) == {"Move", "Look", "GamepadLook", "Jump", "Sprint", "Escape"}, "FPS input actions must match the controller.")
            require(actions["Look"] != actions["GamepadLook"], "Mouse delta and stick rate must stay separate.")
        csharp = list(payload.rglob("*.cs"))
        require(
            bool(csharp) == bool(manifest["managedAssemblies"]),
            f"{definition.slug} must explicitly classify all managed code.",
        )
        require(
            not any(
                path.suffix.lower() in {".csproj", ".dll", ".exe", ".ps1", ".sh"}
                for path in payload.rglob("*")
                if path.is_file()
            ),
            f"{definition.slug} contains a prohibited marketplace payload type.",
        )

    sub_asset_payload = temporary_root / "sub-asset-dependency"
    sub_asset_payload.mkdir()
    (sub_asset_payload / "parent.keiredata").write_text("{}\n", encoding="utf-8")
    (sub_asset_payload / "parent.keiredata.keiremeta").write_text(
        '{"dependencies":[],"id":"parent","subAssets":["child"],"type":"data"}\n',
        encoding="utf-8",
    )
    (sub_asset_payload / "consumer.keiredata").write_text("{}\n", encoding="utf-8")
    (sub_asset_payload / "consumer.keiredata.keiremeta").write_text(
        '{"dependencies":["child"],"id":"consumer","subAssets":[],"type":"data"}\n',
        encoding="utf-8",
    )
    sub_asset_inventory = module.asset_inventory(sub_asset_payload)
    require(
        len(sub_asset_inventory) == 2,
        "Official packages must resolve dependencies through included sub-assets.",
    )

print(
    "Official marketplace package selection validation passed for six deterministic products."
)
