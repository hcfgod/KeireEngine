#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
source "$ROOT/Scripts/Unix/common.sh"

assert_true() { "$@" || { printf 'Assertion failed: %s\n' "$*" >&2; exit 1; }; }
assert_false() { if "$@"; then printf 'Expected failure: %s\n' "$*" >&2; exit 1; fi; }

lock="$ROOT/Config/Dependencies.lock"
entitlements="$ROOT/Config/Signing/KeireManagedHost.entitlements"
common="$ROOT/Scripts/Unix/common.sh"

[[ "$(config_value "$lock" MACOS_DEPLOYMENT_TARGET)" == 12.0 ]] || {
  printf 'The release macOS deployment target must remain pinned to 12.0.\n' >&2
  exit 1
}
assert_true test -f "$entitlements"
assert_true test "$(grep -c '<key>com\.apple\.security\.cs\.' "$entitlements")" -eq 4
assert_true test "$(grep -c '<true/>' "$entitlements")" -eq 4
for key in allow-jit allow-unsigned-executable-memory allow-dyld-environment-variables \
  disable-library-validation; do
  assert_true grep -Fq "<key>com.apple.security.cs.$key</key>" "$entitlements"
done
assert_false grep -Fq 'get-task-allow' "$entitlements"
python3 - "$entitlements" <<'PY'
import sys
import xml.etree.ElementTree as ET

expected = {
    "com.apple.security.cs.allow-jit",
    "com.apple.security.cs.allow-unsigned-executable-memory",
    "com.apple.security.cs.allow-dyld-environment-variables",
    "com.apple.security.cs.disable-library-validation",
}
root = ET.parse(sys.argv[1]).getroot()
items = list(root.find("dict"))
assert len(items) == len(expected) * 2
actual = {}
for index in range(0, len(items), 2):
    assert items[index].tag == "key"
    actual[items[index].text] = items[index + 1].tag
assert actual == {key: "true" for key in expected}
PY

assert_true grep -Fq 'systemversion(DependencyManifest.MacOSDeploymentTarget)' "$ROOT/premake5.lua"
assert_true grep -Fq 'MacOSDeploymentTarget = "$macos_deployment_target"' \
  "$ROOT/Scripts/Unix/dependencies.sh"
assert_true grep -Fq 'MacOSDeploymentTarget = "$($Lock.MACOS_DEPLOYMENT_TARGET)"' \
  "$ROOT/Scripts/Windows/dependencies.ps1"
assert_true grep -Fq -- '-DCMAKE_OSX_DEPLOYMENT_TARGET=$macos_deployment_target' \
  "$ROOT/Scripts/Unix/dependencies.sh"
assert_true grep -Fq -- '-DCMAKE_OSX_DEPLOYMENT_TARGET=$macos_deployment_target' \
  "$ROOT/Scripts/Unix/shader-compiler.sh"
assert_true grep -Fq -- '-DCMAKE_OSX_DEPLOYMENT_TARGET=$macos_deployment_target' \
  "$ROOT/Scripts/Unix/coral.sh"
assert_true grep -Fq -- '-mmacosx-version-min=$MACOS_DEPLOYMENT_TARGET' "$ROOT/Scripts/Unix/ffmpeg.sh"
assert_true grep -Fq -- '-mmacosx-version-min=$macos_deployment_target' "$ROOT/Scripts/Unix/package.sh"

assert_true grep -q '^sign_macos_app_inside_out()' "$common"
assert_true grep -Fq 'verify_macos_signed_macho_tree "$dotnet_root"' "$common"
assert_true grep -Fq 'write_sha256_tree_manifest "$dotnet_root"' "$common"
assert_true grep -Fq 'validate_macos_managed_host_entitlements "$managed_host"' "$common"
assert_false grep -Fq -- '--force --deep' "$ROOT/Scripts/Unix/package-installer.sh" \
  "$ROOT/Scripts/Unix/package-hub-installer.sh" "$common"

fixture="$(mktemp -d)"
trap 'rm -rf "$fixture"' EXIT
mkdir -p "$fixture/tools" "$fixture/package/bin/Managed/Dotnet"
printf '%s\n' fixture > "$fixture/package/valid"
printf '%s\n' fixture > "$fixture/package/bin/Managed/Dotnet/vendor-newer"
cat > "$fixture/tools/file" <<'EOF'
#!/usr/bin/env bash
printf '%s\n' 'Mach-O 64-bit executable arm64'
EOF
cat > "$fixture/tools/vtool" <<'EOF'
#!/usr/bin/env bash
version=12.0
[[ "$2" == *older* ]] && version=11.0
[[ "$2" == *newer* ]] && version=13.0
printf 'Load command 0\n      cmd LC_BUILD_VERSION\n    minos %s\n' "$version"
EOF
chmod +x "$fixture/tools/file" "$fixture/tools/vtool"

PATH="$fixture/tools:$PATH" assert_true validate_macos_macho_minimum "$fixture/package" 12.0 \
  "$fixture/package/bin/Managed/Dotnet"
printf '%s\n' fixture > "$fixture/package/newer"
PATH="$fixture/tools:$PATH" assert_false validate_macos_macho_minimum "$fixture/package" 12.0 \
  "$fixture/package/bin/Managed/Dotnet" 2>/dev/null
rm "$fixture/package/newer"
printf '%s\n' fixture > "$fixture/package/older"
PATH="$fixture/tools:$PATH" assert_false validate_macos_macho_minimum "$fixture/package" 12.0 \
  "$fixture/package/bin/Managed/Dotnet" 2>/dev/null
rm "$fixture/package/older" "$fixture/tools/vtool"
cat > "$fixture/tools/otool" <<'EOF'
#!/usr/bin/env bash
printf '%s\n' 'Load command 0' '      cmd LC_VERSION_MIN_MACOSX' '  cmdsize 16' '  version 12.0' '      sdk 14.0'
EOF
chmod +x "$fixture/tools/otool"
PATH="$fixture/tools:$PATH" assert_true validate_macos_macho_minimum "$fixture/package" 12.0 \
  "$fixture/package/bin/Managed/Dotnet"

printf 'macOS packaging contract checks passed.\n'
