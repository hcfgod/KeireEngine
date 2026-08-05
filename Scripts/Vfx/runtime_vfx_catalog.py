#!/usr/bin/env python3
"""Read Kéire's build-time VFX node catalog contract."""

from __future__ import annotations

import re
from dataclasses import dataclass
from pathlib import Path


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_CONTRACT = REPOSITORY_ROOT / "KeireCore/Source/Vfx/VfxNodeCatalogContract.inc"
TYPE_ID_PATTERN = re.compile(r"^keire\.[a-z0-9][a-z0-9.-]*$")
ENTRY_PATTERN = re.compile(
    r'^KEIRE_VFX_NODE\("(?P<type_id>[^"]+)",\s*"(?P<label>[^"]+)",\s*'
    r"(?P<node_class>[A-Za-z]+),\s*(?P<support>[A-Za-z]+),\s*(?P<backend>[A-Za-z]+)\)$"
)
NODE_CLASSES = {
    "Operator",
    "Parameter",
    "Constant",
    "Attribute",
    "Subgraph",
    "Block",
    "Context",
    "Output",
}
SUPPORT_TIERS = {"Supported", "GpuRequired", "KeireEquivalent", "Disabled"}
BACKEND_TIERS = {"CpuOnly", "CpuAndGpu", "GpuRequired"}


@dataclass(frozen=True)
class RuntimeVfxNode:
    type_id: str
    label: str
    node_class: str
    support: str
    backend: str


def load_runtime_catalog(path: Path = DEFAULT_CONTRACT) -> list[RuntimeVfxNode]:
    """Load and strictly validate the macro contract consumed by C++."""

    entries: list[RuntimeVfxNode] = []
    for line_number, raw_line in enumerate(
        path.read_text(encoding="utf-8").splitlines(), start=1
    ):
        line = raw_line.strip()
        if not line or line.startswith("//"):
            continue
        match = ENTRY_PATTERN.fullmatch(line)
        if not match:
            raise ValueError(
                f"{path}:{line_number}: invalid VFX catalog contract entry"
            )
        entry = RuntimeVfxNode(**match.groupdict())
        if TYPE_ID_PATTERN.fullmatch(entry.type_id) is None:
            raise ValueError(f"{path}:{line_number}: invalid type ID {entry.type_id!r}")
        if not entry.label:
            raise ValueError(f"{path}:{line_number}: label must not be empty")
        if entry.node_class not in NODE_CLASSES:
            raise ValueError(
                f"{path}:{line_number}: invalid node class {entry.node_class!r}"
            )
        if entry.support not in SUPPORT_TIERS:
            raise ValueError(
                f"{path}:{line_number}: invalid support tier {entry.support!r}"
            )
        if entry.backend not in BACKEND_TIERS:
            raise ValueError(
                f"{path}:{line_number}: invalid backend tier {entry.backend!r}"
            )
        entries.append(entry)

    if not entries:
        raise ValueError(f"{path}: VFX catalog contract is empty")
    identifiers = [entry.type_id for entry in entries]
    duplicates = sorted(
        {identifier for identifier in identifiers if identifiers.count(identifier) > 1}
    )
    if duplicates:
        raise ValueError(f"{path}: duplicate type IDs: {duplicates}")
    if identifiers != sorted(identifiers):
        raise ValueError(f"{path}: entries are not ordered by type ID")
    return entries


def export_runtime_catalog(entries: list[RuntimeVfxNode]) -> dict[str, object]:
    return {
        "schema": 1,
        "source": "KeireCore/Source/Vfx/VfxNodeCatalogContract.inc",
        "entries": [
            {
                "typeId": entry.type_id,
                "label": entry.label,
                "class": entry.node_class,
                "support": entry.support,
                "backendTier": entry.backend,
            }
            for entry in entries
        ],
    }
