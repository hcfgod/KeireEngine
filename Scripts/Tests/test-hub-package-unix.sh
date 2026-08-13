#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
source "$ROOT/Scripts/Unix/common.sh"

assert_true() { "$@" || { printf 'Assertion failed: %s\n' "$*" >&2; exit 1; }; }
assert_false() { if "$@"; then printf 'Expected failure: %s\n' "$*" >&2; exit 1; fi; }

grep -q 'package-hub' "$ROOT/Scripts/project.sh"
grep -q 'Build/Distributions' "$ROOT/Scripts/Unix/package-hub.sh"
grep -q 'validate_hub_package_stage' "$ROOT/Scripts/Unix/package-hub.sh"
grep -Fq 'validate_macos_macho_minimum "$stage" "$macos_deployment_target"' \
  "$ROOT/Scripts/Unix/package-hub.sh"
grep -Fq '<key>LSMinimumSystemVersion</key>' "$ROOT/Scripts/Unix/package-hub.sh"
grep -q 'write-package-manifest.py' "$ROOT/Scripts/Unix/package-hub.sh"
grep -q 'write-distribution-config.py' "$ROOT/Scripts/Unix/package-hub.sh"
grep -q 'validate-supabase-config.py' "$ROOT/Scripts/Unix/package-hub.sh"
grep -q 'libsodium' "$ROOT/Scripts/Unix/package-hub.sh"
if grep -Eq 'Scripts/Unix/package-editor\.sh|Scripts/Unix/package\.sh' "$ROOT/Scripts/Unix/package-hub.sh"; then
  printf 'The standalone Unix Hub package must not stage through the editor or SDK package.\n' >&2
  exit 1
fi

stage="$(mktemp -d)"
archive="$(mktemp).tar.gz"
trap 'rm -rf "$stage"; rm -f "$archive"' EXIT
while IFS= read -r path; do
  mkdir -p "$stage/$(dirname "$path")"
  : > "$stage/$path"
done < <(hub_package_required_paths Hub Core)
rm -rf "$stage/content"
mkdir -p "$stage/content"
cp -R "$ROOT/KeireHubContent/." "$stage/content/"
rm -rf "$stage/Docs" "$stage/Samples"
copy_tracked_tree "$ROOT" Docs "$stage/Docs"
copy_tracked_tree "$ROOT" Samples/KeireSandbox "$stage/Samples/KeireSandbox"
grep -q '"localPath": "Samples/KeireSandbox/README.md"' "$stage/content/Content/en-US.json"
assert_true test -f "$stage/Samples/KeireSandbox/README.md"
printf '%s\n' '#!/usr/bin/env sh' 'exit 0' > "$stage/bin/Hub"
printf '%s\n' '#!/usr/bin/env sh' 'exit 0' > "$stage/bin/CoreHubWorker"
touch "$stage/bin/libsodium.so" "$stage/third-party/licenses/libsodium-LICENSE.txt"
printf '%s\n' '#!/usr/bin/env sh' 'exit 0' > "$stage/launch-hub.sh"
chmod +x "$stage/bin/Hub" "$stage/bin/CoreHubWorker" "$stage/launch-hub.sh"
cp "$ROOT/Config/Supabase.json" "$stage/Config/Supabase.json"

manifest_writer="$ROOT/Scripts/Packaging/write-package-manifest.py"
python3 "$ROOT/Scripts/Packaging/write-distribution-config.py" \
  --output "$stage/Config/Distribution.json" --source-config "$ROOT/Config/Distribution.json"
python3 - "$stage/Config/Distribution.json" "$ROOT/Config/Distribution.json" <<'PY'
import json
import sys

with open(sys.argv[1], encoding="utf-8") as stream:
    distribution = json.load(stream)
with open(sys.argv[2], encoding="utf-8") as stream:
    authority = json.load(stream)
assert distribution["onlineDiscoveryEnabled"] is True
assert distribution["serviceBaseUrl"] == authority["serviceBaseUrl"]
assert distribution["trustedKeys"]
assert [key["keyId"] for key in distribution["trustedKeys"]] == [
    key["keyId"] for key in authority["trustedKeys"]
]
PY
manifest_arguments=(
  "$manifest_writer" write --stage "$stage" --output hub-package.json --artifact hub
  --package-prefix fixture --project Fixture --version 1.2.3 --channel Stable --commit fixture
  --dirty false --development-artifact false --platform Linux --architecture x86_64 --configuration Dist
  --launcher launch-hub.sh --module-definition Config/SourceModules.premake.lua
  --entrypoint hub=bin/Hub --entrypoint worker=bin/CoreHubWorker
  --template-catalog content/Templates/catalog.json --release-notes CHANGELOG.md
)
python3 "${manifest_arguments[@]}"
python3 - "$stage/hub-package.json" <<'PY'
import json
import sys

with open(sys.argv[1], encoding="utf-8") as stream:
    manifest = json.load(stream)
assert manifest["schemaVersion"] == 2
assert manifest["artifact"] == "hub"
assert manifest["entrypoints"]["hub"] == "bin/Hub"
assert manifest["entrypoints"]["worker"] == "bin/CoreHubWorker"
assert len(manifest["packagedTemplates"]) == 3
assert "content/Content/en-US.json" in {entry["path"] for entry in manifest["files"]}
assert "content/Licenses/catalog.json" in {entry["path"] for entry in manifest["files"]}
assert "Config/Distribution.json" in {entry["path"] for entry in manifest["files"]}
assert "Config/Supabase.json" in {entry["path"] for entry in manifest["files"]}
assert "Config/Marketplace/trusted-marketplace-key.json" in {entry["path"] for entry in manifest["files"]}
assert "content/Fonts/Inter-OFL.txt" in manifest["licenseReferences"]
assert manifest["files"]
PY

assert_true validate_hub_package_stage "$stage" Hub Client Core Linux
tar -C "$stage" -czf "$archive" .
assert_true assert_package_archive_generated_data_free "$archive"

rm "$stage/content/Fonts/Inter-Variable.ttf"
assert_false validate_hub_package_stage "$stage" Hub Client Core Linux
cp "$ROOT/KeireHubContent/Fonts/Inter-Variable.ttf" "$stage/content/Fonts/"
rm "$stage/content/Templates/catalog.json"
assert_false validate_hub_package_stage "$stage" Hub Client Core Linux
cp "$ROOT/KeireHubContent/Templates/catalog.json" "$stage/content/Templates/"
rm "$stage/content/Content/en-US.json"
assert_false validate_hub_package_stage "$stage" Hub Client Core Linux
cp "$ROOT/KeireHubContent/Content/en-US.json" "$stage/content/Content/"
rm "$stage/content/Licenses/catalog.json"
assert_false validate_hub_package_stage "$stage" Hub Client Core Linux
cp "$ROOT/KeireHubContent/Licenses/catalog.json" "$stage/content/Licenses/"

printf '%s\n' tampered >> "$stage/README.md"
assert_false python3 "$manifest_writer" validate --stage "$stage" --manifest hub-package.json --artifact hub
python3 "${manifest_arguments[@]}"

printf '%s\n' '#!/usr/bin/env sh' 'exit 0' > "$stage/bin/Client"
chmod +x "$stage/bin/Client"
assert_false validate_hub_package_stage "$stage" Hub Client Core Linux
rm "$stage/bin/Client"

printf '%s\n' '#!/usr/bin/env sh' 'exit 0' > "$stage/bin/CoreAssetWorker"
chmod +x "$stage/bin/CoreAssetWorker"
assert_false validate_hub_package_stage "$stage" Hub Client Core Linux
rm "$stage/bin/CoreAssetWorker"

mkdir "$stage/include"
assert_false validate_hub_package_stage "$stage" Hub Client Core Linux

printf 'Unix standalone Hub package checks passed.\n'
