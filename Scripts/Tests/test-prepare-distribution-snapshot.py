#!/usr/bin/env python3
"""Regression checks for exact signed-distribution snapshot preparation."""

from __future__ import annotations

from datetime import datetime, timedelta, timezone
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
PREPARER = ROOT / "Scripts" / "Packaging" / "prepare-distribution-snapshot.py"
KEY_ID = "ed25519-0123456789abcdef0123456789abcdef"


class DistributionSnapshotPreparationTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory(prefix="keire-distribution-preparation-")
        self.root = Path(self.temporary.name)
        self.package = self.root / "editor.keirepackage"
        self.package.write_bytes(b"package-bytes")
        digest = hashlib.sha256(self.package.read_bytes()).hexdigest()
        self.manifest = self.root / "manifest.json"
        self.document = {
            "schemaVersion": 1,
            "packageId": "keire.editor",
            "version": "1.2.3",
            "type": "editor",
            "displayName": "Keire Editor 1.2.3",
            "channel": "stable",
            "platform": "windows",
            "architecture": "x86_64",
            "artifact": {"sizeBytes": self.package.stat().st_size, "sha256": digest},
            "installedSizeBytes": 3,
            "files": [
                {
                    "path": "bin/KeireClient.exe",
                    "sizeBytes": 3,
                    "sha256": hashlib.sha256(b"exe").hexdigest(),
                    "mode": 420,
                }
            ],
            "signatureKeyId": KEY_ID,
        }
        self.write_manifest()

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def write_manifest(self) -> None:
        self.manifest.write_text(json.dumps(self.document), encoding="utf-8")

    def run_preparer(self, output: str) -> subprocess.CompletedProcess[str]:
        expiry = (datetime.now(timezone.utc) + timedelta(days=1)).strftime("%Y-%m-%dT%H:%M:%SZ")
        environment = dict(os.environ)
        environment["PYTHONDONTWRITEBYTECODE"] = "1"
        return subprocess.run(
            [
                sys.executable,
                str(PREPARER),
                "--package-manifest",
                str(self.manifest),
                "--package",
                str(self.package),
                "--output",
                str(self.root / output),
                "--key-id",
                KEY_ID,
                "--sequence",
                "7",
                "--expires-at",
                expiry,
            ],
            check=False,
            capture_output=True,
            text=True,
            env=environment,
        )

    def test_prepares_exact_catalog_and_content_addressed_package(self) -> None:
        result = self.run_preparer("prepared")
        self.assertEqual(result.returncode, 0, result.stderr)
        catalog = json.loads(
            (self.root / "prepared" / "catalogs" / "stable" / "windows" / "x86_64.json").read_text(
                encoding="utf-8"
            )
        )
        self.assertEqual(catalog["keyId"], KEY_ID)
        self.assertEqual(catalog["sequence"], 7)
        self.assertEqual(catalog["packages"], [self.document])
        digest = self.document["artifact"]["sha256"]
        self.assertEqual((self.root / "prepared" / "packages" / digest).read_bytes(), self.package.read_bytes())

    def test_rejects_tampering_draft_contracts_and_existing_outputs(self) -> None:
        self.package.write_bytes(b"tampered")
        self.assertNotEqual(self.run_preparer("tampered").returncode, 0)
        self.package.write_bytes(b"package-bytes")

        package_id = self.document.pop("packageId")
        self.document["id"] = package_id
        self.write_manifest()
        self.assertNotEqual(self.run_preparer("draft-contract").returncode, 0)

        self.document["packageId"] = self.document.pop("id")
        self.write_manifest()
        self.document["unexpected"] = True
        self.write_manifest()
        self.assertNotEqual(self.run_preparer("unexpected-field").returncode, 0)
        self.document.pop("unexpected")
        self.write_manifest()
        (self.root / "existing").mkdir()
        self.assertNotEqual(self.run_preparer("existing").returncode, 0)


if __name__ == "__main__":
    unittest.main()
