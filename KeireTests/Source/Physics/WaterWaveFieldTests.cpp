#include "Keire/Water/WaterWaveField.h"

#include <doctest/doctest.h>

#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>

TEST_CASE("water waves preserve phase at distant translated origins and long times")
{
    const std::array waves{
        Keire::WaterWaveDefinition{.WavelengthMetres = 16.0, .AmplitudeMetres = 0.5, .SpeedMetresPerSecond = 2.0}};
    const Keire::WaterWaveField local(waves);
    const Keire::WaterWaveField distant(waves, {1.0e12, -1.0e12, 1.0e12});
    const auto reference = local.SampleParametric({4.0, 0.0, 2.0}, 1.0);
    const auto translated = distant.SampleParametric({1.0e12 + 4.0, -1.0e12, 1.0e12 + 2.0}, 1.0);
    CHECK(translated.Displacement == reference.Displacement);
    CHECK(translated.Normal == reference.Normal);
    CHECK(translated.Velocity == reference.Velocity);
    const auto periodic = local.SampleParametric({4.0, 0.0, 2.0}, 8.0e12 + 1.0);
    CHECK(periodic.Displacement == reference.Displacement);
    CHECK(periodic.Normal == reference.Normal);
}

TEST_CASE("water wave analytic normal and particle velocity match differential geometry")
{
    const std::array waves{
        Keire::WaterWaveDefinition{.AmplitudeMetres = 0.4, .DirectionDegrees = 35.0},
        Keire::WaterWaveDefinition{.WavelengthMetres = 7.0, .AmplitudeMetres = 0.2, .DirectionDegrees = -70.0}};
    const Keire::WaterWaveField field(waves);
    constexpr double epsilon = 0.00001;
    const Keire::Vector3d point{1.25, 0.0, -2.5};
    const auto center = field.SampleParametric(point, 1.3);
    const auto later = field.SampleParametric(point, 1.3 + epsilon);
    const auto earlier = field.SampleParametric(point, 1.3 - epsilon);
    CHECK(center.Velocity.X == doctest::Approx((later.Displacement.X - earlier.Displacement.X) / (2.0 * epsilon)));
    CHECK(center.Velocity.Y == doctest::Approx((later.Displacement.Y - earlier.Displacement.Y) / (2.0 * epsilon)));
    CHECK(center.Velocity.Z == doctest::Approx((later.Displacement.Z - earlier.Displacement.Z) / (2.0 * epsilon)));
    for (const Keire::Vector3d offset : {Keire::Vector3d{epsilon, 0.0, 0.0}, {0.0, 0.0, epsilon}})
    {
        const auto plus = field.SampleParametric({point.X + offset.X, 0.0, point.Z + offset.Z}, 1.3);
        const auto minus = field.SampleParametric({point.X - offset.X, 0.0, point.Z - offset.Z}, 1.3);
        const Keire::Vector3d tangent{(2.0 * offset.X + plus.Displacement.X - minus.Displacement.X) / (2.0 * epsilon),
                                      (plus.Displacement.Y - minus.Displacement.Y) / (2.0 * epsilon),
                                      (2.0 * offset.Z + plus.Displacement.Z - minus.Displacement.Z) / (2.0 * epsilon)};
        CHECK(std::abs(tangent.X * center.Normal.X + tangent.Y * center.Normal.Y + tangent.Z * center.Normal.Z) <
              0.000001);
    }
    CHECK(std::hypot(center.Normal.X, center.Normal.Y, center.Normal.Z) == doctest::Approx(1.0));
}

TEST_CASE("water wave inversion follows displaced crests without folding")
{
    const std::array waves{
        Keire::WaterWaveDefinition{.WavelengthMetres = 4.0, .AmplitudeMetres = 2.0, .Steepness = 1.0},
        Keire::WaterWaveDefinition{
            .WavelengthMetres = 5.0, .AmplitudeMetres = 1.0, .DirectionDegrees = 70.0, .Steepness = 1.0}};
    const Keire::WaterWaveField field(waves);
    CHECK(field.HorizontalScale() < 1.0);
    for (int index = 0; index < 32; ++index)
    {
        const Keire::Vector3d parameter{index * 0.25, 0.0, index * -0.1};
        const auto mesh = field.SampleParametric(parameter, 0.7);
        REQUIRE(mesh.Normal.Y > 0.0);
        const auto surface =
            field.SampleSurface({parameter.X + mesh.Displacement.X, 0.0, parameter.Z + mesh.Displacement.Z}, 0.7);
        CHECK(surface.Displacement.X == 0.0);
        CHECK(surface.Displacement.Z == 0.0);
        CHECK(surface.Displacement.Y == doctest::Approx(mesh.Displacement.Y).epsilon(0.00001));
        CHECK(surface.Normal.Y == doctest::Approx(mesh.Normal.Y).epsilon(0.00001));
    }
}

TEST_CASE("water waves validate inputs and support still and empty surfaces")
{
    const Keire::WaterWaveField empty({});
    CHECK(empty.SampleSurface({}, 0.0).Displacement == Keire::Vector3d{});
    CHECK(empty.SampleParametric({}, 0.0).Normal == Keire::Vector3d{0.0, 1.0, 0.0});
    auto definition = Keire::WaterWaveDefinition{};
    definition.SpeedMetresPerSecond = 0.0;
    const std::array stillDefinitions{definition};
    const Keire::WaterWaveField still(stillDefinitions);
    CHECK(still.SampleParametric({}, 1.0).Displacement == still.SampleParametric({}, 1.0e15).Displacement);
    CHECK(still.SampleParametric({}, 1.0).Velocity == Keire::Vector3d{});
    const double invalid = std::numeric_limits<double>::quiet_NaN();
    CHECK_THROWS_AS((void)empty.SampleSurface({}, invalid), std::invalid_argument);
    CHECK_THROWS_AS((void)empty.SampleParametric({invalid, 0.0, 0.0}, 0.0), std::invalid_argument);
    definition.WavelengthMetres = 0.0;
    const std::array invalidDefinitions{definition};
    CHECK_THROWS_AS(Keire::WaterWaveField{invalidDefinitions}, std::invalid_argument);
    const std::array<Keire::WaterWaveDefinition, 17> excessive{};
    CHECK_THROWS_AS(Keire::WaterWaveField{excessive}, std::invalid_argument);
}

TEST_CASE("water render patches preserve wave phase with nearby float shader coordinates")
{
    const std::array waves{Keire::WaterWaveDefinition{.WavelengthMetres = 16.0,
                                                      .AmplitudeMetres = 0.5,
                                                      .SpeedMetresPerSecond = 2.0,
                                                      .DirectionDegrees = 32.0,
                                                      .PhaseDegrees = 12.0},
                           Keire::WaterWaveDefinition{.WavelengthMetres = 8.0,
                                                      .AmplitudeMetres = 0.1,
                                                      .SpeedMetresPerSecond = -1.0,
                                                      .DirectionDegrees = -70.0,
                                                      .PhaseDegrees = 45.0}};
    const Keire::Vector3d origin{1.0e12, -1.0e12, 1.0e12};
    const Keire::WaterWaveField field(waves, origin);
    const Keire::Vector3d anchor{origin.X + 128.0, origin.Y, origin.Z - 64.0};
    const double time = 8.0e12 + 0.25;
    const auto patch = field.CaptureRenderPatch(anchor, time);
    CHECK(patch.Origin == anchor);
    REQUIRE(patch.Parameters.Count == 2);
    CHECK(patch.Parameters.Padding == std::array<std::uint32_t, 3>{});
    for (const float offset : {-16.0F, 0.0F, 0.25F, 16.0F})
    {
        Keire::Vector3 rendered;
        Keire::Vector3 velocity;
        for (std::uint32_t index = 0; index < patch.Parameters.Count; ++index)
        {
            const auto& wave = patch.Parameters.Waves[index];
            const auto& direction = wave.DirectionFrequencyPhase;
            const auto& amplitude = wave.AmplitudesVelocity;
            const float phase = (direction[0] * offset + direction[1] * offset * 0.5F) * direction[2] + direction[3];
            const float sine = std::sin(phase);
            const float cosine = std::cos(phase);
            rendered.X += direction[0] * amplitude[1] * cosine;
            rendered.Y += amplitude[0] * sine;
            rendered.Z += direction[1] * amplitude[1] * cosine;
            velocity.X += direction[0] * amplitude[3] * sine;
            velocity.Y -= amplitude[2] * cosine;
            velocity.Z += direction[1] * amplitude[3] * sine;
        }
        const auto expected = field.SampleParametric({anchor.X + offset, anchor.Y, anchor.Z + offset * 0.5}, time);
        CHECK(std::abs(rendered.X - expected.Displacement.X) < 0.00001);
        CHECK(std::abs(rendered.Y - expected.Displacement.Y) < 0.00001);
        CHECK(std::abs(rendered.Z - expected.Displacement.Z) < 0.00001);
        CHECK(std::abs(velocity.X - expected.Velocity.X) < 0.00001);
        CHECK(std::abs(velocity.Y - expected.Velocity.Y) < 0.00001);
        CHECK(std::abs(velocity.Z - expected.Velocity.Z) < 0.00001);
    }
    CHECK(patch.Parameters.Waves.back().DirectionFrequencyPhase == std::array<float, 4>{});
}

TEST_CASE("water render patches reject unsafe float coefficients and preserve empty fields")
{
    const Keire::WaterWaveField empty({});
    CHECK(empty.CaptureRenderPatch({}, 0.0).Parameters.Count == 0);
    const double invalid = std::numeric_limits<double>::infinity();
    CHECK_THROWS_AS((void)empty.CaptureRenderPatch({invalid, 0.0, 0.0}, 0.0), std::invalid_argument);
    CHECK_THROWS_AS((void)empty.CaptureRenderPatch({}, invalid), std::invalid_argument);
    const std::array underflow{Keire::WaterWaveDefinition{.WavelengthMetres = 1.0e50}};
    const Keire::WaterWaveField tinyFrequency(underflow);
    CHECK_THROWS_AS((void)tinyFrequency.CaptureRenderPatch({}, 0.0), std::out_of_range);
    const std::array excessive{Keire::WaterWaveDefinition{.AmplitudeMetres = 1.0e20}};
    const Keire::WaterWaveField hugeSlope(excessive);
    CHECK_THROWS_AS((void)hugeSlope.CaptureRenderPatch({}, 0.0), std::out_of_range);
}

TEST_CASE("water surface inversion preserves full wave geometry at its converged parameter")
{
    std::array<Keire::WaterWaveDefinition, Keire::WaterWaveField::MaximumWaves> definitions;
    for (std::size_t index = 0; index < definitions.size(); ++index)
    {
        definitions[index].WavelengthMetres = 8.0 + static_cast<double>(index);
        definitions[index].AmplitudeMetres = 0.2;
        definitions[index].Steepness = 1.0;
        definitions[index].DirectionDegrees = static_cast<double>(index) * 23.0;
        definitions[index].SpeedMetresPerSecond = index % 2 == 0 ? 3.0 : -2.0;
    }
    const Keire::Vector3d origin{1.0e9, 0.0, -1.0e9};
    const Keire::WaterWaveField field(definitions, origin);
    for (const double offset : {-3.0, 0.0, 4.0})
    {
        const Keire::Vector3d parameter{origin.X + offset, 0.0, origin.Z + offset * 0.5};
        const auto expected = field.SampleParametric(parameter, 1.0e12);
        const Keire::Vector3d query{parameter.X + expected.Displacement.X, 0.0, parameter.Z + expected.Displacement.Z};
        const auto surface = field.SampleSurface(query, 1.0e12);
        CHECK(surface.Displacement.X == 0.0);
        CHECK(surface.Displacement.Z == 0.0);
        CHECK(surface.Displacement.Y == doctest::Approx(expected.Displacement.Y).epsilon(0.00001));
        CHECK(surface.Normal.X == doctest::Approx(expected.Normal.X).epsilon(0.00001));
        CHECK(surface.Normal.Y == doctest::Approx(expected.Normal.Y).epsilon(0.00001));
        CHECK(surface.Normal.Z == doctest::Approx(expected.Normal.Z).epsilon(0.00001));
        CHECK(surface.Velocity.X == doctest::Approx(expected.Velocity.X).epsilon(0.00001));
        CHECK(surface.Velocity.Y == doctest::Approx(expected.Velocity.Y).epsilon(0.00001));
        CHECK(surface.Velocity.Z == doctest::Approx(expected.Velocity.Z).epsilon(0.00001));
    }
}
