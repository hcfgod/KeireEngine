#include "Keire/Core.h"
#include "KeireInternal/Scenes/SceneRuntimeRenderingInternal.h"
#include "KeireInternal/Spatial/PlanetRuntimeCheckpointRecord.h"
#include "KeireInternal/Spatial/SceneRuntimePlanetAccess.h"
#include "KeireInternal/Spatial/SceneRuntimeSpatialAccess.h"

#include <doctest/doctest.h>

#include <atomic>
#include <limits>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace
{
    class PlanetRuntimeFixture final
    {
      public:
        PlanetRuntimeFixture()
        {
            Keire::AssetSystemSpecification assets;
            assets.Mode = Keire::AssetMode::Development;
            Assets = Keire::CreateRef<Keire::AssetSystem>(std::move(assets));
            Events = Keire::CreateRef<Keire::EventBus>();
            Scenes = Keire::CreateRef<Keire::SceneSystem>(
                Keire::SceneSystemSpecification{.Mode = Keire::SceneMode::Enabled}, Assets, Events);
            Physics = Keire::CreateRef<Keire::PhysicsSystem>(
                Keire::PhysicsSystemSpecification{.Mode = Keire::PhysicsMode::Enabled});
            World = Keire::CreateRef<Keire::SceneRuntimeWorld>(
                Keire::SceneRuntimeWorldSpecification{.Scenes = Scenes, .Assets = Assets, .Physics = Physics});
        }

        ~PlanetRuntimeFixture()
        {
            World->Close();
            Physics->Close();
            Scenes->Close();
            Assets->Close();
            Events->Close();
        }

        Keire::Ref<Keire::AssetSystem> Assets;
        Keire::Ref<Keire::EventBus> Events;
        Keire::Ref<Keire::SceneSystem> Scenes;
        Keire::Ref<Keire::PhysicsSystem> Physics;
        Keire::Ref<Keire::SceneRuntimeWorld> World;
    };

    Keire::Detail::PlanetDefinition TestPlanet()
    {
        Keire::Detail::PlanetDefinition planet;
        planet.Planet = Keire::AssetId(1, 2);
        planet.Frame = 2;
        planet.ParentCenter = Keire::Detail::SectorPosition({1, 1}, {}, {1.0e12, -100.0, 0.0});
        planet.RadiusMetres = 100.0;
        planet.GravitationalParameter = 1000.0;
        return planet;
    }
} // namespace

TEST_CASE("Runtime planet configuration owns frames and rejects invalid replacement transactionally")
{
    using namespace Keire::Detail;
    PlanetRuntimeFixture fixture;
    const auto oldRoot = SceneRuntimeSpatialAccess::Frames(*fixture.World).Find(1);
    const auto planet = TestPlanet();
    SceneRuntimePlanetAccess::Configure(*fixture.World, {planet});
    const auto& frames = SceneRuntimeSpatialAccess::Frames(*fixture.World);
    CHECK_FALSE(frames.IsLive(oldRoot));
    CHECK(frames.AttachmentCount(frames.Find(2)) == 1);
    auto invalid = planet;
    invalid.Frame = 3;
    invalid.ParentCenter = SectorPosition({1, 999}, {}, {});
    CHECK_THROWS_AS(SceneRuntimePlanetAccess::Configure(*fixture.World, {invalid}), std::invalid_argument);
    CHECK(SceneRuntimeSpatialAccess::Frames(*fixture.World).IsLive(frames.Find(2)));
    CHECK(SceneRuntimePlanetAccess::Definitions(*fixture.World).front().Planet == planet.Planet);
    CHECK_THROWS_AS(SceneRuntimePlanetAccess::Configure(*fixture.World, {planet, planet}), std::invalid_argument);
    std::atomic<bool> rejected = false;
    std::thread worker(
        [&]
        {
            try
            {
                SceneRuntimePlanetAccess::Configure(*fixture.World, {});
            }
            catch (const std::logic_error&)
            {
                rejected = true;
            }
        });
    worker.join();
    CHECK(rejected);
    const auto sample = SceneRuntimePlanetAccess::Resolve(*fixture.World, planet.Planet, frames.Find(1),
                                                          planet.ParentCenter.Translated({0.0, 101.0, 0.0}), 0.0);
    CHECK(sample.ObserverFromCenterPlanetMetres[1] == doctest::Approx(101.0));
    CHECK(sample.RadiusMetres == 100.0);
    fixture.World->Close();
    CHECK_THROWS_AS(SceneRuntimePlanetAccess::Definitions(*fixture.World), std::logic_error);
}

TEST_CASE("Runtime planet ownership resolves independent and nested planets without camera-follow placement")
{
    using namespace Keire::Detail;
    PlanetRuntimeFixture fixture;
    const auto primary = TestPlanet();
    auto secondary = primary;
    secondary.Planet = Keire::AssetId(1, 3);
    secondary.Frame = 3;
    secondary.ParentCenter = SectorPosition({1, 2}, {}, {1000.0, 0.0, 0.0});
    SceneRuntimePlanetAccess::Configure(*fixture.World, {secondary, primary});
    const auto& frames = SceneRuntimeSpatialAccess::Frames(*fixture.World);
    const auto observer = primary.ParentCenter.Translated({0.0, 101.0, 0.0});
    const auto first = SceneRuntimePlanetAccess::Resolve(*fixture.World, primary.Planet, frames.Find(1), observer, 0.0);
    const auto second =
        SceneRuntimePlanetAccess::Resolve(*fixture.World, secondary.Planet, frames.Find(1), observer, 0.0);
    CHECK(first.ObserverFromCenterPlanetMetres[0] == 0.0);
    CHECK(second.ObserverFromCenterPlanetMetres[0] == -1000.0);
    const auto departing = SceneRuntimePlanetAccess::Resolve(*fixture.World, primary.Planet, frames.Find(1),
                                                             observer.Translated({0.0, 1000.0, 0.0}), 0.0);
    CHECK(departing.ObserverFromCenterPlanetMetres[1] == 1101.0);
    CHECK(departing.TopologyRevision == first.TopologyRevision);
    auto cycle = primary;
    cycle.ParentCenter = SectorPosition({1, 3}, {}, {});
    CHECK_THROWS_AS(SceneRuntimePlanetAccess::Configure(*fixture.World, {cycle, secondary}), std::invalid_argument);
    CHECK(SceneRuntimeSpatialAccess::Frames(*fixture.World).IsLive(frames.Find(3)));
}

TEST_CASE("Runtime authored planets project gravity without relabeling live scene transforms")
{
    using namespace Keire::Detail;
    PlanetRuntimeFixture fixture;
    const auto planet = TestPlanet();
    SceneRuntimePlanetAccess::Configure(*fixture.World, {planet});
    auto scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(), Keire::SceneAsset::EmptyDefinition());
    const auto entity = scene->CreateEntity("Observer");
    const auto session = Keire::CreateRef<Keire::SceneRuntimeSession>(
        scene, fixture.Assets, Keire::Ref<Keire::AudioSystem>{}, fixture.Physics);
    const auto handle = fixture.World->Adopt(session);
    session->Play();
    REQUIRE(session->State() == Keire::ScenePlayState::Playing);
    REQUIRE(session->Physics());
    const auto originalGravity = session->Physics()->Gravity();
    SceneRuntimePlanetAccess::ApplyGravity(*fixture.World, handle, 0.0);
    const auto sources = session->Physics()->GravitySources();
    REQUIRE(sources.size() == 1);
    CHECK(sources[0].Center.X == 1.0e12);
    CHECK(sources[0].Center.Y == -100.0);
    CHECK(sources[0].GravitationalParameter == planet.GravitationalParameter);
    CHECK(session->Physics()->Gravity() == originalGravity);
    CHECK_THROWS_AS(SceneRuntimePlanetAccess::Configure(*fixture.World, {}), std::logic_error);
    const auto& frames = SceneRuntimeSpatialAccess::Frames(*fixture.World);
    CHECK(frames
              .Identity(SceneRuntimeSpatialAccess::FrameForEntity(*fixture.World,
                                                                  session->RuntimeScene()->FindEntity(entity.Id())))
              .Frame == 1);
    CHECK(session->Physics()->GravitySources() == sources);
}

TEST_CASE("Runtime planet gravity rejects lossy root projection before replacing existing sources")
{
    using namespace Keire::Detail;
    PlanetRuntimeFixture fixture;
    auto planet = TestPlanet();
    planet.ParentCenter = SectorPosition({1, 1}, {1'000'000'000'000, 0, 0}, {0.125, 0.0, 0.0});
    SceneRuntimePlanetAccess::Configure(*fixture.World, {planet});
    auto scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(), Keire::SceneAsset::EmptyDefinition());
    const auto session = Keire::CreateRef<Keire::SceneRuntimeSession>(
        scene, fixture.Assets, Keire::Ref<Keire::AudioSystem>{}, fixture.Physics);
    session->Play();
    const auto handle = fixture.World->Adopt(session);
    REQUIRE(session->Physics());
    const std::vector<Keire::PhysicsGravitySource> original{{{1.0, 2.0, 3.0}, 2.0, 1.0}};
    session->Physics()->SetGravitySources(original);
    CHECK_THROWS_AS(SceneRuntimePlanetAccess::ApplyGravity(*fixture.World, handle, 0.0), std::out_of_range);
    CHECK(session->Physics()->GravitySources() == original);
}

TEST_CASE("Runtime planet project modes initialize gravity and resolve views using paused fixed simulation time")
{
    using namespace Keire::Detail;
    for (const auto mode : {PlanetGravityMode::Uniform, PlanetGravityMode::Radial, PlanetGravityMode::Combined})
    {
        PlanetRuntimeFixture fixture;
        PlanetProjectSettings settings;
        settings.Planets = {TestPlanet()};
        settings.SelectedPlanet = settings.Planets.front().Planet;
        settings.Geometry = PlanetGeometryMode::Planetary;
        settings.Gravity = mode;
        SceneRuntimePlanetAccess::ConfigureProject(*fixture.World, settings);
        CHECK(SceneRuntimePlanetAccess::Settings(*fixture.World) == settings);
        auto scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(), Keire::SceneAsset::EmptyDefinition());
        const auto session = Keire::CreateRef<Keire::SceneRuntimeSession>(
            scene, fixture.Assets, Keire::Ref<Keire::AudioSystem>{}, fixture.Physics);
        (void)fixture.World->Adopt(session);
        session->Play();
        REQUIRE(session->Physics());
        const auto originalGravity = session->Physics()->Gravity();
        SceneRuntimePlanetAccess::InitializeGravity(*fixture.World);
        CHECK(session->Physics()->GravitySources().size() == (mode == PlanetGravityMode::Uniform ? 0 : 1));
        CHECK(session->Physics()->Gravity() ==
              (mode == PlanetGravityMode::Radial ? Keire::Vector3{} : originalGravity));
        Keire::RenderCamera camera;
        camera.WorldOrigin = {1.0e12, 1.0, 0.0};
        const auto initial = Keire::Internal::CaptureRuntimePlanetEnvironment(*fixture.World, camera);
        REQUIRE(initial);
        CHECK(initial->ObserverFromCenterPlanetMetres[1] == 101.0);
        CHECK(initial->SnapshotTime == 0.0);
        fixture.World->FixedUpdate(0.25F);
        const auto advanced = Keire::Internal::CaptureRuntimePlanetEnvironment(*fixture.World, camera);
        REQUIRE(advanced);
        CHECK(advanced->SnapshotTime == 0.25);
        fixture.World->Pause();
        fixture.World->FixedUpdate(0.25F);
        CHECK(Keire::Internal::CaptureRuntimePlanetEnvironment(*fixture.World, camera)->SnapshotTime == 0.25);
        CHECK(fixture.World->Step(0.125F));
        CHECK(Keire::Internal::CaptureRuntimePlanetEnvironment(*fixture.World, camera)->SnapshotTime == 0.375);
        CHECK_THROWS_AS(SceneRuntimePlanetAccess::ConfigureProject(*fixture.World, {}), std::logic_error);
    }
}

TEST_CASE("Planet replay checkpoint restores simulation time and split environment clock atomically")
{
    using namespace Keire::Detail;
    PlanetRuntimeFixture fixture;
    PlanetProjectSettings settings;
    settings.Planets = {TestPlanet()};
    settings.Planets.front().AngularVelocity = {0.0, 0.0, 0.25};
    settings.SelectedPlanet = settings.Planets.front().Planet;
    settings.Geometry = PlanetGeometryMode::Planetary;
    SceneRuntimePlanetAccess::ConfigureProject(*fixture.World, settings);
    auto checkpoint = SceneRuntimePlanetAccess::CaptureCheckpoint(*fixture.World);
    checkpoint.SimulationTime = 12.25;
    checkpoint.Clock.Day = std::numeric_limits<std::int64_t>::max();
    checkpoint.Clock.DayFraction = 0.125;
    checkpoint.Clock.Rate = 0.0;
    const auto encoded = nlohmann::json::to_cbor(PlanetRuntimeCheckpointRecord::Encode(checkpoint));
    const auto restored = PlanetRuntimeCheckpointRecord::Decode(nlohmann::json::from_cbor(encoded));
    SceneRuntimePlanetAccess::RestoreCheckpoint(*fixture.World, restored);
    const auto observer = settings.Planets.front().ParentCenter.Translated({0.0, 101.0, 0.0});
    const auto before = SceneRuntimePlanetAccess::ResolveSelected(*fixture.World, observer);
    REQUIRE(before);
    CHECK(before->SnapshotTime == 12.25);
    CHECK(before->Clock.Day == std::numeric_limits<std::int64_t>::max());
    CHECK(before->Clock.DayFraction == 0.125);
    CHECK(before->Clock.Rate == 0.0);
    auto invalid = restored;
    invalid.SimulationTime = -1.0;
    CHECK_THROWS_AS(SceneRuntimePlanetAccess::RestoreCheckpoint(*fixture.World, invalid), std::invalid_argument);
    invalid = restored;
    invalid.Settings.Planets.front().RadiusMetres += 1.0;
    CHECK_THROWS_AS(SceneRuntimePlanetAccess::RestoreCheckpoint(*fixture.World, invalid), std::invalid_argument);
    invalid = restored;
    invalid.Clock.DayFraction = 1.0;
    CHECK_THROWS_AS(SceneRuntimePlanetAccess::RestoreCheckpoint(*fixture.World, invalid), std::invalid_argument);
    const auto afterFailures = SceneRuntimePlanetAccess::ResolveSelected(*fixture.World, observer);
    REQUIRE(afterFailures);
    CHECK(afterFailures->DiscontinuityRevision == before->DiscontinuityRevision);
    CHECK(afterFailures->ObserverFromCenterPlanetMetres == before->ObserverFromCenterPlanetMetres);
    CHECK(afterFailures->Clock == before->Clock);
    SceneRuntimePlanetAccess::RestoreCheckpoint(*fixture.World, restored);
    const auto reapplied = SceneRuntimePlanetAccess::ResolveSelected(*fixture.World, observer);
    REQUIRE(reapplied);
    CHECK(reapplied->DiscontinuityRevision > before->DiscontinuityRevision);
    CHECK(reapplied->ObserverFromCenterPlanetMetres == before->ObserverFromCenterPlanetMetres);
}
