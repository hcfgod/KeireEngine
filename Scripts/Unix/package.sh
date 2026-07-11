#!/usr/bin/env bash
set -euo pipefail
PLATFORM="$1"; shift
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"; source "$ROOT/Scripts/Unix/common.sh"
GENERATOR=ninja; CONFIGURATION=Release; ARCHITECTURE="$(native_architecture)"; TOOLSET=default; TARGET=Client; CI=0; UPDATE=0; FORCE=0; INSTALL_OPTIONAL=0
parse_build_arguments "$@"; [[ "$CONFIGURATION" == Release || "$CONFIGURATION" == Dist ]] || { printf 'Package requires Release or Dist.\n' >&2; exit 1; }
load_project_config "$ROOT"; TOOLSET="$(resolve_unix_toolset "$PLATFORM" "$TOOLSET")"; system=linux; os_name=linux; [[ "$PLATFORM" == Mac ]] && { system=macosx; os_name=macos; }
common=(--generator "$GENERATOR" --configuration "$CONFIGURATION" --architecture "$ARCHITECTURE" --toolset "$TOOLSET"); [[ $CI -eq 1 ]] && common+=(--ci)
test_args=("${common[@]}"); [[ $UPDATE -eq 1 ]] && test_args+=(--update); [[ $FORCE -eq 1 ]] && test_args+=(--force)
bash "$ROOT/Scripts/$PLATFORM/test.sh" "${test_args[@]}"; bash "$ROOT/Scripts/$PLATFORM/run.sh" "${common[@]}"
output_arch="$(architecture_output_name "$ARCHITECTURE")"; name="$ARTIFACT_PREFIX-$os_name-$ARCHITECTURE-$CONFIGURATION"; stage="$ROOT/Artifacts/$name"
archive="$ROOT/Artifacts/$name.tar.gz"; symbols="$ROOT/Artifacts/$name-symbols.tar.gz"; symbol_stage="$ROOT/Artifacts/$name-symbols"
rm -rf "$stage" "$symbol_stage"; rm -f "$archive" "$archive.sha256" "$symbols" "$symbols.sha256"
mkdir -p "$stage/bin" "$stage/lib" "$stage/include" "$stage/third-party/spdlog" "$stage/third-party/licenses"
client_source="$ROOT/Build/Bin/$CONFIGURATION-$system-$output_arch/$CLIENT_TARGET/$CLIENT_TARGET"
core_source="$ROOT/Build/Bin/$CONFIGURATION-$system-$output_arch/$CORE_TARGET/lib$CORE_TARGET.a"
cp "$client_source" "$stage/bin/"; cp "$core_source" "$stage/lib/"
cp -R "$ROOT/$CORE_DIRECTORY/Include/"* "$stage/include/"; cp -R "$ROOT/Vendor/spdlog/include/spdlog/"* "$stage/third-party/spdlog/"
cp "$ROOT/Vendor/spdlog/LICENSE" "$stage/third-party/licenses/spdlog-LICENSE.txt"
cp "$ROOT/Vendor/spdlog/include/spdlog/fmt/bundled/fmt.license.rst" "$stage/third-party/licenses/fmt-LICENSE.rst"
cp "$ROOT/Vendor/doctest/LICENSE.txt" "$stage/third-party/licenses/doctest-LICENSE.txt"
cp "$ROOT/README.md" "$ROOT/LICENSE.txt" "$ROOT/THIRD_PARTY_NOTICES.md" "$stage/"
commit="$(git -C "$ROOT" rev-parse HEAD 2>/dev/null || true)"; spdlog="$(config_value "$ROOT/Config/Dependencies.lock" SPDLOG_COMMIT)"; doctest="$(config_value "$ROOT/Config/Dependencies.lock" DOCTEST_COMMIT)"
compiler_command=gcc; [[ "$TOOLSET" == clang ]] && compiler_command=clang
compiler="$("$compiler_command" --version | awk 'NR == 1 { print }')"
printf '{\n  "project": "%s",\n  "commit": "%s",\n  "platform": "%s",\n  "architecture": "%s",\n  "configuration": "%s",\n  "generator": "%s",\n  "toolset": "%s",\n  "compiler": "%s",\n  "spdlog": "%s",\n  "doctest": "%s"\n}\n' "$(json_escape "$PROJECT_IDENTIFIER")" "$(json_escape "$commit")" "$(json_escape "$os_name")" "$(json_escape "$ARCHITECTURE")" "$(json_escape "$CONFIGURATION")" "$(json_escape "$GENERATOR")" "$(json_escape "$TOOLSET")" "$(json_escape "$compiler")" "$(json_escape "$spdlog")" "$(json_escape "$doctest")" > "$stage/build-manifest.json"
validate_package_stage "$stage" "$CLIENT_TARGET" "$CORE_TARGET" "$PROJECT_NAMESPACE"

if [[ "$CONFIGURATION" == Release && "$PLATFORM" == Linux ]]; then
  command -v objcopy >/dev/null 2>&1 || { printf 'objcopy is required to package Linux Release symbols.\n' >&2; exit 1; }
  mkdir -p "$symbol_stage/Client" "$symbol_stage/Core"
  objcopy --only-keep-debug "$client_source" "$symbol_stage/Client/$CLIENT_TARGET.debug"
  cp "$core_source" "$symbol_stage/Core/lib$CORE_TARGET.a"
  objcopy --strip-debug "$stage/bin/$CLIENT_TARGET"
  objcopy --add-gnu-debuglink="$symbol_stage/Client/$CLIENT_TARGET.debug" "$stage/bin/$CLIENT_TARGET"
  objcopy --strip-debug "$stage/lib/lib$CORE_TARGET.a"
elif [[ "$CONFIGURATION" == Release && "$PLATFORM" == Mac ]]; then
  mkdir -p "$symbol_stage/Client"
  xcrun dsymutil "$client_source" -o "$symbol_stage/Client/$CLIENT_TARGET.dSYM"
  xcrun strip -S "$stage/bin/$CLIENT_TARGET"
fi

tar -C "$stage" -czf "$archive" .
if command -v sha256sum >/dev/null 2>&1; then
  digest="$(sha256sum "$archive" | awk '{print $1}')"
else
  digest="$(shasum -a 256 "$archive" | awk '{print $1}')"
fi
printf '%s  %s\n' "$digest" "$(basename "$archive")" > "$archive.sha256"
if [[ -d "$symbol_stage" ]] && find "$symbol_stage" -type f -print -quit | grep -q .; then
  tar -C "$symbol_stage" -czf "$symbols" .
  if command -v sha256sum >/dev/null 2>&1; then symbol_digest="$(sha256sum "$symbols" | awk '{print $1}')"; else symbol_digest="$(shasum -a 256 "$symbols" | awk '{print $1}')"; fi
  printf '%s  %s\n' "$symbol_digest" "$(basename "$symbols")" > "$symbols.sha256"
fi
printf '==> Package created: %s\n' "$archive"
