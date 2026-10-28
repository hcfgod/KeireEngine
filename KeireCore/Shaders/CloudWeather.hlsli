#pragma once

// Weather is independent of the repeating 3D shape/erosion atlas. The host wraps camera/wind
// coordinates every 256 pattern units, so every octave must wrap its lattice to that domain.
uint VolumeCloudWeatherHash(int x, int z, int period, uint seed)
{
    // Validated region sizes and octave frequencies make period a positive power of two.
    // Unsigned masking preserves Euclidean wrapping for negative coordinates without GPU integer division.
    const uint mask = uint(period) - 1U;
    const uint wrappedX = uint(x) & mask;
    const uint wrappedZ = uint(z) & mask;
    uint value = wrappedX * 0x8da6b343U ^ wrappedZ * 0xcb1ab31fU ^ seed;
    value ^= value >> 16U;
    value *= 0x7feb352dU;
    value ^= value >> 15U;
    value *= 0x846ca68bU;
    return value ^ (value >> 16U);
}

float VolumeCloudWeatherCorner(int x, int z, int period, uint seed)
{
    return float(VolumeCloudWeatherHash(x, z, period, seed) >> 8U) / 16777216.0F;
}

float VolumeCloudWeatherLattice(float x, float z, int period, uint seed)
{
    const int cellX = int(floor(x));
    const int cellZ = int(floor(z));
    const float fractionX = x - float(cellX);
    const float fractionZ = z - float(cellZ);
    // Quintic interpolation has zero first and second derivatives at cell boundaries.
    const float blendX = fractionX * fractionX * fractionX *
        (fractionX * (fractionX * 6.0F - 15.0F) + 10.0F);
    const float blendZ = fractionZ * fractionZ * fractionZ *
        (fractionZ * (fractionZ * 6.0F - 15.0F) + 10.0F);
    const float bottomLeft = VolumeCloudWeatherCorner(cellX, cellZ, period, seed);
    const float bottomRight = VolumeCloudWeatherCorner(cellX + 1, cellZ, period, seed);
    const float topLeft = VolumeCloudWeatherCorner(cellX, cellZ + 1, period, seed);
    const float topRight = VolumeCloudWeatherCorner(cellX + 1, cellZ + 1, period, seed);
    const float bottom = bottomLeft + (bottomRight - bottomLeft) * blendX;
    const float top = topLeft + (topRight - topLeft) * blendX;
    return bottom + (top - bottom) * blendZ;
}

// Coordinates, spacing and region size are in pattern units. Valid region sizes are powers of two
// from 1 to 64. Fade unresolved octaves toward their mean rather than changing weather coverage.
float VolumeCloudWeatherField(float x, float z, float spacing, float regionSize, uint seed)
{
    const float boundedRegionSize = clamp(regionSize, 1.0F, 64.0F);
    float result = 0.5F;
    for (int octave = 0; octave < 3; ++octave)
    {
        const float frequency = float(1U << uint(octave));
        const float cellSize = boundedRegionSize / frequency;
        const int period = int(256.0F / cellSize);
        const float resolved = 1.0F - smoothstep(0.25F, 0.75F, max(spacing, 0.0F) / cellSize);
        if (resolved <= 0.0F)
        {
            // Finer octaves have an even larger footprint and contribute their mean (zero deviation).
            break;
        }
        const float weight = octave == 0 ? 0.625F : octave == 1 ? 0.25F : 0.125F;
        const float sampleValue = VolumeCloudWeatherLattice(x / cellSize, z / cellSize,
            period, seed + uint(octave) * 0x9e3779b9U);
        result += weight * resolved * (sampleValue - 0.5F);
    }
    return saturate(result);
}

// A separate weather channel moves the local layer without changing density or cloud coverage.
// Geometry must not change with view/shadow sample spacing, so evaluate this field without LOD fading.
float VolumeCloudWeatherBaseOffset(float x, float z, float regionSize, uint seed, float amplitude)
{
    if (amplitude <= 0.0F)
    {
        return 0.0F;
    }
    const float weather = VolumeCloudWeatherField(x, z, 0.0F, regionSize, seed ^ 0x63d83595U);
    return (2.0F * weather - 1.0F) * amplitude;
}

// Move a smooth weather threshold across the field as coverage increases. Unlike a bounded
// perturbation, full regional strength can designate genuinely empty and fully cloudy regions.
float VolumeCloudWeatherCoverage(float coverage, float weather, float strength)
{
    const float boundedCoverage = saturate(coverage);
    const float transitionWidth = 0.2F;
    const float lower = 1.0F - boundedCoverage * (1.0F + transitionWidth);
    const float regional = smoothstep(lower, lower + transitionWidth, saturate(weather));
    return boundedCoverage + (regional - boundedCoverage) * saturate(strength);
}
