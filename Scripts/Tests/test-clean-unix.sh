#!/usr/bin/env bash
set -euo pipefail

SOURCE_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
fixture=""
trap '[[ -z "$fixture" ]] || rm -rf "$fixture"' EXIT

grep -q '^CLEAN_SCOPE=full$' "$SOURCE_ROOT/Scripts/project.sh"
grep -q 'clean) bash "$PLATFORM_DIR/clean.sh" "$CLEAN_SCOPE" ;;' "$SOURCE_ROOT/Scripts/project.sh"

new_clean_fixture() {
    fixture="$(mktemp -d)"
    mkdir -p "$fixture/Scripts/Unix" "$fixture/KeireCore" "$fixture/KeireClient" \
        "$fixture/KeireHub" "$fixture/KeireTests"
    cp "$SOURCE_ROOT/Scripts/Unix/clean.sh" "$fixture/Scripts/Unix/clean.sh"
    cat > "$fixture/Scripts/Unix/common.sh" <<'EOF'
load_project_config() {
    CORE_DIRECTORY=KeireCore
    CLIENT_DIRECTORY=KeireClient
    HUB_DIRECTORY=KeireHub
    TESTS_DIRECTORY=KeireTests
}
EOF
}

new_clean_fixture
mkdir -p "$fixture/Build/Dependencies" "$fixture/Build/UnclassifiedOutput" "$fixture/Artifacts" "$fixture/Vendor"
printf '%s\n' cache > "$fixture/Build/Dependencies/cache.bin"
printf '%s\n' stale > "$fixture/Build/UnclassifiedOutput/stale.txt"
printf '%s\n' archive > "$fixture/Artifacts/package.tar.gz"
printf '%s\n' generated > "$fixture/Fixture.sln"
printf '%s\n' generated > "$fixture/KeireCore/Fixture.make"
printf '%s\n' keep > "$fixture/Vendor/sentinel.txt"
bash "$fixture/Scripts/Unix/clean.sh" full
[[ ! -e "$fixture/Build" ]]
[[ ! -e "$fixture/Artifacts" ]]
[[ ! -e "$fixture/Fixture.sln" ]]
[[ ! -e "$fixture/KeireCore/Fixture.make" ]]
[[ -f "$fixture/Vendor/sentinel.txt" ]]
rm -rf "$fixture"
fixture=""

new_clean_fixture
for directory in Dependencies Generated Projects Bin Managed UnknownOutput; do
    mkdir -p "$fixture/Build/$directory"
    printf '%s\n' fixture > "$fixture/Build/$directory/sentinel.txt"
done
printf '%s\n' loose > "$fixture/Build/loose-output.txt"
mkdir -p "$fixture/Artifacts"
bash "$fixture/Scripts/Unix/clean.sh" build
for preserved in Dependencies Generated Projects; do
    [[ -f "$fixture/Build/$preserved/sentinel.txt" ]]
done
for removed in Bin Managed UnknownOutput; do
    [[ ! -e "$fixture/Build/$removed" ]]
done
[[ ! -e "$fixture/Build/loose-output.txt" ]]
[[ ! -e "$fixture/Artifacts" ]]
rm -rf "$fixture"
fixture=""

new_clean_fixture
for directory in Bin Dependencies Generated Projects; do
    mkdir -p "$fixture/Build/$directory"
    printf '%s\n' fixture > "$fixture/Build/$directory/sentinel.txt"
done
bash "$fixture/Scripts/Unix/clean.sh" generated
[[ -f "$fixture/Build/Bin/sentinel.txt" ]]
[[ -f "$fixture/Build/Dependencies/sentinel.txt" ]]
[[ ! -e "$fixture/Build/Generated" ]]
[[ ! -e "$fixture/Build/Projects" ]]

printf 'Unix clean regression tests passed.\n'
