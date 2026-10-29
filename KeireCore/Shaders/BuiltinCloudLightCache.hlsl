#define KEIRE_CLOUD_NOISE_TEXTURE t0
#define KEIRE_CLOUD_NOISE_SAMPLER s0
#define KEIRE_CLOUD_NOISE_SPACE space0
#include "CloudVolumeView.hlsli"

RWStructuredBuffer<float4> LightTransmission : register(u0, space1);

cbuffer CacheConstants : register(b0, space2)
{
    CloudVolumeView VolumeView;
    float4 GridOrigin;
    float4 CellSize;
    uint4 GridDimensions;
};

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    if (any(id >= GridDimensions.xyz))
    {
        return;
    }
    const uint index = (id.z * GridDimensions.y + id.y) * GridDimensions.x + id.x;
    if (VolumeView.Quality.x == 0U)
    {
        LightTransmission[index] = 1.0F.xxxx;
        return;
    }
    const CloudVolumeParameters parameters = MakeCloudVolumeParameters(VolumeView);
    const float3 position = GridOrigin.xyz + float3(id) * CellSize.xyz;
    if (parameters.PlanetaryCloud.Controls.x != 0U)
    {
        const float altitude = PlanetaryCloudAltitude(position, parameters.PlanetaryCloud);
        const float margin = 2.0F * length(CellSize.xyz) + VolumeCloudBaseHeightVariation(parameters);
        if (altitude < parameters.BaseHeight - margin ||
            altitude > parameters.BaseHeight + parameters.Thickness + margin)
        {
            LightTransmission[index] = 1.0F.xxxx;
            return;
        }
    }
    const uint shadowSteps = clamp(parameters.ShadowSteps, 1U, 16U);
    const float depth = any(VolumeView.SunRadiance.xyz > 0.0F) ?
        VolumeCloudLightOpticalDepth(position, VolumeView.SunDirection.xyz, parameters, shadowSteps) : 0.0F;
    const float occlusion = saturate(parameters.MultipleScatteringOcclusion);
    LightTransmission[index] = float4(exp(-depth * float3(1.0F, occlusion, occlusion * occlusion)),
        VolumeCloudAmbientTransmission(position, parameters));
}
