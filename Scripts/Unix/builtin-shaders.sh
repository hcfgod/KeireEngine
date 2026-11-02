#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cache_helper="$ROOT/Scripts/Unix/generated-content-cache.sh"
source "$cache_helper"
blob_helper="$ROOT/Scripts/Unix/shader-blob-cache.sh"
source "$blob_helper"
blob_cache="$ROOT/Build/Cache/ShaderBlobs/unix"
compiler="$ROOT/Build/Tools/ShaderCompiler/KeireShaderCompiler"
source_files=("$ROOT/KeireCore/Shaders/BuiltinUnlit.hlsl" "$ROOT/KeireCore/Shaders/BuiltinSky.hlsl" \
  "$ROOT/KeireCore/Shaders/BuiltinCloudVolume.hlsl" \
  "$ROOT/KeireCore/Shaders/BuiltinCloudTrace.hlsl" \
  "$ROOT/KeireCore/Shaders/BuiltinCloudResolve.hlsl" \
  "$ROOT/KeireCore/Shaders/BuiltinCloudComposite.hlsl" \
  "$ROOT/KeireCore/Shaders/BuiltinGrid.hlsl" \
  "$ROOT/KeireCore/Shaders/BuiltinWater.hlsl" \
  "$ROOT/KeireCore/Shaders/BuiltinDepthVelocity.hlsl" \
  "$ROOT/KeireCore/Shaders/BuiltinShadow.hlsl" "$ROOT/KeireCore/Shaders/BuiltinToneMap.hlsl" \
  "$ROOT/KeireCore/Shaders/BuiltinBloom.hlsl" \
  "$ROOT/KeireCore/Shaders/BuiltinRuntimeUi.hlsl" "$ROOT/KeireCore/Shaders/BuiltinDeferredGBuffer.hlsl" \
  "$ROOT/KeireCore/Shaders/BuiltinDeferredLighting.hlsl" "$ROOT/KeireCore/Shaders/BuiltinIrradyn.hlsl"
  "$ROOT/KeireCore/Shaders/BuiltinCloudLightCache.hlsl")
prefixes=(BuiltinUnlit BuiltinSky BuiltinCloudVolume BuiltinCloudTrace BuiltinCloudResolve BuiltinCloudComposite BuiltinGrid BuiltinWater BuiltinDepthVelocity BuiltinShadow BuiltinToneMap BuiltinBloom BuiltinRuntimeUi \
  BuiltinDeferredGBuffer BuiltinDeferredLighting BuiltinIrradyn BuiltinCloudLightCache)
include_files=("$ROOT/KeireCore/Shaders/BuiltinLighting.hlsli" "$ROOT/KeireCore/Shaders/CloudLayered.hlsl"
               "$ROOT/KeireCore/Shaders/BuiltinBrdf.hlsli"
               "$ROOT/KeireCore/Shaders/BuiltinDirectionalShadow.hlsli"
               "$ROOT/KeireCore/Shaders/WaterWaves.hlsli" "$ROOT/KeireCore/Shaders/WaterOptics.hlsli"
               "$ROOT/KeireCore/Shaders/GridAppearance.hlsli"
               "$ROOT/KeireCore/Shaders/AtmosphereMedium.hlsli"
               "$ROOT/KeireCore/Shaders/AtmosphereGround.hlsli"
               "$ROOT/KeireCore/Shaders/PlanetAtmosphereSet.hlsli"
               "$ROOT/KeireCore/Shaders/AtmosphereSunOcclusion.hlsli"
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
               "$ROOT/KeireCore/Shaders/BuiltinPostProcess.hlsli"
               "$ROOT/KeireCore/Shaders/BuiltinDepthOfField.hlsli"
               "$ROOT/KeireCore/Shaders/CloudVolumeDensity.hlsli"
               "$ROOT/KeireCore/Shaders/CloudWeather.hlsli"
               "$ROOT/KeireCore/Shaders/SkyColorGradient.hlsli"
               "$ROOT/KeireCore/Shaders/PlanetVolumeWeatherUniforms.hlsli"
               "$ROOT/KeireCore/Shaders/PlanetVolumeIntervalGrid.hlsli"
               "$ROOT/KeireCore/Shaders/PlanetVolumeRayIntervals.hlsli"
               "$ROOT/KeireCore/Shaders/PlanetVolumeTransport.hlsli"
               "$ROOT/KeireCore/Shaders/CloudVolumeView.hlsli"
               "$ROOT/KeireCore/Shaders/VolumeSurfaceComposition.hlsli"
               "$ROOT/KeireCore/Shaders/VolumeTransport.hlsli")
generated="$ROOT/Build/Generated/Keire/BuiltinUnlitShaders.h"
generated_source="$ROOT/Build/Generated/Keire/CloudVolumeSource.h"
stamp="$ROOT/Build/Generated/Keire/BuiltinRenderingShaders.stamp"
lock="$ROOT/Build/Generated/.locks/builtin-rendering.lock"

[[ -x "$compiler" ]] || { printf 'KeireShaderCompiler is required before generating built-in rendering shaders.\n' >&2; exit 1; }
dependency_list="$(mktemp)"
shader_blob_global_inputs "$compiler" "$ROOT/KeireCore/Shaders" "$dependency_list" "${source_files[@]}" || {
  rm -f "$dependency_list"
  exit 1
}
fingerprint_inputs=("${BASH_SOURCE[0]}" "$cache_helper" "$blob_helper" "${include_files[@]}")
while IFS= read -r dependency; do fingerprint_inputs+=("$dependency"); done < "$dependency_list"
rm -f "$dependency_list"
fingerprint="$(generated_content_fingerprint builtin-rendering-v3 "${fingerprint_inputs[@]}")"
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
  source_identity="$(shader_blob_source_identity "$compiler" "${source_files[$source_index]}" \
    "$ROOT/KeireCore/Shaders" "$temporary")"
  for index in "${source_variants[@]}"; do
    shader_blob_compile "$blob_cache" "$compiler" "${source_files[$source_index]}" \
      "$temporary/${prefixes[$source_index]}-${files[$index]}" "${stages[$index]}" "${entries[$index]}" \
      "${destinations[$index]}" "$ROOT/KeireCore/Shaders" "$source_identity"
  done
done

{
  printf '#pragma once\n\nnamespace Keire::Detail\n{\n'
  for source_index in "${!source_files[@]}"; do
    select_source_variants "${prefixes[$source_index]}"
    for index in "${source_variants[@]}"; do
      path="$temporary/${prefixes[$source_index]}-${files[$index]}"
      printf '    inline constexpr unsigned char %s%s[] =\n' "${prefixes[$source_index]}" "${names[$index]}"
      od -An -v -t x1 "$path" | awk '{
        for (i = 1; i <= NF; ++i) {
          chunk = chunk "\\x" $i
          ++bytes
          if (bytes == 8192) {
            printf "        \"%s\"\n", chunk
            chunk = ""
            bytes = 0
          }
        }
      } END { if (bytes) printf "        \"%s\"\n", chunk }'
      printf '    ;\n'
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
  for source_name in VolumeTransport AtmosphereMedium AtmosphereSunOcclusion PlanetAtmosphereSet CloudVolumeGeometry \
    PlanetaryCloudIntersections PlanetaryCloudGeometry PlanetaryCloudWeather \
    FogExclusion FogDensity FogRayIntervals CloudVolumeLighting CloudVolumeDensity CloudWeather CloudNoiseBuffer CloudVolume PlanetVolumeWeatherUniforms PlanetVolumeIntervalGrid PlanetVolumeRayIntervals PlanetVolumeTransport VolumeSurfaceComposition CloudVolumeView; do
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
