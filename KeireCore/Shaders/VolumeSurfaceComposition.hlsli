#pragma once

// One linear color channel. Blend this source over an already volume-composited background using
// ONE / ONE_MINUS_SRC_ALPHA. The integral stops at this surface, not at the opaque depth behind it.
// Multiplying front radiance by coverage prevents counting that scattering twice at transparent edges.
float VolumePremultipliedSurfaceSource(float surfacePremultipliedRadiance, float coverage,
                                       float frontRadiance, float frontTransmittance)
{
    return coverage * frontRadiance + frontTransmittance * surfacePremultipliedRadiance;
}

// Additive emissive surfaces do not replace any background radiance or add front scattering again.
float VolumeAdditiveSurfaceSource(float surfaceRadiance, float frontTransmittance)
{
    return frontTransmittance * surfaceRadiance;
}
