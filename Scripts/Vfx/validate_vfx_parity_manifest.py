#!/usr/bin/env python3
"""Validate the checked-in Unity 6.3 LTS VFX parity manifest."""

from __future__ import annotations

import argparse
import json
import re
import sys
from collections import Counter
from pathlib import Path
from typing import Any

import generate_vfx_parity_manifest as generator
from runtime_vfx_catalog import RuntimeVfxNode, load_runtime_catalog


ID_PATTERN = re.compile(
    r"^unity\.(operator|block|context|output)\.[a-z0-9][a-z0-9.-]*$"
)
KEIRE_ID_PATTERN = re.compile(r"^keire\.[a-z0-9][a-z0-9.-]*$")
KINDS = ("Operator", "Block", "Context", "Output")
REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
RUNTIME_SUPPORT = {
    "Supported": "Supported",
    "GpuRequired": "GPU Required",
    "KeireEquivalent": "Kéire Equivalent",
    "Disabled": "Disabled",
}
RUNTIME_BACKEND = {
    "CpuOnly": "CPU Only",
    "CpuAndGpu": "CPU and GPU",
    "GpuRequired": "GPU Required",
}


def arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--manifest",
        type=Path,
        default=Path("docs/VfxParityManifest.json"),
        help="Manifest to validate.",
    )
    parser.add_argument(
        "--unity-source",
        type=Path,
        help="Optional pinned Unity Graphics checkout for provenance and coverage checks.",
    )
    return parser.parse_args()


def require(condition: bool, message: str, errors: list[str]) -> None:
    if not condition:
        errors.append(message)


def validate_top_level(manifest: dict[str, Any], errors: list[str]) -> None:
    require(manifest.get("manifestSchema") == 1, "manifestSchema must be 1", errors)
    snapshot = manifest.get("snapshot", {})
    require(
        snapshot.get("unityRelease") == generator.UNITY_RELEASE,
        "snapshot.unityRelease is not pinned",
        errors,
    )
    require(
        snapshot.get("unityEditorLine") == generator.UNITY_EDITOR_LINE,
        "snapshot.unityEditorLine is not pinned",
        errors,
    )
    require(
        snapshot.get("package") == "com.unity.visualeffectgraph",
        "snapshot.package is invalid",
        errors,
    )
    require(
        snapshot.get("packageVersion") == generator.PACKAGE_VERSION,
        "snapshot.packageVersion is invalid",
        errors,
    )
    require(
        snapshot.get("graphicsCommit") == generator.GRAPHICS_COMMIT,
        "snapshot.graphicsCommit is invalid",
        errors,
    )
    require(
        snapshot.get("graphicsRepository") == generator.GRAPHICS_REPOSITORY,
        "snapshot.graphicsRepository is invalid",
        errors,
    )
    require(
        re.fullmatch(r"[0-9a-f]{64}", snapshot.get("documentationSha256", ""))
        is not None,
        "snapshot.documentationSha256 must be SHA-256",
        errors,
    )

    policy = manifest.get("policy", {})
    require(
        policy.get("supportValues") == list(generator.SUPPORTED_STATUSES),
        "policy.supportValues changed",
        errors,
    )
    require(
        policy.get("backendTierValues") == list(generator.BACKEND_TIERS),
        "policy.backendTierValues changed",
        errors,
    )
    require(
        policy.get("unityAssetCompatibility") is False,
        "Unity asset compatibility must remain out of scope",
        errors,
    )
    require(
        policy.get("copiedUnitySourceOrIcons") is False,
        "Copied Unity source/icons must remain out of scope",
        errors,
    )
    tooling = manifest.get("tooling", {})
    require(
        tooling.get("generator") == "Scripts/Vfx/generate_vfx_parity_manifest.py",
        "tooling.generator is invalid",
        errors,
    )
    require(
        tooling.get("validator") == "Scripts/Vfx/validate_vfx_parity_manifest.py",
        "tooling.validator is invalid",
        errors,
    )
    require(
        tooling.get("runtimeCatalogContract")
        == "KeireCore/Source/Vfx/VfxNodeCatalogContract.inc",
        "tooling.runtimeCatalogContract is invalid",
        errors,
    )
    require(
        tooling.get("runtimeCatalogExporter")
        == "Scripts/Vfx/export_vfx_runtime_catalog.py",
        "tooling.runtimeCatalogExporter is invalid",
        errors,
    )
    require(
        tooling.get("offlineReconciler") == "Scripts/Vfx/reconcile_vfx_manifest.py",
        "tooling.offlineReconciler is invalid",
        errors,
    )
    require(
        tooling.get("optionalUnityCatalogExporter")
        == "Scripts/Vfx/UnityVfxCatalogExporter.cs",
        "tooling.optionalUnityCatalogExporter is invalid",
        errors,
    )


def validate_setting(setting: Any, location: str, errors: list[str]) -> None:
    require(isinstance(setting, dict), f"{location} must be an object", errors)
    if not isinstance(setting, dict):
        return
    require(
        isinstance(setting.get("name"), str) and bool(setting["name"]),
        f"{location}.name is required",
        errors,
    )
    for field in ("type", "description", "section"):
        require(
            isinstance(setting.get(field), str),
            f"{location}.{field} must be a string",
            errors,
        )


def validate_entry(
    entry: Any,
    index: int,
    runtime_catalog: dict[str, RuntimeVfxNode],
    errors: list[str],
) -> None:
    location = f"entries[{index}]"
    require(isinstance(entry, dict), f"{location} must be an object", errors)
    if not isinstance(entry, dict):
        return
    entry_id = entry.get("id", "")
    require(
        isinstance(entry_id, str) and ID_PATTERN.fullmatch(entry_id) is not None,
        f"{location}.id is invalid",
        errors,
    )
    require(entry.get("kind") in KINDS, f"{location}.kind is invalid", errors)
    for field in (
        "unityLabel",
        "unityReferenceTitle",
        "unityCategory",
        "unityMenuPath",
    ):
        require(
            isinstance(entry.get(field), str),
            f"{location}.{field} must be a string",
            errors,
        )
        if isinstance(entry.get(field), str):
            require(
                "\\" not in entry[field],
                f"{location}.{field} contains an unresolved Markdown escape",
                errors,
            )
    require(bool(entry.get("unityLabel")), f"{location}.unityLabel is required", errors)
    require(
        bool(entry.get("unityCategory")),
        f"{location}.unityCategory is required",
        errors,
    )

    settings = entry.get("unitySettings")
    require(
        isinstance(settings, list), f"{location}.unitySettings must be an array", errors
    )
    if isinstance(settings, list):
        for setting_index, setting in enumerate(settings):
            validate_setting(
                setting, f"{location}.unitySettings[{setting_index}]", errors
            )

    source = entry.get("unitySource")
    require(
        isinstance(source, dict), f"{location}.unitySource must be an object", errors
    )
    if isinstance(source, dict):
        documentation = source.get("documentation", "")
        require(
            isinstance(documentation, str)
            and documentation.startswith(
                generator.PACKAGE_PATH.as_posix() + "/Documentation~/"
            ),
            f"{location}.unitySource.documentation is invalid",
            errors,
        )
        implementations = source.get("implementations")
        require(
            isinstance(implementations, list),
            f"{location}.unitySource.implementations must be an array",
            errors,
        )
        if isinstance(implementations, list):
            require(
                all(
                    isinstance(path, str) and path.endswith(".cs")
                    for path in implementations
                ),
                f"{location}.unitySource.implementations contains an invalid path",
                errors,
            )

    keire = entry.get("keire")
    require(isinstance(keire, dict), f"{location}.keire must be an object", errors)
    if not isinstance(keire, dict):
        return
    support = keire.get("support")
    require(
        support in generator.SUPPORTED_STATUSES,
        f"{location}.keire.support is invalid",
        errors,
    )
    require(support != "Missing", f"{location} may not use Missing support", errors)
    implementation = keire.get("implementation")
    require(
        implementation is None
        or (
            isinstance(implementation, str)
            and KEIRE_ID_PATTERN.fullmatch(implementation) is not None
        ),
        f"{location}.keire.implementation is invalid",
        errors,
    )
    expected_implementation = generator.keire_implementation(
        entry.get("unityCategory", ""),
        entry.get("unityLabel", ""),
        entry.get("unityReferenceTitle", ""),
    )
    require(
        implementation == expected_implementation,
        f"{location}.keire.implementation drifted; expected {expected_implementation!r}",
        errors,
    )
    require(
        keire.get("backendTier") in generator.BACKEND_TIERS,
        f"{location}.keire.backendTier is invalid",
        errors,
    )
    for field in ("tests", "documentation"):
        values = keire.get(field)
        require(
            isinstance(values, list),
            f"{location}.keire.{field} must be an array",
            errors,
        )
        if isinstance(values, list):
            require(
                all(isinstance(value, str) and value for value in values),
                f"{location}.keire.{field} contains an invalid path",
                errors,
            )
            for value in values:
                if isinstance(value, str) and value:
                    require(
                        (REPOSITORY_ROOT / value).is_file(),
                        f"{location}.keire.{field} path does not exist: {value}",
                        errors,
                    )
    if implementation is not None:
        require(
            bool(keire.get("tests")),
            f"{location} mapped implementation needs a focused test",
            errors,
        )
        require(
            bool(keire.get("documentation")),
            f"{location} mapped implementation needs documentation",
            errors,
        )
        runtime = runtime_catalog.get(implementation)
        require(
            runtime is not None,
            f"{location} maps an ID absent from the runtime VfxNodeCatalog: {implementation}",
            errors,
        )
        if runtime is not None:
            require(
                runtime.node_class == entry.get("kind"),
                f"{location} kind does not match runtime class {runtime.node_class}",
                errors,
            )
            require(
                runtime.support != "Disabled",
                f"{location} maps a disabled runtime descriptor: {implementation}",
                errors,
            )
    else:
        runtime = None
    reason = keire.get("disabledReason")
    if support == "Disabled":
        require(
            isinstance(reason, str) and len(reason) >= 24,
            f"{location} Disabled row needs a useful disabledReason",
            errors,
        )
    else:
        require(
            reason is None,
            f"{location} enabled row must not have disabledReason",
            errors,
        )
        require(
            runtime is not None,
            f"{location} enabled row needs a runtime implementation",
            errors,
        )
        if runtime is not None:
            runtime_support = RUNTIME_SUPPORT[runtime.support]
            require(
                runtime_support == support
                or (support == "Kéire Equivalent" and runtime_support == "Supported"),
                f"{location}.keire.support is incompatible with runtime support {runtime_support!r}",
                errors,
            )
            require(
                RUNTIME_BACKEND[runtime.backend] == keire.get("backendTier"),
                f"{location}.keire.backendTier does not match runtime backend "
                f"{RUNTIME_BACKEND[runtime.backend]!r}",
                errors,
            )


def validate_runtime_mappings(
    runtime_catalog: dict[str, RuntimeVfxNode], errors: list[str]
) -> None:
    mapped = set(generator.KEIRE_IMPLEMENTATIONS.values())
    missing = sorted(mapped.difference(runtime_catalog))
    require(
        not missing,
        f"generator maps IDs absent from the runtime VfxNodeCatalog: {missing}",
        errors,
    )
    stale_tests = sorted(set(generator.KEIRE_TESTS).difference(mapped))
    require(
        not stale_tests,
        f"generator has tests for unmapped implementation IDs: {stale_tests}",
        errors,
    )


def validate_counts(
    manifest: dict[str, Any], entries: list[dict[str, Any]], errors: list[str]
) -> None:
    actual = {
        kind: sum(entry.get("kind") == kind for entry in entries) for kind in KINDS
    }
    actual["Total"] = len(entries)
    actual["Disabled"] = sum(
        entry.get("keire", {}).get("support") == "Disabled" for entry in entries
    )
    actual["WithKeireImplementation"] = sum(
        bool(entry.get("keire", {}).get("implementation")) for entry in entries
    )
    require(
        manifest.get("counts") == actual, f"counts are stale; expected {actual}", errors
    )


def validate_production_slices(
    manifest: dict[str, Any], entries: list[dict[str, Any]], errors: list[str]
) -> None:
    slices = manifest.get("productionSlices")
    require(
        slices == generator.PRODUCTION_SLICES,
        "productionSlices drifted from the generator policy",
        errors,
    )
    if not isinstance(slices, list):
        return

    covered: set[str] = set()
    for index, production_slice in enumerate(slices):
        location = f"productionSlices[{index}]"
        if not isinstance(production_slice, dict):
            require(False, f"{location} must be an object", errors)
            continue
        implementations = production_slice.get("implementations")
        require(
            isinstance(implementations, list) and bool(implementations),
            f"{location}.implementations must be a non-empty array",
            errors,
        )
        if isinstance(implementations, list):
            require(
                implementations == sorted(set(implementations)),
                f"{location}.implementations must be unique and sorted",
                errors,
            )
            covered.update(value for value in implementations if isinstance(value, str))
        for field in ("tests", "samples", "documentation"):
            paths = production_slice.get(field)
            require(
                isinstance(paths, list) and bool(paths),
                f"{location}.{field} must be non-empty",
                errors,
            )
            if isinstance(paths, list):
                for path in paths:
                    require(
                        isinstance(path, str) and (REPOSITORY_ROOT / path).is_file(),
                        f"{location}.{field} path does not exist: {path}",
                        errors,
                    )

    enabled = {
        entry.get("keire", {}).get("implementation")
        for entry in entries
        if entry.get("keire", {}).get("support") != "Disabled"
    }
    require(
        enabled == covered,
        "productionSlices do not cover every enabled manifest implementation",
        errors,
    )


def validate_source(
    manifest: dict[str, Any],
    entries: list[dict[str, Any]],
    root: Path,
    errors: list[str],
) -> None:
    try:
        package_root, _ = generator.verify_snapshot(root)
    except (OSError, RuntimeError, ValueError, json.JSONDecodeError) as error:
        errors.append(str(error))
        return
    documentation_root = package_root / "Documentation~"
    documents = generator.catalog_documents(documentation_root)
    digest = generator.documentation_digest(documents, documentation_root)
    require(
        digest == manifest.get("snapshot", {}).get("documentationSha256"),
        "documentation digest does not match the pinned checkout",
        errors,
    )
    expected_documents = {
        f"{generator.PACKAGE_PATH.as_posix()}/Documentation~/{path.name}"
        for path in documents
    }
    actual_documents = {entry["unitySource"]["documentation"] for entry in entries}
    require(
        actual_documents == expected_documents,
        "manifest does not cover exactly the selected official reference documents",
        errors,
    )
    for entry in entries:
        for source_path in [
            entry["unitySource"]["documentation"],
            *entry["unitySource"]["implementations"],
        ]:
            require(
                (root / source_path).is_file(),
                f"pinned source path does not exist: {source_path}",
                errors,
            )


def main() -> int:
    options = arguments()
    errors: list[str] = []
    try:
        encoded = options.manifest.read_bytes()
        if encoded.startswith(b"\xef\xbb\xbf"):
            errors.append("manifest must be UTF-8 without a byte-order mark")
        decoded = encoded.decode("utf-8")
        manifest = json.loads(decoded)
        canonical = (json.dumps(manifest, ensure_ascii=False, indent=2) + "\n").encode(
            "utf-8"
        )
        if encoded != canonical:
            errors.append(
                "manifest encoding is not canonical UTF-8 JSON with two-space indentation and LF newline"
            )
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as error:
        print(f"VFX parity manifest validation failed: {error}", file=sys.stderr)
        return 1

    try:
        runtime_entries = load_runtime_catalog()
        runtime_catalog = {entry.type_id: entry for entry in runtime_entries}
    except (OSError, ValueError) as error:
        errors.append(str(error))
        runtime_catalog = {}
    validate_runtime_mappings(runtime_catalog, errors)
    validate_top_level(manifest, errors)
    entries = manifest.get("entries")
    require(
        isinstance(entries, list) and bool(entries),
        "entries must be a non-empty array",
        errors,
    )
    if not isinstance(entries, list):
        entries = []
    for index, entry in enumerate(entries):
        validate_entry(entry, index, runtime_catalog, errors)
    identifiers = [entry.get("id") for entry in entries if isinstance(entry, dict)]
    duplicates = sorted(
        identifier for identifier, count in Counter(identifiers).items() if count > 1
    )
    require(not duplicates, f"duplicate IDs: {duplicates}", errors)
    expected_order = sorted(
        entries,
        key=lambda entry: (
            entry["kind"],
            entry["unityCategory"],
            entry["unityLabel"],
            entry["id"],
        ),
    )
    require(entries == expected_order, "entries are not in canonical order", errors)
    validate_counts(manifest, entries, errors)
    validate_production_slices(manifest, entries, errors)
    if options.unity_source:
        validate_source(manifest, entries, options.unity_source.resolve(), errors)

    if errors:
        print("VFX parity manifest validation failed:", file=sys.stderr)
        for error in errors:
            print(f"- {error}", file=sys.stderr)
        return 1

    counts = manifest["counts"]
    print(
        "VFX parity manifest is valid: "
        f"{counts['Total']} entries "
        f"({counts['Operator']} Operators, {counts['Block']} Blocks, "
        f"{counts['Context']} Contexts, {counts['Output']} Outputs)."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
