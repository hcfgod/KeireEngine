#!/usr/bin/env bash
set -euo pipefail

requested_architecture="${1:-x86_64}"
signature_key_id="${3:-}"
channel="${4:-stable}"
case "$(printf '%s' "$requested_architecture" | tr '[:upper:]' '[:lower:]')" in
    x86_64) architecture=x86_64; build_architecture=x86_64 ;;
    arm64|aarch64) architecture=arm64; build_architecture=ARM64 ;;
    *) printf 'Architecture must be x86_64 or arm64.\n' >&2; exit 2 ;;
esac

repository_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
output_directory="${2:-$repository_root/Build/PlayerSupport}"
case "$(uname -s)" in
    Linux) platform="linux"; system="linux"; toolset="clang" ;;
    Darwin) platform="macos"; system="macosx"; toolset="clang" ;;
    *) printf 'Player Support release packaging requires Linux or macOS.\n' >&2; exit 2 ;;
esac

case "$(uname -m)" in
    x86_64|amd64) host_architecture=x86_64 ;;
    arm64|aarch64) host_architecture=ARM64 ;;
    *) printf 'Unsupported host architecture.\n' >&2; exit 2 ;;
esac
"$repository_root/Scripts/project.sh" build --generator ninja --configuration Debug --architecture "$host_architecture" --toolset "$toolset" --target KeireAssetTool
asset_tool="$repository_root/Build/Bin/Debug-$system-$host_architecture/KeireAssetTool/KeireAssetTool"
metadata="$($asset_tool describe-player-support-host)"

for configuration in Debug Release Dist; do
    "$repository_root/Scripts/project.sh" build --generator ninja --configuration "$configuration" --architecture "$build_architecture" --toolset "$toolset" --target KeireRuntime
done

engine_version="$(python3 -c 'import json,sys; print(json.load(sys.stdin)["engineVersion"])' <<<"$metadata")"
pack_id="$platform-$architecture-$engine_version"
mkdir -p "$output_directory"
staging="$(mktemp -d "$output_directory/.staging-XXXXXXXX")"
trap 'rm -rf -- "$staging"' EXIT
payload="$staging/payload"
mkdir -p "$payload"

for mapping in 'Debug:Development' 'Release:Release' 'Dist:Dist'; do
    build="${mapping%%:*}"
    variant="${mapping##*:}"
    source="$repository_root/Build/Bin/$build-$system-$build_architecture/KeireRuntime"
    test -x "$source/KeireRuntime" || { printf 'Incomplete player template: %s\n' "$source" >&2; exit 1; }
    if [[ "$platform" == macos ]]; then
        destination="$payload/$variant/KeireRuntime.app/Contents"
        mkdir -p "$destination/MacOS" "$destination/Resources"
        cp -R "$source"/. "$destination/MacOS/"
        if [[ -d "$destination/MacOS/Managed" ]]; then
            mv "$destination/MacOS/Managed" "$destination/Resources/Managed"
        fi
    else
        mkdir -p "$payload/$variant"
        cp -R "$source"/. "$payload/$variant/"
    fi
done

METADATA="$metadata" PLATFORM="$platform" ARCHITECTURE="$architecture" PACK_ID="$pack_id" \
python3 - "$staging/manifest.json" <<'PY'
import json, os, sys
metadata = json.loads(os.environ["METADATA"])
platform = os.environ["PLATFORM"]
arch = os.environ["ARCHITECTURE"]
variants = []
for configuration, root in (("development", "Development"), ("release", "Release"), ("dist", "Dist")):
    if platform == "macos":
        executable = "KeireRuntime.app/Contents/MacOS/KeireRuntime"
        bundle = "KeireRuntime.app"
    else:
        executable = "KeireRuntime"
        bundle = ""
    variants.append({"configuration": configuration, "root": root, "executable": executable,
                     "bundle": bundle, "symbols": []})
manifest = {"schemaVersion": 1, "playerAbi": metadata["playerAbi"], "id": os.environ["PACK_ID"],
            "engineVersion": metadata["engineVersion"], "platform": platform, "architecture": arch,
            "moduleFingerprint": metadata["moduleFingerprint"], "sourceModules": metadata["sourceModules"],
            "variants": variants, "files": [], "brandingSlots": []}
with open(sys.argv[1], "w", encoding="utf-8", newline="\n") as output:
    json.dump(manifest, output, indent=2)
    output.write("\n")
PY

archive="$output_directory/$pack_id.keireplayersupport"
"$asset_tool" pack-player-support --catalog "$staging/manifest.json" --input "$payload" --output "$archive" --compression-level 9
"$asset_tool" verify-player-support --input "$archive"

ARCHIVE="$archive" ENGINE_VERSION="$engine_version" PLATFORM="$platform" ARCHITECTURE="$architecture" PACK_ID="$pack_id" \
python3 - "$output_directory/player-support-catalog.json" <<'PY'
import hashlib, json, os, pathlib, sys
archive = pathlib.Path(os.environ["ARCHIVE"])
path = pathlib.Path(sys.argv[1])
packages = []
if path.is_file():
    existing = json.loads(path.read_text(encoding="utf-8"))
    if existing.get("engineVersion") != os.environ["ENGINE_VERSION"]:
        raise SystemExit("Existing player-support-catalog.json targets a different engine version.")
    packages = [item for item in existing.get("packages", []) if item.get("id") != os.environ["PACK_ID"]]
digest = hashlib.sha256()
with archive.open("rb") as source:
    for chunk in iter(lambda: source.read(1024 * 1024), b""):
        digest.update(chunk)
packages.append({"id": os.environ["PACK_ID"], "platform": os.environ["PLATFORM"],
                 "architecture": os.environ["ARCHITECTURE"], "file": archive.name,
                 "size": archive.stat().st_size, "sha256": digest.hexdigest()})
catalog = {"schemaVersion": 1, "engineVersion": os.environ["ENGINE_VERSION"],
           "packages": sorted(packages, key=lambda item: item["id"])}
temporary = path.with_name(path.name + ".tmp")
with open(temporary, "w", encoding="utf-8", newline="\n") as output:
    json.dump(catalog, output, indent=2)
    output.write("\n")
temporary.replace(path)
PY
printf 'Created %s\n' "$archive"
if [[ -n "$signature_key_id" ]]; then
    "$repository_root/Scripts/project.sh" build --generator ninja --configuration Debug \
        --architecture "$host_architecture" --toolset "$toolset" --target KeireHubPackagePublisher
    publisher="$repository_root/Build/Bin/Debug-$system-$host_architecture/KeireHubPackagePublisher/KeireHubPackagePublisher"
    "$publisher" create-build-support --player-support-package "$archive" --channel "$channel" \
        --output "$output_directory/$pack_id.keirepackage" \
        --manifest-output "$output_directory/$pack_id.manifest.json" --signature-key-id "$signature_key_id"
fi
