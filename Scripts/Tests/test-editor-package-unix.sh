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
grep -q 'validate_editor_package_stage' "$ROOT/Scripts/Unix/package-editor.sh"

stage="$(mktemp -d)"
archive="$(mktemp).tar.gz"
trap 'rm -rf "$stage"; rm -f "$archive"' EXIT
while IFS= read -r path; do
  mkdir -p "$stage/$(dirname "$path")"
  : > "$stage/$path"
done < <(editor_package_required_paths Client Hub Core Core)
for path in bin/Client bin/Hub bin/CoreAssetTool bin/CoreAssetWorker bin/CoreRuntime bin/KeireShaderCompiler \
  bin/Managed/Dotnet/dotnet launch-editor.sh; do
  printf '%s\n' '#!/usr/bin/env sh' 'exit 0' > "$stage/$path"
  chmod +x "$stage/$path"
done
for path in libavcodec.so.62 libavformat.so.62 libavutil.so.60 libswresample.so.6; do
  : > "$stage/bin/$path"
done
mkdir -p "$stage/bin/Managed/Dotnet/sdk/10.0.100/Sdks/Fixture/build"
: > "$stage/bin/Managed/Dotnet/sdk/10.0.100/Sdks/Fixture/build/Fixture.targets"

assert_true validate_editor_package_stage "$stage" Client Hub Core Core Linux
tar -C "$stage" -czf "$archive" .
assert_true assert_package_archive_generated_data_free "$archive"

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
