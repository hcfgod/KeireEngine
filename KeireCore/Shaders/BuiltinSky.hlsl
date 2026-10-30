#include "AtmosphereGround.hlsli"
#include "PlanetAtmosphereSet.hlsli"
#include "CloudLayered.hlsl"
#include "SkyColorGradient.hlsli"

struct VertexOutput
{
    float2 UV : TEXCOORD0;
    float4 Position : SV_Position;
};

cbuffer SkyConstants : register(b0, space3)
{
    float4x4 InverseProjection;
    float4x4 InverseView;
    float4 SkyParameters;
    float4 AtmosphereObserverRadius;
    float4 AtmosphereSunHeight;
    float4 AtmosphereRayleighScale;
    float4 AtmosphereMie;
    float4 AtmosphereAbsorptionPeak;
    float4 AtmosphereSolarWidth;
    float4 AtmosphereControls;
    float4 CloudGeometry0;
    float4 CloudPattern0;
    float4 CloudColor0;
    uint4 CloudNoise0;
    float4 CloudGeometry1;
    float4 CloudPattern1;
    float4 CloudColor1;
    uint4 CloudNoise1;
    float4 CloudAmbientLight;
    float4 CloudSunLight;
    float4 CloudSunDirection;
    float4 StarAppearance;
    float4 StarTintRotation;
    float4 StarTwinkleFade;
    uint4 StarIdentity;
    float4 SkyHorizonWeight;
    float4 SkyZenithExponent;
    float4 GroundReflectance;
    PlanetaryCloudGeometry PlanetaryCloud;
    float4 PlanetaryCloudPhase[2];
    float4 StarOrientation;
    PlanetAtmosphereSet PlanetAtmospheres;
};

Texture2D EnvironmentTexture : register(t0, space2);
SamplerState EnvironmentSampler : register(s0, space2);

VertexOutput VSMain(uint vertexId : SV_VertexID)
{
    VertexOutput output;
    output.UV = float2((vertexId << 1) & 2, vertexId & 2);
    output.Position = float4(output.UV * float2(2.0F, -2.0F) + float2(-1.0F, 1.0F), 0.0F, 1.0F);
    return output;
}

float3 DecodeRgbe(float4 sampleValue)
{
    const float exponent = sampleValue.a * 255.0F - 136.0F;
    return sampleValue.rgb * (255.0F * exp2(exponent));
}

float3 AcesFitted(float3 value)
{
    const float3 numerator = value * (2.51F * value + 0.03F);
    const float3 denominator = value * (2.43F * value + 0.59F) + 0.14F;
    return saturate(numerator / denominator);
}

float2 CubemapAtlasUv(float3 direction, int layout)
{
    const float3 absoluteDirection = abs(direction);
    int face = 0;
    float2 local;
    if (absoluteDirection.x >= absoluteDirection.y && absoluteDirection.x >= absoluteDirection.z)
    {
        const float inverse = 1.0F / absoluteDirection.x;
        if (direction.x >= 0.0F)
        {
            face = 0;
            local = float2(-direction.z, -direction.y) * inverse;
        }
        else
        {
            face = 1;
            local = float2(direction.z, -direction.y) * inverse;
        }
    }
    else if (absoluteDirection.y >= absoluteDirection.z)
    {
        const float inverse = 1.0F / absoluteDirection.y;
        if (direction.y >= 0.0F)
        {
            face = 2;
            local = float2(direction.x, direction.z) * inverse;
        }
        else
        {
            face = 3;
            local = float2(direction.x, -direction.z) * inverse;
        }
    }
    else
    {
        const float inverse = 1.0F / absoluteDirection.z;
        if (direction.z >= 0.0F)
        {
            face = 4;
            local = float2(direction.x, -direction.y) * inverse;
        }
        else
        {
            face = 5;
            local = float2(-direction.x, -direction.y) * inverse;
        }
    }
    local = local * 0.5F + 0.5F;
    uint textureWidth;
    uint textureHeight;
    EnvironmentTexture.GetDimensions(textureWidth, textureHeight);
    const float2 grid = layout == 4   ? float2(6.0F, 1.0F)
                        : layout == 5 ? float2(1.0F, 6.0F)
                        : layout == 2 ? float2(4.0F, 3.0F)
                                      : float2(3.0F, 4.0F);
    const float2 cellPixels = max(float2(textureWidth, textureHeight) / grid, 1.0F);
    const float2 inset = 0.5F / cellPixels;
    local = clamp(local, inset, 1.0F - inset);
    if (layout == 4)
        return float2((face + local.x) / 6.0F, local.y);
    if (layout == 5)
        return float2(local.x, (face + local.y) / 6.0F);

    const int2 horizontalCells[6] = {int2(2, 1), int2(0, 1), int2(1, 0), int2(1, 2), int2(1, 1), int2(3, 1)};
    const int2 verticalCells[6] = {int2(2, 1), int2(0, 1), int2(1, 0), int2(1, 2), int2(1, 1), int2(1, 3)};
    if (layout == 2)
        return (float2(horizontalCells[face]) + local) / float2(4.0F, 3.0F);
    return (float2(verticalCells[face]) + local) / float2(3.0F, 4.0F);
}

// All lengths below are kilometres and all scattering/extinction coefficients are inverse kilometres.
// This bounded single-scattering reference path deliberately does not approximate multiple scattering or aerial
// perspective.
AtmosphereMedium CurrentAtmosphereMedium()
{
    AtmosphereMedium medium;
    medium.ObserverRadius = AtmosphereObserverRadius;
    medium.SunHeight = AtmosphereSunHeight;
    medium.RayleighScale = AtmosphereRayleighScale;
    medium.Mie = AtmosphereMie;
    medium.AbsorptionPeak = AtmosphereAbsorptionPeak;
    medium.SolarWidth = AtmosphereSolarWidth;
    medium.Controls = AtmosphereControls;
    return medium;
}

bool AtmosphereGroundIntersection(float3 origin, float3 direction, out float2 interval)
{
    return AtmosphereMediumGroundIntersection(origin, direction, CurrentAtmosphereMedium(), interval);
}

float AtmosphereSunCoverage(float3 direction, float angularRadius)
{
    // Chord distances retain tiny angular radii for which float cos(radius) rounds to exactly one.
    const float chordRadius = 2.0F * sin(angularRadius * 0.5F);
    const float chordDistance = length(direction - AtmosphereSunHeight.xyz);
    const float halfPixel = max(0.5F * fwidth(chordDistance), 1.0e-8F);
    const float filteredRadius = max(chordRadius, halfPixel);
    const float areaRatio = chordRadius / filteredRadius;
    // Unresolved discs keep their approximate energy instead of becoming a full-brightness one-pixel point.
    return (1.0F - smoothstep(filteredRadius - halfPixel, filteredRadius + halfPixel, chordDistance)) * areaRatio *
           areaRatio;
}

float3 AtmosphereSunTransmission(float3 position)
{
    return AtmosphereMediumSunTransmission(position, CurrentAtmosphereMedium());
}

float3 AtmosphereRadiance(float3 direction, out float3 backgroundTransmission, out float3 sunRadiance)
{
    if (PlanetAtmospheres.Controls.x > 1U)
    {
        uint ground;
        float endDistance;
        const AtmosphereTransport air = IntegratePlanetAtmosphereSet(0.0F, direction, 3.0e30F,
            PlanetAtmospheres, true, true, ground, endDistance);
        sunRadiance = 0.0F;
        backgroundTransmission = ground == KEIRE_MAX_PLANET_ATMOSPHERES ? air.Transmission : 0.0F;
        float3 radiance = air.Radiance;
        if (ground < KEIRE_MAX_PLANET_ATMOSPHERES)
        {
            const AtmosphereMedium medium = PlanetAtmospheres.Media[ground];
            const float3 groundOffset = direction * endDistance;
            const float3 normal = normalize(medium.ObserverRadius.xyz + groundOffset);
            const float3 direct = medium.SolarWidth.xyz * saturate(dot(normal, medium.SunHeight.xyz)) *
                PlanetAtmosphereSunTransmission(groundOffset + normal * 0.001F, PlanetAtmospheres);
            const float3 boundary = GroundReflectance.xyz * GroundReflectance.w *
                (direct * 0.31830988618F + CloudAmbientLight.xyz);
            return radiance + air.Transmission * boundary;
        }
        const float coverage = AtmosphereSunCoverage(direction, AtmosphereControls.w);
        if (coverage > 0.0F)
        {
            const float sineHalfRadius = sin(AtmosphereControls.w * 0.5F);
            sunRadiance = air.Transmission * AtmosphereSolarWidth.xyz *
                (coverage / (12.5663706144F * sineHalfRadius * sineHalfRadius));
            radiance += sunRadiance;
        }
        return radiance;
    }
    sunRadiance = 0.0F;
    backgroundTransmission = 0.0F;
    const float pi = 3.14159265359F;
    const float3 observer = AtmosphereObserverRadius.xyz;
    const float planetRadius = AtmosphereObserverRadius.w;
    // Inside an opaque planet there is no visible sky. Surface observers can see the upward hemisphere.
    if (length(observer) < planetRadius)
    {
        return 0.0F;
    }
    float2 atmosphereInterval;
    const bool intersectsAtmosphere =
        AtmosphereSphereInterval(observer, direction, planetRadius + AtmosphereSunHeight.w, atmosphereInterval);
    float2 groundInterval;
    const bool groundHit = AtmosphereGroundIntersection(observer, direction, groundInterval);
    if (groundHit)
    {
        // Ambient radiance is the same authored environment used by scene/cloud lighting.
        // Convert uniform hemispherical radiance to irradiance; a sky-irradiance LUT is separate work.
        return AtmosphereGroundRadiance(observer, direction, groundInterval.x, CurrentAtmosphereMedium(),
                                        GroundReflectance.xyz * GroundReflectance.w, CloudAmbientLight.xyz * pi);
    }
    const float maximumDistance = intersectsAtmosphere ? max(atmosphereInterval.y, 0.0F) : 0.0F;
    const AtmosphereTransport transport =
        IntegrateAtmosphereMedium(observer, direction, maximumDistance, CurrentAtmosphereMedium());
    float3 radiance = transport.Radiance;
    const float sunCoverage = AtmosphereSunCoverage(direction, AtmosphereControls.w);
    if (!groundHit && sunCoverage > 0.0F)
    {
        const float sineHalfRadius = sin(AtmosphereControls.w * 0.5F);
        const float sunSolidAngle = 4.0F * pi * sineHalfRadius * sineHalfRadius;
        sunRadiance = transport.Transmission * (sunCoverage / sunSolidAngle) * AtmosphereSolarWidth.xyz;
        radiance += sunRadiance;
    }
    backgroundTransmission = groundHit ? 0.0F : transport.Transmission;
    return radiance;
}

uint StarHash(uint value)
{
    value ^= value >> 16U;
    value *= 0x7feb352dU;
    value ^= value >> 15U;
    value *= 0x846ca68bU;
    return value ^ (value >> 16U);
}

float StarRandom(uint value) { return (StarHash(value) >> 8U) * (1.0F / 16777216.0F); }

float3 StarRadiance(float3 direction)
{
    if (StarIdentity.y == 0U || StarAppearance.x <= 0.0F || StarAppearance.y <= 0.0F)
    {
        return 0.0F;
    }
    const float3 up = AtmosphereControls.x > 0.5F
                          ? AtmosphereObserverRadius.xyz / max(length(AtmosphereObserverRadius.xyz), 1.0e-6F)
                          : float3(0, 1, 0);
    const float solarElevation = degrees(asin(clamp(dot(AtmosphereSunHeight.xyz, up), -1.0F, 1.0F)));
    const float nightFraction =
        saturate((StarTwinkleFade.z - solarElevation) / max(StarTwinkleFade.z - StarTwinkleFade.w, 1.0e-4F));
    const float atmosphereHeight = max(AtmosphereSunHeight.w, 1.0F);
    const float spaceVisibility = AtmosphereControls.x > 0.5F
        ? smoothstep(AtmosphereObserverRadius.w + atmosphereHeight * 0.8F,
                     AtmosphereObserverRadius.w + atmosphereHeight, length(AtmosphereObserverRadius.xyz))
        : 0.0F;
    const float nightVisibility = max(
        nightFraction * nightFraction * (3.0F - 2.0F * nightFraction), spaceVisibility);
    if (nightVisibility <= 0.0F)
    {
        return 0.0F;
    }
    direction += 2.0 * cross(StarOrientation.xyz,
                             cross(StarOrientation.xyz, direction) + StarOrientation.w * direction);
    const float sineRotation = sin(StarTintRotation.w);
    const float cosineRotation = cos(StarTintRotation.w);
    direction = float3(direction.x * cosineRotation - direction.z * sineRotation, direction.y,
                       direction.x * sineRotation + direction.z * cosineRotation);
    // A thin spherical shell of seeded Cartesian cells avoids longitude seams and polar clustering.
    const float shellRadius = 64.0F;
    const int3 cell = (int3)floor(direction * shellRadius);
    const float halfPixel = max(0.5F * max(length(ddx(direction)), length(ddy(direction))), 1.0e-7F);
    const float radius = 2.0F * sin(StarAppearance.z * 0.5F);
    const float filteredRadius = max(radius, halfPixel);
    const float energyScale = (radius * radius) / (filteredRadius * filteredRadius);
    const int neighborhood = (filteredRadius + halfPixel) * shellRadius > 0.5F ? 2 : 1;
    float3 radiance = 0.0F;
    [loop] for (int z = -neighborhood; z <= neighborhood; ++z)
    {
        [loop] for (int y = -neighborhood; y <= neighborhood; ++y)
        {
            [loop] for (int x = -neighborhood; x <= neighborhood; ++x)
            {
                const int3 candidateCell = cell + int3(x, y, z);
                const uint3 bits = asuint(candidateCell);
                const uint seed = StarHash(bits.x ^ StarHash(bits.y ^ StarHash(bits.z ^ StarIdentity.x)));
                if (StarRandom(seed) >= StarAppearance.x)
                {
                    continue;
                }
                const float3 position =
                    float3(candidateCell) + float3(StarRandom(seed + 1U), StarRandom(seed + 2U), StarRandom(seed + 3U));
                if (abs(length(position) - shellRadius) > 0.5F)
                {
                    continue;
                }
                const float distance = length(direction - normalize(position));
                const float coverage =
                    1.0F - smoothstep(filteredRadius - halfPixel, filteredRadius + halfPixel, distance);
                const float warmth = StarRandom(seed + 4U);
                const float3 color = lerp(float3(0.65F, 0.8F, 1.0F), float3(1.0F, 0.7F, 0.45F), warmth);
                const float twinkle =
                    1.0F + StarTwinkleFade.x * sin(6.28318530718F * (StarTwinkleFade.y + StarRandom(seed + 5U)));
                radiance += lerp(1.0F, color, StarAppearance.w) * coverage * energyScale * twinkle;
            }
        }
    }
    // Fade before the pixel footprint outgrows the bounded neighborhood, rather than introduce unstable holes.
    const float footprintVisibility = 1.0F - smoothstep(0.003F, 0.006F, halfPixel);
    return radiance * StarTintRotation.xyz * StarAppearance.y * nightVisibility * footprintVisibility;
}

float3 CloudSunlightAtSheet(float3 direction, float4 geometry, uint4 noise)
{
    if (noise.x != 1U || geometry.w <= 0.0F || geometry.z <= 0.0F || all(CloudSunLight.xyz <= 0.0F) ||
        abs(direction.y) < 0.001F)
    {
        return 0.0F;
    }
    const float distanceToSheet = geometry.x / direction.y;
    if (distanceToSheet <= 0.0F || distanceToSheet >= 100000.0F)
    {
        return 0.0F;
    }
    if (AtmosphereControls.x > 0.5F)
    {
        const float3 position = AtmosphereObserverRadius.xyz + direction * (distanceToSheet * 0.001F);
        return CloudSunLight.xyz * AtmosphereSunTransmission(position);
    }
    // Without a planet model, the existing cloud frame's local horizon defines daylight.
    return CloudSunDirection.y > 0.0F ? CloudSunLight.xyz : 0.0F;
}

float4 PSMain(VertexOutput input) : SV_Target0
{
    const float2 clip = input.UV * float2(2.0F, -2.0F) + float2(-1.0F, 1.0F);
    const float4 viewPosition = mul(InverseProjection, float4(clip, 1.0F, 1.0F));
    float3 direction = normalize(mul((float3x3)InverseView, viewPosition.xyz / max(abs(viewPosition.w), 1.0e-6F)));
    float3 radiance = 0.0F;
    float3 backgroundTransmission = 1.0F;
    float3 sunRadiance = 0.0F;
    if (AtmosphereControls.x > 0.5F)
    {
        radiance = AtmosphereRadiance(direction, backgroundTransmission, sunRadiance);
    }
    else if (SkyParameters.w >= 0.0F)
    {
        const float rotation = radians(SkyParameters.x);
        const float sineRotation = sin(rotation);
        const float cosineRotation = cos(rotation);
        const float3 environmentDirection =
            float3(direction.x * cosineRotation - direction.z * sineRotation, direction.y,
                   direction.x * sineRotation + direction.z * cosineRotation);
        const int layout = ((int)SkyParameters.w) & 15;
        const float2 uv = layout <= 1
                              ? float2(0.5F + atan2(environmentDirection.x, environmentDirection.z) / 6.28318530718F,
                                       0.5F - asin(clamp(environmentDirection.y, -1.0F, 1.0F)) / 3.14159265359F)
                              : CubemapAtlasUv(environmentDirection, layout);
        const float4 sampleValue = EnvironmentTexture.SampleLevel(EnvironmentSampler, uv, 0.0F);
        radiance = (SkyParameters.w >= 16.0F ? DecodeRgbe(sampleValue) : sampleValue.rgb) * SkyParameters.y;
    }
    if (SkyHorizonWeight.w > 0.0F)
    {
        float3 up = float3(0.0F, 1.0F, 0.0F);
        bool visibleSky = true;
        float gradientWeight = SkyHorizonWeight.w;
        if (AtmosphereControls.x > 0.5F)
        {
            const float observerRadius = length(AtmosphereObserverRadius.xyz);
            gradientWeight *=
                SkyGradientAtmosphereWeight(observerRadius, AtmosphereObserverRadius.w, AtmosphereSunHeight.w);
            up = AtmosphereObserverRadius.xyz / max(observerRadius, 1.0e-6F);
            float2 groundInterval;
            visibleSky = observerRadius >= AtmosphereObserverRadius.w &&
                         !AtmosphereGroundIntersection(AtmosphereObserverRadius.xyz, direction, groundInterval);
        }
        if (visibleSky && gradientWeight > 0.0F)
        {
            // Override only the background, retaining the solar disc and later stars/clouds.
            const float3 background = max(radiance - sunRadiance, 0.0F);
            const float cosineZenith = dot(direction, up);
            radiance = float3(BlendSkyColorChannel(background.r, SkyHorizonWeight.r, SkyZenithExponent.r, cosineZenith,
                                                   gradientWeight, SkyZenithExponent.w),
                              BlendSkyColorChannel(background.g, SkyHorizonWeight.g, SkyZenithExponent.g, cosineZenith,
                                                   gradientWeight, SkyZenithExponent.w),
                              BlendSkyColorChannel(background.b, SkyHorizonWeight.b, SkyZenithExponent.b, cosineZenith,
                                                   gradientWeight, SkyZenithExponent.w)) +
                       sunRadiance;
        }
    }
    radiance += StarRadiance(direction) * backgroundTransmission;
    if (PlanetaryCloud.Controls.x != 0U)
    {
        PlanetaryCloudLayer layer0;
        layer0.Geometry = CloudGeometry0;
        layer0.Pattern = CloudPattern0;
        layer0.Color = CloudColor0;
        layer0.Noise = CloudNoise0;
        layer0.Phase = PlanetaryCloudPhase[0];
        PlanetaryCloudLayer layer1;
        layer1.Geometry = CloudGeometry1;
        layer1.Pattern = CloudPattern1;
        layer1.Color = CloudColor1;
        layer1.Noise = CloudNoise1;
        layer1.Phase = PlanetaryCloudPhase[1];
        const float4 cloud =
            EvaluatePlanetaryCloudLayers(direction, layer0, layer1, CloudAmbientLight.xyz, CloudSunLight.xyz,
                                         CloudSunDirection.xyz, PlanetaryCloud, CurrentAtmosphereMedium());
        radiance = cloud.rgb + (1.0F - cloud.a) * radiance;
    }
    else
    {
        float4 cloud0 = EvaluateCloudLayer(
            direction, CloudGeometry0, CloudPattern0, CloudColor0, CloudNoise0, CloudAmbientLight.xyz,
            CloudSunlightAtSheet(direction, CloudGeometry0, CloudNoise0), CloudSunDirection.xyz);
        float4 cloud1 = EvaluateCloudLayer(
            direction, CloudGeometry1, CloudPattern1, CloudColor1, CloudNoise1, CloudAmbientLight.xyz,
            CloudSunlightAtSheet(direction, CloudGeometry1, CloudNoise1), CloudSunDirection.xyz);
        if (abs(direction.y) >= 0.001F)
        {
            const float distance0 = CloudGeometry0.x / direction.y;
            const float distance1 = CloudGeometry1.x / direction.y;
            if (AtmosphereControls.x > 0.5F)
            {
                float2 groundInterval;
                if (length(AtmosphereObserverRadius.xyz) < AtmosphereObserverRadius.w)
                {
                    cloud0 = 0.0F;
                    cloud1 = 0.0F;
                }
                else if (AtmosphereGroundIntersection(AtmosphereObserverRadius.xyz, direction, groundInterval))
                {
                    // Atmosphere lengths are kilometres; the cloud sheet contract uses metres.
                    cloud0 *= distance0 < groundInterval.x * 1000.0F ? 1.0F : 0.0F;
                    cloud1 *= distance1 < groundInterval.x * 1000.0F ? 1.0F : 0.0F;
                }
            }
            const float4 farCloud = distance0 >= distance1 ? cloud0 : cloud1;
            const float4 nearCloud = distance0 >= distance1 ? cloud1 : cloud0;
            radiance = farCloud.rgb + radiance * (1.0F - farCloud.a);
            radiance = nearCloud.rgb + radiance * (1.0F - nearCloud.a);
        }
    }
    // Keep the atmosphere's finite RGBA16F storage contract after cloud composition and exposure.
    const float3 exposed = radiance * SkyParameters.z;
    const bool finiteSkyRequired = AtmosphereControls.x > 0.5F || CloudGeometry0.w > 0.0F || CloudGeometry1.w > 0.0F ||
                                   StarIdentity.y != 0U || SkyHorizonWeight.w > 0.0F;
    return float4(finiteSkyRequired ? min(exposed, 65504.0F) : exposed, 1.0F);
}
