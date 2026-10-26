#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
fixture="$(mktemp -d)"
trap 'rm -rf -- "$fixture"' EXIT
mkdir -p "$fixture/Scripts/Unix" "$fixture/Scripts/ManagedDesignTime" "$fixture/Config" \
  "$fixture/Build/Dependencies/dotnet-sdk" "$fixture/KeireManaged" \
  "$fixture/KeireEditorManaged" "$fixture/KeireManaged.Generators"
cp "$root/Scripts/Unix/build-managed.sh" "$root/Scripts/Unix/stage-managed-host.sh" \
  "$root/Scripts/Unix/common.sh" "$fixture/Scripts/Unix/"
cp "$root/Config/Dependencies.lock" "$fixture/Config/Dependencies.lock"
printf 'source\n' > "$fixture/KeireManaged/Runtime.cs"
printf 'project\n' > "$fixture/Scripts/ManagedDesignTime/Core.csproj"
printf 'release\n' > "$fixture/KeireManaged.Generators/AnalyzerReleases.Shipped.md"
cat > "$fixture/Build/Dependencies/dotnet-sdk/dotnet" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../../.." && pwd)"
output="" project=""
while [[ $# -gt 0 ]]; do
  case "$1" in
    *.csproj) project="$1"; shift ;;
    --output) output="$2"; shift 2 ;;
    *) shift ;;
  esac
done
mkdir -p "$output"
case "$project" in
  *Keire.Editor.Managed.csproj)
    printf 'runtime\n' > "$output/Keire.Managed.dll"
    printf 'editor\n' > "$output/Keire.Editor.Managed.dll" ;;
  *Keire.Managed.Generators.csproj)
    printf 'generator\n' > "$output/Keire.Managed.Generators.dll" ;;
  *Keire.Editor.Managed.DesignTime.csproj)
    printf 'design-core\n' > "$output/Keire.Managed.dll"
    printf 'design-editor\n' > "$output/Keire.Editor.Managed.dll" ;;
  *) exit 2 ;;
esac
printf 'build\n' >> "$root/invocations"
EOF
chmod +x "$fixture/Build/Dependencies/dotnet-sdk/dotnet"
assert_build_count() {
  local expected="$1"
  [[ "$(wc -l < "$fixture/invocations" | tr -d ' ')" == "$expected" ]] || {
    printf 'Unexpected managed invocation count; expected %s\n' "$expected" >&2
    exit 1
  }
}
bash "$fixture/Scripts/Unix/build-managed.sh"
assert_build_count 3
bash "$fixture/Scripts/Unix/build-managed.sh"
assert_build_count 3
expected=3
for input in KeireManaged/Runtime.cs Scripts/ManagedDesignTime/Core.csproj \
  KeireManaged.Generators/AnalyzerReleases.Shipped.md Config/Dependencies.lock; do
  sleep 1
  touch "$fixture/$input"
  bash "$fixture/Scripts/Unix/build-managed.sh"
  expected=$((expected + 3))
  assert_build_count "$expected"
done
rm "$fixture/Build/Managed/DesignTime/Keire.Managed.dll"
bash "$fixture/Scripts/Unix/build-managed.sh"
assert_build_count 18

coral="$fixture/Build/Dependencies/coral/Build/Release"
sdk="$fixture/Build/Dependencies/dotnet-sdk"
target="$fixture/Build/Bin/Release-linux-x86_64/KeireClient"
mkdir -p "$coral" "$target" "$sdk/host/fxr/10.0.1" "$sdk/shared/Microsoft.NETCore.App/10.0.1"
for file in Coral.Managed.dll Coral.Managed.deps.json Coral.Managed.runtimeconfig.json; do
  printf 'host\n' > "$coral/$file"
done
printf 'fxr\n' > "$sdk/host/fxr/10.0.1/libhostfxr.so"
printf 'clr\n' > "$sdk/shared/Microsoft.NETCore.App/10.0.1/libcoreclr.so"
mv "$fixture/Build/Managed/DesignTime/Keire.Managed.dll" "$fixture/missing-design.dll"
if bash "$fixture/Scripts/Unix/stage-managed-host.sh" "$fixture" Release linux x86_64 KeireClient true; then
  printf 'Missing design-time API was not rejected.\n' >&2
  exit 1
fi
[[ ! -d "$target/Managed" ]]
mv "$fixture/missing-design.dll" "$fixture/Build/Managed/DesignTime/Keire.Managed.dll"
bash "$fixture/Scripts/Unix/stage-managed-host.sh" "$fixture" Release linux x86_64 KeireClient false
[[ ! -e "$target/Managed/DesignTime" ]]
bash "$fixture/Scripts/Unix/stage-managed-host.sh" "$fixture" Release linux x86_64 KeireClient true
cmp "$fixture/Build/Managed/DesignTime/Keire.Managed.dll" "$target/Managed/DesignTime/Keire.Managed.dll"
cmp "$fixture/Build/Managed/DesignTime/Keire.Editor.Managed.dll" "$target/Managed/DesignTime/Keire.Editor.Managed.dll"
printf 'Managed compatibility build/cache/staging regression checks passed.\n'
