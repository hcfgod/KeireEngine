#!/usr/bin/env bash
set -euo pipefail

repository_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

require_regular_player_support_file() {
    local path="$1"
    [[ -f "$path" && ! -L "$path" ]] || {
        printf 'Player Support requires a regular non-symbolic file: %s\n' "$path" >&2
        return 1
    }
}

require_player_support_directory() {
    local path="$1"
    [[ -d "$path" && ! -L "$path" ]] || {
        printf 'Player Support requires a non-symbolic directory: %s\n' "$path" >&2
        return 1
    }
}

copy_file_if_changed() {
    local source="$1" destination="$2"
    require_regular_player_support_file "$source"
    if [[ -f "$destination" ]] && cmp -s "$source" "$destination"; then
        touch -r "$source" "$destination"
        chmod 0644 "$destination"
        return
    fi
    mkdir -p "$(dirname "$destination")"
    cp -p "$source" "$destination"
    chmod 0644 "$destination"
}

copy_tree_if_changed() {
    local source_root="$1" destination_root="$2" source_file relative
    require_player_support_directory "$source_root"
    [[ -z "$(find "$source_root" -type l -print -quit)" ]] || {
        printf 'Player Support runtime closure contains a symbolic link: %s\n' "$source_root" >&2
        return 1
    }
    while IFS= read -r -d '' source_file; do
        relative="${source_file#"$source_root"/}"
        copy_file_if_changed "$source_file" "$destination_root/$relative"
    done < <(find "$source_root" -type f -print0)
}

copy_player_support_licenses() {
    local destination="$1" target_architecture="$2" dependency_architecture=x86_64
    [[ "$target_architecture" == arm64 ]] && dependency_architecture=AARCH64
    local system_name
    case "$(uname -s)" in
        Linux) system_name=linux ;;
        Darwin) system_name=macosx ;;
        *) system_name=linux ;;
    esac
    local override="${KEIRE_PLAYER_SUPPORT_LICENSE_SOURCE:-}"
    local source_root="$repository_root"
    [[ -z "$override" ]] || source_root="$override"
    local dependency_install="$source_root/Build/Dependencies/$system_name-$dependency_architecture-clang/Release/install"
    require_player_support_directory "$source_root"
    require_player_support_directory "$dependency_install"
    local names=(
        Keire-LICENSE.txt Keire-THIRD_PARTY_NOTICES.md Coral-LICENSE.txt dotnet-LICENSE.txt
        dotnet-ThirdPartyNotices.txt SDL-LICENSE.txt assimp-LICENSE.txt assimp-zlib-LICENSE.txt stb-LICENSE.txt
        Jolt-LICENSE.txt Recast-LICENSE.txt miniaudio-LICENSE.txt spdlog-LICENSE.txt fmt-LICENSE.rst
        nlohmann-json-LICENSE.MIT.txt dear-imgui-LICENSE.txt zstandard-LICENSE.txt entt-LICENSE.txt glm-COPYING.txt
    )
    local sources=(
        "$source_root/LICENSE.txt" "$source_root/THIRD_PARTY_NOTICES.md"
        "$source_root/Build/Dependencies/coral-patched/LICENSE"
        "$source_root/Build/Dependencies/dotnet-sdk/LICENSE.txt"
        "$source_root/Build/Dependencies/dotnet-sdk/ThirdPartyNotices.txt"
        "$dependency_install/licenses/SDL3/LICENSE.txt"
        "$source_root/Vendor/assimp/LICENSE" "$source_root/Vendor/assimp/contrib/zlib/LICENSE"
        "$source_root/Vendor/stb/LICENSE" "$dependency_install/share/licenses/keire/Jolt-LICENSE.txt"
        "$dependency_install/share/licenses/keire/Recast-LICENSE.txt"
        "$dependency_install/share/licenses/keire/miniaudio-LICENSE.txt"
        "$source_root/Vendor/spdlog/LICENSE"
        "$source_root/Vendor/spdlog/include/spdlog/fmt/bundled/fmt.license.rst"
        "$source_root/Vendor/json/LICENSE.MIT" "$source_root/Vendor/imgui/LICENSE.txt"
        "$source_root/Vendor/zstd/LICENSE" "$source_root/Vendor/entt/LICENSE"
        "$source_root/Vendor/glm/copying.txt"
    )
    local index source
    for ((index = 0; index < ${#names[@]}; ++index)); do
        source="${sources[$index]}"
        copy_file_if_changed "$source" "$destination/${names[$index]}"
    done
}

latest_version_directory() {
    python3 - "$1" <<'PY'
import pathlib, re, sys
root = pathlib.Path(sys.argv[1])
candidates = [path for path in root.iterdir() if path.is_dir() and not path.is_symlink()]
if not candidates:
    raise SystemExit(1)
def key(path):
    return tuple(int(part) if part.isdigit() else part for part in re.split(r"[.-]", path.name))
print(max(candidates, key=key))
PY
}

copy_player_runtime_closure() {
    local source="$1" destination="$2" include_symbols="$3" target_architecture="$4"
    local license_destination="${5:-$destination/Licenses}" executable_name=KeireRuntime
    local managed="$source/Managed" hostfxr runtime notice symbol native
    local required=(Coral.Managed.dll Coral.Managed.deps.json Coral.Managed.runtimeconfig.json Keire.Managed.dll)
    require_player_support_directory "$source"
    [[ -f "$source/$executable_name" ]] || {
        printf 'The player runtime closure is missing: %s\n' "$source/$executable_name" >&2
        return 1
    }
    for notice in sdk packs templates sdk-manifests metadata tools cache logs; do
        [[ ! -e "$managed/Dotnet/$notice" ]] || {
            printf 'The player runtime closure contains prohibited .NET content: %s\n' "$notice" >&2
            return 1
        }
    done
    for native in KeireAssetTool KeireAssetWorker KeireClient KeireShaderCompiler dotnet; do
        [[ ! -e "$source/$native" ]] || {
            printf 'The player runtime closure contains an unexpected executable: %s\n' "$native" >&2
            return 1
        }
    done
    mkdir -p "$destination"
    copy_file_if_changed "$source/$executable_name" "$destination/$executable_name"
    chmod 0755 "$destination/$executable_name"
    for native in "$source"/libnethost.so* "$source"/libnethost*.dylib; do
        [[ -f "$native" ]] && copy_file_if_changed "$native" "$destination/$(basename "$native")"
    done
    for notice in "${required[@]}"; do
        [[ -f "$managed/$notice" ]] || {
            printf 'The player runtime closure is missing: Managed/%s\n' "$notice" >&2
            return 1
        }
        copy_file_if_changed "$managed/$notice" "$destination/Managed/$notice"
    done
    hostfxr="$(latest_version_directory "$managed/Dotnet/host/fxr")"
    runtime="$(latest_version_directory "$managed/Dotnet/shared/Microsoft.NETCore.App")"
    local hostpolicy=libhostpolicy.so
    [[ "$(uname -s)" == Darwin ]] && hostpolicy=libhostpolicy.dylib
    require_regular_player_support_file "$runtime/$hostpolicy"
    require_regular_player_support_file "$runtime/System.Private.CoreLib.dll"
    copy_tree_if_changed "$hostfxr" "$destination/Managed/Dotnet/host/fxr/$(basename "$hostfxr")"
    copy_tree_if_changed "$runtime" "$destination/Managed/Dotnet/shared/Microsoft.NETCore.App/$(basename "$runtime")"
    if [[ -f "$destination/Managed/Dotnet/shared/Microsoft.NETCore.App/$(basename "$runtime")/createdump" ]]; then
        chmod 0755 "$destination/Managed/Dotnet/shared/Microsoft.NETCore.App/$(basename "$runtime")/createdump"
    fi
    copy_player_support_licenses "$license_destination" "$target_architecture"
    if [[ "$include_symbols" == true ]]; then
        for symbol in KeireRuntime.pdb KeireRuntime.ilk; do
            [[ -f "$source/$symbol" ]] && copy_file_if_changed "$source/$symbol" "$destination/$symbol"
        done
    fi
    [[ "$(find "$destination/Managed/Dotnet/host/fxr" -mindepth 1 -maxdepth 1 -type d | wc -l | tr -d ' ')" == 1 ]]
    [[ "$(find "$destination/Managed/Dotnet/shared/Microsoft.NETCore.App" -mindepth 1 -maxdepth 1 -type d | wc -l | tr -d ' ')" == 1 ]]
}

cleanup_player_support_staging() {
    case "$staging" in
        "$output_directory"/.staging-*) ;;
        *) printf 'Refusing to remove an unowned Player Support staging directory.\n' >&2; return 1 ;;
    esac
    [[ -d "$staging" && ! -L "$staging" && -f "$staging/.keire-player-support-operation" &&
       ! -L "$staging/.keire-player-support-operation" ]] || {
        printf 'Refusing to remove an unowned Player Support staging directory.\n' >&2
        return 1
    }
    [[ "${KEIRE_PLAYER_SUPPORT_TEST_FAIL_CLEANUP:-0}" != 1 ]] || {
        printf 'Injected Player Support cleanup failure.\n' >&2
        return 86
    }
    rm -rf -- "$staging"
}

cleanup_player_support_on_exit() {
    local primary_status=$? cleanup_status=0
    trap - EXIT
    set +e
    cleanup_player_support_staging
    cleanup_status=$?
    set -e
    if [[ $primary_status -ne 0 ]]; then
        [[ $cleanup_status -eq 0 ]] || printf 'Player Support cleanup also failed with status %s.\n' "$cleanup_status" >&2
        exit "$primary_status"
    fi
    exit "$cleanup_status"
}

publish_player_support_archive() {
    local source="$1" output="$2" id="$3" engine_version="$4" platform="$5" architecture="$6"
    require_regular_player_support_file "$source"
    mkdir -p "$output"
    require_player_support_directory "$output"
    local status=0 published
    set +e
    published="$(python3 - "$source" "$output" "$id" "$engine_version" "$platform" "$architecture" \
        "${KEIRE_PLAYER_SUPPORT_TEST_FAIL_CATALOG_PUBLISH:-0}" <<'PY'
import fcntl, hashlib, json, os, pathlib, re, sys, uuid
source, output = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2])
pack_id, engine_version, platform, architecture, fail = sys.argv[3:]
MAXIMUM_CATALOG_BYTES = 4 * 1024 * 1024
MAXIMUM_PACKAGE_BYTES = 32 * 1024 * 1024 * 1024
SAFE_SEGMENT = re.compile(r"[A-Za-z0-9._-]{1,128}")
SAFE_VERSION = re.compile(r"[A-Za-z0-9.+-]{1,128}")
def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()
def safe_segment(value):
    return isinstance(value, str) and value not in (".", "..") and SAFE_SEGMENT.fullmatch(value) is not None
def validate_packages(packages):
    if len(packages) > 32:
        raise RuntimeError("Existing Player Support catalog exceeds its package limit.")
    ids, files = set(), set()
    for item in packages:
        if not isinstance(item, dict):
            raise RuntimeError("Existing Player Support catalog entry is invalid or duplicated.")
        required = ("id", "file", "platform", "architecture", "size", "sha256")
        if any(field not in item for field in required):
            raise RuntimeError("Existing Player Support catalog entry is invalid or duplicated.")
        pack_size = item["size"]
        file_key = item["file"].casefold() if isinstance(item["file"], str) else ""
        if (not safe_segment(item["id"]) or not safe_segment(item["file"])
                or not item["file"].endswith(".keireplayersupport")
                or item["platform"] not in ("windows", "linux", "macos")
                or item["architecture"] not in ("x86_64", "arm64")
                or isinstance(pack_size, bool) or not isinstance(pack_size, int)
                or pack_size <= 0 or pack_size > MAXIMUM_PACKAGE_BYTES
                or not isinstance(item["sha256"], str)
                or re.fullmatch(r"[0-9a-f]{64}", item["sha256"]) is None
                or item["id"] in ids or file_key in files):
            raise RuntimeError("Existing Player Support catalog entry is invalid or duplicated.")
        ids.add(item["id"])
        files.add(file_key)
        local_archive = output / item["file"]
        if local_archive.is_symlink() or not local_archive.is_file():
            raise RuntimeError("Existing Player Support catalog references a missing or redirected archive.")
        if local_archive.stat().st_size != pack_size or sha256(local_archive) != item["sha256"]:
            raise RuntimeError("Existing Player Support catalog archive size or digest does not match.")
if (not safe_segment(pack_id) or SAFE_VERSION.fullmatch(engine_version) is None
        or platform not in ("windows", "linux", "macos") or architecture not in ("x86_64", "arm64")
        or source.is_symlink() or not source.is_file()
        or source.stat().st_size <= 0 or source.stat().st_size > MAXIMUM_PACKAGE_BYTES):
    raise RuntimeError("New Player Support catalog entry is invalid.")
digest = sha256(source)
archive = output / f"{digest}.keireplayersupport"
catalog_path = output / "player-support-catalog.json"
temporary = output / f".{catalog_path.name}.{uuid.uuid4().hex}.tmp"
lock_path = output / ".player-support-catalog.lock"
lock_flags = os.O_CREAT | os.O_RDWR | getattr(os, "O_NOFOLLOW", 0)
lock_fd = os.open(lock_path, lock_flags, 0o600)
with os.fdopen(lock_fd, "r+b") as lock:
    fcntl.flock(lock.fileno(), fcntl.LOCK_EX)
    published = False
    try:
        packages = []
        if catalog_path.is_symlink():
            raise RuntimeError("Existing player-support-catalog.json is redirected.")
        if catalog_path.is_file():
            if catalog_path.stat().st_size > MAXIMUM_CATALOG_BYTES:
                raise RuntimeError("Existing player-support-catalog.json exceeds its size limit.")
            existing = json.loads(catalog_path.read_text(encoding="utf-8"))
            if (not isinstance(existing, dict) or existing.get("schemaVersion") != 1
                    or isinstance(existing.get("schemaVersion"), bool)
                    or not isinstance(existing.get("packages"), list)):
                raise RuntimeError("Existing player-support-catalog.json has an invalid schema.")
            if (not isinstance(existing.get("engineVersion"), str)
                    or SAFE_VERSION.fullmatch(existing["engineVersion"]) is None
                    or existing["engineVersion"] != engine_version):
                raise RuntimeError("Existing player-support-catalog.json targets a different engine version.")
            validate_packages(existing["packages"])
            packages = [item for item in existing.get("packages", []) if item.get("id") != pack_id]
        packages.append({"id": pack_id, "platform": platform, "architecture": architecture,
                         "file": archive.name, "size": source.stat().st_size, "sha256": digest})
        if len(packages) > 32 or len({item["file"].casefold() for item in packages}) != len(packages):
            raise RuntimeError("Merged Player Support catalog exceeds its limit or duplicates an archive file.")
        catalog = {"schemaVersion": 1, "engineVersion": engine_version,
                   "packages": sorted(packages, key=lambda item: item["id"])}
        with temporary.open("w", encoding="utf-8", newline="\n") as stream:
            json.dump(catalog, stream, indent=2)
            stream.write("\n")
            stream.flush()
            os.fsync(stream.fileno())
        if archive.is_symlink():
            raise RuntimeError("A content-addressed Player Support archive is redirected.")
        if archive.exists():
            if not archive.is_file():
                raise RuntimeError("A content-addressed Player Support archive is not a regular file.")
            if sha256(archive) != digest:
                raise RuntimeError("A content-addressed Player Support archive has conflicting bytes.")
            source.unlink()
        else:
            source.replace(archive)
            published = True
        if fail == "1":
            raise RuntimeError("Injected Player Support catalog publication failure.")
        temporary.replace(catalog_path)
        published = False
        print(archive)
    except Exception:
        temporary.unlink(missing_ok=True)
        if published:
            archive.unlink(missing_ok=True)
        raise
PY
)" || status=$?
    set -e
    [[ $status -eq 0 ]] || return "$status"
    printf '%s\n' "$published"
}

if [[ -n "${KEIRE_PLAYER_SUPPORT_CLEANUP_PROBE:-}" ]]; then
    staging="$KEIRE_PLAYER_SUPPORT_CLEANUP_PROBE"
    output_directory="$(dirname "$staging")"
    trap cleanup_player_support_on_exit EXIT
    [[ "${KEIRE_PLAYER_SUPPORT_TEST_PRIMARY_FAILURE:-0}" != 1 ]] || exit 42
    exit 0
fi

if [[ -n "${KEIRE_PLAYER_SUPPORT_PUBLISH_SOURCE:-}" || -n "${KEIRE_PLAYER_SUPPORT_PUBLISH_OUTPUT:-}" ||
      -n "${KEIRE_PLAYER_SUPPORT_PUBLISH_ID:-}" ]]; then
    [[ -n "${KEIRE_PLAYER_SUPPORT_PUBLISH_SOURCE:-}" && -n "${KEIRE_PLAYER_SUPPORT_PUBLISH_OUTPUT:-}" &&
       -n "${KEIRE_PLAYER_SUPPORT_PUBLISH_ID:-}" ]] || {
        printf 'All Player Support publication fixture values are required.\n' >&2
        exit 2
    }
    publish_player_support_archive "$KEIRE_PLAYER_SUPPORT_PUBLISH_SOURCE" \
        "$KEIRE_PLAYER_SUPPORT_PUBLISH_OUTPUT" "$KEIRE_PLAYER_SUPPORT_PUBLISH_ID" \
        "${KEIRE_PLAYER_SUPPORT_PUBLISH_ENGINE_VERSION:-test-version}" \
        "${KEIRE_PLAYER_SUPPORT_PUBLISH_PLATFORM:-linux}" \
        "${KEIRE_PLAYER_SUPPORT_PUBLISH_ARCHITECTURE:-x86_64}"
    exit 0
fi

if [[ -n "${KEIRE_PLAYER_SUPPORT_RUNTIME_SOURCE:-}" || -n "${KEIRE_PLAYER_SUPPORT_RUNTIME_DESTINATION:-}" ]]; then
    [[ -n "${KEIRE_PLAYER_SUPPORT_RUNTIME_SOURCE:-}" &&
       -n "${KEIRE_PLAYER_SUPPORT_RUNTIME_DESTINATION:-}" ]] || {
        printf 'Both Player Support runtime fixture paths are required.\n' >&2
        exit 2
    }
    copy_player_runtime_closure "$KEIRE_PLAYER_SUPPORT_RUNTIME_SOURCE" \
        "$KEIRE_PLAYER_SUPPORT_RUNTIME_DESTINATION" true "${KEIRE_PLAYER_SUPPORT_RUNTIME_ARCHITECTURE:-x86_64}"
    exit 0
fi

requested_architecture="${1:-x86_64}"
signature_key_id="${3:-}"
channel="${4:-stable}"
case "$(printf '%s' "$requested_architecture" | tr '[:upper:]' '[:lower:]')" in
    x86_64) architecture=x86_64; build_architecture=x86_64 ;;
    arm64|aarch64) architecture=arm64; build_architecture=ARM64 ;;
    *) printf 'Architecture must be x86_64 or arm64.\n' >&2; exit 2 ;;
esac

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
printf 'owned\n' > "$staging/.keire-player-support-operation"
trap cleanup_player_support_on_exit EXIT
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
        copy_player_runtime_closure "$source" "$destination/MacOS" \
            "$([[ "$variant" != Dist ]] && printf true || printf false)" "$architecture" \
            "$payload/$variant/Licenses"
        if [[ -d "$destination/MacOS/Managed" ]]; then
            mv "$destination/MacOS/Managed" "$destination/Resources/Managed"
        fi
    else
        copy_player_runtime_closure "$source" "$payload/$variant" \
            "$([[ "$variant" != Dist ]] && printf true || printf false)" "$architecture"
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

staged_archive="$staging/$pack_id.keireplayersupport"
"$asset_tool" pack-player-support --catalog "$staging/manifest.json" --input "$payload" \
    --output "$staged_archive" --compression-level 9
"$asset_tool" verify-player-support --input "$staged_archive"
archive="$(publish_player_support_archive "$staged_archive" "$output_directory" "$pack_id" \
    "$engine_version" "$platform" "$architecture")"
printf 'Created %s\n' "$archive"
if [[ -n "$signature_key_id" ]]; then
    "$repository_root/Scripts/project.sh" build --generator ninja --configuration Debug \
        --architecture "$host_architecture" --toolset "$toolset" --target KeireHubPackagePublisher
    publisher="$repository_root/Build/Bin/Debug-$system-$host_architecture/KeireHubPackagePublisher/KeireHubPackagePublisher"
    "$publisher" create-build-support --player-support-package "$archive" --channel "$channel" \
        --output "$output_directory/$pack_id.keirepackage" \
        --manifest-output "$output_directory/$pack_id.manifest.json" --signature-key-id "$signature_key_id"
fi
