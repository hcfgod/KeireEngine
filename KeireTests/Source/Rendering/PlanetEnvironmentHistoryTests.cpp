#include "KeireInternal/Rendering/PlanetEnvironmentHistory.h"
#include "KeireInternal/Rendering/RenderSceneRecordingTypesInternal.h"

#include <doctest/doctest.h>

#include <optional>

namespace
{
    Keire::PlanetEnvironmentView Sample()
    {
        Keire::PlanetEnvironmentView result;
        result.Planet = {1, 2};
        result.World = 1;
        result.PlanetFrame = 2;
        result.ObserverFrame = 1;
        result.RadiusMetres = 6360000.0;
        result.ObserverFromCenterPlanetMetres = {0.0, 6360100.0, 0.0};
        return result;
    }
} // namespace

TEST_CASE("Planet environment history keys ignore ordinary time and camera travel but retain geometry ownership")
{
    using namespace Keire::RenderBackend;
    const auto sample = Sample();
    const auto original = MakePlanetEnvironmentHistoryKey(sample);
    REQUIRE(original);
    auto changed = sample;
    changed.SnapshotTime = 10.0;
    changed.Clock.Day = 123456789;
    changed.ObserverFromCenterPlanetMetres = {1000.0, 7360000.0, -2000.0};
    CHECK(MakePlanetEnvironmentHistoryKey(changed) == original);
    changed = sample;
    changed.Planet = {3, 4};
    CHECK(MakePlanetEnvironmentHistoryKey(changed) != original);
    changed = sample;
    ++changed.World;
    CHECK(MakePlanetEnvironmentHistoryKey(changed) != original);
    changed = sample;
    ++changed.PlanetFrame;
    CHECK(MakePlanetEnvironmentHistoryKey(changed) != original);
    changed = sample;
    ++changed.ObserverFrame;
    CHECK(MakePlanetEnvironmentHistoryKey(changed) != original);
    changed = sample;
    ++changed.TopologyRevision;
    CHECK(MakePlanetEnvironmentHistoryKey(changed) != original);
    changed = sample;
    ++changed.DiscontinuityRevision;
    CHECK(MakePlanetEnvironmentHistoryKey(changed) != original);
    changed = sample;
    changed.RadiusMetres += 1.0;
    CHECK(MakePlanetEnvironmentHistoryKey(changed) != original);
}

TEST_CASE("Camera environment continuity rejects planet mode changes and unsupported sky rotation")
{
    using namespace Keire::RenderBackend;
    CameraMotionHistoryEntry previous;
    CameraMotionHistoryEntry current;
    CHECK(previous.HasContinuousEnvironment(current));
    const auto sample = Sample();
    current.PlanetEnvironment = MakePlanetEnvironmentHistoryKey(sample);
    CHECK_FALSE(previous.HasContinuousEnvironment(current));
    previous.PlanetEnvironment = current.PlanetEnvironment;
    CHECK(previous.HasContinuousEnvironment(current));
    auto rotated = sample;
    rotated.PlanetToObserverRotation = {0.0, 0.0, 1.0, 0.0};
    current.PlanetEnvironment = MakePlanetEnvironmentHistoryKey(rotated);
    CHECK_FALSE(previous.HasContinuousEnvironment(current));
    rotated = sample;
    rotated.RootToObserverRotation = {0.0, 1.0, 0.0, 0.0};
    current.PlanetEnvironment = MakePlanetEnvironmentHistoryKey(rotated);
    CHECK_FALSE(previous.HasContinuousEnvironment(current));
    current.PlanetEnvironment = MakePlanetEnvironmentHistoryKey(std::nullopt);
    CHECK_FALSE(previous.HasContinuousEnvironment(current));
}
