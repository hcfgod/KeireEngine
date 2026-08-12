#!/usr/bin/env python3
"""Exercise canonical Hub-installer manifest publication through the native CLI."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


KEY_ID = "ed25519-0123456789abcdef0123456789abcdef"
PUBLISHER: Path | None = None


class HubPackagePublisherTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        if PUBLISHER is None:
            raise unittest.SkipTest("a publisher executable was not provided")
        cls.publisher = PUBLISHER

    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory(prefix="Kéire-publisher-")
        self.root = Path(self.temporary.name)
        self.manifest = self.root / "hub-package.json"
        self.installer = self.root / "KeireHubSetup.exe"
        self.installer.write_bytes(b"native-hub-installer\0" * 128)
        self.document = {
            "schemaVersion": 2,
            "artifact": "hub",
            "packageId": "keire.hub",
            "project": "Kéire",
            "version": "1.2.3",
            "channel": "Stable",
            "platform": "Windows",
            "architecture": "x86_64",
            "dirty": False,
            "developmentArtifact": False,
        }
        self.write_manifest()

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def write_manifest(self) -> None:
        self.manifest.write_text(json.dumps(self.document), encoding="utf-8")

    def run_publisher(
        self, output: str = "installer.manifest.json", installer: Path | None = None
    ) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [
                str(self.publisher),
                "create-hub-installer",
                "--hub-manifest",
                str(self.manifest),
                "--installer",
                str(installer or self.installer),
                "--manifest-output",
                str(self.root / output),
                "--signature-key-id",
                KEY_ID,
            ],
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            check=False,
        )

    def test_creates_deterministic_canonical_manifest(self) -> None:
        first = self.run_publisher("first.json")
        self.assertEqual(first.returncode, 0, first.stderr)
        second = self.run_publisher("second.json")
        self.assertEqual(second.returncode, 0, second.stderr)
        first_bytes = (self.root / "first.json").read_bytes()
        self.assertEqual((self.root / "second.json").read_bytes(), first_bytes)

        published = json.loads(first_bytes)
        digest = hashlib.sha256(self.installer.read_bytes()).hexdigest()
        self.assertEqual(published["schemaVersion"], 1)
        self.assertEqual(published["packageId"], "keire.hub")
        self.assertEqual(published["version"], "1.2.3")
        self.assertEqual(published["type"], "hubInstaller")
        self.assertEqual(published["channel"], "stable")
        self.assertEqual(published["platform"], "windows")
        self.assertEqual(published["architecture"], "x86_64")
        self.assertEqual(published["signatureKeyId"], KEY_ID)
        self.assertEqual(
            published["artifact"],
            {"sha256": digest, "sizeBytes": self.installer.stat().st_size},
        )
        self.assertEqual(
            published["files"],
            [
                {
                    "mode": 420,
                    "path": self.installer.name,
                    "sha256": digest,
                    "sizeBytes": self.installer.stat().st_size,
                }
            ],
        )

    def test_rejects_dirty_malformed_mismatched_and_existing_outputs(self) -> None:
        self.document["dirty"] = True
        self.write_manifest()
        self.assertNotEqual(self.run_publisher("dirty.json").returncode, 0)
        self.assertFalse((self.root / "dirty.json").exists())

        self.document["dirty"] = False
        self.document["packageId"] = "../unsafe"
        self.write_manifest()
        self.assertNotEqual(self.run_publisher("identity.json").returncode, 0)
        self.assertFalse((self.root / "identity.json").exists())

        self.document["packageId"] = "keire.hub"
        self.write_manifest()
        mismatched = self.root / "KeireHub.dmg"
        mismatched.write_bytes(self.installer.read_bytes())
        self.assertNotEqual(
            self.run_publisher("mismatched.json", mismatched).returncode, 0
        )
        self.assertFalse((self.root / "mismatched.json").exists())

        existing = self.root / "existing.json"
        existing.write_text("preserve", encoding="utf-8")
        self.assertNotEqual(self.run_publisher(existing.name).returncode, 0)
        self.assertEqual(existing.read_text(encoding="utf-8"), "preserve")

        self.manifest.write_text("{", encoding="utf-8")
        self.assertNotEqual(self.run_publisher("malformed.json").returncode, 0)
        self.assertFalse((self.root / "malformed.json").exists())

    def test_accepts_native_deb_and_rpm_linux_installers(self) -> None:
        self.document["platform"] = "Linux"
        self.write_manifest()
        for extension in (".deb", ".rpm"):
            installer = self.root / f"keire-hub{extension}"
            installer.write_bytes(self.installer.read_bytes())
            output = f"linux-{extension[1:]}.json"
            result = self.run_publisher(output, installer)
            self.assertEqual(result.returncode, 0, result.stderr)
            published = json.loads((self.root / output).read_bytes())
            self.assertEqual(published["platform"], "linux")
            self.assertEqual(published["files"][0]["path"], installer.name)

        mismatched = self.root / "KeireHubSetup.exe"
        mismatched.write_bytes(self.installer.read_bytes())
        self.assertNotEqual(
            self.run_publisher("linux-mismatched.json", mismatched).returncode, 0
        )
        self.assertFalse((self.root / "linux-mismatched.json").exists())


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("usage: test-hub-package-publisher.py <publisher-executable>")
    PUBLISHER = Path(sys.argv[1]).resolve(strict=True)
    sys.argv[:] = [sys.argv[0]]
    unittest.main()
