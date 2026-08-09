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
        self.temporary = tempfile.TemporaryDirectory(
            prefix="keire-distribution-preparation-"
        )
        self.root = Path(self.temporary.name)
        self.expiry = (datetime.now(timezone.utc) + timedelta(days=1)).strftime(
            "%Y-%m-%dT%H:%M:%SZ"
        )
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

    @staticmethod
    def compact_descriptor(document: dict[str, object], manifest: Path) -> dict[str, object]:
        descriptor = {key: value for key, value in document.items() if key != "files"}
        manifest_bytes = manifest.read_bytes()
        descriptor["manifest"] = {
            "sizeBytes": len(manifest_bytes),
            "sha256": hashlib.sha256(manifest_bytes).hexdigest(),
        }
        return descriptor

    def run_preparer(
        self,
        output: str,
        additional: list[tuple[Path, Path]] | None = None,
        reverse: bool = False,
    ) -> subprocess.CompletedProcess[str]:
        environment = dict(os.environ)
        environment["PYTHONDONTWRITEBYTECODE"] = "1"
        package_pairs = [(self.manifest, self.package), *(additional or [])]
        if reverse:
            package_pairs.reverse()
        package_arguments: list[str] = []
        for manifest, package in package_pairs:
            package_arguments.extend(
                ["--package-manifest", str(manifest), "--package", str(package)]
            )
        return subprocess.run(
            [
                sys.executable,
                str(PREPARER),
                *package_arguments,
                "--output",
                str(self.root / output),
                "--key-id",
                KEY_ID,
                "--sequence",
                "7",
                "--expires-at",
                self.expiry,
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
            (
                self.root
                / "prepared"
                / "catalogs"
                / "stable"
                / "windows"
                / "x86_64.json"
            ).read_text(encoding="utf-8")
        )
        self.assertEqual(catalog["keyId"], KEY_ID)
        self.assertEqual(catalog["sequence"], 7)
        self.assertEqual(catalog["schemaVersion"], 1)
        self.assertEqual(catalog["packages"], [self.document])
        compact_catalog = json.loads(
            (
                self.root
                / "prepared"
                / "catalogs-v2"
                / "stable"
                / "windows"
                / "x86_64.json"
            ).read_text(encoding="utf-8")
        )
        self.assertEqual(compact_catalog["schemaVersion"], 2)
        self.assertEqual(
            compact_catalog["packages"],
            [self.compact_descriptor(self.document, self.manifest)],
        )
        manifest_digest = hashlib.sha256(self.manifest.read_bytes()).hexdigest()
        self.assertEqual(
            (self.root / "prepared" / "manifests" / f"{manifest_digest}.json").read_bytes(),
            self.manifest.read_bytes(),
        )
        digest = self.document["artifact"]["sha256"]
        self.assertEqual(
            (self.root / "prepared" / "packages" / digest).read_bytes(),
            self.package.read_bytes(),
        )

    def test_combines_hub_installer_and_multiple_host_catalogs(self) -> None:
        installer = self.root / "keire-hub.exe"
        installer.write_bytes(b"native-installer")
        installer_digest = hashlib.sha256(installer.read_bytes()).hexdigest()
        installer_manifest = self.root / "hub-installer.json"
        hub_document = {
            **self.document,
            "packageId": "keire.hub",
            "type": "hubInstaller",
            "displayName": "Keire Hub 1.2.3",
            "artifact": {
                "sizeBytes": installer.stat().st_size,
                "sha256": installer_digest,
            },
            "installedSizeBytes": installer.stat().st_size,
            "files": [
                {
                    "path": installer.name,
                    "sizeBytes": installer.stat().st_size,
                    "sha256": installer_digest,
                    "mode": 420,
                }
            ],
        }
        installer_manifest.write_text(json.dumps(hub_document), encoding="utf-8")

        linux_package = self.root / "editor.deb"
        linux_package.write_bytes(b"linux-package")
        linux_digest = hashlib.sha256(linux_package.read_bytes()).hexdigest()
        linux_manifest = self.root / "linux-manifest.json"
        linux_document = {
            **self.document,
            "platform": "linux",
            "artifact": {
                "sizeBytes": linux_package.stat().st_size,
                "sha256": linux_digest,
            },
        }
        linux_manifest.write_text(json.dumps(linux_document), encoding="utf-8")

        result = self.run_preparer(
            "combined",
            [(installer_manifest, installer), (linux_manifest, linux_package)],
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        windows = json.loads(
            (self.root / "combined/catalogs/stable/windows/x86_64.json").read_text(
                encoding="utf-8"
            )
        )
        self.assertEqual(
            [package["packageId"] for package in windows["packages"]],
            ["keire.editor", "keire.hub"],
        )
        linux = json.loads(
            (self.root / "combined/catalogs-v2/stable/linux/x86_64.json").read_text(
                encoding="utf-8"
            )
        )
        self.assertEqual(
            linux["packages"],
            [self.compact_descriptor(linux_document, linux_manifest)],
        )
        self.assertEqual(
            (self.root / "combined/packages" / installer_digest).read_bytes(),
            installer.read_bytes(),
        )

        reversed_result = self.run_preparer(
            "combined-reversed",
            [(installer_manifest, installer), (linux_manifest, linux_package)],
            reverse=True,
        )
        self.assertEqual(reversed_result.returncode, 0, reversed_result.stderr)
        combined_files = {
            path.relative_to(self.root / "combined"): path.read_bytes()
            for path in (self.root / "combined").rglob("*")
            if path.is_file()
        }
        reversed_files = {
            path.relative_to(self.root / "combined-reversed"): path.read_bytes()
            for path in (self.root / "combined-reversed").rglob("*")
            if path.is_file()
        }
        self.assertEqual(reversed_files, combined_files)

        duplicate = self.run_preparer("duplicate", [(self.manifest, self.package)])
        self.assertNotEqual(duplicate.returncode, 0)

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
