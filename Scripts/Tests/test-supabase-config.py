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
