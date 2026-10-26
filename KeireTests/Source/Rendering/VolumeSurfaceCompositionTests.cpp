#include <doctest/doctest.h>

#include <initializer_list>

namespace
{
#include "../../../KeireCore/Shaders/VolumeSurfaceComposition.hlsli"
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
