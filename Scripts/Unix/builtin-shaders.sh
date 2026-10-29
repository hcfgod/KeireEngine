#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cache_helper="$ROOT/Scripts/Unix/generated-content-cache.sh"
source "$cache_helper"
compiler="$ROOT/Build/Tools/ShaderCompiler/KeireShaderCompiler"
source_files=("$ROOT/KeireCore/Shaders/BuiltinUnlit.hlsl" "$ROOT/KeireCore/Shaders/BuiltinSky.hlsl" \
  "$ROOT/KeireCore/Shaders/BuiltinCloudVolume.hlsl" \
  "$ROOT/KeireCore/Shaders/BuiltinCloudTrace.hlsl" \
  "$ROOT/KeireCore/Shaders/BuiltinCloudResolve.hlsl" \
  "$ROOT/KeireCore/Shaders/BuiltinCloudComposite.hlsl" \
  "$ROOT/KeireCore/Shaders/BuiltinGrid.hlsl" \
  "$ROOT/KeireCore/Shaders/BuiltinDepthVelocity.hlsl" \
  "$ROOT/KeireCore/Shaders/BuiltinShadow.hlsl" "$ROOT/KeireCore/Shaders/BuiltinToneMap.hlsl" \
  "$ROOT/KeireCore/Shaders/BuiltinBloom.hlsl" \
  "$ROOT/KeireCore/Shaders/BuiltinRuntimeUi.hlsl" "$ROOT/KeireCore/Shaders/BuiltinDeferredGBuffer.hlsl" \
  "$ROOT/KeireCore/Shaders/BuiltinDeferredLighting.hlsl" "$ROOT/KeireCore/Shaders/BuiltinIrradyn.hlsl"
  "$ROOT/KeireCore/Shaders/BuiltinCloudLightCache.hlsl")
prefixes=(BuiltinUnlit BuiltinSky BuiltinCloudVolume BuiltinCloudTrace BuiltinCloudResolve BuiltinCloudComposite BuiltinGrid BuiltinDepthVelocity BuiltinShadow BuiltinToneMap BuiltinBloom BuiltinRuntimeUi \
  BuiltinDeferredGBuffer BuiltinDeferredLighting BuiltinIrradyn BuiltinCloudLightCache)
include_files=("$ROOT/KeireCore/Shaders/BuiltinLighting.hlsli" "$ROOT/KeireCore/Shaders/CloudLayered.hlsl"
               "$ROOT/KeireCore/Shaders/GridAppearance.hlsli"
               "$ROOT/KeireCore/Shaders/AtmosphereMedium.hlsli"
               "$ROOT/KeireCore/Shaders/AtmosphereGround.hlsli"
               "$ROOT/KeireCore/Shaders/CloudNoiseBuffer.hlsli" "$ROOT/KeireCore/Shaders/CloudVolume.hlsli" "$ROOT/KeireCore/Shaders/CloudVolumeGeometry.hlsli"
               "$ROOT/KeireCore/Shaders/PlanetaryCloudIntersections.hlsli"
               "$ROOT/KeireCore/Shaders/PlanetaryCloudGeometry.hlsli"
               "$ROOT/KeireCore/Shaders/PlanetaryCloudWeather.hlsli"
               "$ROOT/KeireCore/Shaders/PlanetaryCloudLayered.hlsli"
               "$ROOT/KeireCore/Shaders/FogDensity.hlsli" "$ROOT/KeireCore/Shaders/FogExclusion.hlsli" "$ROOT/KeireCore/Shaders/FogRayIntervals.hlsli"
               "$ROOT/KeireCore/Shaders/FogTerrainShadow.hlsli"
               "$ROOT/KeireCore/Shaders/CloudVolumeLighting.hlsli"
               "$ROOT/KeireCore/Shaders/CloudLightCacheSampling.hlsli"
               "$ROOT/KeireCore/Shaders/CloudTemporalRejection.hlsli"
               "$ROOT/KeireCore/Shaders/CloudRadianceEncoding.hlsli"
               "$ROOT/KeireCore/Shaders/DisplayTransfer.hlsli"
               "$ROOT/KeireCore/Shaders/CloudVolumeDensity.hlsli"
               "$ROOT/KeireCore/Shaders/CloudWeather.hlsli"
               "$ROOT/KeireCore/Shaders/SkyColorGradient.hlsli"
               "$ROOT/KeireCore/Shaders/CloudVolumeView.hlsli"
               "$ROOT/KeireCore/Shaders/VolumeSurfaceComposition.hlsli"
               "$ROOT/KeireCore/Shaders/VolumeTransport.hlsli")
generated="$ROOT/Build/Generated/Keire/BuiltinUnlitShaders.h"
generated_source="$ROOT/Build/Generated/Keire/CloudVolumeSource.h"
stamp="$ROOT/Build/Generated/Keire/BuiltinRenderingShaders.stamp"
lock="$ROOT/Build/Generated/.locks/builtin-rendering.lock"

[[ -x "$compiler" ]] || { printf 'KeireShaderCompiler is required before generating built-in rendering shaders.\n' >&2; exit 1; }
fingerprint="$(generated_content_fingerprint builtin-rendering-v2 "${BASH_SOURCE[0]}" "$cache_helper" \
  "$compiler" "${source_files[@]}" "${include_files[@]}")"
[[ -f "$generated_source" ]] && generated_content_is_current "$generated" "$stamp" "$fingerprint" && exit 0

mkdir -p "$(dirname "$generated")"
generated_content_acquire_lock "$lock"
temporary=""
cleanup() {
  [[ -z "$temporary" ]] || rm -rf "$temporary"
  generated_content_release_lock "$lock"
}
trap cleanup EXIT
[[ -f "$generated_source" ]] && generated_content_is_current "$generated" "$stamp" "$fingerprint" && exit 0
temporary="$(mktemp -d "$(dirname "$generated")/.builtin-rendering.XXXXXX")"
names=(VertexDxil FragmentDxil VertexSpirV FragmentSpirV VertexMsl FragmentMsl ComputeDxil ComputeSpirV ComputeMsl)
stages=(vertex fragment vertex fragment vertex fragment compute compute compute)
entries=(VSMain PSMain VSMain PSMain VSMain PSMain CSMain CSMain CSMain)
destinations=(DXIL DXIL SPIRV SPIRV MSL MSL DXIL SPIRV MSL)
files=(vertex.dxil fragment.dxil vertex.spv fragment.spv vertex.metal fragment.metal compute.dxil compute.spv compute.metal)

select_source_variants() {
  if [[ "$1" == BuiltinCloudLightCache ]]; then
    source_variants=(6 7 8)
  else
    source_variants=(0 1 2 3 4 5)
  fi
}

for source_index in "${!source_files[@]}"; do
  select_source_variants "${prefixes[$source_index]}"
  for index in "${source_variants[@]}"; do
    "$compiler" "${source_files[$source_index]}" -s HLSL -d "${destinations[$index]}" -t "${stages[$index]}" \
      -e "${entries[$index]}" -I "$ROOT/KeireCore/Shaders" \
      -o "$temporary/${prefixes[$source_index]}-${files[$index]}"
  done
done

{
  printf '#pragma once\n\nnamespace Keire::Detail\n{\n'
  for source_index in "${!source_files[@]}"; do
    select_source_variants "${prefixes[$source_index]}"
    for index in "${source_variants[@]}"; do
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
# Embed the shared implementation for generated materials in installed editors.
{
  printf '#pragma once\n\nnamespace Keire::Detail\n{\n'
  printf '    inline constexpr char CloudVolumeShaderSource[] = "\\n"\n'
  for source_name in VolumeTransport AtmosphereMedium CloudVolumeGeometry \
    PlanetaryCloudIntersections PlanetaryCloudGeometry PlanetaryCloudWeather \
    FogExclusion FogDensity FogRayIntervals CloudVolumeLighting CloudVolumeDensity CloudWeather CloudNoiseBuffer CloudVolume VolumeSurfaceComposition CloudVolumeView; do
    source_path="$ROOT/KeireCore/Shaders/$source_name.hlsli"
    if grep -Fq ')KEIRE_CLOUD"' "$source_path"; then
      printf 'Cloud source contains the reserved raw string delimiter.\n' >&2
      exit 1
    fi
    sed -E -e 's/\r$//' -e '/^[[:space:]]*#[[:space:]]*pragma[[:space:]]+once/d' \
      -e '/^[[:space:]]*#[[:space:]]*include[[:space:]]/d' "$source_path" |
      LC_ALL=C awk '{
        if (length($0) > 8192) {
          print "Cloud source line exceeds the portable embedded literal limit." > "/dev/stderr"
          exit 1
        }
        printf "R\"KEIRE_CLOUD(%s\n)KEIRE_CLOUD\"\n", $0
      }'
  done
  printf ';\n} // namespace Keire::Detail\n'
} > "$temporary/CloudVolumeSource.h"
if [[ ! -f "$generated_source" ]] || ! cmp -s "$temporary/CloudVolumeSource.h" "$generated_source"; then
  mv "$temporary/CloudVolumeSource.h" "$generated_source"
fi
generated_content_write_stamp "$stamp" "$fingerprint"
