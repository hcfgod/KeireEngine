#include "Keire/ECS/Components/BuoyancyComponent.h"
#include "Keire/Water/WaterHydrodynamics.h"

#include <doctest/doctest.h>

#include <array>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <vector>

namespace
{
    Keire::WaterMedium StillMedium(const std::uint64_t id, const std::int32_t priority = 0,
                                   const double density = 1000.0, const std::uint32_t layer = 0)
    {
        Keire::WaterSurfaceDefinition definition;
        definition.DensityKilogramsPerCubicMetre = density;
        return {Keire::AssetId(1, id), definition, {}, priority, layer};
    }

    Keire::WaterBodyLoads Loads(const Keire::WaterBodyKinematics& body,
                                const Keire::WaterHydrodynamicSettings& settings,
                                const std::span<const Keire::WaterMedium> media, const double dt = 1.0 / 60.0)
    {
        const std::vector<Keire::Vector3d> gravity(settings.Probes.size(), {0.0, -9.81, 0.0});
        return Keire::EvaluateWaterBodyLoads(body, settings, media, gravity, 0.0, dt);
    }
} // namespace

TEST_CASE("water hull displacement is bounded by water top and bed")
{
    Keire::WaterSurfaceDefinition definition;
    definition.DepthMetres = 2.0;
    const std::array media{Keire::WaterMedium(Keire::AssetId(1, 1), definition, {})};
    Keire::WaterHydrodynamicSettings settings;
    settings.DragRatePerSecond = 0.0;
    Keire::WaterBodyKinematics body;
    const double full = (4.0 / 3.0) * std::numbers::pi_v<double> * 0.125;
    CHECK(Loads(body, settings, media).SubmergedVolumeCubicMetres == doctest::Approx(full / 2.0));
    CHECK(Loads(body, settings, media).ForceNewtons.Y == doctest::Approx(full * 0.5 * 9810.0));
    body.CenterOfMass.Y = -1.0;
    CHECK(Loads(body, settings, media).SubmergedVolumeCubicMetres == doctest::Approx(full));
    body.CenterOfMass.Y = -2.0;
    CHECK(Loads(body, settings, media).SubmergedVolumeCubicMetres == doctest::Approx(full / 2.0));
    body.CenterOfMass.Y = -3.0;
    CHECK(Loads(body, settings, media).WetProbeCount == 0);
    body.CenterOfMass.Y = 1.0;
    CHECK(Loads(body, settings, media).WetProbeCount == 0);
}

TEST_CASE("overlapping water chooses one wet medium with stable priority identity and layer rules")
{
    Keire::WaterBodyKinematics body;
    body.CenterOfMass.Y = -1.0;
    Keire::WaterHydrodynamicSettings settings;
    settings.DragRatePerSecond = 0.0;
    const std::array light{StillMedium(1, 0, 500.0)};
    const auto expected = Loads(body, settings, light).ForceNewtons.Y;
    const std::array first{StillMedium(2, 0, 1000.0), StillMedium(1, 0, 500.0)};
    const std::array reversed{StillMedium(1, 0, 500.0), StillMedium(2, 0, 1000.0)};
    CHECK(Loads(body, settings, first).ForceNewtons.Y == expected);
    CHECK(Loads(body, settings, reversed).ForceNewtons.Y == expected);
    const std::array priority{StillMedium(1, 0, 500.0), StillMedium(2, 1, 1000.0)};
    CHECK(Loads(body, settings, priority).ForceNewtons.Y == doctest::Approx(expected * 2.0));
    settings.WaterLayerMask = 1;
    const std::array masked{StillMedium(1, 0, 500.0), StillMedium(2, 2, 1000.0, 1)};
    CHECK(Loads(body, settings, masked).ForceNewtons.Y == expected);
    settings.WaterLayerMask = 0;
    CHECK(Loads(body, settings, masked).WetProbeCount == 0);
    const std::array duplicate{StillMedium(1), StillMedium(1)};
    CHECK_THROWS_AS(Loads(body, settings, duplicate), std::invalid_argument);
}

TEST_CASE("water wave sampling retains tiny probe offsets at trillion metre origins")
{
    const std::array waves{Keire::WaterWaveDefinition{}};
    Keire::WaterSurfaceDefinition definition;
    const Keire::WaterMedium nearby(Keire::AssetId(1, 1), definition, waves);
    definition.Origin = {1.0e12, -1.0e12, 1.0e12};
    const Keire::WaterMedium distant(Keire::AssetId(1, 1), definition, waves);
    const Keire::Vector3d offset{0.000003, -0.000004, 0.000005};
    const auto local = nearby.Sample({}, offset, 0.75);
    const auto far = distant.Sample(definition.Origin, offset, 0.75);
    REQUIRE(local);
    REQUIRE(far);
    CHECK(far->Surface.SignedDistanceMetres == local->Surface.SignedDistanceMetres);
    CHECK(far->Surface.Normal == local->Surface.Normal);
    CHECK(far->Surface.Velocity == local->Surface.Velocity);
}

TEST_CASE("water waves move the physical surface and carry particle velocity")
{
    Keire::WaterSurfaceDefinition definition;
    Keire::WaterWaveDefinition wave;
    wave.AmplitudeMetres = 0.4;
    wave.PhaseDegrees = 90.0;
    const std::array waves{wave};
    const Keire::WaterWaveField field(waves);
    const Keire::WaterMedium medium(Keire::AssetId(1, 1), definition, waves);
    const auto expected = field.SampleSurface({1.0, 0.0, 2.0}, 0.5);
    const auto actual = medium.Sample({}, {1.0, 0.0, 2.0}, 0.5);
    REQUIRE(actual);
    CHECK(actual->Surface.SignedDistanceMetres == doctest::Approx(-expected.Displacement.Y * expected.Normal.Y));
    CHECK(actual->Surface.Normal == expected.Normal);
    CHECK(actual->Surface.Velocity == expected.Velocity);
}

TEST_CASE("water in a rotated moving frame preserves normal velocity and force axes")
{
    Keire::WaterSurfaceDefinition definition;
    const double half = std::sqrt(0.5);
    definition.Rotation = {0.0, 0.0, half, half};
    definition.FrameLinearVelocity = {1.0, 2.0, 3.0};
    definition.FrameAngularVelocity = {0.0, 0.0, 2.0};
    definition.FlowVelocityLocal = {3.0, 0.0, 0.0};
    const std::array media{Keire::WaterMedium(Keire::AssetId(1, 1), definition, {})};
    const auto sample = media[0].Sample({}, {1.0, 2.0, 0.0}, 0.0);
    REQUIRE(sample);
    CHECK(sample->Surface.Normal.X == doctest::Approx(-1.0));
    CHECK(sample->Surface.SignedDistanceMetres == doctest::Approx(-1.0));
    CHECK(sample->Surface.Velocity.X == doctest::Approx(-3.0));
    CHECK(sample->Surface.Velocity.Y == doctest::Approx(7.0));
    CHECK(sample->Surface.Velocity.Z == doctest::Approx(3.0));
    Keire::WaterBodyKinematics body;
    body.CenterOfMass = {1.0, 0.0, 0.0};
    Keire::WaterHydrodynamicSettings settings;
    settings.DragRatePerSecond = 0.0;
    const std::array gravity{Keire::Vector3d{9.81, 0.0, 0.0}};
    const auto load = Keire::EvaluateWaterBodyLoads(body, settings, media, gravity, 0.0, 0.1);
    CHECK(load.ForceNewtons.X < 0.0);
    CHECK(load.ForceNewtons.Y == 0.0);
}

TEST_CASE("spherical water returns radial buoyancy at far origins and rejects planar waves explicitly")
{
    Keire::WaterSurfaceDefinition definition;
    definition.Shape = Keire::WaterSurfaceShape::Sphere;
    definition.Origin = {1.0e12, -1.0e12, 1.0e12};
    definition.RadiusMetres = 100.0;
    definition.DepthMetres = 10.0;
    const std::array media{Keire::WaterMedium(Keire::AssetId(1, 1), definition, {})};
    Keire::WaterBodyKinematics body;
    body.CenterOfMass = {definition.Origin.X + 99.0, definition.Origin.Y, definition.Origin.Z};
    Keire::WaterHydrodynamicSettings settings;
    settings.DragRatePerSecond = 0.0;
    const std::array gravity{Keire::Vector3d{-9.81, 0.0, 0.0}};
    const auto load = Keire::EvaluateWaterBodyLoads(body, settings, media, gravity, 0.0, 0.1);
    CHECK(load.WetProbeCount == 1);
    CHECK(load.ForceNewtons.X > 0.0);
    CHECK(load.ForceNewtons.Y == 0.0);
    const std::array waves{Keire::WaterWaveDefinition{}};
    CHECK_THROWS_AS(Keire::WaterMedium(Keire::AssetId(1, 1), definition, waves), std::invalid_argument);
}

TEST_CASE("point drag remains bounded for large steps and is subdivision invariant at centered full immersion")
{
    const std::array media{StillMedium(1)};
    Keire::WaterHydrodynamicSettings settings;
    settings.BuoyancyScale = 0.0;
    settings.DragRatePerSecond = 2.0;
    Keire::WaterBodyKinematics body;
    body.CenterOfMass.Y = -1.0;
    body.LinearVelocity.X = 10.0;
    body.InverseMass = 0.1;
    for (const double dt : {0.0001, 0.01, 0.5, 10.0})
    {
        const auto load = Loads(body, settings, media, dt);
        const auto next = body.LinearVelocity.X + load.ForceNewtons.X * body.InverseMass * dt;
        CHECK(next == doctest::Approx(10.0 * std::exp(-2.0 * dt)).epsilon(1.0e-10));
        CHECK(next >= -1.0e-12);
        CHECK(next <= 10.0);
    }
    auto stepped = body;
    for (int step = 0; step < 20; ++step)
    {
        stepped.LinearVelocity.X += Loads(stepped, settings, media, 0.05).ForceNewtons.X * stepped.InverseMass * 0.05;
    }
    CHECK(stepped.LinearVelocity.X == doctest::Approx(10.0 * std::exp(-2.0)).epsilon(1.0e-10));
}

TEST_CASE("multiple buoyancy probes produce restoring torque without multiplying drag")
{
    const std::array media{StillMedium(1)};
    Keire::WaterHydrodynamicSettings settings;
    settings.Probes = {{{-1.0, -1.0, 0.0}, 0.5, 1.0}, {{1.0, 1.0, 0.0}, 0.5, 1.0}};
    settings.DragRatePerSecond = 0.0;
    Keire::WaterBodyKinematics body;
    CHECK(Loads(body, settings, media).TorqueNewtonMetres.Z < 0.0);
    settings.Probes = {{{-1.0, 0.0, 0.0}, 0.5, 1.0}, {{1.0, 0.0, 0.0}, 0.5, 1.0}};
    settings.BuoyancyScale = 0.0;
    settings.DragRatePerSecond = 100.0;
    body.CenterOfMass.Y = -1.0;
    body.LinearVelocity = {5.0, 2.0, 0.0};
    body.AngularVelocity.Z = 3.0;
    const auto load = Loads(body, settings, media, 1.0);
    const double initialEnergy = 0.5 * (25.0 + 4.0 + 9.0);
    const double nextX = body.LinearVelocity.X + load.ForceNewtons.X;
    const double nextY = body.LinearVelocity.Y + load.ForceNewtons.Y;
    const double nextAngular = body.AngularVelocity.Z + load.TorqueNewtonMetres.Z;
    const double nextEnergy = 0.5 * (nextX * nextX + nextY * nextY + nextAngular * nextAngular);
    CHECK(nextEnergy <= initialEnergy + 1.0e-10);
}

TEST_CASE("buoyancy serialized controls round trip and failed deserialization preserves authored state")
{
    const auto registration = Keire::CreateBuoyancyComponentRegistration();
    Keire::BuoyancyComponent source;
    auto settings = source.Settings();
    settings.Probes = {{{1.0, -0.25, 2.0}, 0.75, 2.0}, {{-1.0, 0.0, -2.0}, 1.0, 0.5}};
    settings.WaterLayerMask = 0x80000001U;
    settings.BuoyancyScale = 1.5;
    settings.DragRatePerSecond = 0.25;
    source.Configure(settings);
    const auto values = registration.Serialize(source);
    Keire::BuoyancyComponent restored;
    registration.Deserialize(restored, values, 1);
    CHECK(restored.Settings() == settings);
    auto invalid = values;
    invalid["probeCount"] = std::int64_t{33};
    CHECK_THROWS_AS(registration.Deserialize(restored, invalid, 1), std::invalid_argument);
    CHECK(restored.Settings() == settings);
    invalid = values;
    invalid["probe0.radius"] = -1.0;
    CHECK_THROWS_AS(registration.Deserialize(restored, invalid, 1), std::invalid_argument);
    CHECK(restored.Settings() == settings);
    invalid = values;
    invalid["waterLayers"] = std::int64_t{-1};
    CHECK_THROWS_AS(registration.Deserialize(restored, invalid, 1), std::invalid_argument);
    CHECK(restored.Settings() == settings);
}

TEST_CASE("invalid water force inputs fail before a caller can mutate native body forces")
{
    Keire::WaterBodyKinematics body;
    Keire::WaterHydrodynamicSettings settings;
    const std::array media{StillMedium(1)};
    CHECK_THROWS_AS(Loads(body, settings, media, 0.0), std::invalid_argument);
    body.InverseMass = 0.0;
    CHECK_THROWS_AS(Loads(body, settings, media), std::invalid_argument);
    body.InverseMass = 1.0;
    body.InverseInertia.X = -1.0;
    CHECK_THROWS_AS(Loads(body, settings, media), std::invalid_argument);
    settings.Probes.clear();
    CHECK_THROWS_AS(Keire::ValidateWaterHydrodynamicSettings(settings), std::invalid_argument);
    settings.Probes.push_back({});
    settings.Probes[0].RadiusMetres = std::numeric_limits<double>::infinity();
    CHECK_THROWS_AS(Keire::ValidateWaterHydrodynamicSettings(settings), std::invalid_argument);
}

TEST_CASE("co-moving rotating fluid does not create artificial drag below its waterline")
{
    Keire::WaterSurfaceDefinition definition;
    definition.FrameLinearVelocity = {2.0, 3.0, 4.0};
    definition.FrameAngularVelocity = {0.0, 0.0, 2.0};
    const std::array media{Keire::WaterMedium(Keire::AssetId(1, 1), definition, {})};
    Keire::WaterBodyKinematics body;
    body.CenterOfMass = {0.0, -0.25, 0.0};
    body.LinearVelocity = {2.5, 3.0, 4.0};
    body.AngularVelocity = definition.FrameAngularVelocity;
    Keire::WaterHydrodynamicSettings settings;
    settings.BuoyancyScale = 0.0;
    settings.DragRatePerSecond = 100.0;
    const auto loads = Loads(body, settings, media);
    CHECK(loads.WetProbeCount == 1);
    CHECK(loads.ForceNewtons.X == doctest::Approx(0.0).epsilon(1.0e-10));
    CHECK(loads.ForceNewtons.Y == doctest::Approx(0.0).epsilon(1.0e-10));
    CHECK(loads.ForceNewtons.Z == doctest::Approx(0.0).epsilon(1.0e-10));
}

TEST_CASE("water drag uses the authoritative locked-axis inverse inertia tensor")
{
    const std::array media{StillMedium(1)};
    Keire::WaterHydrodynamicSettings settings;
    settings.Probes = {{{1.0, 0.0, 0.0}, 0.5, 1.0}};
    settings.BuoyancyScale = 0.0;
    settings.DragRatePerSecond = 2.0;
    Keire::WaterBodyKinematics body;
    body.CenterOfMass.Y = -1.0;
    body.LinearVelocity.Y = 10.0;
    const double dt = 0.1;
    const double expected = -10.0 * (-std::expm1(-2.0 * dt)) / dt;
    const auto unrestricted = Loads(body, settings, media, dt);
    CHECK(unrestricted.ForceNewtons.Y == doctest::Approx(expected / 2.0));
    body.WorldInverseInertia = std::array<double, 9>{1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0};
    const auto locked = Loads(body, settings, media, dt);
    CHECK(locked.ForceNewtons.Y == doctest::Approx(expected));
    // Unused legacy values cannot override the native masked tensor.
    body.InverseInertia = {-1.0, -1.0, -1.0};
    body.InertiaRotation = {0.0, 0.0, 0.0, 0.0};
    CHECK(Loads(body, settings, media, dt).ForceNewtons.Y == locked.ForceNewtons.Y);
    body.WorldInverseInertia = std::array<double, 9>{};
    CHECK(Loads(body, settings, media, dt).ForceNewtons.Y == locked.ForceNewtons.Y);
}

TEST_CASE("water world tensor matches rotated principal inertia including off diagonal terms")
{
    const std::array media{StillMedium(1)};
    Keire::WaterHydrodynamicSettings settings;
    settings.Probes = {{{0.0, 0.0, 1.0}, 0.5, 1.0}};
    settings.BuoyancyScale = 0.0;
    Keire::WaterBodyKinematics body;
    body.CenterOfMass.Y = -1.0;
    body.LinearVelocity = {3.0, 2.0, 0.0};
    const double angle = std::numbers::pi_v<double> / 8.0;
    body.InertiaRotation = {0.0, 0.0, std::sin(angle), std::cos(angle)};
    body.InverseInertia = {1.0, 3.0, 4.0};
    const auto principal = Loads(body, settings, media);
    body.WorldInverseInertia = std::array<double, 9>{2.0, -1.0, 0.0, -1.0, 2.0, 0.0, 0.0, 0.0, 4.0};
    const auto tensor = Loads(body, settings, media);
    CHECK(tensor.ForceNewtons.X == doctest::Approx(principal.ForceNewtons.X));
    CHECK(tensor.ForceNewtons.Y == doctest::Approx(principal.ForceNewtons.Y));
    CHECK(tensor.TorqueNewtonMetres.X == doctest::Approx(principal.TorqueNewtonMetres.X));
    CHECK(tensor.TorqueNewtonMetres.Y == doctest::Approx(principal.TorqueNewtonMetres.Y));
}

TEST_CASE("water authoritative inverse inertia rejects nonfinite asymmetric and indefinite tensors")
{
    const std::array media{StillMedium(1)};
    Keire::WaterHydrodynamicSettings settings;
    Keire::WaterBodyKinematics body;
    body.WorldInverseInertia = std::array<double, 9>{1.0, 0.2, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0};
    CHECK_THROWS_AS(Loads(body, settings, media), std::invalid_argument);
    body.WorldInverseInertia = std::array<double, 9>{1.0, 2.0, 0.0, 2.0, 1.0, 0.0, 0.0, 0.0, 1.0};
    CHECK_THROWS_AS(Loads(body, settings, media), std::invalid_argument);
    body.WorldInverseInertia = std::array<double, 9>{-1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0};
    CHECK_THROWS_AS(Loads(body, settings, media), std::invalid_argument);
    body.WorldInverseInertia = std::array<double, 9>{std::numeric_limits<double>::infinity()};
    CHECK_THROWS_AS(Loads(body, settings, media), std::invalid_argument);
    body.WorldInverseInertia = std::array<double, 9>{1.0, 1.0, 0.0, 1.0, 1.0, 0.0, 0.0, 0.0, 0.0};
    CHECK_NOTHROW(Loads(body, settings, media));
}
