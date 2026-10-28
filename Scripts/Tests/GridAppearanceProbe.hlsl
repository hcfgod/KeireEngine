#include "GridAppearance.hlsli"

RWStructuredBuffer<float4> Pixels : register(u0, space1);
cbuffer ProbeConstants : register(b0, space2)
{
    float4 Density;
    float4 Shape;
    uint4 Quality;
    float4 Regional;
};

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    const uint width = Quality.x;
    const uint height = Quality.y;
    if (id.x >= width || id.y >= height)
    {
        return;
    }
    const float opacity = float(id.x) / float(width - 1);
    const float fadeDistance = 10.0F + float(id.y) * 4.0F;
    const float nearValue = GridVisibility(opacity, fadeDistance, fadeDistance * 0.25F);
    const float middleValue = GridVisibility(opacity, fadeDistance, fadeDistance * 0.75F);
    const float farValue = GridVisibility(opacity, fadeDistance, fadeDistance);
    bool valid = abs(nearValue - opacity) < 1.0e-6F;
    valid = valid && abs(middleValue - opacity * 0.5F) < 1.0e-6F && abs(farValue) < 1.0e-6F;
    valid = valid && GridVisibility(opacity, fadeDistance, fadeDistance * 2.0F) == 0.0F;
    valid = valid && GridVisibility(0.0F, fadeDistance, 0.0F) == 0.0F;
    valid = valid && GridVisibility(2.0F, fadeDistance, 0.0F) == 1.0F;
    valid = valid && GridVisibility(-1.0F, fadeDistance, 0.0F) == 0.0F;
    Pixels[id.y * width + id.x] = float4(nearValue, middleValue, farValue, valid ? 0.0F : 1.0F);
}
