#include "KeireInternal/Rendering/VolumeShadowUniforms.h"

#include <doctest/doctest.h>

#include <array>
#include <cstddef>

TEST_CASE("Only fog with shadowed direct sunlight needs conservative history rejection")
{
    using namespace Keire::RenderBackend;
    FogViewUniforms fog;
    fog.Controls = {1U, 64U, 0U, 0U};
    fog.HeightDensity[2] = 0.01F;
    VolumeShadowUniforms shadows;
    shadows.Directional.DirectionalParameters = {-4.0F, 1.0F, 0.0F, 0.001F};
    const Keire::Vector4 sunlight{1.0F, 0.0F, 0.0F, 0.0F};
    CHECK(FogReceivesTerrainSunlight(fog, sunlight, shadows));
    CHECK_FALSE(FogReceivesTerrainSunlight(fog, {}, shadows));
    fog.HeightDensity[2] = 0.0F;
    CHECK_FALSE(FogReceivesTerrainSunlight(fog, sunlight, shadows));
    fog.Controls[2] = 1U;
    CHECK(FogReceivesTerrainSunlight(fog, sunlight, shadows));
    fog.Controls[0] = 0U;
    CHECK_FALSE(FogReceivesTerrainSunlight(fog, sunlight, shadows));
    fog.Controls[0] = 1U;
    shadows.Directional.DirectionalParameters.Y = 0.0F;
    CHECK_FALSE(FogReceivesTerrainSunlight(fog, sunlight, shadows));
    shadows.Directional.DirectionalParameters.Y = 1.0F;
    shadows.Directional.DirectionalParameters.X = 0.0F;
    CHECK_FALSE(FogReceivesTerrainSunlight(fog, sunlight, shadows));
}

TEST_CASE("Volume shadow matrices retain local detail at a translated camera")
{
    using namespace Keire::RenderBackend;
    AssetDirectionalShadowUniforms directional;
    directional.DirectionalParameters = {4.0F, 0.75F, 0.001F, 0.0005F};
    Keire::Matrix4 view;
    Keire::Matrix4 inverseView;
    inverseView.Elements[12] = 100000000.0F;
    inverseView.Elements[13] = -100000000.0F;
    inverseView.Elements[14] = 200000000.0F;
    for (auto& matrix : directional.DirectionalMatrices)
    {
        matrix.Elements[12] = -inverseView.Elements[12];
        matrix.Elements[13] = -inverseView.Elements[13];
        matrix.Elements[14] = -inverseView.Elements[14];
    }

    const auto packed = MakeVolumeShadowUniforms(directional, view, inverseView);
    CHECK(packed.Directional.DirectionalParameters == directional.DirectionalParameters);
    CHECK(packed.ViewDepthDirection.Z == 1.0F);
    for (std::size_t cascade = 0; cascade < 4; ++cascade)
    {
        const auto& matrix = packed.Directional.DirectionalMatrices[cascade];
        CHECK(matrix.Elements[12] == 0.0F);
        CHECK(matrix.Elements[13] == 0.0F);
        CHECK(matrix.Elements[14] == 0.0F);
        CHECK(matrix.Elements[15] == 1.0F);
        CHECK(directional.DirectionalMatrices[cascade].Elements[12] == -100000000.0F);
    }
}

TEST_CASE("Volume shadow packing preserves clip coordinates and rotated view depth")
{
    using namespace Keire::RenderBackend;
    AssetDirectionalShadowUniforms directional;
    directional.DirectionalCascadeSplits = {10.0F, 30.0F, 90.0F, 270.0F};
    Keire::Matrix4 view;
    view.Elements[2] = -1.0F;
    view.Elements[6] = 0.0F;
    view.Elements[10] = 0.0F;
    Keire::Matrix4 inverseView;
    inverseView.Elements[12] = 128.0F;
    inverseView.Elements[13] = -64.0F;
    inverseView.Elements[14] = 32.0F;
    for (std::size_t cascade = 0; cascade < 4; ++cascade)
    {
        auto& matrix = directional.DirectionalMatrices[cascade];
        for (std::size_t element = 0; element < 16; ++element)
        {
            matrix.Elements[element] = static_cast<float>(element + cascade + 1) * 0.125F;
        }
    }

    const auto packed = MakeVolumeShadowUniforms(directional, view, inverseView);
    CHECK(packed.ViewDepthDirection.X == -1.0F);
    CHECK(packed.ViewDepthDirection.Y == 0.0F);
    CHECK(packed.ViewDepthDirection.Z == 0.0F);
    CHECK(packed.Directional.DirectionalCascadeSplits == directional.DirectionalCascadeSplits);
    constexpr std::array<double, 4> relative{0.125, -2.0, 3.5, 1.0};
    const double surfaceViewDepth =
        view.Elements[2] * relative[0] + view.Elements[6] * relative[1] + view.Elements[10] * relative[2];
    const double fogViewDepth = packed.ViewDepthDirection.X * relative[0] + packed.ViewDepthDirection.Y * relative[1] +
                                packed.ViewDepthDirection.Z * relative[2];
    CHECK(fogViewDepth == doctest::Approx(surfaceViewDepth));
    const std::array<double, 4> world{relative[0] + 128.0, relative[1] - 64.0, relative[2] + 32.0, 1.0};
    for (std::size_t cascade = 0; cascade < 4; ++cascade)
    {
        for (std::size_t row = 0; row < 4; ++row)
        {
            double expected = 0.0;
            double actual = 0.0;
            for (std::size_t column = 0; column < 4; ++column)
            {
                expected += directional.DirectionalMatrices[cascade].Elements[column * 4 + row] * world[column];
                actual += packed.Directional.DirectionalMatrices[cascade].Elements[column * 4 + row] * relative[column];
            }
            CHECK(actual == doctest::Approx(expected));
        }
    }
}
