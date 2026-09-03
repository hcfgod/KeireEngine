#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
source "$ROOT/Scripts/Unix/common.sh"

assert_true() { "$@" || { printf 'Assertion failed: %s\n' "$*" >&2; exit 1; }; }
assert_false() { if "$@"; then printf 'Expected failure: %s\n' "$*" >&2; exit 1; fi; }

grep -q 'package-editor' "$ROOT/Scripts/project.sh"
grep -q -- '--configuration Dist' "$ROOT/Scripts/Unix/package-editor.sh"
grep -q -- '--stage-only' "$ROOT/Scripts/Unix/package-editor.sh"
grep -q 'Build/Dependencies/dotnet-sdk' "$ROOT/Scripts/Unix/package-editor.sh"
grep -Fq 'cp -RL "$dotnet_source/." "$dotnet_destination/"' "$ROOT/Scripts/Unix/package-editor.sh"
grep -q 'Build/Distributions' "$ROOT/Scripts/Unix/package-editor.sh"
grep -q 'validate_editor_package_stage' "$ROOT/Scripts/Unix/package-editor.sh"
grep -Fq 'validate_macos_macho_minimum "$stage" "$macos_deployment_target"' \
  "$ROOT/Scripts/Unix/package-editor.sh"
grep -Fq 'xvfb-run -a "$stage/bin/$runtime"' "$ROOT/Scripts/Unix/package.sh"
grep -Fq '$CLIENT_TARGET/Managed" "$stage/bin/"' "$ROOT/Scripts/Unix/package.sh"
grep -Fq '<key>LSMinimumSystemVersion</key>' "$ROOT/Scripts/Unix/package-editor.sh"
grep -q 'write-package-manifest.py' "$ROOT/Scripts/Unix/package-editor.sh"
grep -Fq -- '--project-schema-maximum 4' "$ROOT/Scripts/Unix/package-editor.sh"
grep -Fq 'exec \"\$script_dir/bin/$CLIENT_TARGET\"' "$ROOT/Scripts/Unix/package-editor.sh"
if grep -Fq -- '--entrypoint "hub=' "$ROOT/Scripts/Unix/package-editor.sh"; then
  printf 'The standalone Unix editor package must not publish a Hub entrypoint.\n' >&2
  exit 1
fi

stage="$(mktemp -d)"
archive="$(mktemp).tar.gz"
trap 'rm -rf "$stage"; rm -f "$archive"' EXIT
while IFS= read -r path; do
  mkdir -p "$stage/$(dirname "$path")"
  : > "$stage/$path"
done < <(editor_package_required_paths Client Hub Core Core)
rm -rf "$stage/content"
mkdir -p "$stage/content"
cp -R "$ROOT/KeireHubContent/." "$stage/content/"
for path in bin/Client bin/CoreHubWorker bin/CoreAssetTool bin/CoreAssetWorker bin/CoreRuntime \
  bin/KeireShaderCompiler \
  bin/Managed/Dotnet/dotnet launch-editor.sh; do
  printf '%s\n' '#!/usr/bin/env sh' 'exit 0' > "$stage/$path"
  chmod +x "$stage/$path"
done
for path in libavcodec.so.62 libavformat.so.62 libavutil.so.60 libswresample.so.6; do
  : > "$stage/bin/$path"
done
touch "$stage/bin/libsodium.so" "$stage/bin/libsodium.dylib"
mkdir -p "$stage/bin/Managed/Dotnet/sdk/10.0.100/Sdks/Fixture/build"
: > "$stage/bin/Managed/Dotnet/sdk/10.0.100/Sdks/Fixture/build/Fixture.targets"

manifest_writer="$ROOT/Scripts/Packaging/write-package-manifest.py"
manifest_arguments=(
  "$manifest_writer" write --stage "$stage" --output editor-package.json --artifact editor
  --package-prefix fixture --project Fixture --version 1.2.3 --channel Stable --commit fixture
  --dirty false --development-artifact false --platform Linux --architecture x86_64 --configuration Dist
  --launcher launch-editor.sh --build-manifest build-manifest.json --bundled-dotnet-sdk 10.0.100
  --module-definition Config/SourceModules.premake.lua --entrypoint editor=bin/Client
  --entrypoint worker=bin/CoreHubWorker
  --entrypoint assetTool=bin/CoreAssetTool --entrypoint assetWorker=bin/CoreAssetWorker
  --entrypoint runtime=bin/CoreRuntime --entrypoint shaderCompiler=bin/KeireShaderCompiler
  --template-catalog content/Templates/catalog.json
  --toolchain 'dotnet-sdk|10.0.100|bin/Managed/Dotnet' --release-notes CHANGELOG.md
)
python3 "${manifest_arguments[@]}"
python3 - "$stage/editor-package.json" <<'PY'
import json
import re
import sys

with open(sys.argv[1], encoding="utf-8") as stream:
    manifest = json.load(stream)
assert manifest["schemaVersion"] == 2
assert manifest["entrypoints"]["editor"] == "bin/Client"
assert manifest["entrypoints"]["worker"] == "bin/CoreHubWorker"
assert "hub" not in manifest["entrypoints"]
assert manifest["projectSchema"]["maximum"] == 4
assert len(manifest["packagedTemplates"]) == 3
assert manifest["bundledToolchains"][0]["id"] == "dotnet-sdk"
assert manifest["files"]
assert manifest["installedSizeBytes"] > 0
assert re.fullmatch(r"[0-9a-f]{64}", manifest["manifestFingerprint"])
assert manifest["launcher"] == "launch-editor.sh"
assert manifest["bundledDotnetSdk"] == "10.0.100"
assert manifest["buildManifest"] == "build-manifest.json"
assert manifest["compatibility"]["legacySchemaVersion"] == 1
assert "content/Content/en-US.json" in {entry["path"] for entry in manifest["files"]}
assert "content/Licenses/catalog.json" in {entry["path"] for entry in manifest["files"]}
assert "Config/Marketplace/trusted-marketplace-key.json" in {entry["path"] for entry in manifest["files"]}
assert "Config/Marketplace/trusted-marketplace-keys.json" in {entry["path"] for entry in manifest["files"]}
assert "content/Fonts/Inter-OFL.txt" in manifest["licenseReferences"]
assert "content/Fonts/Material-Symbols-Apache-2.0.txt" in manifest["licenseReferences"]
PY
printf '%s\n' tampered >> "$stage/content/Templates/Payloads/Empty/README.md"
assert_false python3 "${manifest_arguments[@]}"
rm -rf "$stage/content"
mkdir -p "$stage/content"
cp -R "$ROOT/KeireHubContent/." "$stage/content/"
python3 "${manifest_arguments[@]}"

assert_true validate_editor_package_stage "$stage" Client Hub Core Core Linux
assert_false test -e "$stage/bin/Hub"
editor_managed_api="$stage/bin/Managed/Keire.Editor.Managed.dll"
rm "$editor_managed_api"
assert_false validate_editor_package_stage "$stage" Client Hub Core Core Linux
: > "$editor_managed_api"
tar -C "$stage" -czf "$archive" .
assert_true assert_package_archive_generated_data_free "$archive"

printf '%s\n' '#!/usr/bin/env sh' 'exit 0' > "$stage/bin/Hub"
chmod +x "$stage/bin/Hub"
assert_false validate_editor_package_stage "$stage" Client Hub Core Core Linux
rm "$stage/bin/Hub"

mkdir -p "$stage/Client.app/Contents/MacOS"
printf '%s\n' '#!/usr/bin/env sh' 'exit 0' > "$stage/Client.app/Contents/MacOS/ClientLauncher"
printf '%s\n' '<plist/>' > "$stage/Client.app/Contents/Info.plist"
chmod +x "$stage/Client.app/Contents/MacOS/ClientLauncher"
assert_true validate_editor_package_stage "$stage" Client Hub Core Core Mac
mkdir -p "$stage/Hub.app"
assert_false validate_editor_package_stage "$stage" Client Hub Core Core Mac
rm -rf "$stage/Hub.app" "$stage/Client.app"

printf '%s\n' tampered >> "$stage/README.md"
assert_false python3 "$manifest_writer" validate --stage "$stage" --manifest editor-package.json --artifact editor
python3 "${manifest_arguments[@]}"

rm "$stage/bin/Managed/Dotnet/dotnet"
assert_false validate_editor_package_stage "$stage" Client Hub Core Core Linux
printf '%s\n' '#!/usr/bin/env sh' 'exit 0' > "$stage/bin/Managed/Dotnet/dotnet"
chmod +x "$stage/bin/Managed/Dotnet/dotnet"

mkdir "$stage/include"
assert_false validate_editor_package_stage "$stage" Client Hub Core Core Linux
rmdir "$stage/include"

mkdir -p "$stage/samples/KeireSandbox/Build"
: > "$stage/samples/KeireSandbox/Build/generated.txt"
assert_false validate_editor_package_stage "$stage" Client Hub Core Core Linux

printf 'Unix editor package checks passed.\n'
