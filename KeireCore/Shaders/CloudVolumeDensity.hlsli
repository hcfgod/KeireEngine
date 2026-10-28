#pragma once

// Zero in the reserved uniform lane means the producer predates independent erosion.
float VolumeCloudErosionAmount(float encodedErosion, float legacyDetail)
{
    return encodedErosion >= 1.0F ? saturate(encodedErosion - 1.0F) : saturate(legacyDetail);
}

// Preserve empty/full coverage and monotonic coverage authoring while varying cloud groups horizontally.
float VolumeCloudRegionalCoverage(float coverage, float weather, float strength)
{
    const float bounded = saturate(coverage);
    const float range = min(bounded, 1.0F - bounded);
    return bounded + (2.0F * saturate(weather) - 1.0F) * range * saturate(strength);
}

// The fixed 64-cubed atlas has a measured conservative displacement Jacobian norm below 0.4
// at strength 0.75 and period 64. Scale that bound with displacement amplitude and frequency.
float VolumeCloudStretchBound(float strength, float period)
{
    return 1.0F + 0.4F * (saturate(strength) / 0.75F) * (64.0F / max(period, 1.0F));
}

// Bounded displacement in atlas periods; sampled low-frequency variation keeps repeated bodies distinct.
float VolumeCloudWarpOffset(float noise)
{
    return (saturate(noise) - 0.5F) * 0.75F;
}

// Fade unresolved octaves continuously instead of sampling detail narrower than the march spacing.
// This filters added noise bands only; it is not a substitute for spatial/temporal reconstruction.
float VolumeCloudBandWeight(float sampleSpacing, float cellSize)
{
    const float footprint = max(sampleSpacing, 0.0F) / max(cellSize, 0.0001F);
    return 1.0F - smoothstep(0.25F, 0.75F, footprint);
}

// Keep the coarse erosion when detail is unresolved, avoiding a coverage jump at the LOD transition.
float VolumeCloudErosion(float coarse, float detail, float sampleSpacing, float patternSize)
{
    const float weight = VolumeCloudBandWeight(sampleSpacing, patternSize * 0.25F);
    return saturate(coarse) + (saturate(detail) - saturate(coarse)) * weight;
}

// Normalized height ramps describe a layer independently of its weather coverage and extinction.
// Degenerate ramps are steps, avoiding undefined smoothstep edges for authored flat boundaries.
float VolumeCloudProfileRamp(float startHeight, float endHeight, float height)
{
    if (endHeight <= startHeight)
    {
        return height >= endHeight ? 1.0F : 0.0F;
    }
    return smoothstep(startHeight, endHeight, height);
}

float VolumeCloudHeightProfile(float height, float baseStart, float baseEnd, float crownStart, float crownEnd)
{
    if (height <= baseStart || height >= crownEnd)
    {
        return 0.0F;
    }
    return VolumeCloudProfileRamp(baseStart, baseEnd, height) *
        (1.0F - VolumeCloudProfileRamp(crownStart, crownEnd, height));
}

// Only an all-zero producer sentinel requests the legacy profile. A nonzero authored layer can
// collapse at float precision; it must remain empty rather than expanding into the default layer.
float VolumeCloudAuthoredHeightProfile(float height, float baseStart, float baseEnd, float crownStart, float crownEnd)
{
    if (baseStart == 0.0F && baseEnd == 0.0F && crownStart == 0.0F && crownEnd == 0.0F)
    {
        return VolumeCloudHeightProfile(height, 0.0F, 0.15F, 0.7F, 1.0F);
    }
    return VolumeCloudHeightProfile(height, baseStart, baseEnd, crownStart, crownEnd);
}

float VolumeCloudDensityWithProfile(float shape, float shapeDetail, float shapeDetailWeight,
                                    float erosion, float coverage, float detailAmount, float envelope)
{
    if (coverage <= 0.0F || envelope <= 0.0F)
    {
        return 0.0F;
    }

    // The centered secondary octave breaks up broad smooth lobes without changing the base noise's mean.
    const float combinedShape = saturate(shape + 0.3F * saturate(shapeDetailWeight) * (shapeDetail - 0.5F));
    const float boundedCoverage = saturate(coverage);
    // Shape the occupied region before thresholding coverage. Fading the final density instead leaves
    // the same wide silhouette at every altitude, producing translucent slabs rather than tapered bodies.
    const float shapedNoise = combinedShape * saturate(envelope);
    const float body = saturate((shapedNoise - (1.0F - boundedCoverage)) / max(boundedCoverage, 0.0001F));
    const float erosionThreshold = saturate(erosion) * saturate(detailAmount);
    if (erosionThreshold >= 1.0F)
    {
        return 0.0F;
    }
    // Remap the surviving interval instead of thinning dense cores by subtracting erosion everywhere.
    const float erodedBody = saturate((body - erosionThreshold) / max(1.0F - erosionThreshold, 0.0001F));
    return erodedBody;
}

// Retain the original envelope for existing scenes and producers until they opt into weather profiles.
float VolumeCloudDensityFromSamples(float shape, float shapeDetail, float shapeDetailWeight,
                                    float erosion, float coverage, float detailAmount, float height)
{
    const float envelope = VolumeCloudHeightProfile(height, 0.0F, 0.15F, 0.7F, 1.0F);
    return VolumeCloudDensityWithProfile(shape, shapeDetail, shapeDetailWeight,
        erosion, coverage, detailAmount, envelope);
}
