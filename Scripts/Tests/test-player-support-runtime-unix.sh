#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
fixture="$(mktemp -d)"
trap 'rm -rf -- "$fixture"' EXIT
source_root="$fixture/source"
license_source="$fixture/licenses"
fixture_system=linux
[[ "$(uname -s)" == Darwin ]] && fixture_system=macosx
hostpolicy_name=libhostpolicy.so
[[ "$(uname -s)" == Darwin ]] && hostpolicy_name=libhostpolicy.dylib
license_names=(
    Keire-LICENSE.txt Keire-THIRD_PARTY_NOTICES.md Coral-LICENSE.txt dotnet-LICENSE.txt
    dotnet-ThirdPartyNotices.txt SDL-LICENSE.txt assimp-LICENSE.txt assimp-zlib-LICENSE.txt stb-LICENSE.txt
    Jolt-LICENSE.txt Recast-LICENSE.txt miniaudio-LICENSE.txt spdlog-LICENSE.txt fmt-LICENSE.rst
    nlohmann-json-LICENSE.MIT.txt dear-imgui-LICENSE.txt zstandard-LICENSE.txt entt-LICENSE.txt glm-COPYING.txt
)
license_paths=(
    LICENSE.txt THIRD_PARTY_NOTICES.md Build/Dependencies/coral/LICENSE
    Build/Dependencies/dotnet-sdk/LICENSE.txt Build/Dependencies/dotnet-sdk/ThirdPartyNotices.txt
    "Build/Dependencies/$fixture_system-x86_64-clang/Release/install/licenses/SDL3/LICENSE.txt"
    Vendor/assimp/LICENSE Vendor/assimp/contrib/zlib/LICENSE Vendor/stb/LICENSE
    "Build/Dependencies/$fixture_system-x86_64-clang/Release/install/share/licenses/keire/Jolt-LICENSE.txt"
    "Build/Dependencies/$fixture_system-x86_64-clang/Release/install/share/licenses/keire/Recast-LICENSE.txt"
    "Build/Dependencies/$fixture_system-x86_64-clang/Release/install/share/licenses/keire/miniaudio-LICENSE.txt"
    Vendor/spdlog/LICENSE Vendor/spdlog/include/spdlog/fmt/bundled/fmt.license.rst Vendor/json/LICENSE.MIT
    Vendor/imgui/LICENSE.txt Vendor/zstd/LICENSE Vendor/entt/LICENSE Vendor/glm/copying.txt
)
for ((index = 0; index < ${#license_names[@]}; ++index)); do
    mkdir -p "$(dirname "$license_source/${license_paths[$index]}")"
    printf 'license-%s\n' "${license_names[$index]}" > "$license_source/${license_paths[$index]}"
done
export KEIRE_PLAYER_SUPPORT_LICENSE_SOURCE="$license_source"

write_fixture() {
    mkdir -p "$(dirname "$source_root/$1")"
    printf fixture > "$source_root/$1"
}
for file in KeireRuntime Managed/Coral.Managed.dll Managed/Coral.Managed.deps.json \
    Managed/Coral.Managed.runtimeconfig.json Managed/Keire.Managed.dll \
    Managed/Dotnet/host/fxr/10.0.10/libhostfxr.so \
    Managed/Dotnet/shared/Microsoft.NETCore.App/10.0.9/libcoreclr.so \
    Managed/Dotnet/shared/Microsoft.NETCore.App/10.0.11/libcoreclr.so \
    "Managed/Dotnet/shared/Microsoft.NETCore.App/10.0.11/$hostpolicy_name" \
    Managed/Dotnet/shared/Microsoft.NETCore.App/10.0.11/System.Private.CoreLib.dll \
    Managed/Dotnet/shared/Microsoft.NETCore.App/10.0.11/createdump; do
    write_fixture "$file"
done
chmod +x "$source_root/KeireRuntime"

KEIRE_PLAYER_SUPPORT_RUNTIME_SOURCE="$source_root" \
KEIRE_PLAYER_SUPPORT_RUNTIME_DESTINATION="$fixture/staged" \
    bash "$root/Scripts/Unix/player-support.sh"
test ! -e "$fixture/staged/Managed/Dotnet/shared/Microsoft.NETCore.App/10.0.9"
test -f "$fixture/staged/Managed/Dotnet/shared/Microsoft.NETCore.App/10.0.11/libcoreclr.so"
for license in "${license_names[@]}"; do test -f "$fixture/staged/Licenses/$license"; done
test "$(stat -c '%a' "$fixture/staged/KeireRuntime" 2>/dev/null || stat -f '%Lp' "$fixture/staged/KeireRuntime")" = 755
test "$(stat -c '%a' "$fixture/staged/Managed/Dotnet/shared/Microsoft.NETCore.App/10.0.11/createdump" 2>/dev/null || stat -f '%Lp' "$fixture/staged/Managed/Dotnet/shared/Microsoft.NETCore.App/10.0.11/createdump")" = 755
test "$(stat -c '%a' "$fixture/staged/Managed/Dotnet/shared/Microsoft.NETCore.App/10.0.11/libcoreclr.so" 2>/dev/null || stat -f '%Lp' "$fixture/staged/Managed/Dotnet/shared/Microsoft.NETCore.App/10.0.11/libcoreclr.so")" = 644

rm -f -- "$source_root/Managed/Dotnet/shared/Microsoft.NETCore.App/10.0.11/$hostpolicy_name"
if KEIRE_PLAYER_SUPPORT_RUNTIME_SOURCE="$source_root" \
    KEIRE_PLAYER_SUPPORT_RUNTIME_DESTINATION="$fixture/hostpolicy-stage" \
    bash "$root/Scripts/Unix/player-support.sh" 2> "$fixture/error"; then
    printf 'Unix runtime closure accepted a missing hostpolicy library.\n' >&2
    exit 1
fi
grep -q 'regular non-symbolic file' "$fixture/error"
write_fixture "Managed/Dotnet/shared/Microsoft.NETCore.App/10.0.11/$hostpolicy_name"

write_fixture Managed/Dotnet/sdk/10.0.302/MSBuild.dll
if KEIRE_PLAYER_SUPPORT_RUNTIME_SOURCE="$source_root" \
    KEIRE_PLAYER_SUPPORT_RUNTIME_DESTINATION="$fixture/sdk-stage" \
    bash "$root/Scripts/Unix/player-support.sh" 2> "$fixture/error"; then
    printf 'Unix runtime allowlist accepted SDK content.\n' >&2
    exit 1
fi
grep -q 'prohibited .NET content' "$fixture/error"
rm -rf -- "$source_root/Managed/Dotnet/sdk"

write_fixture KeireAssetTool
if KEIRE_PLAYER_SUPPORT_RUNTIME_SOURCE="$source_root" \
    KEIRE_PLAYER_SUPPORT_RUNTIME_DESTINATION="$fixture/tool-stage" \
    bash "$root/Scripts/Unix/player-support.sh" 2> "$fixture/error"; then
    printf 'Unix runtime allowlist accepted an unexpected executable.\n' >&2
    exit 1
fi
grep -q 'unexpected executable' "$fixture/error"

rm -f -- "$source_root/KeireAssetTool"
mv "$source_root/Managed/Keire.Managed.dll" "$fixture/Keire.Managed.dll"
ln -s "$fixture/Keire.Managed.dll" "$source_root/Managed/Keire.Managed.dll"
if KEIRE_PLAYER_SUPPORT_RUNTIME_SOURCE="$source_root" \
    KEIRE_PLAYER_SUPPORT_RUNTIME_DESTINATION="$fixture/link-stage" \
    bash "$root/Scripts/Unix/player-support.sh" 2> "$fixture/error"; then
    printf 'Unix runtime allowlist accepted a symbolic required file.\n' >&2
    exit 1
fi
grep -q 'regular non-symbolic file' "$fixture/error"
rm -f -- "$source_root/Managed/Keire.Managed.dll"
mv "$fixture/Keire.Managed.dll" "$source_root/Managed/Keire.Managed.dll"

rm -f -- "$license_source/Build/Dependencies/coral/LICENSE"
if KEIRE_PLAYER_SUPPORT_RUNTIME_SOURCE="$source_root" \
    KEIRE_PLAYER_SUPPORT_RUNTIME_DESTINATION="$fixture/license-stage" \
    bash "$root/Scripts/Unix/player-support.sh" 2> "$fixture/error"; then
    printf 'Unix runtime closure accepted a missing required license.\n' >&2
    exit 1
fi
grep -q 'regular non-symbolic file' "$fixture/error"
printf restored > "$license_source/Build/Dependencies/coral/LICENSE"

publication="$fixture/publication"
printf first > "$fixture/first.keireplayersupport"
KEIRE_PLAYER_SUPPORT_PUBLISH_SOURCE="$fixture/first.keireplayersupport" \
KEIRE_PLAYER_SUPPORT_PUBLISH_OUTPUT="$publication" \
KEIRE_PLAYER_SUPPORT_PUBLISH_ID=linux-x86_64-test \
    bash "$root/Scripts/Unix/player-support.sh" >/dev/null
cp "$publication/player-support-catalog.json" "$fixture/catalog-before-failure.json"
printf replacement > "$fixture/replacement.keireplayersupport"
if KEIRE_PLAYER_SUPPORT_PUBLISH_SOURCE="$fixture/replacement.keireplayersupport" \
    KEIRE_PLAYER_SUPPORT_PUBLISH_OUTPUT="$publication" \
    KEIRE_PLAYER_SUPPORT_PUBLISH_ID=linux-x86_64-test \
    KEIRE_PLAYER_SUPPORT_TEST_FAIL_CATALOG_PUBLISH=1 \
    bash "$root/Scripts/Unix/player-support.sh" 2> "$fixture/error"; then
    printf 'Unix catalog failure injection unexpectedly succeeded.\n' >&2
    exit 1
fi
grep -q 'catalog publication failure' "$fixture/error"
cmp -s "$fixture/catalog-before-failure.json" "$publication/player-support-catalog.json"
python3 - "$publication" <<'PY'
import json, pathlib, sys
root = pathlib.Path(sys.argv[1])
catalog = json.loads((root / "player-support-catalog.json").read_text(encoding="utf-8"))
assert (root / catalog["packages"][0]["file"]).is_file()
PY

invalid_output="$fixture/invalid-catalog"
printf invalid-base > "$fixture/invalid-base.keireplayersupport"
KEIRE_PLAYER_SUPPORT_PUBLISH_SOURCE="$fixture/invalid-base.keireplayersupport" \
KEIRE_PLAYER_SUPPORT_PUBLISH_OUTPUT="$invalid_output" KEIRE_PLAYER_SUPPORT_PUBLISH_ID=linux-x86_64-invalid-base \
    bash "$root/Scripts/Unix/player-support.sh" >/dev/null
python3 - "$invalid_output/player-support-catalog.json" <<'PY'
import json, pathlib, sys
path = pathlib.Path(sys.argv[1])
catalog = json.loads(path.read_text(encoding="utf-8"))
catalog["packages"][0]["sha256"] = catalog["packages"][0]["sha256"].upper()
path.write_text(json.dumps(catalog, indent=2) + "\n", encoding="utf-8")
PY
printf invalid-candidate > "$fixture/invalid-candidate.keireplayersupport"
if KEIRE_PLAYER_SUPPORT_PUBLISH_SOURCE="$fixture/invalid-candidate.keireplayersupport" \
    KEIRE_PLAYER_SUPPORT_PUBLISH_OUTPUT="$invalid_output" \
    KEIRE_PLAYER_SUPPORT_PUBLISH_ID=linux-x86_64-invalid-candidate \
    bash "$root/Scripts/Unix/player-support.sh" 2> "$fixture/error"; then
    printf 'Unix catalog publisher accepted an invalid existing entry.\n' >&2
    exit 1
fi
grep -q 'invalid or duplicated' "$fixture/error"

duplicate_output="$fixture/duplicate-catalog"
printf duplicate-base > "$fixture/duplicate-base.keireplayersupport"
KEIRE_PLAYER_SUPPORT_PUBLISH_SOURCE="$fixture/duplicate-base.keireplayersupport" \
KEIRE_PLAYER_SUPPORT_PUBLISH_OUTPUT="$duplicate_output" \
KEIRE_PLAYER_SUPPORT_PUBLISH_ID=linux-x86_64-duplicate-base \
    bash "$root/Scripts/Unix/player-support.sh" >/dev/null
python3 - "$duplicate_output/player-support-catalog.json" <<'PY'
import json, pathlib, sys
path = pathlib.Path(sys.argv[1])
catalog = json.loads(path.read_text(encoding="utf-8"))
catalog["packages"].append(dict(catalog["packages"][0]))
path.write_text(json.dumps(catalog, indent=2) + "\n", encoding="utf-8")
PY
printf duplicate-candidate > "$fixture/duplicate-candidate.keireplayersupport"
if KEIRE_PLAYER_SUPPORT_PUBLISH_SOURCE="$fixture/duplicate-candidate.keireplayersupport" \
    KEIRE_PLAYER_SUPPORT_PUBLISH_OUTPUT="$duplicate_output" \
    KEIRE_PLAYER_SUPPORT_PUBLISH_ID=linux-x86_64-duplicate-candidate \
    bash "$root/Scripts/Unix/player-support.sh" 2> "$fixture/error"; then
    printf 'Unix catalog publisher accepted duplicate existing entries.\n' >&2
    exit 1
fi
grep -q 'invalid or duplicated' "$fixture/error"

missing_output="$fixture/missing-catalog"
printf missing-base > "$fixture/missing-base.keireplayersupport"
KEIRE_PLAYER_SUPPORT_PUBLISH_SOURCE="$fixture/missing-base.keireplayersupport" \
KEIRE_PLAYER_SUPPORT_PUBLISH_OUTPUT="$missing_output" KEIRE_PLAYER_SUPPORT_PUBLISH_ID=linux-x86_64-missing-base \
    bash "$root/Scripts/Unix/player-support.sh" >/dev/null
python3 - "$missing_output" <<'PY'
import json, pathlib, sys
root = pathlib.Path(sys.argv[1])
catalog = json.loads((root / "player-support-catalog.json").read_text(encoding="utf-8"))
(root / catalog["packages"][0]["file"]).unlink()
PY
printf missing-candidate > "$fixture/missing-candidate.keireplayersupport"
if KEIRE_PLAYER_SUPPORT_PUBLISH_SOURCE="$fixture/missing-candidate.keireplayersupport" \
    KEIRE_PLAYER_SUPPORT_PUBLISH_OUTPUT="$missing_output" \
    KEIRE_PLAYER_SUPPORT_PUBLISH_ID=linux-x86_64-missing-candidate \
    bash "$root/Scripts/Unix/player-support.sh" 2> "$fixture/error"; then
    printf 'Unix catalog publisher accepted a missing referenced archive.\n' >&2
    exit 1
fi
grep -q 'missing or redirected archive' "$fixture/error"

parallel="$fixture/parallel-publication"
printf parallel-one > "$fixture/parallel-one.keireplayersupport"
printf parallel-two > "$fixture/parallel-two.keireplayersupport"
KEIRE_PLAYER_SUPPORT_PUBLISH_SOURCE="$fixture/parallel-one.keireplayersupport" \
KEIRE_PLAYER_SUPPORT_PUBLISH_OUTPUT="$parallel" KEIRE_PLAYER_SUPPORT_PUBLISH_ID=linux-x86_64-test \
    bash "$root/Scripts/Unix/player-support.sh" > "$fixture/parallel-one.log" 2>&1 &
first_pid=$!
KEIRE_PLAYER_SUPPORT_PUBLISH_SOURCE="$fixture/parallel-two.keireplayersupport" \
KEIRE_PLAYER_SUPPORT_PUBLISH_OUTPUT="$parallel" KEIRE_PLAYER_SUPPORT_PUBLISH_ID=linux-arm64-test \
KEIRE_PLAYER_SUPPORT_PUBLISH_ARCHITECTURE=arm64 \
    bash "$root/Scripts/Unix/player-support.sh" > "$fixture/parallel-two.log" 2>&1 &
second_pid=$!
wait "$first_pid"
wait "$second_pid"
python3 - "$parallel/player-support-catalog.json" <<'PY'
import json, pathlib, sys
catalog = json.loads(pathlib.Path(sys.argv[1]).read_text(encoding="utf-8"))
assert len(catalog["packages"]) == 2
PY

cleanup_probe="$fixture/.staging-cleanup-probe"
mkdir -p "$cleanup_probe"
printf owned > "$cleanup_probe/.keire-player-support-operation"
if KEIRE_PLAYER_SUPPORT_CLEANUP_PROBE="$cleanup_probe" KEIRE_PLAYER_SUPPORT_TEST_FAIL_CLEANUP=1 \
    bash "$root/Scripts/Unix/player-support.sh" 2> "$fixture/error"; then
    printf 'A successful Unix operation ignored its cleanup failure.\n' >&2
    exit 1
fi
grep -q 'cleanup failure' "$fixture/error"
set +e
KEIRE_PLAYER_SUPPORT_CLEANUP_PROBE="$cleanup_probe" KEIRE_PLAYER_SUPPORT_TEST_FAIL_CLEANUP=1 \
KEIRE_PLAYER_SUPPORT_TEST_PRIMARY_FAILURE=1 \
    bash "$root/Scripts/Unix/player-support.sh" 2> "$fixture/error"
primary_status=$?
set -e
test "$primary_status" = 42
grep -q 'cleanup also failed' "$fixture/error"
KEIRE_PLAYER_SUPPORT_CLEANUP_PROBE="$cleanup_probe" bash "$root/Scripts/Unix/player-support.sh"
printf 'Unix Player Support runtime closure tests passed.\n'
