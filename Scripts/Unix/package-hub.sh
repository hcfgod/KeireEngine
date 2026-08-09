#!/usr/bin/env bash
set -euo pipefail

PLATFORM="$1"
shift
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
source "$ROOT/Scripts/Unix/common.sh"

stage_only=0
filtered_arguments=()
for argument in "$@"; do
  if [[ "$argument" == --stage-only ]]; then stage_only=1; else filtered_arguments+=("$argument"); fi
done

GENERATOR=ninja
CONFIGURATION=Dist
ARCHITECTURE="$(native_architecture)"
TOOLSET=default
TARGET=KeireHub
CI=0
UPDATE=0
FORCE=0
INSTALL_OPTIONAL=0
ALLOW_DIRTY=0
parse_build_arguments "${filtered_arguments[@]}"
[[ "$CONFIGURATION" == Dist ]] || { printf 'Standalone Hub packaging requires Dist.\n' >&2; exit 1; }
load_project_config "$ROOT"
TOOLSET="$(resolve_unix_toolset "$PLATFORM" "$TOOLSET")"
macos_deployment_target="$(config_value "$ROOT/Config/Dependencies.lock" MACOS_DEPLOYMENT_TARGET)"
system=linux
os_name=linux
platform_name=Linux
if [[ "$PLATFORM" == Mac ]]; then
  system=macosx
  os_name=macos
  platform_name=macOS
fi
read -r dirty development_artifact < <(package_worktree_policy "$ROOT" "$ALLOW_DIRTY" "$CI")

bash "$ROOT/Scripts/Unix/build-info.sh"
common=(--generator "$GENERATOR" --configuration Dist --architecture "$ARCHITECTURE" --toolset "$TOOLSET")
[[ $CI -eq 1 ]] && common+=(--ci)
[[ $UPDATE -eq 1 ]] && common+=(--update)
[[ $FORCE -eq 1 ]] && common+=(--force)
bash "$ROOT/Scripts/$PLATFORM/build.sh" "${common[@]}" --target "$HUB_TARGET"
hub_worker="${PROJECT_NAMESPACE}HubWorker"
bash "$ROOT/Scripts/$PLATFORM/build.sh" "${common[@]}" --target "$hub_worker"
[[ "$PLATFORM" == Linux ]] && activate_linux_toolchain "$ROOT" "$TOOLSET"

output_arch="$(architecture_output_name "$ARCHITECTURE")"
source_root="$ROOT/Build/Bin/Dist-$system-$output_arch/$HUB_TARGET"
[[ -x "$source_root/$HUB_TARGET" ]] || {
  printf 'The standalone Hub build output is missing or not executable: %s\n' "$source_root/$HUB_TARGET" >&2
  exit 1
}

name="$ARTIFACT_PREFIX-hub-$os_name-$ARCHITECTURE-Dist"
distribution_root="$ROOT/Build/Distributions"
stage="$distribution_root/$name"
archive="$ROOT/Artifacts/$name.tar.gz"
validation_root="$ROOT/Artifacts/$name-validation"
case "$stage" in "$distribution_root"/*) ;; *) printf 'Invalid Hub package stage: %s\n' "$stage" >&2; exit 1;; esac
case "$validation_root" in "$ROOT/Artifacts"/*) ;; *) printf 'Invalid Hub validation path: %s\n' "$validation_root" >&2; exit 1;; esac
rm -rf "$stage" "$validation_root"
rm -f "$archive" "$archive.sha256"
mkdir -p "$stage/bin" "$stage/Config/Branding" "$stage/content" "$stage/third-party/licenses" \
  "$ROOT/Artifacts"
cp -R "$source_root/." "$stage/bin/"
hub_worker_source="$ROOT/Build/Bin/Dist-$system-$output_arch/$hub_worker/$hub_worker"
[[ -x "$hub_worker_source" ]] || {
  printf 'The Hub package worker build output is missing or not executable: %s\n' "$hub_worker_source" >&2
  exit 1
}
cp "$hub_worker_source" "$stage/bin/"
cp -R "$ROOT/KeireHubContent/." "$stage/content/"
copy_tracked_tree "$ROOT" Docs "$stage/Docs"
copy_tracked_tree "$ROOT" Samples/KeireSandbox "$stage/Samples/KeireSandbox"
cp "$ROOT/Config/Branding/Keire.png" "$stage/Config/Branding/"
cp "$ROOT/Config/SourceModules.premake.lua" "$stage/Config/"
command -v python3 >/dev/null 2>&1 || { printf 'Python 3 is required to generate package manifests.\n' >&2; exit 1; }
python3 "$ROOT/Scripts/Packaging/validate-supabase-config.py" --config "$ROOT/Config/Supabase.json"
cp "$ROOT/Config/Supabase.json" "$stage/Config/"
cp "$ROOT/README.md" "$ROOT/CHANGELOG.md" "$ROOT/LICENSE.txt" "$ROOT/THIRD_PARTY_NOTICES.md" "$stage/"

dependency_install="$ROOT/Build/Dependencies/$system-$output_arch-$TOOLSET/Release/install"
sodium_runtime_name=libsodium.so
[[ "$PLATFORM" == Mac ]] && sodium_runtime_name=libsodium.dylib
[[ -f "$dependency_install/lib/$sodium_runtime_name" &&
   -f "$dependency_install/share/licenses/libsodium/LICENSE" ]] || {
  printf 'The standalone Hub pinned libsodium runtime or license is missing. Run bootstrap first.\n' >&2
  exit 1
}
cp -L "$dependency_install/lib/$sodium_runtime_name" "$stage/bin/$sodium_runtime_name"
cp "$dependency_install/share/licenses/libsodium/LICENSE" \
  "$stage/third-party/licenses/libsodium-LICENSE.txt"
license_sources=(
  "Vendor/spdlog/LICENSE|spdlog-LICENSE.txt"
  "Vendor/spdlog/include/spdlog/fmt/bundled/fmt.license.rst|fmt-LICENSE.rst"
  "Vendor/json/LICENSE.MIT|nlohmann-json-LICENSE.MIT.txt"
  "Vendor/imgui/LICENSE.txt|dear-imgui-LICENSE.txt"
  "Vendor/zstd/LICENSE|zstandard-LICENSE.txt"
  "Vendor/entt/LICENSE|entt-LICENSE.txt"
  "Vendor/glm/copying.txt|glm-COPYING.txt"
  "Vendor/SDL/LICENSE.txt|SDL3-LICENSE.txt"
  "Vendor/assimp/LICENSE|assimp-LICENSE.txt"
  "Vendor/assimp/contrib/zlib/LICENSE|assimp-zlib-LICENSE.txt"
  "Vendor/stb/LICENSE|stb-LICENSE.txt"
  "Build/Dependencies/coral-patched/LICENSE|Coral-LICENSE.txt"
)
for license in "${license_sources[@]}"; do
  source_relative="${license%%|*}"
  destination_name="${license#*|}"
  [[ -f "$ROOT/$source_relative" ]] || {
    printf 'The standalone Hub license source is missing: %s\n' "$ROOT/$source_relative" >&2
    exit 1
  }
  cp "$ROOT/$source_relative" "$stage/third-party/licenses/$destination_name"
done
for license_name in Jolt-LICENSE.txt Recast-LICENSE.txt miniaudio-LICENSE.txt; do
  [[ -f "$dependency_install/share/licenses/keire/$license_name" ]] || {
    printf 'The standalone Hub dependency license is missing: %s\n' \
      "$dependency_install/share/licenses/keire/$license_name" >&2
    exit 1
  }
  cp "$dependency_install/share/licenses/keire/$license_name" "$stage/third-party/licenses/"
done
dotnet_root="$stage/bin/Managed/Dotnet"
if [[ -f "$dotnet_root/LICENSE.txt" ]]; then
  cp "$dotnet_root/LICENSE.txt" "$stage/third-party/licenses/dotnet-LICENSE.txt"
  cp "$dotnet_root/ThirdPartyNotices.txt" "$stage/third-party/licenses/dotnet-ThirdPartyNotices.txt"
fi

printf '%s\n' '#!/usr/bin/env sh' 'set -eu' \
  'script_dir="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"' \
  "exec \"\$script_dir/bin/$HUB_TARGET\" \"\$@\"" > "$stage/launch-hub.sh"
chmod +x "$stage/launch-hub.sh"

if [[ "$PLATFORM" == Mac ]]; then
  app_root="$stage/${HUB_TARGET}.app/Contents"
  mkdir -p "$app_root/MacOS"
  printf '%s\n' '#!/usr/bin/env sh' 'set -eu' \
    'macos_dir="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"' \
    "exec \"\$macos_dir/../../../bin/$HUB_TARGET\" \"\$@\"" > "$app_root/MacOS/${HUB_TARGET}Launcher"
  chmod +x "$app_root/MacOS/${HUB_TARGET}Launcher"
  bundle_identifier="$(printf '%s' "$ARTIFACT_PREFIX" | tr '[:upper:]' '[:lower:]')"
  printf '%s\n' '<?xml version="1.0" encoding="UTF-8"?>' \
    '<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "https://www.apple.com/DTDs/PropertyList-1.0.dtd">' \
    '<plist version="1.0">' '<dict>' '  <key>CFBundleExecutable</key>' \
    "  <string>${HUB_TARGET}Launcher</string>" '  <key>CFBundleIdentifier</key>' \
    "  <string>org.keire.${bundle_identifier}.hub</string>" '  <key>CFBundleName</key>' \
    "  <string>${PROJECT_IDENTIFIER} Hub</string>" '  <key>CFBundlePackageType</key>' \
    '  <string>APPL</string>' '  <key>CFBundleShortVersionString</key>' \
    "  <string>${PROJECT_VERSION}</string>" '  <key>LSMinimumSystemVersion</key>' \
    "  <string>${macos_deployment_target}</string>" '  <key>NSHighResolutionCapable</key>' '<true/>' '</dict>' \
    '</plist>' > "$app_root/Info.plist"
fi

distribution_arguments=(
  "$ROOT/Scripts/Packaging/write-distribution-config.py"
  --output "$stage/Config/Distribution.json"
)
distribution_service_url="${KEIRE_DISTRIBUTION_SERVICE_URL:-}"
distribution_trusted_key="${KEIRE_DISTRIBUTION_TRUSTED_KEY:-}"
distribution_minimum_sequence="${KEIRE_DISTRIBUTION_MINIMUM_SEQUENCE:-}"
if [[ -n "$distribution_service_url" || -n "$distribution_trusted_key" ]]; then
  distribution_arguments+=(--service-url "$distribution_service_url" --trusted-key "$distribution_trusted_key")
fi
if [[ -n "$distribution_minimum_sequence" ]]; then
  distribution_arguments+=(--minimum-sequence "$distribution_minimum_sequence")
fi
python3 "${distribution_arguments[@]}"
manifest_writer="$ROOT/Scripts/Packaging/write-package-manifest.py"
commit="$(git -C "$ROOT" rev-parse --verify HEAD 2>/dev/null || printf unknown)"
manifest_arguments=(
  "$manifest_writer" write --stage "$stage" --output hub-package.json --artifact hub
  --package-prefix "$ARTIFACT_PREFIX" --project "$PROJECT_IDENTIFIER" --version "$PROJECT_VERSION"
  --channel Stable --commit "$commit" --dirty "$dirty" --development-artifact "$development_artifact"
  --platform "$platform_name" --architecture "$output_arch" --configuration Dist --launcher launch-hub.sh
  --module-definition Config/SourceModules.premake.lua --project-schema-minimum 1 --project-schema-maximum 3
  --entrypoint "hub=bin/$HUB_TARGET" --entrypoint "worker=bin/$hub_worker"
  --template-catalog content/Templates/catalog.json --release-notes CHANGELOG.md
)
dotnet_runtime="$(find "$dotnet_root/shared/Microsoft.NETCore.App" -mindepth 1 -maxdepth 1 -type d -print \
  2>/dev/null | sort | tail -n 1 || true)"
if [[ -n "$dotnet_runtime" ]]; then
  manifest_arguments+=(--toolchain "dotnet-runtime|$(basename "$dotnet_runtime")|bin/Managed/Dotnet")
fi
python3 "${manifest_arguments[@]}"

validate_hub_package_stage "$stage" "$HUB_TARGET" "$CLIENT_TARGET" "$PROJECT_NAMESPACE" "$PLATFORM"
if [[ "$PLATFORM" == Mac ]]; then
  validate_macos_macho_minimum "$stage" "$macos_deployment_target" "$dotnet_root"
fi
hub_version="$("$stage/bin/$HUB_TARGET" --version)"
[[ "$hub_version" == *"$PROJECT_VERSION"* ]] || {
  printf 'Packaged standalone Hub version validation failed.\n' >&2
  exit 1
}
"$stage/bin/$hub_worker" --help | grep -Fq -- '--request' || {
  printf 'Packaged standalone Hub worker validation failed.\n' >&2
  exit 1
}

if [[ $stage_only -eq 1 ]]; then
  printf '==> Standalone Hub package stage created: %s\n' "$stage"
  exit 0
fi

tar -C "$stage" -czf "$archive" .
assert_package_archive_generated_data_free "$archive"
if command -v sha256sum >/dev/null 2>&1; then
  digest="$(sha256sum "$archive" | awk '{print $1}')"
else
  digest="$(shasum -a 256 "$archive" | awk '{print $1}')"
fi
printf '%s  %s\n' "$digest" "$(basename "$archive")" > "$archive.sha256"
mkdir -p "$validation_root"
tar -C "$validation_root" -xzf "$archive"
validate_hub_package_stage "$validation_root" "$HUB_TARGET" "$CLIENT_TARGET" "$PROJECT_NAMESPACE" "$PLATFORM"
if [[ "$PLATFORM" == Mac ]]; then
  validate_macos_macho_minimum "$validation_root" "$macos_deployment_target" \
    "$validation_root/bin/Managed/Dotnet"
fi
python3 "$manifest_writer" validate --stage "$validation_root" --manifest hub-package.json --artifact hub
"$validation_root/bin/$hub_worker" --help | grep -Fq -- '--request' || {
  printf 'Extracted standalone Hub worker validation failed.\n' >&2
  exit 1
}
rm -rf "$validation_root"
printf '==> Ready-to-run standalone Hub distribution: %s\n' "$stage"
printf '==> Launch with: %s\n' "$stage/launch-hub.sh"
printf '==> Standalone Hub package archive created: %s\n' "$archive"
