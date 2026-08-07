#!/usr/bin/env python3
"""Validate that a packaged Supabase desktop configuration contains only safe public values."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import re
from urllib.parse import urlsplit


PUBLISHABLE_KEY = re.compile(r"^sb_publishable_[A-Za-z0-9_-]{16,192}$")


def validate(path: Path) -> None:
    if not path.is_file() or path.is_symlink() or path.stat().st_size > 16 * 1024:
        raise ValueError(f"Supabase configuration is missing, unsafe, or oversized: {path}")
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict) or value.get("schemaVersion") != 1 or not isinstance(value.get("enabled"), bool):
        raise ValueError("Supabase configuration has an invalid schema header.")
    if not value["enabled"]:
        if set(value) != {"schemaVersion", "enabled"}:
            raise ValueError("Disabled Supabase configuration contains unexpected fields.")
        return
    if set(value) != {"schemaVersion", "enabled", "projectUrl", "publishableKey"}:
        raise ValueError("Enabled Supabase configuration has unexpected fields.")
    project_url = value["projectUrl"]
    publishable_key = value["publishableKey"]
    if not isinstance(project_url, str) or len(project_url) > 2048 or project_url.endswith("/"):
        raise ValueError("Supabase project URL is invalid.")
    parsed = urlsplit(project_url)
    if (
        parsed.scheme != "https"
        or not parsed.hostname
        or not parsed.hostname.endswith(".supabase.co")
        or parsed.username
        or parsed.password
        or parsed.port not in (None, 443)
        or parsed.path not in ("", "/")
        or parsed.query
        or parsed.fragment
    ):
        raise ValueError("Supabase project URL must be a canonical HTTPS supabase.co origin.")
    if not isinstance(publishable_key, str) or not PUBLISHABLE_KEY.fullmatch(publishable_key):
        raise ValueError("Supabase desktop configuration must contain a modern publishable key.")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", required=True, type=Path)
    args = parser.parse_args()
    validate(args.config)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
