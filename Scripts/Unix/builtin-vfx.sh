#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cache_helper="$ROOT/Scripts/Unix/generated-content-cache.sh"
source "$cache_helper"
compiler="$ROOT/Build/Tools/ShaderCompiler/KeireShaderCompiler"
source_file="$ROOT/KeireCore/Shaders/BuiltinVfx.hlsl"
visibility_source_file="$ROOT/KeireCore/Shaders/BuiltinVfxVisibility.hlsl"
generated_directory="$ROOT/Build/Generated/Keire"
header="$generated_directory/BuiltinVfxShaders.h"
stamp="$generated_directory/BuiltinVfxShaders.stamp"
lock="$ROOT/Build/Generated/.locks/builtin-vfx.lock"

[[ -x "$compiler" ]] || {
  printf 'KeireShaderCompiler is required before generating the built-in VFX shaders.\n' >&2
  exit 1
}

fingerprint="$(generated_content_fingerprint builtin-vfx-v2 "${BASH_SOURCE[0]}" "$cache_helper" \
  "$compiler" "$source_file" "$visibility_source_file")"
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
temporary="$(mktemp -d "$generated_directory/.builtin-vfx.XXXXXX")"

stages=(
  "compute CSInitialize Initialize"
  "compute CSReset Reset"
  "compute CSKill Kill"
  "compute CSTransform Transform"
  "compute CSSimulate Simulate"
  "compute CSSimulateOutput SimulateOutput"
  "compute CSSpawn Spawn"
  "compute CSSpawnInitialize SpawnInitialize"
  "compute CSSpawnOutput SpawnOutput"
  "compute CSMapStrips MapStrips"
  "compute CSLinkStrips LinkStrips"
  "compute CSFinalize Finalize"
  "compute CSResetRender ResetRender"
  "compute CSFilterRender FilterRender"
  "compute CSBuildVisibilityCandidates BuildVisibilityCandidates"
  "compute CSCompactVisibility CompactVisibility"
  "vertex VSMain Vertex"
  "vertex VSRibbon RibbonVertex"
  "fragment PSMain Fragment"
  "vertex VSMesh MeshVertex"
  "fragment PSMesh MeshFragment"
  "vertex VSCpu CpuVertex"
  "fragment PSCpu CpuFragment"
)
variants=("DXIL Dxil dxil" "SPIRV Spirv spv" "MSL Msl msl")

for stage_row in "${stages[@]}"; do
  read -r stage entry name <<< "$stage_row"
  stage_source="$source_file"
  if [[ "$name" == "BuildVisibilityCandidates" || "$name" == "CompactVisibility" ]]; then
    stage_source="$visibility_source_file"
  fi
  for variant_row in "${variants[@]}"; do
    read -r destination variant extension <<< "$variant_row"
    "$compiler" "$stage_source" --source HLSL --dest "$destination" --stage "$stage" --entrypoint "$entry" \
      --output "$temporary/$name-$variant.$extension"
  done
done

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
  mv "$temporary_header" "$header"
fi
generated_content_write_stamp "$stamp" "$fingerprint"
