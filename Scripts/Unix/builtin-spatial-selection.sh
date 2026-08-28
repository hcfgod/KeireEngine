#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cache_helper="$ROOT/Scripts/Unix/generated-content-cache.sh"
source "$cache_helper"
compiler="$ROOT/Build/Tools/ShaderCompiler/KeireShaderCompiler"
source_file="$ROOT/KeireCore/Shaders/BuiltinSpatialSelection.hlsl"
generated_directory="$ROOT/Build/Generated/Keire"
header="$generated_directory/BuiltinSpatialSelectionShaders.h"
stamp="$generated_directory/BuiltinSpatialSelectionShaders.stamp"
lock="$ROOT/Build/Generated/.locks/builtin-spatial-selection.lock"

[[ -x "$compiler" ]] || {
  printf 'KeireShaderCompiler is required before generating the built-in spatial-selection shader.\n' >&2
  exit 1
}

fingerprint="$(generated_content_fingerprint builtin-spatial-selection-v1 "${BASH_SOURCE[0]}" "$cache_helper" \
  "$compiler" "$source_file")"
generated_content_is_current "$header" "$stamp" "$fingerprint" && exit 0

mkdir -p "$generated_directory"
generated_content_acquire_lock "$lock"
temporary=""
cleanup() {
  [[ -z "$temporary" ]] || rm -rf "$temporary"
  generated_content_release_lock "$lock"
}
trap cleanup EXIT
generated_content_is_current "$header" "$stamp" "$fingerprint" && exit 0
temporary="$(mktemp -d "$generated_directory/.builtin-spatial-selection.XXXXXX")"

"$compiler" "$source_file" --source HLSL --dest DXIL --stage compute --entrypoint CSSelectSpatialLighting \
  --output "$temporary/spatial-selection.dxil"
"$compiler" "$source_file" --source HLSL --dest SPIRV --stage compute --entrypoint CSSelectSpatialLighting \
  --output "$temporary/spatial-selection.spv"
"$compiler" "$source_file" --source HLSL --dest MSL --stage compute --entrypoint CSSelectSpatialLighting \
  --output "$temporary/spatial-selection.msl"

emit_array() {
  local name="$1"
  local file="$2"
  printf '    inline constexpr unsigned char %s[] = {\n' "$name"
  od -An -v -tx1 "$file" | awk '{
    printf "        "
    for (field = 1; field <= NF; ++field)
      printf "0x%s, ", $field
    printf "\n"
  }'
  printf '    };\n\n'
}

temporary_header="$temporary/BuiltinSpatialSelectionShaders.h"
{
  printf '#pragma once\n\n'
  printf 'namespace Keire::Detail\n{\n'
  emit_array BuiltinSpatialSelectionComputeDxil "$temporary/spatial-selection.dxil"
  emit_array BuiltinSpatialSelectionComputeSpirv "$temporary/spatial-selection.spv"
  emit_array BuiltinSpatialSelectionComputeMsl "$temporary/spatial-selection.msl"
  printf '} // namespace Keire::Detail\n'
} > "$temporary_header"

if [[ ! -f "$header" ]] || ! cmp -s "$temporary_header" "$header"; then
  mv "$temporary_header" "$header"
fi
generated_content_write_stamp "$stamp" "$fingerprint"
