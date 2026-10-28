#pragma once

// Radiance and exposure are nonnegative. Clamp after exposure to the finite RGBA16F target range.
float VolumeExposedRadiance(float radiance, float exposure)
{
    const float exposed = radiance * exposure;
    return exposed < 65504.0F ? exposed : 65504.0F;
}

// One linear color channel. Blend this source over an already volume-composited background using
// ONE / ONE_MINUS_SRC_ALPHA. The integral stops at this surface, not at the opaque depth behind it.
// Multiplying front radiance by coverage prevents counting that scattering twice at transparent edges.
float VolumePremultipliedSurfaceSource(float surfacePremultipliedRadiance, float coverage,
                                       float frontRadiance, float frontTransmittance)
{
    return coverage * frontRadiance + frontTransmittance * surfacePremultipliedRadiance;
}

// SRC_ALPHA blending applies coverage after evaluating radiance at the particle or surface depth.
float VolumeStraightSurfaceSource(float surfaceRadiance, float frontRadiance, float frontTransmittance)
{
    return frontRadiance + frontTransmittance * surfaceRadiance;
}

// Matches the separate attenuation (preserve alpha) and radiance (ONE / ONE_MINUS_SRC_ALPHA) passes.
float VolumeSceneOpacity(float backgroundOpacity, float cloudOpacity)
{
    return cloudOpacity + (1.0F - cloudOpacity) * backgroundOpacity;
}

// Additive emissive surfaces do not replace any background radiance or add front scattering again.
float VolumeAdditiveSurfaceSource(float surfaceRadiance, float frontTransmittance)
{
    return frontTransmittance * surfaceRadiance;
}

// MaterialAlphaMode values: opaque 0, mask 1, blend 2, additive 3, modulate 4, composite 5, holdout 6.
float VolumeMaterialSurfaceSource(float radiance, float coverage, float frontRadiance,
                                 float frontTransmittance, float alphaMode)
{
    if (alphaMode < 1.5F)
        return VolumeStraightSurfaceSource(radiance, frontRadiance, frontTransmittance);
    if (alphaMode > 3.5F && alphaMode < 4.5F)
        return radiance;
    if (alphaMode > 2.5F && alphaMode < 3.5F)
        return VolumeAdditiveSurfaceSource(radiance, frontTransmittance);
    if (alphaMode > 5.5F)
        return coverage * frontRadiance;
    if (alphaMode > 4.5F)
        return VolumePremultipliedSurfaceSource(radiance * coverage, coverage, frontRadiance, frontTransmittance);
    return VolumeStraightSurfaceSource(radiance, frontRadiance, frontTransmittance);
}
