#!/usr/bin/env python3
"""Write a bounded Hub distribution configuration without accepting private key material."""

from __future__ import annotations

import argparse
import base64
import binascii
import hashlib
import json
import os
from pathlib import Path
import tempfile
from urllib.parse import urlsplit


def load_public_key(path: Path) -> dict[str, object]:
    if not path.is_file() or path.stat().st_size > 16 * 1024:
        raise ValueError(f"Trusted public-key document is missing or oversized: {path}")
    value = json.loads(path.read_text(encoding="utf-8"))
    expected = {"schemaVersion", "algorithm", "keyId", "publicKey", "fingerprint"}
    if not isinstance(value, dict) or set(value) != expected:
        raise ValueError(f"Trusted public-key document has unexpected fields: {path}")
    if value["schemaVersion"] != 1 or value["algorithm"] != "Ed25519":
        raise ValueError(f"Trusted public-key document has an invalid header: {path}")
    try:
        key = base64.b64decode(str(value["publicKey"]), validate=True)
    except (binascii.Error, ValueError, TypeError) as exception:
        raise ValueError(f"Trusted public key is not canonical base64: {path}") from exception
    if len(key) != 32 or base64.b64encode(key).decode("ascii") != value["publicKey"]:
        raise ValueError(f"Trusted Ed25519 public key is not exactly 32 bytes: {path}")
    digest = hashlib.sha256(key).hexdigest()
    if value["keyId"] != f"ed25519-{digest[:32]}" or value["fingerprint"] != f"sha256:{digest}":
        raise ValueError(f"Trusted public-key identity does not match its bytes: {path}")
    return value


def canonical_service_url(value: str) -> str:
    if len(value) > 2048 or value.endswith("/"):
        raise ValueError("Distribution service URL must be canonical and have no trailing slash.")
    parsed = urlsplit(value)
    if parsed.scheme != "https" or not parsed.hostname or parsed.username or parsed.password:
        raise ValueError("Distribution service URL must use HTTPS without embedded credentials.")
    if parsed.query or parsed.fragment or parsed.path not in ("", "/"):
        raise ValueError("Distribution service URL must not include a path, query, or fragment.")
    return value


def write_atomic(path: Path, value: dict[str, object]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    encoded = (json.dumps(value, indent=2, ensure_ascii=False) + "\n").encode("utf-8")
    descriptor, temporary_name = tempfile.mkstemp(prefix=f".{path.name}.", suffix=".tmp", dir=path.parent)
    try:
        with os.fdopen(descriptor, "wb") as stream:
            stream.write(encoded)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary_name, path)
    except BaseException:
        try:
            os.unlink(temporary_name)
        except FileNotFoundError:
            pass
        raise


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--service-url", default="")
    parser.add_argument("--trusted-key", action="append", default=[], type=Path)
    parser.add_argument("--minimum-sequence", type=int, default=1)
    args = parser.parse_args()

    if bool(args.service_url) != bool(args.trusted_key):
        raise ValueError("Distribution service URL and trusted public key must be supplied together.")
    if args.minimum_sequence < 1 or args.minimum_sequence > (2**63 - 1):
        raise ValueError("Distribution minimum sequence must be between 1 and 2^63-1.")
    if not args.service_url:
        write_atomic(args.output, {"schemaVersion": 1, "onlineDiscoveryEnabled": False})
        return 0
    if len(args.trusted_key) > 8:
        raise ValueError("At most eight trusted distribution keys may be packaged.")

    keys = [load_public_key(path) for path in args.trusted_key]
    key_ids = [str(key["keyId"]) for key in keys]
    if len(set(key_ids)) != len(key_ids):
        raise ValueError("Trusted distribution key IDs must be unique.")
    write_atomic(
        args.output,
        {
            "schemaVersion": 1,
            "onlineDiscoveryEnabled": True,
            "serviceBaseUrl": canonical_service_url(args.service_url),
            "minimumSequence": args.minimum_sequence,
            "trustedKeys": keys,
        },
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
