#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
compiler="$ROOT/Build/Tools/ShaderCompiler/KeireShaderCompiler"
source_file="$ROOT/KeireCore/Shaders/BuiltinVfx.hlsl"
generated_directory="$ROOT/Build/Generated/Keire"
header="$generated_directory/BuiltinVfxShaders.h"

[[ -x "$compiler" ]] || {
  printf 'KeireShaderCompiler is required before generating the built-in VFX shaders.\n' >&2
  exit 1
}

temporary="$(mktemp -d)"
trap 'rm -rf "$temporary"' EXIT

stages=(
  "compute CSInitialize Initialize"
  "compute CSReset Reset"
  "compute CSKill Kill"
  "compute CSTransform Transform"
  "compute CSSimulate Simulate"
  "compute CSSpawn Spawn"
  "compute CSFinalize Finalize"
  "vertex VSMain Vertex"
  "fragment PSMain Fragment"
)
variants=("DXIL Dxil dxil" "SPIRV Spirv spv" "MSL Msl msl")

for stage_row in "${stages[@]}"; do
  read -r stage entry name <<< "$stage_row"
  for variant_row in "${variants[@]}"; do
    read -r destination variant extension <<< "$variant_row"
    "$compiler" "$source_file" --source HLSL --dest "$destination" --stage "$stage" --entrypoint "$entry" \
      --output "$temporary/$name-$variant.$extension"
  done
done

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
temporary_header="$temporary/BuiltinVfxShaders.h"
{
  printf '#pragma once\n\n'
  printf 'namespace Keire::Detail\n{\n'
  for stage_row in "${stages[@]}"; do
    read -r _ _ name <<< "$stage_row"
    for variant_row in "${variants[@]}"; do
      read -r _ variant extension <<< "$variant_row"
      emit_array "BuiltinVfx$name$variant" "$temporary/$name-$variant.$extension"
    done
  done
  printf '} // namespace Keire::Detail\n'
} > "$temporary_header"

if [[ ! -f "$header" ]] || ! cmp -s "$temporary_header" "$header"; then
  cp "$temporary_header" "$header"
fi
