#!/usr/bin/env python3
"""Record attested acceptance results without confusing historical/cross builds with native acceptance."""

from __future__ import annotations

import argparse
from contextlib import contextmanager
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[2]
LANES = ("build", "core-tests", "editor-tests", "render", "sdk-low-level", "sdk-managed",
         "player", "install-update", "interactive")
TARGETS = tuple(f"{system}-{arch}" for system in ("windows", "linux", "macos") for arch in ("x86_64", "arm64"))


def git(root: Path, *arguments: str) -> bytes:
    return subprocess.check_output(["git", "-c", "core.safecrlf=false", *arguments], cwd=root)


def digest(path: Path) -> dict:
    result = hashlib.sha256()
    size = 0
    with path.open("rb") as stream:
        while data := stream.read(1024 * 1024):
            result.update(data)
            size += len(data)
    return {"sha256": result.hexdigest(), "bytes": size}


def snapshot(root: Path) -> dict:
    revision = git(root, "rev-parse", "HEAD").decode().strip()
    status = git(root, "status", "--porcelain=v1", "-z", "--untracked-files=all")
    diff = git(root, "diff", "--no-ext-diff", "--no-textconv", "--ignore-submodules=none", "--binary", "HEAD", "--")
    if re.search(rb"^\+Subproject commit [0-9a-f]+-dirty$", diff, re.MULTILINE):
        raise ValueError("Dirty submodule content cannot be fingerprinted as a release candidate.")
    value = hashlib.sha256(revision.encode() + b"\0" + diff)
    untracked = sorted(git(root, "ls-files", "--others", "--exclude-standard", "-z").split(b"\0"))
    observed = []
    for name in untracked:
        if name:
            path = root / os.fsdecode(name)
            content = (os.readlink(path).encode() if path.is_symlink()
                       else json.dumps(digest(path), sort_keys=True).encode())
            value.update(name + b"\0" + content + b"\0")
            observed.append((path, content))
    # A result must not be assigned to a checkout changing while its identity is captured.
    if (git(root, "rev-parse", "HEAD").decode().strip() != revision
            or git(root, "status", "--porcelain=v1", "-z", "--untracked-files=all") != status
            or git(root, "diff", "--no-ext-diff", "--no-textconv", "--ignore-submodules=none", "--binary", "HEAD", "--") != diff):
        raise ValueError("The checkout changed while capturing evidence identity; retry after edits stop.")
    for path, content in observed:
        latest = (os.readlink(path).encode() if path.is_symlink()
                  else json.dumps(digest(path), sort_keys=True).encode())
        if latest != content:
            raise ValueError("An untracked source changed while capturing evidence identity.")
    return {"revision": revision, "workingTreeSha256": value.hexdigest(), "dirty": bool(status)}


def host_target() -> str:
    system = {"Windows": "windows", "Linux": "linux", "Darwin": "macos"}.get(platform.system(), "unknown")
    architecture = {"AMD64": "x86_64", "x86_64": "x86_64", "aarch64": "arm64",
                    "arm64": "arm64", "ARM64": "arm64"}.get(platform.machine(), "unknown")
    return f"{system}-{architecture}"


def utc_now() -> str:
    return datetime.now(timezone.utc).isoformat()


@contextmanager
def ledger_lock(path: Path):
    path.parent.mkdir(parents=True, exist_ok=True)
    lock = path.with_suffix(path.suffix + ".lock")
    try:
        descriptor = os.open(lock, os.O_CREAT | os.O_EXCL | os.O_WRONLY)
    except FileExistsError as error:
        raise ValueError(f"Ledger is being written: {lock}. A stale lock requires manual inspection.") from error
    try:
        os.close(descriptor)
        yield
    finally:
        lock.unlink()


def write_ledger(path: Path, ledger: dict) -> None:
    descriptor, name = tempfile.mkstemp(prefix=path.name + ".", dir=path.parent)
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8", newline="\n") as stream:
            json.dump(ledger, stream, indent=2, ensure_ascii=False)
            stream.write("\n")
        os.replace(name, path)
    finally:
        if os.path.exists(name):
            os.unlink(name)


def read_ledger(path: Path) -> dict:
    ledger = json.loads(path.read_text(encoding="utf-8"))
    if (not isinstance(ledger, dict) or ledger.get("schemaVersion") != 1 or not isinstance(ledger.get("source"), dict)
            or not isinstance(ledger.get("records"), list) or not isinstance(ledger.get("targets"), list)
            or not ledger["targets"] or any(target not in TARGETS for target in ledger["targets"])):
        raise ValueError("Invalid or unsupported acceptance ledger.")
    source = ledger["source"]
    if (not re.fullmatch(r"[0-9a-f]{40,64}", str(source.get("revision", "")))
            or not re.fullmatch(r"[0-9a-f]{64}", str(source.get("workingTreeSha256", "")))
            or not isinstance(source.get("dirty"), bool)):
        raise ValueError("Invalid acceptance source identity.")
    for record in ledger["records"]:
        if (not isinstance(record, dict) or record.get("target") not in ledger["targets"]
                or record.get("lane") not in LANES or record.get("result") not in ("pass", "fail", "unavailable")
                or record.get("mode") not in ("native", "cross") or not isinstance(record.get("artifacts"), list)):
            raise ValueError("Invalid acceptance record.")
    return ledger


def artifact_record(path: Path, ledger: Path) -> dict:
    return {"path": os.path.relpath(path.resolve(), ledger.parent), **digest(path)}


def verify_artifacts(record: dict, ledger: Path) -> bool:
    try:
        return bool(record["artifacts"]) and all(
            digest(ledger.parent / artifact["path"]) == {"sha256": artifact["sha256"], "bytes": artifact["bytes"]}
            for artifact in record["artifacts"])
    except (OSError, KeyError, TypeError):
        return False


def require_current(ledger: dict, current: dict) -> None:
    if ledger["source"] != current:
        raise ValueError("Evidence source revision/working tree differs. Create a new ledger; do not relabel old runs.")


def make_record(arguments, ledger: dict, path: Path, current: dict) -> dict:
    require_current(ledger, current)
    if arguments.target not in ledger["targets"]:
        raise ValueError("Target is outside this ledger's requested acceptance matrix.")
    if arguments.mode == "native" and arguments.target != host_target():
        raise ValueError("Native evidence target must match this host's operating system and architecture.")
    if arguments.result in ("pass", "fail") and not arguments.artifact:
        raise ValueError("Pass/fail records require retained evidence artifacts.")
    if arguments.result == "unavailable" and not arguments.notes:
        raise ValueError("Unavailable evidence requires the concrete missing capability in --notes.")
    if arguments.lane == "render" and not arguments.backend:
        raise ValueError("Render evidence requires --backend (for example d3d12, vulkan, or metal).")
    return {"target": arguments.target, "lane": arguments.lane, "result": arguments.result,
            "mode": arguments.mode, "host": host_target(), "hostDescription": platform.platform(),
            "toolchain": arguments.toolchain, "backend": arguments.backend, "command": arguments.command,
            "notes": arguments.notes, "recordedAtUtc": utc_now(),
            "artifacts": [artifact_record(Path(item), path) for item in arguments.artifact]}


def summarize(ledger: dict, path: Path, current: dict) -> tuple[list[str], bool]:
    matches = ledger["source"] == current
    lines = [f"Source: {ledger['source']['revision']}",
             f"Working tree: {ledger['source']['workingTreeSha256']}",
             f"Evidence identity: {'CURRENT' if matches else 'HISTORICAL / DIFFERENT WORKING TREE'}"]
    complete = matches
    for target in ledger["targets"]:
        for lane in LANES:
            records = [item for item in ledger["records"] if item.get("target") == target and item.get("lane") == lane]
            latest = records[-1] if records else None
            state = "pending"
            accepted = False
            if latest:
                state = latest.get("result", "invalid")
                if state in ("pass", "fail") and not verify_artifacts(latest, path):
                    state = "invalid-artifact"
                accepted = (state == "pass" and latest.get("mode") == "native" and latest.get("host") == target)
                state += f" ({latest.get('mode', 'invalid')})"
            complete &= accepted
            lines.append(f"{target:16} {lane:15} {state}")
    lines.append("Native matrix: " + ("COMPLETE" if complete else "INCOMPLETE"))
    return lines, complete


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ledger", type=Path, required=True, help="Ignored JSON path under this checkout's Build/.")
    subcommands = parser.add_subparsers(dest="action", required=True)
    begin = subcommands.add_parser("begin", help="Capture source identity before running acceptance.")
    begin.add_argument("--target", action="append", choices=TARGETS, required=True)
    record = subcommands.add_parser("record", help="Attest a completed run; this does not execute or parse tests.")
    record.add_argument("--target", choices=TARGETS, required=True)
    record.add_argument("--lane", choices=LANES, required=True)
    record.add_argument("--result", choices=("pass", "fail", "unavailable"), required=True)
    record.add_argument("--mode", choices=("native", "cross"), default="native")
    record.add_argument("--toolchain", required=True)
    record.add_argument("--command", required=True)
    record.add_argument("--artifact", action="append", default=[])
    record.add_argument("--backend", default="")
    record.add_argument("--notes", default="")
    report = subcommands.add_parser("report")
    report.add_argument("--require-complete", action="store_true")
    arguments = parser.parse_args(argv)
    path = arguments.ledger.resolve()
    try:
        if not path.is_relative_to((ROOT / "Build").resolve()):
            raise ValueError("Ledger must live under ignored Build/ so recording results does not change source identity.")
        if arguments.action == "report":
            lines, complete = summarize(read_ledger(path), path, snapshot(ROOT))
            print("\n".join(lines))
            return 0 if complete or not arguments.require_complete else 1
        with ledger_lock(path):
            current = snapshot(ROOT)
            if arguments.action == "begin":
                if path.exists():
                    raise ValueError("Ledger already exists; use a new path to preserve prior evidence.")
                ledger = {"schemaVersion": 1, "source": current, "createdAtUtc": utc_now(),
                          "targets": sorted(set(arguments.target)), "records": []}
            else:
                ledger = read_ledger(path)
                ledger["records"].append(make_record(arguments, ledger, path, current))
            require_current(ledger, snapshot(ROOT))
            write_ledger(path, ledger)
        print(f"Saved {path}")
        return 0
    except (OSError, ValueError, KeyError, TypeError, subprocess.CalledProcessError) as error:
        print(f"Acceptance evidence: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
