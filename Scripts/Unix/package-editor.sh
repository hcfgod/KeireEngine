#!/usr/bin/env bash
set -euo pipefail

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
os_name=linux
[[ "$PLATFORM" == Mac ]] && os_name=macos

common=(--generator "$GENERATOR" --configuration Dist --architecture "$ARCHITECTURE" --toolset "$TOOLSET")
[[ $CI -eq 1 ]] && common+=(--ci)
[[ $UPDATE -eq 1 ]] && common+=(--update)
[[ $FORCE -eq 1 ]] && common+=(--force)
[[ $ALLOW_DIRTY -eq 1 ]] && common+=(--allow-dirty)
bash "$ROOT/Scripts/Unix/package.sh" "$PLATFORM" "${common[@]}" --stage-only

sdk_name="$ARTIFACT_PREFIX-$os_name-$ARCHITECTURE-Dist"
sdk_stage="$ROOT/Artifacts/$sdk_name"
[[ -d "$sdk_stage" ]] || {
  printf 'The Dist package gate did not produce its staging directory: %s\n' "$sdk_stage" >&2
  exit 1
}

name="$ARTIFACT_PREFIX-editor-$os_name-$ARCHITECTURE-Dist"
stage="$ROOT/Artifacts/$name"
archive="$ROOT/Artifacts/$name.tar.gz"
validation_root="$ROOT/Artifacts/$name-validation"
rm -rf "$stage" "$validation_root"
rm -f "$archive" "$archive.sha256"
mkdir -p "$stage/Config" "$stage/third-party"
cp -R "$sdk_stage/bin" "$sdk_stage/samples" "$sdk_stage/docs" "$stage/"
cp "$sdk_stage/Config/Client.json" "$stage/Config/"
cp -R "$sdk_stage/third-party/licenses" "$stage/third-party/"
cp "$sdk_stage/README.md" "$sdk_stage/LICENSE.txt" "$sdk_stage/THIRD_PARTY_NOTICES.md" \
  "$sdk_stage/build-manifest.json" "$stage/"

dotnet_source="$ROOT/Build/Dependencies/dotnet-sdk"
dotnet_destination="$stage/bin/Managed/Dotnet"
[[ -x "$dotnet_source/dotnet" ]] || {
  printf 'The bundled .NET SDK is missing or not executable: %s\n' "$dotnet_source/dotnet" >&2
  exit 1
}
rm -rf "$dotnet_destination"
mkdir -p "$dotnet_destination"
cp -R "$dotnet_source/." "$dotnet_destination/"
chmod +x "$dotnet_destination/dotnet"

dotnet_sdk="$(find "$dotnet_destination/sdk" -mindepth 1 -maxdepth 1 -type d -name '10.*' -print |
  sort | tail -n 1)"
[[ -n "$dotnet_sdk" ]] || { printf 'The bundled editor runtime does not contain the .NET 10 SDK.\n' >&2; exit 1; }
dotnet_sdk="$(basename "$dotnet_sdk")"
read -r dirty development_artifact < <(package_worktree_policy "$ROOT" "$ALLOW_DIRTY" "$CI")
commit="$(git -C "$ROOT" rev-parse --verify HEAD)"
platform_name=Linux
[[ "$PLATFORM" == Mac ]] && platform_name=macOS
printf '{\n  "schemaVersion": 1,\n  "artifact": "editor",\n  "project": "%s",\n  "version": "%s",\n  "commit": "%s",\n  "dirty": %s,\n  "developmentArtifact": %s,\n  "platform": "%s",\n  "architecture": "%s",\n  "configuration": "Dist",\n  "launcher": "launch-editor.sh",\n  "bundledDotnetSdk": "%s",\n  "buildManifest": "build-manifest.json"\n}\n' \
  "$(json_escape "$PROJECT_IDENTIFIER")" "$(json_escape "$PROJECT_VERSION")" "$(json_escape "$commit")" \
  "$dirty" "$development_artifact" "$platform_name" "$(architecture_output_name "$ARCHITECTURE")" \
  "$(json_escape "$dotnet_sdk")" > "$stage/editor-package.json"

printf '%s\n' '#!/usr/bin/env sh' 'set -eu' \
  'script_dir="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"' \
  "exec \"\$script_dir/bin/$HUB_TARGET\" \"\$@\"" > "$stage/launch-editor.sh"
chmod +x "$stage/launch-editor.sh"

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
    "  <string>${PROJECT_IDENTIFIER} Project Hub</string>" '  <key>CFBundlePackageType</key>' \
    '  <string>APPL</string>' '  <key>CFBundleShortVersionString</key>' \
    "  <string>${PROJECT_VERSION}</string>" '  <key>NSHighResolutionCapable</key>' '<true/>' '</dict>' \
    '</plist>' > "$app_root/Info.plist"
fi

validate_editor_package_stage "$stage" "$CLIENT_TARGET" "$HUB_TARGET" "$CORE_TARGET" "$PROJECT_NAMESPACE" \
  "$PLATFORM"
hub_version="$("$stage/bin/$HUB_TARGET" --version)"
[[ "$hub_version" == *"$PROJECT_VERSION"* ]] || { printf 'Packaged Project Hub version validation failed.\n' >&2; exit 1; }
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
"$validation_root/bin/Managed/Dotnet/dotnet" --list-sdks | grep -Fq "$dotnet_sdk" || {
  printf 'Extracted editor package .NET SDK validation failed.\n' >&2
  exit 1
}
rm -rf "$validation_root" "$sdk_stage"
printf '==> Editor package created: %s\n' "$archive"
