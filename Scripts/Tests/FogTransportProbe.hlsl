#define KEIRE_CLOUD_NOISE_TEXTURE t0
#define KEIRE_CLOUD_NOISE_SAMPLER s0
#define KEIRE_CLOUD_NOISE_SPACE space0
#include "CloudVolume.hlsli"

RWStructuredBuffer<float4> Pixels : register(u0, space1);
cbuffer ProbeConstants : register(b0, space2)
{
    float4 Density;
    float4 Shape;
    uint4 Quality;
    float4 Regional;
};

float4 TraceFog(FogViewDensity fog, float distance, bool radiance)
{
    CloudVolumeParameters cloud = (CloudVolumeParameters)0;
    cloud.PatternSize = 1000.0F;
    cloud.Thickness = 100.0F;
    AtmosphereMedium air = (AtmosphereMedium)0;
    float meanDistance;
    return IntegrateCloudVolumeWithDepth(0.0F.xxx, float3(1, 0, 0), 0.0F, distance,
        float3(0, 1, 0), 0.0F.xxx, 1.0F.xxx, cloud, air, radiance, fog, meanDistance);
}

bool CheckCloudFogOverlap(float offset)
{
    CloudVolumeParameters cloud = (CloudVolumeParameters)0;
    cloud.BaseHeight = -50.0F;
    cloud.Thickness = 100.0F;
    cloud.PatternSize = 100.0F;
    cloud.Coverage = 1.0F;
    cloud.Extinction = 0.01F;
    cloud.ViewSteps = 8U;
    cloud.ShadowSteps = 1U;
    cloud.MaximumDistance = 100.0F;
    cloud.ScatteringAlbedo = 0.5F.xxx;
    cloud.HeightProfile = float4(0, 0.1F, 0.9F, 1);
    cloud.ShapeControls = float4(1, 0, 1, 1);
    FogViewDensity fog = (FogViewDensity)0;
    fog.HeightDensity = float4(0, 0, 0.01F, 100);
    fog.Scattering = float4(0.5F, 0.5F, 0.5F, 0);
    fog.Controls = uint4(1U, 8U, 0U, 0U);
    float meanDistance;
    const float3 origin = float3(offset, 0, 0);
    const float4 combined = IntegrateCloudVolumeWithDepth(origin, float3(1, 0, 0), 0, 100,
        float3(0, 1, 0), 0.0F.xxx, 1.0F.xxx, cloud, (AtmosphereMedium)0, true, fog, meanDistance);
    const float4 cloudOnly = IntegrateCloudVolumeWithDepth(origin, float3(1, 0, 0), 0, 100,
        float3(0, 1, 0), 0.0F.xxx, 1.0F.xxx, cloud, (AtmosphereMedium)0, false, meanDistance);
    // Empty enabled fog must not alter cloud quadrature or force its higher view-step budget.
    fog.HeightDensity.z = 0.0F;
    fog.Controls.y = 128U;
    cloud.RaySamplePhase = 0.37F;
    const float4 emptyFog = IntegrateCloudVolumeWithDepth(origin, float3(1, 0, 0), 0, 100,
        float3(0, 1, 0), 0.0F.xxx, 1.0F.xxx, cloud, (AtmosphereMedium)0, true, fog, meanDistance);
    const float4 shiftedCloud = IntegrateCloudVolumeWithDepth(origin, float3(1, 0, 0), 0, 100,
        float3(0, 1, 0), 0.0F.xxx, 1.0F.xxx, cloud, (AtmosphereMedium)0, false, meanDistance);
    const bool emptyFogPreservesClouds = abs(emptyFog.a - shiftedCloud.a) < 2.0e-5F;
    cloud.RaySamplePhase = 0.0F;
    fog.HeightDensity.z = 0.01F;
    fog.Controls = uint4(1U, 8U, 0U, 1U);
    fog.Exclusions[0].CenterStrength = float4(offset + 50.0F, 0, 0, 1);
    fog.Exclusions[0].HalfExtentEdge = float4(100, 100, 100, 0);
    const float4 excludedFog = IntegrateCloudVolumeWithDepth(origin, float3(1, 0, 0), 0, 100,
        float3(0, 1, 0), 0.0F.xxx, 1.0F.xxx, cloud, (AtmosphereMedium)0, true, fog, meanDistance);
    const float4 litCloudOnly = IntegrateCloudVolumeWithDepth(origin, float3(1, 0, 0), 0, 100,
        float3(0, 1, 0), 0.0F.xxx, 1.0F.xxx, cloud, (AtmosphereMedium)0, true, meanDistance);
    const bool exclusionPreservesClouds = abs(excludedFog.a - cloudOnly.a) < 2.0e-5F &&
        all(abs(excludedFog.rgb - litCloudOnly.rgb) < 2.0e-5F);
    const float expectedOpacity = 1.0F - (1.0F - cloudOnly.a) * exp(-1.0F);
    return exclusionPreservesClouds && emptyFogPreservesClouds && cloudOnly.a > 0.0F &&
        abs(combined.a - expectedOpacity) < 2.0e-5F &&
        all(abs(combined.rgb - expectedOpacity * 0.5F) < 2.0e-5F);
}

bool CheckHeightFog(uint steps)
{
    FogViewDensity fog = (FogViewDensity)0;
    fog.Controls = uint4(1U, steps, 0U, 0U);
    fog.HeightDensity = float4(0, 0.1F, 0.2F, 100);
    fog.Scattering = float4(1, 1, 1, 0);
    CloudVolumeParameters cloud = (CloudVolumeParameters)0;
    float meanDistance;
    const float expected = 1.0F - exp(-2.0F * (1.0F - exp(-10.0F)));
    const float4 ascending = IntegrateCloudVolumeWithDepth(0.0F.xxx, float3(0, 1, 0), 0, 100,
        float3(0, 1, 0), 0.0F.xxx, 1.0F.xxx, cloud, (AtmosphereMedium)0, true, fog, meanDistance);
    const float4 descending = IntegrateCloudVolumeWithDepth(float3(0, 100, 0), float3(0, -1, 0), 0, 100,
        float3(0, 1, 0), 0.0F.xxx, 1.0F.xxx, cloud, (AtmosphereMedium)0, true, fog, meanDistance);
    fog.HeightDensity.z = 0.02F;
    const float crossingExpected = 1.0F - exp(-0.02F * (10.0F + (1.0F - exp(-9.0F)) / 0.1F));
    const float4 crossing = IntegrateCloudVolumeWithDepth(float3(0, -10, 0), float3(0, 1, 0), 0, 100,
        float3(0, 1, 0), 0.0F.xxx, 1.0F.xxx, cloud, (AtmosphereMedium)0, true, fog, meanDistance);
    const float horizontal = AverageHeightFogDensity(10.0F, 10.0F, fog);
    const float nearlyHorizontal = AverageHeightFogDensity(10.0F, 10.0001F, fog);
    return abs(ascending.a - expected) < 2.0e-5F && abs(descending.a - expected) < 2.0e-5F &&
        abs(crossing.a - crossingExpected) < 2.0e-5F &&
        abs(horizontal - 0.02F * exp(-1.0F)) < 1.0e-7F && abs(horizontal - nearlyHorizontal) < 1.0e-7F;
}

bool CheckObliqueSoftBox(uint sampleIndex)
{
    FogViewDensity fog = (FogViewDensity)0;
    fog.Controls = uint4(1U, 1U, 1U, 0U);
    fog.Local[0].CenterExtinction = float4(0, 0, 0, 0.2F);
    fog.Local[0].HalfExtentEdge = float4(1, 2, 3, sampleIndex % 2U == 0U ? 0.2F : 1.0F);
    const float3 midpoint = float3(0.1F, float(sampleIndex % 31U) * 0.1F - 1.5F, 0.3F);
    const float3 span = float3(3.0F, -4.0F, 5.0F);
    const float integrated = AverageLocalFogDensity(midpoint, span, fog.Local[0]);
    float reference = 0.0F;
    for (uint index = 0U; index < 128U; ++index)
    {
        const float fraction = (float(index) + 0.5F) / 128.0F - 0.5F;
        reference += SampleLocalFogDensity(midpoint + span * fraction, fog) / 128.0F;
    }
    const float split = 0.5F * (AverageLocalFogDensity(midpoint - span * 0.25F, span * 0.5F, fog.Local[0]) +
        AverageLocalFogDensity(midpoint + span * 0.25F, span * 0.5F, fog.Local[0]));
    return abs(integrated - reference) < 1.0e-4F && abs(integrated - split) < 2.0e-6F &&
        abs(integrated - AverageLocalFogDensity(midpoint, -span, fog.Local[0])) < 2.0e-6F;
}
bool CheckObliqueExclusion(uint sampleIndex)
{
    FogViewDensity fog = (FogViewDensity)0;
    fog.Controls = uint4(1U, 1U + sampleIndex % 32U, 0U, 2U);
    fog.HeightDensity = float4(-3, 0.2F, 0.1F, 20);
    fog.Scattering = float4(1, 1, 1, 0);
    fog.Exclusions[0].CenterStrength = float4(0, 0, 0, 0.8F);
    fog.Exclusions[0].HalfExtentEdge = float4(1, 2, 3, 0.5F);
    fog.Exclusions[1] = fog.Exclusions[0];
    fog.Exclusions[1].CenterStrength = float4(0.5F, 0.5F, 0, 0.6F);
    const float3 origin = float3(-4, -3, float(sampleIndex % 21U) * 0.2F - 2.0F);
    const float3 direction = normalize(float3(1, 0.7F, 0.2F));
    const float distance = 10.0F;
    float referenceDepth = 0.0F;
    for (uint index = 0U; index < 512U; ++index)
    {
        referenceDepth += SampleFogDensity(origin + direction * ((float(index) + 0.5F) * distance / 512.0F), fog) *
            distance / 512.0F;
    }
    float meanDistance;
    const float4 result = IntegrateCloudVolumeWithDepth(origin, direction, 0, distance,
        float3(0, 1, 0), 0.0F.xxx, 1.0F.xxx, (CloudVolumeParameters)0, (AtmosphereMedium)0, true, fog, meanDistance);
    return abs(result.a - (1.0F - exp(-referenceDepth))) < 2.0e-4F;
}

bool CheckExclusionTransport(uint sampleIndex)
{
    FogViewDensity fog = (FogViewDensity)0;
    fog.Controls = uint4(1U, 1U, 0U, 1U);
    fog.HeightDensity = float4(0, 0, 0.01F, 100);
    fog.Scattering = float4(1, 1, 1, 0);
    fog.Exclusions[0].CenterStrength = float4(13.0F + float(sampleIndex % 20U), 0, 0, 1);
    fog.Exclusions[0].HalfExtentEdge = float4(0.25F, 2, 2, 0);
    const float hardExpected = 1.0F - exp(-0.01F * 99.5F);
    bool valid = abs(TraceFog(fog, 100, true).a - hardExpected) < 2.0e-5F;
    fog.Exclusions[0].HalfExtentEdge.w = 0.2F;
    const float softExpected = 1.0F - exp(-0.01F * 99.55F);
    valid = valid && abs(TraceFog(fog, 100, true).a - softExpected) < 2.0e-5F;
    fog.Exclusions[1] = fog.Exclusions[0];
    fog.Controls.w = 2U;
    valid = valid && abs(TraceFog(fog, 100, true).a - softExpected) < 2.0e-5F;
    fog.Exclusions[0].CenterStrength.w = 0.5F;
    fog.Exclusions[1].CenterStrength.w = 0.5F;
    const float partialExpected = 1.0F - exp(-0.01F * 99.775F);
    return valid && abs(TraceFog(fog, 100, true).a - partialExpected) < 2.0e-5F;
}

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= Quality.x || id.y >= Quality.y)
    {
        return;
    }
    FogViewDensity fog = (FogViewDensity)0;
    fog.Controls = uint4(1U, 1U + id.y % 128U, 0U, 0U);
    fog.HeightDensity = float4(0, 0, 0.02F, 50);
    fog.Scattering = float4(0.5F, 0.75F, 1.0F, 0);
    const float distance = 1.0F + float(id.x) * 0.2F;
    const float opacity = 1.0F - exp(-0.02F * min(distance, 50.0F));
    float4 value = TraceFog(fog, distance, true);
    bool valid = abs(value.a - opacity) < 2.0e-5F;
    valid = valid && all(abs(value.rgb - opacity * fog.Scattering.xyz) < 2.0e-5F);


    valid = valid && abs(TraceFog(fog, distance, false).a - value.a) < 2.0e-5F;

    // A half-metre box must survive even a single nominal kilometre-long sample.
    fog.Controls = uint4(1U, 1U, 2U, 0U);
    fog.HeightDensity = float4(0, 0, 0, 1000);
    fog.Local[0].CenterExtinction = float4(13.0F + float(id.x) * 0.1F, 0, 0, 0.2F);
    fog.Local[0].HalfExtentEdge = float4(0.25F, 2, 2, 0);
    fog.Local[1] = fog.Local[0];
    value = TraceFog(fog, 1000, true);
    const float thinOpacity = 1.0F - exp(-0.2F);
    valid = valid && abs(value.a - thinOpacity) < 2.0e-5F;
    valid = valid && all(abs(value.rgb - thinOpacity * fog.Scattering.xyz) < 2.0e-5F);
    valid = valid && TraceFog(fog, 5, true).a == 0.0F;
    // A complete soft-edged box has less integrated density than its solid core.
    // Along its central axis each smoothstep ramp averages exactly one half.
    fog.Local[0].HalfExtentEdge.w = 0.2F;
    fog.Local[1].HalfExtentEdge.w = 0.2F;
    const float softOpacity = 1.0F - exp(-0.2F * (1.0F - 0.2F * 0.5F));
    valid = valid && abs(TraceFog(fog, 1000, true).a - softOpacity) < 2.0e-5F;
    fog.Controls.x = 0U;
    valid = valid && all(TraceFog(fog, 1000, true) == 0.0F);
    valid = valid && CheckCloudFogOverlap(float(id.x) * 0.1F);
    valid = valid && CheckHeightFog(1U + id.y % 128U);
    valid = valid && CheckObliqueSoftBox(id.x);
    valid = valid && CheckExclusionTransport(id.x);
    valid = valid && CheckObliqueExclusion(id.x);
    Pixels[id.y * Quality.x + id.x] = float4(value.rgb, valid ? 0.0F : 1.0F);
}
