#!/usr/bin/env python3
"""Reconcile Kéire status fields without requiring the pinned Unity checkout.

The frozen Unity catalog and source provenance remain untouched. This tool only
projects the checked-in Kéire mapping policy and runtime descriptor contract
onto those catalog rows.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

import generate_vfx_parity_manifest as generator
from runtime_vfx_catalog import load_runtime_catalog


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
        default=Path("Docs/VfxParityManifest.json"),
        help="Checked-in manifest to reconcile.",
    )
    parser.add_argument(
        "--check",
        action="store_true",
        help="Fail if reconciliation would change the manifest.",
    )
    return parser.parse_args()


def reconcile(manifest: dict[str, object]) -> dict[str, object]:
    runtime = {entry.type_id: entry for entry in load_runtime_catalog()}
    entries = manifest.get("entries")
    if not isinstance(entries, list):
        raise ValueError("Manifest entries must be an array.")

    for entry in entries:
        if not isinstance(entry, dict):
            raise ValueError("Manifest entry must be an object.")
        implementation = generator.keire_implementation(
            str(entry.get("unityCategory", "")),
            str(entry.get("unityLabel", "")),
            str(entry.get("unityReferenceTitle", "")),
        )
        mapped_runtime = runtime.get(implementation) if implementation else None
        enabled = implementation in generator.KEIRE_ENABLED_EQUIVALENTS
        backend = (
            RUNTIME_BACKEND[mapped_runtime.backend]
            if enabled and mapped_runtime is not None
            else generator.backend_tier(
                str(entry.get("kind", "")),
                str(entry.get("unityLabel", "")),
                str(entry.get("unityCategory", "")),
            )
        )
        entry["keire"] = {
            "support": "Kéire Equivalent" if enabled else "Disabled",
            "implementation": implementation,
            "backendTier": backend,
            "tests": generator.KEIRE_TESTS.get(implementation, [])
            if implementation
            else [],
            "documentation": ["Docs/Vfx.md"] if implementation else [],
            "disabledReason": (
                None
                if enabled
                else generator.disabled_reason(
                    str(entry.get("kind", "")), implementation, backend
                )
            ),
        }

    counts = {
        kind: sum(entry.get("kind") == kind for entry in entries)
        for kind in ("Operator", "Block", "Context", "Output")
    }
    counts["Total"] = len(entries)
    counts["Disabled"] = sum(
        entry.get("keire", {}).get("support") == "Disabled" for entry in entries
    )
    counts["WithKeireImplementation"] = sum(
        bool(entry.get("keire", {}).get("implementation")) for entry in entries
    )
    manifest["counts"] = counts
    manifest["productionSlices"] = generator.PRODUCTION_SLICES
    tooling = manifest.get("tooling")
    if not isinstance(tooling, dict):
        raise ValueError("Manifest tooling must be an object.")
    tooling["offlineReconciler"] = "Scripts/Vfx/reconcile_vfx_manifest.py"
    return manifest


def main() -> int:
    options = arguments()
    try:
        original = options.manifest.read_text(encoding="utf-8")
        manifest = reconcile(json.loads(original))
        encoded = json.dumps(manifest, ensure_ascii=False, indent=2) + "\n"
        if options.check:
            if original != encoded:
                print(
                    f"VFX parity manifest Kéire projection is stale: {options.manifest}",
                    file=sys.stderr,
                )
                return 1
            print(
                f"VFX parity manifest Kéire projection is current ({manifest['counts']['Total']} entries)."
            )
            return 0
        options.manifest.write_text(encoded, encoding="utf-8", newline="\n")
        print(f"Reconciled {options.manifest} ({manifest['counts']['Total']} entries).")
        return 0
    except (OSError, ValueError, json.JSONDecodeError) as error:
        print(f"VFX parity manifest reconciliation failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
