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
rm -rf "$stage"; mkdir -p "$stage/bin" "$stage/lib" "$stage/include" "$stage/third-party/spdlog"
cp "$ROOT/Build/Bin/$CONFIGURATION-$system-$output_arch/$CLIENT_TARGET/$CLIENT_TARGET" "$stage/bin/"
cp "$ROOT/Build/Bin/$CONFIGURATION-$system-$output_arch/$CORE_TARGET/lib$CORE_TARGET.a" "$stage/lib/"
cp -R "$ROOT/$CORE_DIRECTORY/Include/"* "$stage/include/"; cp -R "$ROOT/Vendor/spdlog/include/spdlog/"* "$stage/third-party/spdlog/"
cp "$ROOT/README.md" "$ROOT/LICENSE.txt" "$ROOT/THIRD_PARTY_NOTICES.md" "$stage/"
commit="$(git -C "$ROOT" rev-parse HEAD 2>/dev/null || true)"; spdlog="$(config_value "$ROOT/Config/Dependencies.lock" SPDLOG_COMMIT)"; doctest="$(config_value "$ROOT/Config/Dependencies.lock" DOCTEST_COMMIT)"
compiler_command=gcc; [[ "$TOOLSET" == clang ]] && compiler_command=clang
compiler="$("$compiler_command" --version | awk 'NR == 1 { print }')"
printf '{\n  "project": "%s",\n  "commit": "%s",\n  "platform": "%s",\n  "architecture": "%s",\n  "configuration": "%s",\n  "generator": "%s",\n  "toolset": "%s",\n  "compiler": "%s",\n  "spdlog": "%s",\n  "doctest": "%s"\n}\n' "$PROJECT_IDENTIFIER" "$commit" "$os_name" "$ARCHITECTURE" "$CONFIGURATION" "$GENERATOR" "$TOOLSET" "$compiler" "$spdlog" "$doctest" > "$stage/build-manifest.json"
archive="$ROOT/Artifacts/$name.tar.gz"; tar -C "$stage" -czf "$archive" .
if command -v sha256sum >/dev/null 2>&1; then
  digest="$(sha256sum "$archive" | awk '{print $1}')"
else
  digest="$(shasum -a 256 "$archive" | awk '{print $1}')"
fi
printf '%s  %s\n' "$digest" "$(basename "$archive")" > "$archive.sha256"
if [[ -d "$ROOT/Build/Bin/$CONFIGURATION-$system-$output_arch/$CLIENT_TARGET/$CLIENT_TARGET.dSYM" ]]; then
  symbols="$ROOT/Artifacts/$name-symbols.tar.gz"
  tar -C "$ROOT/Build/Bin/$CONFIGURATION-$system-$output_arch/$CLIENT_TARGET" -czf "$symbols" "$CLIENT_TARGET.dSYM"
  if command -v sha256sum >/dev/null 2>&1; then symbol_digest="$(sha256sum "$symbols" | awk '{print $1}')"; else symbol_digest="$(shasum -a 256 "$symbols" | awk '{print $1}')"; fi
  printf '%s  %s\n' "$symbol_digest" "$(basename "$symbols")" > "$symbols.sha256"
fi
printf '==> Package created: %s\n' "$archive"
