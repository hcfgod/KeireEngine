#include "AtmosphereMedium.hlsli"

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
    if (id.x >= Quality.x || id.y >= Quality.y)
    {
        return;
    }
    const float radius = 6471.0F;
    const float distance = 10000.0F + 10000000.0F * float(id.x) / float(Quality.x);
    const float offset = radius + 1.0F + float(id.y);
    float2 interval;
    bool valid = !AtmosphereSphereInterval(float3(distance, offset, 0), float3(-1, 0, 0), radius, interval);
    valid = valid && AtmosphereSphereInterval(float3(distance, 0, 0), float3(-1, 0, 0), radius, interval);
    // Bound error by float spacing at the observer distance, not a relative planet-radius tolerance.
    const float tolerance = max(0.002F, distance * 2.0F * 1.192092896e-7F);
    valid = valid && abs(interval.x - (distance - radius)) <= tolerance;
    valid = valid && abs(interval.y - (distance + radius)) <= tolerance;
    Pixels[id.y * Quality.x + id.x] = float4(float(id.x) / float(Quality.x), float(id.y) / float(Quality.y), 0, valid ? 0.0F : 1.0F);
}
