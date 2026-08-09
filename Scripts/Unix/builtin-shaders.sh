#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
compiler="$ROOT/Build/Tools/ShaderCompiler/KeireShaderCompiler"
source_files=("$ROOT/KeireCore/Shaders/BuiltinUnlit.hlsl" "$ROOT/KeireCore/Shaders/BuiltinSky.hlsl" \
  "$ROOT/KeireCore/Shaders/BuiltinGrid.hlsl" \
  "$ROOT/KeireCore/Shaders/BuiltinShadow.hlsl" "$ROOT/KeireCore/Shaders/BuiltinToneMap.hlsl" \
  "$ROOT/KeireCore/Shaders/BuiltinRuntimeUi.hlsl")
prefixes=(BuiltinUnlit BuiltinSky BuiltinGrid BuiltinShadow BuiltinToneMap BuiltinRuntimeUi)
generated="$ROOT/Build/Generated/Keire/BuiltinUnlitShaders.h"
temporary="$ROOT/Build/Generated/Keire/BuiltinShaderTemporary"

[[ -x "$compiler" ]] || { printf 'KeireShaderCompiler is required before generating built-in rendering shaders.\n' >&2; exit 1; }
if [[ -f "$generated" && "$generated" -nt "$compiler" ]]; then
  current=true
  for source_file in "${source_files[@]}"; do
    [[ "$generated" -nt "$source_file" ]] || current=false
  done
  $current && exit 0
fi

mkdir -p "$temporary" "$(dirname "$generated")"
names=(VertexDxil FragmentDxil VertexSpirV FragmentSpirV VertexMsl FragmentMsl)
stages=(vertex fragment vertex fragment vertex fragment)
entries=(VSMain PSMain VSMain PSMain VSMain PSMain)
destinations=(DXIL DXIL SPIRV SPIRV MSL MSL)
files=(vertex.dxil fragment.dxil vertex.spv fragment.spv vertex.metal fragment.metal)

for source_index in "${!source_files[@]}"; do
  for index in "${!names[@]}"; do
    "$compiler" "${source_files[$source_index]}" -s HLSL -d "${destinations[$index]}" -t "${stages[$index]}" \
      -e "${entries[$index]}" -o "$temporary/${prefixes[$source_index]}-${files[$index]}"
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
} > "$generated.tmp"
mv "$generated.tmp" "$generated"
rm -rf "$temporary"
