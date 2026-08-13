#!/usr/bin/env bash
set -euo pipefail
umask 0022

PLATFORM="$1"
shift
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
source "$ROOT/Scripts/Unix/common.sh"

GENERATOR=ninja
ARCHITECTURE="$(native_architecture)"
TOOLSET=default
TARGET=KeireClient
CI=0
UPDATE=0
FORCE=0
INSTALL_OPTIONAL=0
ALLOW_DIRTY=0
parse_build_arguments "$@"
load_project_config "$ROOT"
TOOLSET="$(resolve_unix_toolset "$PLATFORM" "$TOOLSET")"
macos_deployment_target="$(config_value "$ROOT/Config/Dependencies.lock" MACOS_DEPLOYMENT_TARGET)"
os_name=linux
[[ "$PLATFORM" == Mac ]] && os_name=macos

common=(--generator "$GENERATOR" --configuration Dist --architecture "$ARCHITECTURE" --toolset "$TOOLSET")
[[ $CI -eq 1 ]] && common+=(--ci)
[[ $UPDATE -eq 1 ]] && common+=(--update)
[[ $FORCE -eq 1 ]] && common+=(--force)
[[ $ALLOW_DIRTY -eq 1 ]] && common+=(--allow-dirty)
bash "$ROOT/Scripts/Unix/package.sh" "$PLATFORM" "${common[@]}" --stage-only
[[ "$PLATFORM" == Linux ]] && activate_linux_toolchain "$ROOT" "$TOOLSET"
hub_worker="${PROJECT_NAMESPACE}HubWorker"
bash "$ROOT/Scripts/$PLATFORM/build.sh" "${common[@]}" --target "$hub_worker"

sdk_name="$ARTIFACT_PREFIX-$os_name-$ARCHITECTURE-Dist"
sdk_stage="$ROOT/Artifacts/$sdk_name"
[[ -d "$sdk_stage" ]] || {
  printf 'The Dist package gate did not produce its staging directory: %s\n' "$sdk_stage" >&2
  exit 1
}

name="$ARTIFACT_PREFIX-editor-$os_name-$ARCHITECTURE-Dist"
distribution_root="$ROOT/Build/Distributions"
stage="$distribution_root/$name"
legacy_stage="$ROOT/Artifacts/$name"
archive="$ROOT/Artifacts/$name.tar.gz"
validation_root="$ROOT/Artifacts/$name-validation"
rm -rf "$stage" "$legacy_stage" "$validation_root"
rm -f "$archive" "$archive.sha256"
mkdir -p "$distribution_root"
mkdir -p "$stage/Config/Branding" "$stage/Config/Marketplace" "$stage/content" "$stage/third-party/licenses"
cp -R "$sdk_stage/bin" "$sdk_stage/samples" "$sdk_stage/Docs" "$stage/"
# The standalone Hub package owns the Hub executable and every native Hub launcher.
rm -f "$stage/bin/$HUB_TARGET"
system=linux
[[ "$PLATFORM" == Mac ]] && system=macosx
output_arch="$(architecture_output_name "$ARCHITECTURE")"
hub_worker_source="$ROOT/Build/Bin/Dist-$system-$output_arch/$hub_worker/$hub_worker"
[[ -x "$hub_worker_source" ]] || {
  printf 'The Hub package worker build output is missing or not executable: %s\n' "$hub_worker_source" >&2
  exit 1
}
cp "$hub_worker_source" "$stage/bin/"
cp -R "$ROOT/KeireHubContent/." "$stage/content/"
cp "$sdk_stage/Config/Client.json" "$stage/Config/"
cp "$ROOT/Config/SourceModules.premake.lua" "$stage/Config/"
cp "$ROOT/Config/Branding/Keire.png" "$stage/Config/Branding/"
cp "$ROOT/Config/Marketplace/trusted-marketplace-key.json" "$stage/Config/Marketplace/"
cp -R "$sdk_stage/third-party/licenses" "$stage/third-party/"
cp "$sdk_stage/README.md" "$sdk_stage/LICENSE.txt" "$sdk_stage/THIRD_PARTY_NOTICES.md" \
  "$sdk_stage/build-manifest.json" "$stage/"
cp "$ROOT/CHANGELOG.md" "$stage/"

dependency_install="$ROOT/Build/Dependencies/$system-$output_arch-$TOOLSET/Release/install"
sodium_runtime_name=libsodium.so
[[ "$PLATFORM" == Mac ]] && sodium_runtime_name=libsodium.dylib
[[ -f "$dependency_install/lib/$sodium_runtime_name" &&
   -f "$dependency_install/share/licenses/libsodium/LICENSE" ]] || {
  printf 'The Editor marketplace verifier runtime or license is missing. Run bootstrap first.\n' >&2
  exit 1
}
cp -L "$dependency_install/lib/$sodium_runtime_name" "$stage/bin/$sodium_runtime_name"
cp "$dependency_install/share/licenses/libsodium/LICENSE" \
  "$stage/third-party/licenses/libsodium-LICENSE.txt"

dotnet_source="$ROOT/Build/Dependencies/dotnet-sdk"
dotnet_destination="$stage/bin/Managed/Dotnet"
[[ -x "$dotnet_source/dotnet" ]] || {
  printf 'The bundled .NET SDK is missing or not executable: %s\n' "$dotnet_source/dotnet" >&2
  exit 1
}
rm -rf "$dotnet_destination"
mkdir -p "$dotnet_destination"
# Product manifests and deterministic archives intentionally reject symbolic links.
# Materialize the SDK's platform-pack aliases so the installed editor payload is
# self-contained and remains valid after the source SDK tree is removed.
cp -RL "$dotnet_source/." "$dotnet_destination/"
chmod +x "$dotnet_destination/dotnet"

dotnet_sdk="$(find "$dotnet_destination/sdk" -mindepth 1 -maxdepth 1 -type d -name '10.*' -print |
  sort | tail -n 1)"
[[ -n "$dotnet_sdk" ]] || { printf 'The bundled editor runtime does not contain the .NET 10 SDK.\n' >&2; exit 1; }
dotnet_sdk="$(basename "$dotnet_sdk")"

printf '%s\n' '#!/usr/bin/env sh' 'set -eu' \
  'script_dir="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"' \
  "exec \"\$script_dir/bin/$CLIENT_TARGET\" \"\$@\"" > "$stage/launch-editor.sh"
chmod +x "$stage/launch-editor.sh"

if [[ "$PLATFORM" == Mac ]]; then
  app_root="$stage/${CLIENT_TARGET}.app/Contents"
  mkdir -p "$app_root/MacOS"
  printf '%s\n' '#!/usr/bin/env sh' 'set -eu' \
    'macos_dir="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"' \
    "exec \"\$macos_dir/../../../bin/$CLIENT_TARGET\" \"\$@\"" > "$app_root/MacOS/${CLIENT_TARGET}Launcher"
  chmod +x "$app_root/MacOS/${CLIENT_TARGET}Launcher"
  bundle_identifier="$(printf '%s' "$ARTIFACT_PREFIX" | tr '[:upper:]' '[:lower:]')"
  printf '%s\n' '<?xml version="1.0" encoding="UTF-8"?>' \
    '<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "https://www.apple.com/DTDs/PropertyList-1.0.dtd">' \
    '<plist version="1.0">' '<dict>' '  <key>CFBundleExecutable</key>' \
    "  <string>${CLIENT_TARGET}Launcher</string>" '  <key>CFBundleIdentifier</key>' \
    "  <string>org.keire.${bundle_identifier}.editor</string>" '  <key>CFBundleName</key>' \
    "  <string>${PROJECT_IDENTIFIER} Editor</string>" '  <key>CFBundlePackageType</key>' \
    '  <string>APPL</string>' '  <key>CFBundleShortVersionString</key>' \
    "  <string>${PROJECT_VERSION}</string>" '  <key>LSMinimumSystemVersion</key>' \
    "  <string>${macos_deployment_target}</string>" '  <key>NSHighResolutionCapable</key>' '<true/>' '</dict>' \
    '</plist>' > "$app_root/Info.plist"
fi

read -r dirty development_artifact < <(package_worktree_policy "$ROOT" "$ALLOW_DIRTY" "$CI")
commit="$(git -C "$ROOT" rev-parse --verify HEAD)"
platform_name=Linux
[[ "$PLATFORM" == Mac ]] && platform_name=macOS
command -v python3 >/dev/null 2>&1 || { printf 'Python 3 is required to generate package manifests.\n' >&2; exit 1; }
manifest_writer="$ROOT/Scripts/Packaging/write-package-manifest.py"
manifest_arguments=(
  "$manifest_writer" write --stage "$stage" --output editor-package.json
  --artifact editor --package-prefix "$ARTIFACT_PREFIX" --project "$PROJECT_IDENTIFIER"
  --version "$PROJECT_VERSION" --channel Stable --commit "$commit" --dirty "$dirty"
  --development-artifact "$development_artifact" --platform "$platform_name"
  --architecture "$(architecture_output_name "$ARCHITECTURE")" --configuration Dist
  --launcher launch-editor.sh --build-manifest build-manifest.json --bundled-dotnet-sdk "$dotnet_sdk"
  --module-definition Config/SourceModules.premake.lua --project-schema-minimum 1 --project-schema-maximum 3
  --entrypoint "editor=bin/$CLIENT_TARGET" --entrypoint "worker=bin/$hub_worker"
  --entrypoint "assetTool=bin/${PROJECT_NAMESPACE}AssetTool"
  --entrypoint "assetWorker=bin/${PROJECT_NAMESPACE}AssetWorker"
  --entrypoint "runtime=bin/${PROJECT_NAMESPACE}Runtime" --entrypoint shaderCompiler=bin/KeireShaderCompiler
  --template-catalog content/Templates/catalog.json
  --toolchain "dotnet-sdk|$dotnet_sdk|bin/Managed/Dotnet" --release-notes CHANGELOG.md
)
python3 "${manifest_arguments[@]}"

validate_editor_package_stage "$stage" "$CLIENT_TARGET" "$HUB_TARGET" "$CORE_TARGET" "$PROJECT_NAMESPACE" \
  "$PLATFORM"
if [[ "$PLATFORM" == Mac ]]; then
  validate_macos_macho_minimum "$stage" "$macos_deployment_target" "$dotnet_destination"
fi
editor_version="$("$stage/bin/$CLIENT_TARGET" --version)"
[[ "$editor_version" == *"$PROJECT_VERSION"* ]] || {
  printf 'Packaged editor version validation failed.\n' >&2
  exit 1
}
"$stage/bin/$hub_worker" --help | grep -Fq -- '--request' || {
  printf 'Packaged Hub worker validation failed.\n' >&2
  exit 1
}
"$dotnet_destination/dotnet" --list-sdks | grep -Fq "$dotnet_sdk" || {
  printf 'Packaged .NET SDK validation failed.\n' >&2
  exit 1
}

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
validate_editor_package_stage "$validation_root" "$CLIENT_TARGET" "$HUB_TARGET" "$CORE_TARGET" \
  "$PROJECT_NAMESPACE" "$PLATFORM"
if [[ "$PLATFORM" == Mac ]]; then
  validate_macos_macho_minimum "$validation_root" "$macos_deployment_target" \
    "$validation_root/bin/Managed/Dotnet"
fi
python3 "$manifest_writer" validate --stage "$validation_root" --manifest editor-package.json --artifact editor
"$validation_root/bin/Managed/Dotnet/dotnet" --list-sdks | grep -Fq "$dotnet_sdk" || {
  printf 'Extracted editor package .NET SDK validation failed.\n' >&2
  exit 1
}
"$validation_root/bin/$hub_worker" --help | grep -Fq -- '--request' || {
  printf 'Extracted editor package Hub worker validation failed.\n' >&2
  exit 1
}
rm -rf "$validation_root" "$sdk_stage"
printf '==> Ready-to-run editor distribution: %s\n' "$stage"
printf '==> Launch with: %s --project <path>\n' "$stage/launch-editor.sh"
printf '==> Editor package archive created: %s\n' "$archive"
