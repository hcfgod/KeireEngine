#include "KeireInternal/Rendering/AtmosphereUniforms.h"

#include <doctest/doctest.h>

#include <limits>

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
