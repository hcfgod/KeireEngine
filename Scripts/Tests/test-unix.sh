#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
source "$ROOT/Scripts/Unix/common.sh"
assert_equal() { [[ "$1" == "$2" ]] || { printf '%s: expected %s, got %s\n' "$3" "$2" "$1" >&2; exit 1; }; }
assert_true() { "$@" || { printf 'Assertion failed: %s\n' "$*" >&2; exit 1; }; }
assert_false() { if "$@"; then printf 'Expected failure: %s\n' "$*" >&2; exit 1; fi; }

load_project_config "$ROOT"
assert_true test -n "$PROJECT_IDENTIFIER"
assert_equal "$PROJECT_MACRO_PREFIX" "$(identifier_to_macro_prefix "$PROJECT_IDENTIFIER")" 'project macro prefix'
assert_equal "$(identifier_to_macro_prefix HTTPServer2Client)" HTTP_SERVER2_CLIENT 'macro prefix derivation'
assert_equal "$(normalize_architecture amd64)" x86_64 'x64 normalization'
assert_equal "$(normalize_architecture aarch64)" ARM64 'ARM normalization'
assert_equal "$(resolve_unix_toolset Linux default)" gcc 'Linux default toolset'
assert_equal "$(resolve_unix_toolset Mac default)" clang 'macOS default toolset'
assert_true version_at_least 16.0.1 16.0
assert_true version_at_least 17 16.9
assert_false version_at_least 15.9 16.0
assert_equal "$(printf 'Xcode 16.4\nBuild version 16F6\n' | extract_version)" 16.4 'multi-line version extraction'
assert_equal "$(package_name apt-get ninja)" ninja-build 'APT Ninja package'
assert_equal "$(package_name pacman ninja)" ninja 'pacman Ninja package'
assert_equal "$(package_name dnf cxx)" gcc-c++ 'DNF C++ package'
assert_equal "$(package_name pacman python)" python 'pacman Python package'
assert_equal "$(package_name apt-get uuid)" uuid-dev 'APT UUID development package'
assert_equal "$(package_name dnf uuid)" libuuid-devel 'DNF UUID development package'
assert_equal "$(package_name pacman uuid)" util-linux-libs 'pacman UUID development package'
assert_equal "$(package_name zypper uuid)" libuuid-devel 'Zypper UUID development package'
assert_equal "$(package_name apt-get llvm)" llvm 'LLVM tools package'
assert_equal "$(package_name pacman binutils)" binutils 'binutils package'
assert_equal "$(package_install_arguments pacman | tr '\n' ' ' | sed 's/ $//')" '-Syu --needed --noconfirm' 'pacman safe install arguments'
assert_equal "$(package_install_arguments apt-get)" -y 'APT install arguments'
assert_equal "$(package_install_arguments dnf)" -y 'DNF install arguments'
assert_equal "$(package_install_arguments zypper | tr '\n' ' ' | sed 's/ $//')" '--non-interactive install' 'Zypper install arguments'
assert_true mac_requires_full_xcode xcode4
assert_false mac_requires_full_xcode ninja
assert_equal "$(json_escape $'quote" slash\\ tab\t')" 'quote\" slash\\ tab\t' 'JSON escaping'
assert_equal "$(config_value "$ROOT/Config/Dependencies.lock" SPDLOG_COMMIT)" 79524ddd08a4ec981b7fea76afd08ee05f83755d 'spdlog lock'
assert_equal "$(config_value "$ROOT/Config/Dependencies.lock" DOCTEST_COMMIT)" 2d0a9359a60c51affe2a9bebb1be1dca47868151 'doctest lock'

package_stage="$(mktemp -d)"
for path in bin/Client lib/libCore.a include/Core/Core.h include/Core/Log.h third-party/spdlog/spdlog.h third-party/licenses/spdlog-LICENSE.txt third-party/licenses/fmt-LICENSE.rst third-party/licenses/doctest-LICENSE.txt README.md LICENSE.txt THIRD_PARTY_NOTICES.md build-manifest.json; do
  mkdir -p "$package_stage/$(dirname "$path")"; : > "$package_stage/$path"
done
assert_true validate_package_stage "$package_stage" Client Core Core
rm "$package_stage/third-party/licenses/spdlog-LICENSE.txt"
assert_false validate_package_stage "$package_stage" Client Core Core
rm -rf "$package_stage"

parent_fixture="$(mktemp -d)"; fixture="$parent_fixture/Template"
trap 'rm -rf "$parent_fixture"' EXIT
mkdir -p "$fixture/archive"
printf '%s\n' premake > "$fixture/archive/premake5"
chmod 0644 "$fixture/archive/premake5"
assert_equal "$(find_premake_binary "$fixture/archive")" "$fixture/archive/premake5" 'non-executable Premake discovery'
mkdir -p "$fixture/Scripts/Unix" "$fixture/Config" "$fixture/$CORE_DIRECTORY/Include/$PROJECT_NAMESPACE" "$fixture/$CORE_DIRECTORY/Source" "$fixture/$CLIENT_DIRECTORY/Source" "$fixture/$TESTS_DIRECTORY/Source" "$fixture/Vendor" "$fixture/Build/Bin"
cp "$ROOT/Scripts/Unix/common.sh" "$ROOT/Scripts/Unix/rename.sh" "$ROOT/Scripts/Unix/clean.sh" "$fixture/Scripts/Unix/"
cp "$ROOT/Config/Project.conf" "$fixture/Config/Project.conf"
printf '%s\n' "#ifndef ${PROJECT_MACRO_PREFIX}_CORE_CORE_H" "#define ${PROJECT_MACRO_PREFIX}_CORE_CORE_H" "namespace $PROJECT_NAMESPACE { const char* GetName(); }" '#endif' > "$fixture/$CORE_DIRECTORY/Include/$PROJECT_NAMESPACE/Core.h"
printf '%s\n' "#ifndef ${PROJECT_MACRO_PREFIX}_CORE_LOG_H" "#define ${PROJECT_MACRO_PREFIX}_CORE_LOG_H" "namespace $PROJECT_NAMESPACE { class Log; }" '#endif' > "$fixture/$CORE_DIRECTORY/Include/$PROJECT_NAMESPACE/Log.h"
for source in "$fixture/$CORE_DIRECTORY/Source/Library.cpp" "$fixture/$CLIENT_DIRECTORY/Source/Main.cpp" "$fixture/$TESTS_DIRECTORY/Source/Main.cpp"; do
  printf '#include "%s/Core.h"\n' "$PROJECT_NAMESPACE" > "$source"
done
printf '%s %s\n' "$PROJECT_IDENTIFIER" "$REPOSITORY_SLUG" > "$fixture/README.md"
printf '%s\n' vendor > "$fixture/Vendor/keep.txt"
printf '%s\n' build > "$fixture/Build/Bin/remove.txt"

git -C "$parent_fixture" init --quiet
git -C "$parent_fixture" config user.email scripts@example.invalid
git -C "$parent_fixture" config user.name 'Script Tests'
git -C "$parent_fixture" add Template
git -C "$parent_fixture" commit --quiet -m fixture
printf '%s\n' dirty >> "$fixture/README.md"
assert_false bash "$fixture/Scripts/Unix/rename.sh" BlockedRename
git -C "$parent_fixture" checkout --quiet -- Template/README.md

bash "$fixture/Scripts/Unix/rename.sh" ScriptFixture 'Script Fixture' example/script-fixture >/dev/null
assert_false test -e "$fixture/.git"
assert_true test -f "$fixture/ScriptFixtureCore/Include/ScriptFixture/Core.h"
assert_true grep -q '^CORE_TARGET=ScriptFixtureCore$' "$fixture/Config/Project.conf"
assert_true grep -q '^PROJECT_MACRO_PREFIX=SCRIPT_FIXTURE$' "$fixture/Config/Project.conf"
assert_false grep -R -q "$PROJECT_MACRO_PREFIX" "$fixture/ScriptFixtureCore/Include"
assert_true grep -q SCRIPT_FIXTURE_CORE_CORE_H "$fixture/ScriptFixtureCore/Include/ScriptFixture/Core.h"
bash "$fixture/Scripts/Unix/clean.sh" full >/dev/null
assert_false test -d "$fixture/Build/Bin"
assert_true test -f "$fixture/Vendor/keep.txt"
assert_true test -f "$fixture/ScriptFixtureCore/Source/Library.cpp"
printf 'Unix script regression tests passed.\n'
