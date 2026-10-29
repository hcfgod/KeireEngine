#include "KeireInternal/Environment/EnvironmentLightingEvaluation.h"
#include "KeireInternal/Environment/EnvironmentLightingRecord.h"
#include "KeireInternal/Environment/PlanetCelestialEvaluation.h"
#include "KeireInternal/Rendering/StarFieldUniforms.h"

#include <doctest/doctest.h>

#include <array>
#include <cmath>
#include <limits>
#include <vector>

TEST_CASE("Planetary celestial directions follow their explicit frame rather than observer radial up")
{
    using namespace Keire;
    using namespace Keire::Detail;
    const auto quarterTurn = FrameRotation::Normalize(0.0, 0.0, 1.0, 1.0);
    EnvironmentLightingSnapshot snapshot{AssetId(1, 2), 1, EnvironmentDirectionalLight{}};
    snapshot.Directional->Direction = {1.0F, 0.0F, 0.0F};

    const auto legacy = ResolvePlanetCelestialLighting(snapshot, quarterTurn, quarterTurn);
    CHECK(legacy.Directional->Direction == snapshot.Directional->Direction);
    snapshot.DirectionFrame = EnvironmentDirectionalFrame::InertialRoot;
    const auto root = ResolvePlanetCelestialLighting(snapshot, quarterTurn, {});
    CHECK(root.Directional->Direction == snapshot.Directional->Direction);
    const auto rotatedObserver = ResolvePlanetCelestialLighting(snapshot, {}, quarterTurn);
    CHECK(rotatedObserver.Directional->Direction[0] == doctest::Approx(0.0).epsilon(1.0e-6));
    CHECK(rotatedObserver.Directional->Direction[1] == doctest::Approx(1.0));
    CHECK(rotatedObserver.DirectionFrame == EnvironmentDirectionalFrame::LegacyWorld);
    CHECK(ResolvePlanetCelestialLighting(rotatedObserver, quarterTurn, quarterTurn).Directional->Direction ==
          rotatedObserver.Directional->Direction);

    snapshot.DirectionFrame = EnvironmentDirectionalFrame::PlanetFixed;
    const auto fixed = ResolvePlanetCelestialLighting(snapshot, quarterTurn, {});
    CHECK(fixed.Directional->Direction == rotatedObserver.Directional->Direction);
    snapshot.DirectionFrame = static_cast<EnvironmentDirectionalFrame>(255);
    CHECK_THROWS_AS(ResolvePlanetCelestialLighting(snapshot, {}, {}), std::invalid_argument);
}

TEST_CASE("Directional frame records migrate legacy daily lighting without changing its basis")
{
    using namespace Keire;
    using namespace Keire::Detail;
    const std::vector<EnvironmentLightingKeyframe> keys{{0.0, 90.0, 0.0}, {0.5, 90.0, 0.0}};
    for (const auto frame : {EnvironmentDirectionalFrame::LegacyWorld, EnvironmentDirectionalFrame::PlanetFixed,
                             EnvironmentDirectionalFrame::InertialRoot})
    {
        const EnvironmentLightingProfile profile(EnvironmentDirectionalOwnership::Environment, keys, {}, {}, frame);
        auto record = EnvironmentLightingRecord::Encode(profile);
        CHECK(record.at("version") == (frame == EnvironmentDirectionalFrame::LegacyWorld ? 2 : 5));
        const auto restored = EnvironmentLightingRecord::Decode(record);
        CHECK(restored == profile);
        const auto snapshot =
            EvaluateEnvironmentLighting(restored, {std::numeric_limits<std::int64_t>::max(), 0.25}, AssetId(1, 2), 1);
        CHECK(snapshot.DirectionFrame == frame);
        REQUIRE(snapshot.Directional);
        CHECK(snapshot.Directional->Direction[0] == doctest::Approx(-1.0));
        if (frame != EnvironmentDirectionalFrame::LegacyWorld)
        {
            record["directionFrame"] = "camera-tangent";
            CHECK_THROWS_AS(EnvironmentLightingRecord::Decode(record), std::invalid_argument);
        }
    }
}

TEST_CASE("Star orientation packs inverse root rotation while legacy packing stays identity")
{
    const auto identity = Keire::RenderBackend::MakeStarFieldOrientation();
    CHECK(identity == (std::array<float, 4>{0.0F, 0.0F, 0.0F, 1.0F}));
    const auto rotation = Keire::Detail::FrameRotation::Normalize(1.0, 2.0, 3.0, 4.0);
    const auto packed = Keire::RenderBackend::MakeStarFieldOrientation(rotation);
    const auto expected = rotation.Inverse().Components();
    for (std::size_t axis = 0; axis < packed.size(); ++axis)
    {
        CHECK(packed[axis] == doctest::Approx(expected[axis]));
    }
}
