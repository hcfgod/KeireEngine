#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cache_helper="$ROOT/Scripts/Unix/generated-content-cache.sh"
source "$cache_helper"
compiler="$ROOT/Build/Tools/ShaderCompiler/KeireShaderCompiler"
generated_directory="$ROOT/Build/Generated/Keire"
header="$generated_directory/BuiltinOcclusionShaders.h"
stamp="$generated_directory/BuiltinOcclusionShaders.stamp"
lock="$ROOT/Build/Generated/.locks/builtin-occlusion.lock"
sources=(
  "$ROOT/KeireCore/Shaders/BuiltinOcclusionDepth.hlsl"
  "$ROOT/KeireCore/Shaders/BuiltinOcclusionPyramid.hlsl"
  "$ROOT/KeireCore/Shaders/BuiltinOcclusionClassify.hlsl"
  "$ROOT/KeireCore/Shaders/BuiltinOcclusionScanBlocks.hlsl"
  "$ROOT/KeireCore/Shaders/BuiltinOcclusionScanBatches.hlsl"
  "$ROOT/KeireCore/Shaders/BuiltinOcclusionScatter.hlsl"
  "$ROOT/KeireCore/Shaders/BuiltinOcclusionDebugPyramid.hlsl"
  "$ROOT/KeireCore/Shaders/BuiltinOcclusionDebugBounds.hlsl"
)

[[ -x "$compiler" ]] || {
  printf 'KeireShaderCompiler is required before generating the built-in occlusion shaders.\n' >&2
  exit 1
}

fingerprint="$(generated_content_fingerprint builtin-occlusion-v1 "${BASH_SOURCE[0]}" "$cache_helper" \
  "$compiler" "${sources[@]}")"
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
temporary="$(mktemp -d "$generated_directory/.builtin-occlusion.XXXXXX")"

stages=(
  "${sources[0]} vertex VSDepth DepthVertex"
  "${sources[0]} fragment PSDepth DepthFragment"
  "${sources[1]} compute CSBuildBase PyramidBase"
  "${sources[1]} compute CSReduce PyramidReduce"
  "${sources[2]} compute CSClassify Classify"
  "${sources[3]} compute CSScanBlocks ScanBlocks"
  "${sources[4]} compute CSScanBatches ScanBatches"
  "${sources[5]} compute CSScatter Scatter"
  "${sources[6]} vertex VSDebugPyramid DebugPyramidVertex"
  "${sources[6]} fragment PSDebugPyramid DebugPyramidFragment"
  "${sources[7]} vertex VSDebugBounds DebugBoundsVertex"
  "${sources[7]} fragment PSDebugBounds DebugBoundsFragment"
)
variants=("DXIL Dxil dxil" "SPIRV Spirv spv" "MSL Msl msl")

for stage_row in "${stages[@]}"; do
  read -r source_file stage entry name <<< "$stage_row"
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
    for (field = 1; field <= NF; ++field)
      printf "0x%s, ", $field
    printf "\n"
  }'
  printf '    };\n\n'
}

temporary_header="$temporary/BuiltinOcclusionShaders.h"
{
  printf '#pragma once\n\n'
  printf 'namespace Keire::Detail\n{\n'
  for stage_row in "${stages[@]}"; do
    read -r _ _ _ name <<< "$stage_row"
    for variant_row in "${variants[@]}"; do
      read -r _ variant extension <<< "$variant_row"
      emit_array "BuiltinOcclusion$name$variant" "$temporary/$name-$variant.$extension"
    done
  done
  printf '} // namespace Keire::Detail\n'
} > "$temporary_header"

if [[ ! -f "$header" ]] || ! cmp -s "$temporary_header" "$header"; then
  mv "$temporary_header" "$header"
fi
generated_content_write_stamp "$stamp" "$fingerprint"
