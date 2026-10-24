#!/usr/bin/env python3
"""Regression checks for packaged public-only Supabase configuration."""

from __future__ import annotations

import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
VALIDATOR = ROOT / "Scripts" / "Packaging" / "validate-supabase-config.py"


class SupabaseConfigurationTests(unittest.TestCase):
    def test_packaged_configuration_selects_native_website_authentication(self) -> None:
        document = json.loads((ROOT / "Config" / "Supabase.json").read_text(encoding="utf-8"))
        self.assertEqual(document, {
            "schemaVersion": 3,
            "enabled": True,
            "provider": "mysql",
            "projectUrl": "https://keireengine.duckdns.org",
            "hubOAuthClientId": "keire-hub-desktop",
        })
        self.assertEqual(self.validate(document).returncode, 0)

    def test_native_configuration_rejects_secrets_and_cross_provider_fields(self) -> None:
        valid = {
            "schemaVersion": 3,
            "enabled": True,
            "provider": "mysql",
            "projectUrl": "https://platform.example",
            "hubOAuthClientId": "keire-hub-desktop",
        }
        self.assertEqual(self.validate(valid).returncode, 0)
        self.assertEqual(self.validate({
            "schemaVersion": 3, "enabled": False, "provider": "mysql",
        }).returncode, 0)
        for change in (
            {"provider": "supabase"},
            {"publishableKey": "sb_publishable_0123456789abcdef"},
            {"serviceRoleKey": "private"},
            {"hubOAuthWebsiteCallbackUrl": "https://other.example/oauth/hub/callback/"},
            {"hubOAuthEnabled": True},
            {"enabled": False},
            {"hubOAuthClientId": "client with spaces"},
            {"hubOAuthClientId": "short"},
            {"schemaVersion": True},
        ):
            with self.subTest(change=change):
                self.assertNotEqual(self.validate(dict(valid, **change)).returncode, 0)
        for origin in (
            "http://platform.example", "https://platform.example/", "https://platform.example/path",
            "https://user@platform.example", "https://platform.example:444",
            "https://platform.example?", "https://platform.example#", "https://platform.example\n",
        ):
            with self.subTest(origin=origin):
                self.assertNotEqual(self.validate(dict(valid, projectUrl=origin)).returncode, 0)

    def validate(self, document: dict[str, object]) -> subprocess.CompletedProcess[str]:
        with tempfile.TemporaryDirectory(prefix="keire-supabase-config-") as directory:
            path = Path(directory) / "Supabase.json"
            path.write_text(json.dumps(document), encoding="utf-8")
            environment = dict(os.environ)
            environment["PYTHONDONTWRITEBYTECODE"] = "1"
            return subprocess.run(
                [sys.executable, str(VALIDATOR), "--config", str(path)],
                check=False,
                capture_output=True,
                text=True,
                env=environment,
            )

    def test_accepts_only_public_desktop_configuration(self) -> None:
        valid = {
            "schemaVersion": 1,
            "enabled": True,
            "projectUrl": "https://fixture.supabase.co",
            "publishableKey": "sb_publishable_0123456789abcdef",
        }
        self.assertEqual(self.validate(valid).returncode, 0)
        self.assertEqual(
            self.validate({"schemaVersion": 1, "enabled": False}).returncode, 0
        )

        self.assertNotEqual(
            self.validate(
                dict(valid, publishableKey="sb_secret_0123456789abcdef")
            ).returncode,
            0,
        )
        self.assertNotEqual(
            self.validate(
                dict(valid, projectUrl="http://fixture.supabase.co")
            ).returncode,
            0,
        )
        self.assertNotEqual(
            self.validate(dict(valid, projectUrl="https://example.com")).returncode, 0
        )
        self.assertNotEqual(
            self.validate(dict(valid, serviceRoleKey="never")).returncode, 0
        )


if __name__ == "__main__":
    unittest.main()
