#!/usr/bin/env python3
"""Focused regression tests for the offline VFX parity tooling."""

from __future__ import annotations

import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

import generate_vfx_parity_manifest as generator
from runtime_vfx_catalog import load_runtime_catalog


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
MANIFEST = REPOSITORY_ROOT / "Docs/VfxParityManifest.json"
VALIDATOR = REPOSITORY_ROOT / "Scripts/Vfx/validate_vfx_parity_manifest.py"
RECONCILER = REPOSITORY_ROOT / "Scripts/Vfx/reconcile_vfx_manifest.py"
CAPABILITY_GENERATOR = REPOSITORY_ROOT / "Scripts/Vfx/generate_vfx_capabilities.py"


class MarkdownCatalogTests(unittest.TestCase):
    def test_dynamic_placeholders_survive_markdown_cleanup(self) -> None:
        cases = {
            r"**Attribute > Set \<Attribute> From Map**": "Attribute > Set <Attribute> From Map",
            r"**Attribute > Curve > [Add/Set] \<Attribute> \<Mode>**": (
                "Attribute > Curve > [Add/Set] <Attribute> <Mode>"
            ),
            r"**Position > Set Position (Sequential : \<SequentialMode\>)**": (
                "Position > Set Position (Sequential : <SequentialMode>)"
            ),
            r"<span>Spawn</span> > Set SpawnEvent \<Attribute>": "Spawn > Set SpawnEvent <Attribute>",
        }
        for source, expected in cases.items():
            with self.subTest(source=source):
                self.assertEqual(generator.clean_markdown(source), expected)

    def test_menu_split_does_not_split_placeholder_closing_brackets(self) -> None:
        self.assertEqual(
            generator.split_menu_path(
                "Attribute > Curve > [Add/Set] <Attribute> <Mode>"
            ),
            ["Attribute", "Curve", "[Add/Set] <Attribute> <Mode>"],
        )

    def test_checked_manifest_preserves_exact_dynamic_labels(self) -> None:
        manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
        by_document = {
            Path(entry["unitySource"]["documentation"]).name: entry
            for entry in manifest["entries"]
        }
        expected = {
            "Block-SetAttributeFromMap.md": "Attribute > Set <Attribute> From Map",
            "Block-SetAttributeFromCurve.md": "Attribute > Curve > [Add/Set] <Attribute> <Mode>",
            "Block-SetAttribute.md": "Attribute > Set > [Add/Blend/Inherit/Multiply/Set] <Attribute>",
            "Block-SetPosition(Sequential).md": "Position > Set Position (Sequential : <SequentialMode>)",
            "Block-SetSpawnEvent.md": "Spawn > Set SpawnEvent <Attribute>",
        }
        for document, menu_path in expected.items():
            with self.subTest(document=document):
                self.assertEqual(by_document[document]["unityMenuPath"], menu_path)
        self.assertEqual(
            by_document["Block-SetSpawnEvent.md"]["unityLabel"],
            "Set SpawnEvent <Attribute>",
        )


class RuntimeCatalogIntegrityTests(unittest.TestCase):
    def test_every_generator_mapping_exists_in_runtime_contract(self) -> None:
        runtime_ids = {entry.type_id for entry in load_runtime_catalog()}
        self.assertTrue(
            set(generator.KEIRE_IMPLEMENTATIONS.values()).issubset(runtime_ids)
        )

    def test_validator_rejects_mapping_drift_without_unity(self) -> None:
        manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
        mapped = next(
            entry
            for entry in manifest["entries"]
            if entry["keire"]["implementation"] is not None
        )
        mapped["keire"]["implementation"] = "keire.operator.add"
        encoded = json.dumps(manifest, ensure_ascii=False, indent=2) + "\n"
        with tempfile.TemporaryDirectory() as temporary_directory:
            path = Path(temporary_directory) / "manifest.json"
            with path.open("w", encoding="utf-8", newline="\n") as stream:
                stream.write(encoded)
            result = subprocess.run(
                [sys.executable, str(VALIDATOR), "--manifest", str(path)],
                cwd=REPOSITORY_ROOT,
                capture_output=True,
                text=True,
                check=False,
            )
        self.assertEqual(result.returncode, 1)
        self.assertIn("implementation drifted", result.stderr)

    def test_validator_rejects_noncanonical_encoding_without_unity(self) -> None:
        encoded = MANIFEST.read_bytes().replace(b"\n", b"\r\n")
        with tempfile.TemporaryDirectory() as temporary_directory:
            path = Path(temporary_directory) / "manifest.json"
            path.write_bytes(encoded)
            result = subprocess.run(
                [sys.executable, str(VALIDATOR), "--manifest", str(path)],
                cwd=REPOSITORY_ROOT,
                capture_output=True,
                text=True,
                check=False,
            )
        self.assertEqual(result.returncode, 1)
        self.assertIn("encoding is not canonical", result.stderr)

    def test_checked_manifest_and_capability_reference_are_reconciled(self) -> None:
        for command in (
            [sys.executable, str(RECONCILER), "--check"],
            [sys.executable, str(CAPABILITY_GENERATOR), "--check"],
        ):
            with self.subTest(command=command[1]):
                result = subprocess.run(
                    command,
                    cwd=REPOSITORY_ROOT,
                    capture_output=True,
                    text=True,
                    check=False,
                )
                self.assertEqual(result.returncode, 0, result.stderr)

    def test_every_enabled_implementation_belongs_to_a_production_slice(self) -> None:
        manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
        enabled = {
            entry["keire"]["implementation"]
            for entry in manifest["entries"]
            if entry["keire"]["support"] != "Disabled"
        }
        covered = {
            implementation
            for production_slice in manifest["productionSlices"]
            for implementation in production_slice["implementations"]
        }
        self.assertEqual(enabled, covered)
        for production_slice in manifest["productionSlices"]:
            self.assertTrue(production_slice["samples"])
            for sample in production_slice["samples"]:
                self.assertTrue((REPOSITORY_ROOT / sample).is_file(), sample)

    def test_first_major_milestone_and_priorities_are_auditable(self) -> None:
        manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
        milestone = manifest["firstMajorParityMilestone"]
        self.assertEqual(milestone["target"], 50)
        self.assertGreaterEqual(milestone["completedParityRows"], milestone["target"])
        self.assertTrue(milestone["achieved"])
        self.assertEqual(
            milestone["remainingParityRows"], manifest["counts"]["Disabled"]
        )
        expansion = manifest["portableParityExpansion"]
        self.assertEqual(expansion["baselineParityRows"], 125)
        self.assertEqual(expansion["targetAdditionalRows"], 120)
        self.assertEqual(expansion["completedAdditionalRows"], 120)
        self.assertEqual(expansion["remainingAdditionalRows"], 0)
        self.assertTrue(expansion["achieved"])

        for entry in manifest["entries"]:
            expected = generator.parity_priority(
                entry["kind"],
                entry["unityLabel"],
                entry["unityCategory"],
                entry["keire"]["support"],
            )
            self.assertEqual(entry["keire"]["priority"], expected, entry["id"])

    def test_validator_rejects_priority_drift(self) -> None:
        manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
        unfinished = next(
            entry
            for entry in manifest["entries"]
            if entry["keire"]["support"] == "Disabled"
        )
        unfinished["keire"]["priority"] = "Complete"
        encoded = json.dumps(manifest, ensure_ascii=False, indent=2) + "\n"
        with tempfile.TemporaryDirectory() as temporary_directory:
            path = Path(temporary_directory) / "manifest.json"
            with path.open("w", encoding="utf-8", newline="\n") as stream:
                stream.write(encoded)
            result = subprocess.run(
                [sys.executable, str(VALIDATOR), "--manifest", str(path)],
                cwd=REPOSITORY_ROOT,
                capture_output=True,
                text=True,
                check=False,
            )
        self.assertEqual(result.returncode, 1)
        self.assertIn("priority drifted", result.stderr)


if __name__ == "__main__":
    unittest.main()
