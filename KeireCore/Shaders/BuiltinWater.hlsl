#include "WaterWaves.hlsli"
#include "WaterOptics.hlsli"
#define KEIRE_CLOUD_NOISE_TEXTURE t0
#define KEIRE_CLOUD_NOISE_SAMPLER s0
#define KEIRE_PLANET_VOLUME_SET 1
#include "CloudVolumeView.hlsli"

cbuffer WaterVertexConstants : register(b0, space1)
{
    float4x4 ViewProjection;
    float4 PatchOrigin;
    float4 AxisX;
    float4 AxisY;
    float4 AxisZ;
    float4 Grid;
    float4 Footprint;
    float4 ViewDepthPlane;
    WaterWaveShaderParameters Waves;
};

cbuffer WaterShadowConstants : register(b1, space3)
{
    float4 DirectionalShadowParameters;
    float4 DirectionalCascadeSplits;
    float4x4 DirectionalShadowMatrices[4];
};

cbuffer WaterEnvironmentConstants : register(b2, space3)
{
    float4 DiffuseIrradiance[9];
    float4 EnvironmentParameters;
    float4 EnvironmentEncoding;
};

Texture2DArray<float> DirectionalShadowTexture : register(t1, space2);
SamplerState DirectionalShadowSampler : register(s1, space2);
Texture2D<float4> EnvironmentTexture : register(t2, space2);
SamplerState EnvironmentSampler : register(s2, space2);

#include "BuiltinLighting.hlsli"
#include "BuiltinDirectionalShadow.hlsli"

cbuffer WaterFragmentConstants : register(b0, space3)
{
    float4 ObserverExposure;
    float4 SunDirectionEnabled;
    float4 SunColorIntensity;
    float4 AmbientColorIntensity;
    float4 SurfaceColor;
    float4 Appearance;
    CloudVolumeView SurfaceCloudVolume;
};

cbuffer WaterDetailConstants : register(b3, space3)
{
    float4 DetailAxisX;
    float4 DetailAxisY;
    float4 DetailAxisZ;
    WaterWaveShaderParameters DetailWaves;
};

struct VertexOutput
{
    float4 Position : SV_Position;
    float3 WorldPosition : TEXCOORD0;
    float2 PatchOffset : TEXCOORD1;
    float2 FootprintPosition : TEXCOORD2;
    float ViewDepth : TEXCOORD4;
};

VertexOutput VSMain(const uint vertexId : SV_VertexID)
{
    // A shared procedural grid needs no persistent vertex/index buffer or per-body mesh uploads.
    const uint2 corners[6] = {uint2(0, 0), uint2(0, 1), uint2(1, 0),
                              uint2(1, 0), uint2(0, 1), uint2(1, 1)};
    const uint cellsX = (uint)Grid.z;
    const uint cell = vertexId / 6U;
    const uint2 coordinate = uint2(cell % cellsX, cell / cellsX) + corners[vertexId % 6U];
    const float2 offset = float2(coordinate) * Grid.xy / Grid.zw;
    const WaterWaveEvaluation wave = EvaluateWaterWaves(offset, Waves);
    const float3 localPosition = float3(offset.x, 0.0F, offset.y) + wave.Displacement;
    VertexOutput output;
    output.WorldPosition = PatchOrigin.xyz + AxisX.xyz * localPosition.x +
        AxisY.xyz * localPosition.y + AxisZ.xyz * localPosition.z;
    output.Position = mul(ViewProjection, float4(output.WorldPosition, 1.0F));
    output.ViewDepth = dot(ViewDepthPlane, float4(output.WorldPosition, 1.0F));
    output.PatchOffset = offset;
    output.FootprintPosition = Footprint.xy + offset * Footprint.zw;
    return output;
}

float4 PSMain(const VertexOutput input) : SV_Target0
{
    // Capture derivatives before discard; all lanes must participate in footprint estimation.
    const float2 pixelDx = ddx(input.PatchOffset);
    const float2 pixelDy = ddy(input.PatchOffset);
    if (Appearance.w > 0.5F && dot(input.FootprintPosition, input.FootprintPosition) > 1.0F)
    {
        discard;
    }
    const float3 toObserver = ObserverExposure.xyz - input.WorldPosition;
    const float distance = length(toObserver);
    const float fade = 1.0F - smoothstep(Appearance.z * 0.8F, Appearance.z, distance);
    if (fade <= 0.0F)
    {
        discard;
    }
    const WaterSurfaceDetail detail = EvaluateWaterSurfaceDetail(input.PatchOffset, pixelDx, pixelDy, DetailWaves);
    float3 normal = SafeNormalize(DetailAxisX.xyz * detail.Normal.x + DetailAxisY.xyz * detail.Normal.y +
        DetailAxisZ.xyz * detail.Normal.z, DetailAxisY.xyz);
    const float3 viewDirection = SafeNormalize(toObserver, normal);
    normal = dot(normal, viewDirection) < 0.0F ? -normal : normal;
    // Compression-driven visual foam; shoreline/contact foam requires scene-depth integration.
    const float foam = smoothstep(0.55F, 0.95F, detail.Compression) * Appearance.y;
    const float3 albedo = lerp(SurfaceColor.rgb, 1.0F.xxx, foam);
    const float baseRoughness = lerp(Appearance.x, 0.8F, foam);
    const float roughness = clamp(sqrt(baseRoughness * baseRoughness + detail.UnresolvedSlopeVariance), 0.04F, 1.0F);
    const float3 sunDirection = SafeNormalize(-SunDirectionEnabled.xyz, normal);
    const float shadow = SampleDirectionalShadow(input.WorldPosition, input.ViewDepth);
    const float3 sunlight = SunColorIntensity.rgb * SunColorIntensity.a * SunDirectionEnabled.w * shadow;
    const float3 direct = EvaluateDirectLighting(normal, viewDirection, sunDirection, sunlight,
                                                 albedo, 0.0F, roughness, 0.25F);
    const float3 ambient = AmbientColorIntensity.rgb * AmbientColorIntensity.a;
    const float reflectance = WaterDielectricReflectance(dot(normal, viewDirection), 1.0F, 1.333F);
    const float3 reflected = SampleEnvironment(reflect(-viewDirection, normal), roughness * EnvironmentParameters.w) *
        EnvironmentParameters.z;
    const float3 diffuse = albedo * EvaluateEnvironmentDiffuse(normal) * EnvironmentParameters.y / Pi;
    const float2 integratedBrdf = ApproximateIntegratedBrdf(saturate(dot(normal, viewDirection)), roughness);
    const float3 specular = reflected * (FresnelSchlickRoughness(dot(normal, viewDirection), 0.02F.xxx, roughness) *
        integratedBrdf.x + integratedBrdf.y);
    const float3 radiance = direct + albedo * ambient / Pi + diffuse + specular;
    const float opacity = saturate(SurfaceColor.a + (1.0F - SurfaceColor.a) * max(reflectance, foam));
    const CloudVolumeViewTransport volume = EvaluateCloudVolumeView(
        input.Position.xy * SurfaceCloudVolume.CameraControls.zw, input.Position.z, SurfaceCloudVolume, true);
    return float4(volume.Radiance + volume.Transmission * radiance * ObserverExposure.w, opacity * fade);
}
