#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cache_helper="$ROOT/Scripts/Unix/generated-content-cache.sh"
source "$cache_helper"
compiler="$ROOT/Build/Tools/ShaderCompiler/KeireShaderCompiler"
source_file="$ROOT/KeireCore/Shaders/BuiltinSkinning.hlsl"
generated_directory="$ROOT/Build/Generated/Keire"
header="$generated_directory/BuiltinSkinningShaders.h"
stamp="$generated_directory/BuiltinSkinningShaders.stamp"
lock="$ROOT/Build/Generated/.locks/builtin-skinning.lock"

[[ -x "$compiler" ]] || {
  printf 'KeireShaderCompiler is required before generating the built-in skinning shader.\n' >&2
  exit 1
}

fingerprint="$(generated_content_fingerprint builtin-skinning-v1 "${BASH_SOURCE[0]}" "$cache_helper" \
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
temporary="$(mktemp -d "$generated_directory/.builtin-skinning.XXXXXX")"

"$compiler" "$source_file" --source HLSL --dest DXIL --stage compute --entrypoint CSMain \
  --output "$temporary/skinning.dxil"
"$compiler" "$source_file" --source HLSL --dest SPIRV --stage compute --entrypoint CSMain \
  --output "$temporary/skinning.spv"
"$compiler" "$source_file" --source HLSL --dest MSL --stage compute --entrypoint CSMain \
  --output "$temporary/skinning.msl"

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

mkdir -p "$generated_directory"
temporary_header="$temporary/BuiltinSkinningShaders.h"
{
  printf '#pragma once\n\n'
  printf 'namespace Keire::Detail\n{\n'
  emit_array BuiltinSkinningComputeDxil "$temporary/skinning.dxil"
  emit_array BuiltinSkinningComputeSpirv "$temporary/skinning.spv"
  emit_array BuiltinSkinningComputeMsl "$temporary/skinning.msl"
  printf '} // namespace Keire::Detail\n'
} > "$temporary_header"

if [[ ! -f "$header" ]] || ! cmp -s "$temporary_header" "$header"; then
  mv "$temporary_header" "$header"
fi
generated_content_write_stamp "$stamp" "$fingerprint"
