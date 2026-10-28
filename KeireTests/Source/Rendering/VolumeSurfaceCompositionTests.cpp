#include <doctest/doctest.h>

#include "KeireInternal/Rendering/MaterialBlendingInternal.h"

#include <initializer_list>

namespace
{
#include "../../../KeireCore/Shaders/VolumeSurfaceComposition.hlsli"
}

TEST_CASE("Single-pass scene composition preserves the split passes including background alpha")
{
    for (const float backgroundOpacity : {0.0F, 0.25F, 1.0F})
    {
        for (const float cloudOpacity : {0.0F, 0.5F, 1.0F})
        {
            const float alphaAfterAttenuation = backgroundOpacity;
            const float alphaAfterRadiance = cloudOpacity + alphaAfterAttenuation * (1.0F - cloudOpacity);
            CHECK(VolumeSceneOpacity(backgroundOpacity, cloudOpacity) == alphaAfterRadiance);
            for (const float transmission : {0.0F, 0.2F, 0.9F, 1.0F})
            {
                // RGB transmission is independent of cloud opacity because atmosphere attenuation is colored.
                constexpr float backgroundRadiance = 10.0F;
                constexpr float frontRadiance = 3.0F;
                const float split = transmission * backgroundRadiance + frontRadiance;
                CHECK(VolumeStraightSurfaceSource(backgroundRadiance, frontRadiance, transmission) == split);
            }
        }
    }
}

TEST_CASE("Volume transparent composition preserves the front segment exactly once")
{
    for (const float coverage : {0.0F, 0.25F, 0.5F, 1.0F})
    {
        for (const float transmission : {0.0F, 0.2F, 1.0F})
        {
            constexpr float frontRadiance = 3.0F;
            constexpr float backRadiance = 7.0F;
            constexpr float surfaceRadiance = 11.0F;
            const float background = frontRadiance + transmission * backRadiance;
            const float source =
                VolumePremultipliedSurfaceSource(surfaceRadiance * coverage, coverage, frontRadiance, transmission);
            const float blended = source + (1.0F - coverage) * background;
            const float expected =
                frontRadiance + transmission * (coverage * surfaceRadiance + (1.0F - coverage) * backRadiance);
            CHECK(blended == doctest::Approx(expected));
        }
    }
}

TEST_CASE("Additive surfaces attenuate without adding scattering or obscuring background")
{
    CHECK(VolumeAdditiveSurfaceSource(10.0F, 0.0F) == 0.0F);
    CHECK(VolumeAdditiveSurfaceSource(10.0F, 0.25F) == 2.5F);
    CHECK(VolumeAdditiveSurfaceSource(10.0F, 1.0F) == 10.0F);
    CHECK(VolumePremultipliedSurfaceSource(0.0F, 0.0F, 100.0F, 0.2F) == 0.0F);
}

TEST_CASE("Opaque and masked particles drawn after media replace the background with transported radiance")
{
    for (const float alphaMode : {0.0F, 1.0F})
    {
        for (const float transmission : {0.0F, 0.25F, 1.0F})
        {
            constexpr float front = 2.0F;
            constexpr float surface = 9.0F;
            CHECK(VolumeMaterialSurfaceSource(surface, 1.0F, front, transmission, alphaMode) ==
                  doctest::Approx(front + transmission * surface));
        }
        // Ordinary opaque scene draws receive disabled media uniforms and must remain unchanged.
        CHECK(VolumeMaterialSurfaceSource(9.0F, 1.0F, 0.0F, 1.0F, alphaMode) == 9.0F);
    }
}

TEST_CASE("Built-in particle straight-alpha composition preserves zero coverage and opaque endpoints")
{
    for (const float coverage : {0.0F, 0.01F, 0.5F, 1.0F})
    {
        for (const float transmission : {0.0F, 0.25F, 1.0F})
        {
            constexpr float front = 2.0F;
            constexpr float surface = 9.0F;
            constexpr float behind = 5.0F;
            const float source = VolumeStraightSurfaceSource(surface, front, transmission);
            const float background = front + transmission * behind;
            const float blended = coverage * source + (1.0F - coverage) * background;
            CHECK(blended == doctest::Approx(front + transmission * (coverage * surface + (1.0F - coverage) * behind)));
        }
    }
    CHECK(VolumeStraightSurfaceSource(9.0F, 0.0F, 1.0F) == 9.0F);
}

TEST_CASE("Cloud volume radiance uses scene exposure before finite HDR storage")
{
    CHECK(VolumeExposedRadiance(10.0F, 0.0F) == 0.0F);
    CHECK(VolumeExposedRadiance(10.0F, 0.5F) == 5.0F);
    CHECK(VolumeExposedRadiance(10.0F, 2.0F) == 20.0F);
    CHECK(VolumeExposedRadiance(65504.0F, 2.0F) == 65504.0F);
    CHECK(VolumeExposedRadiance(100000.0F, 0.5F) == 50000.0F);
    // Exposure commutes with volume/surface composition when no finite-target clipping occurs.
    const float composed = VolumePremultipliedSurfaceSource(3.0F, 0.5F, 2.0F, 0.25F);
    CHECK(VolumeExposedRadiance(composed, 2.0F) ==
          VolumePremultipliedSurfaceSource(VolumeExposedRadiance(3.0F, 2.0F), 0.5F, VolumeExposedRadiance(2.0F, 2.0F),
                                           0.25F));
}

TEST_CASE("Generated material source matches straight premultiplied additive and holdout blending")
{
    constexpr float front = 3.0F;
    constexpr float transmission = 0.2F;
    constexpr float behind = 7.0F;
    constexpr float surface = 11.0F;
    const float background = front + transmission * behind;
    for (const float coverage : {0.0F, 0.25F, 0.5F, 1.0F})
    {
        const float straight = VolumeMaterialSurfaceSource(surface, coverage, front, transmission, 2.0F);
        const float premultiplied = VolumeMaterialSurfaceSource(surface, coverage, front, transmission, 5.0F);
        CHECK(coverage * straight + (1.0F - coverage) * background ==
              doctest::Approx(front + transmission * (coverage * surface + (1.0F - coverage) * behind)));
        CHECK(premultiplied == doctest::Approx(coverage * straight));
        const float additive = VolumeMaterialSurfaceSource(surface, coverage, front, transmission, 3.0F);
        CHECK(coverage * additive + background == doctest::Approx(background + coverage * transmission * surface));
        const float holdout = VolumeMaterialSurfaceSource(surface, coverage, front, transmission, 6.0F);
        CHECK(holdout + (1.0F - coverage) * background ==
              doctest::Approx(front + transmission * (1.0F - coverage) * behind));
        CHECK(VolumeMaterialSurfaceSource(surface, coverage, 0.0F, 1.0F, 2.0F) == surface);
    }
    using namespace Keire::RenderBackend;
    CHECK(MaterialBlending(Keire::MaterialAlphaMode::AlphaHoldout).SourceColor == MaterialBlendFactor::Zero);
    CHECK(MaterialBlending(Keire::MaterialAlphaMode::AlphaHoldout, true).SourceColor == MaterialBlendFactor::One);
}
