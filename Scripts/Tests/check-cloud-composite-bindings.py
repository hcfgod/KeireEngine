"""Read existing cloud-composite artifacts; never compile shaders or modify the repository."""

import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
import sys


TEXTURES = {
    "SceneDepth": 0, "SceneColor": 1, "CloudRadiance": 2, "CloudDepth": 3,
    "VolumeCloudNoiseTexture": 4, "FogTerrainDepth": 5,
}
SAMPLERS = {
    "SceneDepthSampler": 0, "SceneColorSampler": 1,
    "VolumeCloudNoiseSampler": 4, "FogTerrainSampler": 5,
}
UNIFORMS = {"VolumeConstants": 0, "CloudCacheConstants": 1, "FogTerrainConstants": 2}
STORAGE = {"CloudLightTransmissionCache": 6}
FORMATS = {"DXIL": "Dxil", "SPIRV": "SpirV", "MSL": "Msl"}


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def checked_bytes(item):
    data = Path(item["path"]).read_bytes()
    if sha256(data) != item["sha256"]:
        raise ValueError(f"Source or artifact changed: {item['path']}")
    return data


def require_equal(actual, expected, label):
    if actual != expected:
        raise ValueError(f"{label}: expected {expected!r}; observed {actual!r}")


def spirv_bindings(data):
    if len(data) % 4:
        raise ValueError("SPIR-V byte length is not aligned")
    words = struct.unpack(f"<{len(data) // 4}I", data)
    if len(words) < 5 or words[0] != 0x07230203:
        raise ValueError("Unsupported SPIR-V header")
    names = {}
    decorations = {}
    variables = set()
    offset = 5
    while offset < len(words):
        count, opcode = words[offset] >> 16, words[offset] & 0xFFFF
        if not count or offset + count > len(words):
            raise ValueError("Malformed SPIR-V instruction")
        args = words[offset + 1:offset + count]
        if opcode == 5:  # OpName
            names[args[0]] = struct.pack(f"<{len(args) - 1}I", *args[1:]).split(b"\0")[0].decode()
        elif opcode == 71 and len(args) >= 3 and args[1] in (33, 34):  # Binding / DescriptorSet
            decorations.setdefault(args[0], {})[args[1]] = args[2]
        elif opcode == 59:  # OpVariable
            variables.add(args[1])
        offset += count
    resources = {}
    for identifier, values in decorations.items():
        if identifier in variables and 33 in values:
            name = names.get(identifier)
            if not name:
                raise ValueError("Unnamed SPIR-V bound resource: explicit evidence unavailable")
            resources[name] = [values.get(34), values[33]]
    expected = {name: [2, slot] for name, slot in (TEXTURES | SAMPLERS | STORAGE).items()}
    expected.update({name: [3, slot] for name, slot in UNIFORMS.items()})
    for name, binding in expected.items():
        require_equal(resources.get(name), binding, f"SPIR-V {name}")
    allowed = set(expected) | {"CloudRadianceSampler", "CloudDepthSampler"}
    if set(resources) - allowed:
        raise ValueError(f"Unreviewed SPIR-V resources: {set(resources) - allowed}")
    for name, slot in (("CloudRadianceSampler", 2), ("CloudDepthSampler", 3)):
        if name in resources:
            require_equal(resources[name], [2, slot], f"SPIR-V optional {name}")
    return resources


def msl_bindings(data):
    source = data.decode("utf-8").rstrip("\0")
    entry = re.search(r"\bfragment\s+\w+\s+PSMain\s*\((.*?)\)\s*\{", source, re.S)
    if not entry:
        raise ValueError("Unsupported MSL entry signature; cannot verify addresses")
    resources = {
        name: [kind, int(slot)]
        for name, kind, slot in re.findall(
            r"\b(\w+)\s*\[\[\s*(texture|sampler|buffer)\s*\(\s*(\d+)\s*\)\s*\]\]", entry[1])
    }
    expected = {name: ["texture", slot] for name, slot in TEXTURES.items()}
    expected.update({name: ["sampler", slot] for name, slot in SAMPLERS.items()})
    expected.update({name: ["buffer", slot] for name, slot in UNIFORMS.items()})
    expected["CloudLightTransmissionCache"] = ["buffer", 3]
    require_equal(resources, expected, "MSL resource map")
    for name in ("CloudRadiance", "CloudDepth"):
        if not re.search(rf"\b{name}\.read\(", source):
            raise ValueError(f"MSL no longer uses exact reads for {name}; review gather semantics")
    return resources


def dxil_bindings(path, dxc):
    # Explicit opt-in: this invokes disassembly only, never compilation.
    result = subprocess.run([str(dxc), "-dumpbin", str(path)], capture_output=True,
                            text=True, timeout=60, check=True)
    section = result.stdout.split("Resource Bindings:", 1)
    if len(section) != 2:
        raise ValueError("Unsupported DXIL dump: no Resource Bindings table")
    resources = {}
    for line in section[1].splitlines():
        match = re.match(r";\s*(\w+)\s+.*?\s+(cb|[tsu])(\d+)(?:,space(\d+))?\s+(\d+)\s*$", line)
        if match:
            name, kind, slot, space, count = match.groups()
            resources[name] = [kind, int(slot), int(space or 0), int(count)]
    expected = {name: ["t", slot, 2, 1] for name, slot in (TEXTURES | STORAGE).items()}
    expected.update({name: ["s", slot, 2, 1] for name, slot in SAMPLERS.items()})
    expected.update({name: ["cb", slot, 3, 1] for name, slot in UNIFORMS.items()})
    require_equal(resources, expected, "DXIL resource map")
    return resources


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--report", type=Path, required=True)
    parser.add_argument("--dxc", type=Path)
    parser.add_argument("--allow-unverified-dxil", action="store_true")
    args = parser.parse_args()
    manifest = json.loads(args.manifest.read_text(encoding="utf-8-sig"))
    require_equal(manifest.get("schema"), 1, "provenance schema")
    for item in manifest["inputs"]:
        checked_bytes(item)
    header = checked_bytes(manifest["header"]).decode("utf-8-sig")
    artifacts = {item["format"]: item for item in manifest["artifacts"]}
    require_equal(set(artifacts), set(FORMATS), "artifact formats")
    data = {}
    for fmt, suffix in FORMATS.items():
        data[fmt] = checked_bytes(artifacts[fmt])
        match = re.search(rf"BuiltinCloudCompositeFragment{suffix}\[\]\s*=\s*\{{([^}}]+)\}}", header)
        if not match:
            raise ValueError(f"Missing generated-header array: {fmt}")
        embedded = bytes(int(value, 16) for value in re.findall(r"0x([0-9a-fA-F]{2})", match[1]))
        require_equal(sha256(embedded), artifacts[fmt]["sha256"], f"Current-source {fmt} vs generated header")
    report = {
        "source": manifest["source"], "inputIdentity": manifest["inputIdentity"],
        "headerSha256": manifest["header"]["sha256"],
        "artifactSha256": {fmt: item["sha256"] for fmt, item in artifacts.items()},
        "SPIRV": spirv_bindings(data["SPIRV"]), "MSL": msl_bindings(data["MSL"]),
    }
    if args.dxc:
        report["DXIL"] = dxil_bindings(Path(artifacts["DXIL"]["path"]), args.dxc)
        report["status"] = "all-address-checks-passed"
    else:
        report["DXIL"] = {"status": "unsupported", "reason": "No --dxc disassembler supplied; hash only, bindings NOT parsed"}
        report["status"] = "partial-SPIRV-MSL-only"
    # Reject a race with compilation or source edits during inspection.
    for item in manifest["inputs"] + [manifest["header"]] + manifest["artifacts"]:
        checked_bytes(item)
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))
    return 0 if args.dxc or args.allow_unverified_dxil else 2


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (ValueError, OSError, subprocess.SubprocessError) as error:
        print(f"Cloud composite ABI check failed: {error}", file=sys.stderr)
        sys.exit(1)
