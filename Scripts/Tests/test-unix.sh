#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
source "$ROOT/Scripts/Unix/common.sh"
assert_equal() { [[ "$1" == "$2" ]] || { printf '%s: expected %s, got %s\n' "$3" "$2" "$1" >&2; exit 1; }; }
assert_true() { "$@" || { printf 'Assertion failed: %s\n' "$*" >&2; exit 1; }; }
assert_false() { if "$@"; then printf 'Expected failure: %s\n' "$*" >&2; exit 1; fi; }

load_project_config "$ROOT"
assert_equal "$PROJECT_IDENTIFIER" CrossPlatformCoreClientTemplate 'project manifest'
assert_equal "$(normalize_architecture amd64)" x86_64 'x64 normalization'
assert_equal "$(normalize_architecture aarch64)" ARM64 'ARM normalization'
assert_equal "$(resolve_unix_toolset Linux default)" gcc 'Linux default toolset'
assert_equal "$(resolve_unix_toolset Mac default)" clang 'macOS default toolset'
assert_true version_at_least 16.0.1 16.0
assert_true version_at_least 17 16.9
assert_false version_at_least 15.9 16.0
assert_equal "$(package_name apt-get ninja)" ninja-build 'APT Ninja package'
assert_equal "$(package_name pacman ninja)" ninja 'pacman Ninja package'
assert_equal "$(package_name dnf cxx)" gcc-c++ 'DNF C++ package'
assert_equal "$(package_name pacman python)" python 'pacman Python package'
assert_equal "$(config_value "$ROOT/Config/Dependencies.lock" SPDLOG_COMMIT)" 79524ddd08a4ec981b7fea76afd08ee05f83755d 'spdlog lock'
assert_equal "$(config_value "$ROOT/Config/Dependencies.lock" DOCTEST_COMMIT)" 2d0a9359a60c51affe2a9bebb1be1dca47868151 'doctest lock'

fixture="$(mktemp -d)"
trap 'rm -rf "$fixture"' EXIT
mkdir -p "$fixture/Scripts/Unix" "$fixture/Config" "$fixture/Core/Include/Core" "$fixture/Core/Source" "$fixture/Client/Source" "$fixture/Tests/Source" "$fixture/Vendor" "$fixture/Build/Bin"
cp "$ROOT/Scripts/Unix/common.sh" "$ROOT/Scripts/Unix/rename.sh" "$ROOT/Scripts/Unix/clean.sh" "$fixture/Scripts/Unix/"
cp "$ROOT/Config/Project.conf" "$fixture/Config/Project.conf"
printf '%s\n' 'namespace Core { const char* GetName(); }' > "$fixture/Core/Include/Core/Core.h"
for source in "$fixture/Core/Source/Core.cpp" "$fixture/Client/Source/Main.cpp" "$fixture/Tests/Source/Main.cpp"; do
  printf '%s\n' '#include "Core/Core.h"' > "$source"
done
printf '%s\n' 'CrossPlatformCoreClientTemplate hcfgod/C-Cross-Platform-Core-Client-Template' > "$fixture/README.md"
printf '%s\n' vendor > "$fixture/Vendor/keep.txt"
printf '%s\n' build > "$fixture/Build/Bin/remove.txt"

bash "$fixture/Scripts/Unix/rename.sh" ScriptFixture 'Script Fixture' example/script-fixture >/dev/null
assert_true test -f "$fixture/ScriptFixtureCore/Include/ScriptFixture/Core.h"
assert_true grep -q '^CORE_TARGET=ScriptFixtureCore$' "$fixture/Config/Project.conf"
bash "$fixture/Scripts/Unix/clean.sh" full >/dev/null
assert_false test -d "$fixture/Build/Bin"
assert_true test -f "$fixture/Vendor/keep.txt"
assert_true test -f "$fixture/ScriptFixtureCore/Source/Core.cpp"
printf 'Unix script regression tests passed.\n'
