#include "KeireInternal/Rendering/PlanetEnvironmentCapture.h"
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
        result.TopologyRevision = 1;
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

TEST_CASE("Selected cloud and fog overrides invalidate camera history before packet capture")
{
    using namespace Keire::RenderBackend;
    Keire::RenderEnvironmentSettings inherited;
    inherited.Clouds.Volume.Enabled = true;
    inherited.Fog.Enabled = true;
    inherited.Fog.LocalVolumes.emplace_back();
    const auto historyFor = [&](const Keire::PlanetEnvironmentView& view)
    {
        auto environment = inherited;
        Keire::Detail::ApplyPlanetEnvironmentOverrides(view, environment.Atmosphere, environment.Clouds,
                                                       environment.Fog);
        auto history = MakeCameraMotionHistory({}, 1, environment, std::nullopt);
        history.PlanetEnvironment = MakePlanetEnvironmentHistoryKey(view);
        return history;
    };
    const auto sample = Sample();
    const auto original = historyFor(sample);
    auto changed = sample;
    SUBCASE("cloud shape edit")
    {
        changed.CloudOverride = inherited.Clouds;
        changed.CloudOverride->Volume.ThicknessMeters += 200.0;
    }
    SUBCASE("cloud explicit disable")
    {
        changed.CloudOverride = inherited.Clouds;
        changed.CloudOverride->Volume.Enabled = false;
    }
    SUBCASE("fog explicit disable preserves authored local volumes")
    {
        changed.FogOverride = inherited.Fog;
        changed.FogOverride->Enabled = false;
        const auto disabled = historyFor(changed);
        CHECK_FALSE(disabled.Fog.Enabled);
        CHECK(disabled.Fog.LocalVolumes == inherited.Fog.LocalVolumes);
    }
    SUBCASE("fog local volume edit")
    {
        changed.FogOverride = inherited.Fog;
        changed.FogOverride->LocalVolumes.front().ExtinctionPerMeter = 0.5;
    }
    const auto edited = historyFor(changed);
    CHECK_FALSE(original.HasContinuousEnvironment(edited));
    CHECK(edited.HasContinuousEnvironment(historyFor(changed)));
    CHECK(original.HasContinuousEnvironment(historyFor(sample)));
}

TEST_CASE("An explicit profile matching inheritance does not unnecessarily invalidate camera history")
{
    using namespace Keire::RenderBackend;
    Keire::RenderEnvironmentSettings inherited;
    const auto original = MakeCameraMotionHistory({}, 1, inherited, std::nullopt);
    auto view = Sample();
    view.CloudOverride = inherited.Clouds;
    view.FogOverride = inherited.Fog;
    auto resolved = inherited;
    Keire::Detail::ApplyPlanetEnvironmentOverrides(view, resolved.Atmosphere, resolved.Clouds, resolved.Fog);
    CHECK(original.HasContinuousEnvironment(MakeCameraMotionHistory({}, 2, resolved, std::nullopt)));
}
