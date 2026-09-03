#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cache_helper="$ROOT/Scripts/Unix/generated-content-cache.sh"
source "$cache_helper"
compiler="$ROOT/Build/Tools/ShaderCompiler/KeireShaderCompiler"
source_files=("$ROOT/KeireCore/Shaders/BuiltinUnlit.hlsl" "$ROOT/KeireCore/Shaders/BuiltinSky.hlsl" \
  "$ROOT/KeireCore/Shaders/BuiltinGrid.hlsl" \
  "$ROOT/KeireCore/Shaders/BuiltinShadow.hlsl" "$ROOT/KeireCore/Shaders/BuiltinToneMap.hlsl" \
  "$ROOT/KeireCore/Shaders/BuiltinRuntimeUi.hlsl" "$ROOT/KeireCore/Shaders/BuiltinDeferredGBuffer.hlsl" \
  "$ROOT/KeireCore/Shaders/BuiltinDeferredLighting.hlsl" "$ROOT/KeireCore/Shaders/BuiltinIrradyn.hlsl")
prefixes=(BuiltinUnlit BuiltinSky BuiltinGrid BuiltinShadow BuiltinToneMap BuiltinRuntimeUi \
  BuiltinDeferredGBuffer BuiltinDeferredLighting BuiltinIrradyn)
generated="$ROOT/Build/Generated/Keire/BuiltinUnlitShaders.h"
stamp="$ROOT/Build/Generated/Keire/BuiltinRenderingShaders.stamp"
lock="$ROOT/Build/Generated/.locks/builtin-rendering.lock"

[[ -x "$compiler" ]] || { printf 'KeireShaderCompiler is required before generating built-in rendering shaders.\n' >&2; exit 1; }
fingerprint="$(generated_content_fingerprint builtin-rendering-v2 "${BASH_SOURCE[0]}" "$cache_helper" \
  "$compiler" "${source_files[@]}")"
generated_content_is_current "$generated" "$stamp" "$fingerprint" && exit 0

mkdir -p "$(dirname "$generated")"
generated_content_acquire_lock "$lock"
temporary=""
cleanup() {
  [[ -z "$temporary" ]] || rm -rf "$temporary"
  generated_content_release_lock "$lock"
}
trap cleanup EXIT
generated_content_is_current "$generated" "$stamp" "$fingerprint" && exit 0
temporary="$(mktemp -d "$(dirname "$generated")/.builtin-rendering.XXXXXX")"
names=(VertexDxil FragmentDxil VertexSpirV FragmentSpirV VertexMsl FragmentMsl)
stages=(vertex fragment vertex fragment vertex fragment)
entries=(VSMain PSMain VSMain PSMain VSMain PSMain)
destinations=(DXIL DXIL SPIRV SPIRV MSL MSL)
files=(vertex.dxil fragment.dxil vertex.spv fragment.spv vertex.metal fragment.metal)

for source_index in "${!source_files[@]}"; do
  for index in "${!names[@]}"; do
    "$compiler" "${source_files[$source_index]}" -s HLSL -d "${destinations[$index]}" -t "${stages[$index]}" \
      -e "${entries[$index]}" \
      -o "$temporary/${prefixes[$source_index]}-${files[$index]}"
  done
done

{
  printf '#pragma once\n\nnamespace Keire::Detail\n{\n'
  for source_index in "${!source_files[@]}"; do
    for index in "${!names[@]}"; do
      path="$temporary/${prefixes[$source_index]}-${files[$index]}"
      printf '    inline constexpr unsigned char %s%s[] = {\n' "${prefixes[$source_index]}" "${names[$index]}"
      od -An -v -t x1 "$path" | awk '{ printf "        "; for (i = 1; i <= NF; ++i) printf "0x%s, ", $i; printf "\n" }'
      printf '    };\n'
      printf '    inline constexpr unsigned int %s%sSize = %s;\n' "${prefixes[$source_index]}" "${names[$index]}" "$(wc -c < "$path" | tr -d ' ')"
    done
  done
  printf '} // namespace Keire::Detail\n'
} > "$temporary/BuiltinUnlitShaders.h"
if [[ ! -f "$generated" ]] || ! cmp -s "$temporary/BuiltinUnlitShaders.h" "$generated"; then
  mv "$temporary/BuiltinUnlitShaders.h" "$generated"
fi
generated_content_write_stamp "$stamp" "$fingerprint"
