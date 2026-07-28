#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
compiler="$ROOT/Build/Tools/ShaderCompiler/KeireShaderCompiler"
source_file="$ROOT/KeireCore/Shaders/BuiltinSkinning.hlsl"
generated_directory="$ROOT/Build/Generated/Keire"
header="$generated_directory/BuiltinSkinningShaders.h"

[[ -x "$compiler" ]] || {
  printf 'KeireShaderCompiler is required before generating the built-in skinning shader.\n' >&2
  exit 1
}

temporary="$(mktemp -d)"
trap 'rm -rf "$temporary"' EXIT

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
    for (index = 1; index <= NF; ++index)
      printf "0x%s, ", $index
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
  cp "$temporary_header" "$header"
fi
