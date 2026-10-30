#include "KeireInternal/Rendering/AtmosphereUniforms.h"
#include "KeireInternal/Rendering/EnvironmentViewCoordinates.h"
#include "KeireInternal/Rendering/FogViewUniforms.h"
#include "KeireInternal/Rendering/PlanetAtmosphereSetUniforms.h"

#include <doctest/doctest.h>

#include <array>
#include <limits>
#include <stdexcept>
#include <vector>

TEST_CASE("Multiple planet atmosphere packing retains relative centers and rejects candidate overflow")
{
    using namespace Keire::RenderBackend;
    Keire::AtmosphereSettings settings;
    settings.Enabled = true;
    std::vector<Keire::Detail::ResolvedPlanetFrame> frames(2);
    frames[0].RadiusMetres = 6'360'000.0;
    frames[0].ObserverFromCenterPlanetMetres = {0.125, 6'360'100.0, 0.0};
    frames[1].RadiusMetres = 1'000'000.0;
    frames[1].ObserverFromCenterPlanetMetres = {2'000'000.0, 0.0, 0.0};
    const auto packed = MakePlanetAtmosphereSetUniforms(settings, {0.0F, -1.0F, 0.0F}, frames);
    CHECK(packed.Controls[0] == 2);
    CHECK(packed.Media[0].ObserverRadius.X == doctest::Approx(0.000125));
    CHECK(packed.Media[1].ObserverRadius.X == doctest::Approx(2000.0));
    CHECK(packed.Media[1].ObserverRadius.W == doctest::Approx(1000.0));
    CHECK(packed.Media[0].SunHeight == packed.Media[1].SunHeight);
    frames.resize(MaximumPlanetAtmospheres + 1, frames[0]);
    CHECK_THROWS_AS(MakePlanetAtmosphereSetUniforms(settings, {}, frames), std::length_error);
    settings.Enabled = false;
    CHECK(MakePlanetAtmosphereSetUniforms(settings, {}, frames).Controls[0] == 0);
    settings.Enabled = true;
    frames.resize(1);
    CHECK(MakePlanetAtmosphereSetUniforms(settings, {}, frames).Controls[0] == 0);
}

TEST_CASE("Atmosphere and fog retain local observer detail around distant environment anchors")
{
    const Keire::Vector3 observer{0.015625F, 0.03125F, -0.0625F};
    const Keire::Vector3d anchor{1e15, 1e15, -1e15};
    const Keire::Vector3d origin{anchor.X + 1024.0, anchor.Y + 128.0, anchor.Z - 512.0};
    const auto relative = Keire::RenderBackend::EnvironmentRelativeObserver(observer, origin, anchor);
    CHECK(relative == std::array<double, 3>{1024.015625, 128.03125, -512.0625});
    Keire::AtmosphereSettings atmosphere;
    const auto expected = Keire::RenderBackend::MakeAtmosphereUniforms(
        atmosphere, {1024.015625F, 128.03125F, -512.0625F}, {0.0F, -1.0F, 0.0F});
    const auto actual =
        Keire::RenderBackend::MakeAtmosphereUniforms(atmosphere, observer, {0.0F, -1.0F, 0.0F}, origin, anchor);
    CHECK(actual.ObserverRadius == expected.ObserverRadius);
    Keire::FogSettings fog;
    fog.Enabled = true;
    CHECK(Keire::RenderBackend::MakeFogViewUniforms(fog, relative) ==
          Keire::RenderBackend::MakeFogViewUniforms(fog, {1024.015625, 128.03125, -512.0625}));
    CHECK_THROWS_AS(Keire::RenderBackend::EnvironmentRelativeObserver(
                        observer, {std::numeric_limits<double>::quiet_NaN(), 0.0, 0.0}, anchor),
                    std::invalid_argument);
}

TEST_CASE("Atmosphere shader packing preserves optical units and selected sun direction")
{
    Keire::AtmosphereSettings settings;
    settings.Enabled = true;
    const auto packed =
        Keire::RenderBackend::MakeAtmosphereUniforms(settings, {0.0F, 2000.0F, 0.0F}, {0.0F, -2.0F, 0.0F});
    CHECK(packed.ObserverRadius.Y == doctest::Approx(6362.0F));
    CHECK(packed.ObserverRadius.W == doctest::Approx(6360.0F));
    CHECK(packed.SunHeight.Y == 1.0F);
    CHECK(packed.SunHeight.W == doctest::Approx(100.0F));
    CHECK(packed.RayleighScale.X == doctest::Approx(0.005802F));
    CHECK(packed.RayleighScale.W == doctest::Approx(8.0F));
    CHECK(packed.Mie.X == doctest::Approx(0.003996F));
    CHECK(packed.Mie.Y == doctest::Approx(0.00444F));
    CHECK(packed.Mie.W == doctest::Approx(1.2F));
    CHECK(packed.AbsorptionPeak.Y == doctest::Approx(0.001881F));
    CHECK(packed.AbsorptionPeak.W == doctest::Approx(25.0F));
    CHECK(packed.SolarWidth.W == doctest::Approx(15.0F));
    CHECK(packed.Controls.X == 1.0F);
    CHECK(packed.Controls.Y == 24.0F);
    CHECK(packed.Controls.Z == 8.0F);
    CHECK(packed.Controls.W == doctest::Approx(0.004675F));
}

TEST_CASE("Atmosphere packing subtracts the double planet center before narrowing")
{
    Keire::AtmosphereSettings settings;
    settings.PlanetCenter = {1'000'000'000.25, -6'360'000.0, -1'000'000'000.5};
    const auto packed =
        Keire::RenderBackend::MakeAtmosphereUniforms(settings, {1.0e9F, 0.0F, -1.0e9F}, {1.0F, 0.0F, 0.0F});
    CHECK(packed.ObserverRadius.X == doctest::Approx(-0.00025F));
    CHECK(packed.ObserverRadius.Z == doctest::Approx(0.0005F));
    CHECK(packed.SunHeight.X == -1.0F);
    CHECK(packed.Controls.X == 0.0F);
}

TEST_CASE("Atmosphere shader packing bounds loops and handles degenerate light vectors")
{
    Keire::AtmosphereSettings settings;
    settings.ViewSamples = 0;
    settings.LightSamples = std::numeric_limits<unsigned int>::max();
    const auto packed = Keire::RenderBackend::MakeAtmosphereUniforms(settings, {}, {});
    CHECK(packed.Controls.Y == 4.0F);
    CHECK(packed.Controls.Z == 32.0F);
    CHECK(packed.SunHeight.Y == 1.0F);
    const auto invalidDirection = Keire::RenderBackend::MakeAtmosphereUniforms(
        settings, {}, {std::numeric_limits<float>::infinity(), 0.0F, 0.0F});
    CHECK(invalidDirection.SunHeight.Y == 1.0F);
}
