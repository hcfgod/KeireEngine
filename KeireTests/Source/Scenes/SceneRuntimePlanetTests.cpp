#include "Keire/Core.h"
#include "KeireInternal/Scenes/SceneRuntimeIdentityAccess.h"
#include "KeireInternal/Scenes/SceneRuntimeRenderingInternal.h"
#include "KeireInternal/Spatial/PlanetProjectFrameResolver.h"
#include "KeireInternal/Spatial/PlanetProjectSettingsRecord.h"
#include "KeireInternal/Spatial/PlanetRuntimeCheckpointRecord.h"
#include "KeireInternal/Spatial/SceneRuntimePlanetAccess.h"
#include "KeireInternal/Spatial/SceneRuntimeSpatialAccess.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <limits>
#include <memory>
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

TEST_CASE("Runtime nearest planet selection matches preview for nested far-origin planets and checkpoint replay")
{
    using namespace Keire::Detail;
    PlanetRuntimeFixture fixture;
    const auto primary = TestPlanet();
    auto moon = primary;
    moon.Planet = Keire::AssetId(1, 3);
    moon.Frame = 3;
    moon.ParentCenter = SectorPosition({1, 2}, {}, {1000.0, 0.0, 0.0});
    PlanetProjectSettings settings;
    settings.Planets = {moon, primary};
    settings.SelectedPlanet = primary.Planet;
    settings.Geometry = PlanetGeometryMode::Planetary;
    settings.Selection = PlanetSelectionMode::NearestSurface;
    SceneRuntimePlanetAccess::ConfigureProject(*fixture.World, settings);
    PlanetProjectFrameResolver preview;
    preview.Configure(settings);
    for (const auto offset : {0.0, 500.0, 1000.0})
    {
        const auto observer = primary.ParentCenter.Translated({offset, 101.0, 0.0});
        const auto runtime = SceneRuntimePlanetAccess::ResolveSelected(*fixture.World, observer);
        const auto editor = preview.Resolve(observer, 0.0, {});
        REQUIRE(runtime);
        REQUIRE(editor);
        CHECK(runtime->Planet == editor->Planet);
        CHECK(runtime->ObserverFromCenterPlanetMetres == editor->ObserverFromCenterPlanetMetres);
        CHECK(runtime->Planet == (offset <= 500.0 ? primary.Planet : moon.Planet));
        const auto candidates = SceneRuntimePlanetAccess::ResolveAll(*fixture.World, observer);
        REQUIRE(candidates.size() == 2);
        CHECK(candidates[0].Planet == primary.Planet);
        CHECK(candidates[1].Planet == moon.Planet);
        CHECK(candidates[0].SnapshotTime == candidates[1].SnapshotTime);
        CHECK(candidates[0].TopologyRevision == candidates[1].TopologyRevision);
        const auto& selected = candidates[runtime->Planet == primary.Planet ? 0 : 1];
        CHECK(selected.ObserverFromCenterPlanetMetres == runtime->ObserverFromCenterPlanetMetres);
    }
    CHECK_THROWS_AS(SceneRuntimePlanetAccess::ResolveAll(*fixture.World, SectorPosition({1, 2}, {}, {})),
                    std::invalid_argument);
    auto checkpoint = SceneRuntimePlanetAccess::CaptureCheckpoint(*fixture.World);
    checkpoint = PlanetRuntimeCheckpointRecord::Decode(PlanetRuntimeCheckpointRecord::Encode(checkpoint));
    CHECK(checkpoint.Settings.Selection == PlanetSelectionMode::NearestSurface);
    SceneRuntimePlanetAccess::RestoreCheckpoint(*fixture.World, checkpoint);
    const auto observer = primary.ParentCenter.Translated({1000.0, 101.0, 0.0});
    CHECK(SceneRuntimePlanetAccess::ResolveSelected(*fixture.World, observer)->Planet == moon.Planet);
    Keire::RenderCamera camera;
    camera.WorldOrigin = {1.0e12 + 1000.0, 1.0, 0.0};
    const auto view = Keire::Internal::CaptureRuntimePlanetEnvironment(*fixture.World, camera);
    REQUIRE(view);
    CHECK(view->Planet == moon.Planet);
    CHECK(view->ObserverFromCenterPlanetMetres[1] == doctest::Approx(101.0));
    settings.Geometry = PlanetGeometryMode::Flat;
    SceneRuntimePlanetAccess::ConfigureProject(*fixture.World, settings);
    CHECK(SceneRuntimePlanetAccess::ResolveAll(*fixture.World, observer).empty());
}

TEST_CASE("Planet camera travel preserves preview runtime and reconstructed atmosphere composition")
{
    using namespace Keire;
    using namespace Keire::Detail;
    for (const double origin : {0.0, 1.0e12, -1.0e12})
    {
        CAPTURE(origin);
        PlanetRuntimeFixture runtime;
        auto earth = TestPlanet();
        earth.RadiusMetres = 6'360'000.0;
        earth.ParentCenter = SectorPosition({1, 1}, {}, {origin, -earth.RadiusMetres, 0.0});
        earth.AngularVelocity = {0.0, 0.01, 0.0};
        auto mars = earth;
        mars.Planet = AssetId(1, 3);
        mars.Frame = 3;
        mars.RadiusMetres = 3'389'500.0;
        mars.ParentCenter = SectorPosition({1, 1}, {}, {origin + 75'000'000.0, -mars.RadiusMetres, 0.0});
        mars.AngularVelocity = {0.0, -0.02, 0.0};
        PlanetProjectSettings settings;
        settings.Geometry = PlanetGeometryMode::Planetary;
        settings.Selection = PlanetSelectionMode::NearestSurface;
        settings.SelectedPlanet = earth.Planet;
        settings.Planets = {mars, earth};
        AtmosphereSettings martianAir;
        martianAir.Enabled = true;
        martianAir.RayleighScattering = {0.00001, 0.000002, 0.000001};
        settings.AtmosphereOverrides = {{mars.Planet, martianAir}};
        CloudSettings martianClouds;
        martianClouds.Layers[0].Coverage = 0.2;
        settings.CloudOverrides = {{mars.Planet, martianClouds}};
        FogSettings martianFog;
        martianFog.Enabled = true;
        martianFog.PlanetaryHeight = true;
        settings.FogOverrides = {{mars.Planet, martianFog}};
        SceneRuntimePlanetAccess::ConfigureProject(*runtime.World, settings);
        PlanetProjectFrameResolver preview;
        preview.Configure(settings);
        PlanetRuntimeFixture reconstructed;
        SceneRuntimePlanetAccess::ConfigureProject(
            *reconstructed.World, PlanetProjectSettingsRecord::Decode(PlanetProjectSettingsRecord::Encode(settings)));

        RenderEnvironmentSettings inherited;
        inherited.Atmosphere.Enabled = true;
        for (const double epoch : {0.0, 123.25})
        {
            auto checkpoint = SceneRuntimePlanetAccess::CaptureCheckpoint(*runtime.World);
            checkpoint.SimulationTime = epoch;
            checkpoint.Clock.Day = 1234567890123456789LL;
            checkpoint.Clock.DayFraction = 0.375;
            SceneRuntimePlanetAccess::RestoreCheckpoint(*runtime.World, checkpoint);
            SceneRuntimePlanetAccess::RestoreCheckpoint(
                *reconstructed.World,
                PlanetRuntimeCheckpointRecord::Decode(PlanetRuntimeCheckpointRecord::Encode(checkpoint)));
            for (const auto& visited : {earth, mars, earth})
            {
                CAPTURE(epoch);
                CAPTURE(visited.Planet.ToString());
                const auto center = visited.ParentCenter.RelativeTo(SectorPosition({1, 1}, {}, {}));
                RenderCamera camera;
                camera.WorldOrigin = {center[0], center[1] + visited.RadiusMetres + 10.0, center[2]};
                camera.View = Math::LookAt({0.125F, 0.25F, -0.5F}, {0.125F, 0.25F, 0.5F}, {0.0F, 1.0F, 0.0F});
                const auto selected = Internal::CaptureRuntimePlanetEnvironment(*runtime.World, camera);
                REQUIRE(selected);
                CHECK(selected->Planet == visited.Planet);
                const auto candidates = Internal::CaptureRuntimePlanetAtmospheres(*runtime.World, camera);
                const auto restoredCandidates = Internal::CaptureRuntimePlanetAtmospheres(*reconstructed.World, camera);
                const auto restoredSelected = Internal::CaptureRuntimePlanetEnvironment(*reconstructed.World, camera);
                const auto captured = CapturePlanetAtmospheres(candidates, selected, inherited.Atmosphere);
                const auto restored =
                    CapturePlanetAtmospheres(restoredCandidates, restoredSelected, inherited.Atmosphere);
                const SectorPosition rootObserver(
                    {1, 1}, {},
                    {camera.WorldOrigin.X + 0.125, camera.WorldOrigin.Y + 0.25, camera.WorldOrigin.Z - 0.5});
                const auto previewFrames = preview.ResolveAll(rootObserver, epoch, checkpoint.Clock);
                REQUIRE(captured.Frames.size() == 2);
                REQUIRE(restored.Frames.size() == 2);
                REQUIRE(previewFrames.size() == 2);
                CHECK(captured.Profiles == restored.Profiles);
                CHECK(captured.Profiles[0] == inherited.Atmosphere);
                CHECK(captured.Profiles[1] == martianAir);
                for (std::size_t index = 0; index < captured.Frames.size(); ++index)
                {
                    const auto& frame = captured.Frames[index];
                    CHECK(frame.Planet == previewFrames[index].Planet);
                    CHECK(frame.ObserverFromCenterPlanetMetres == previewFrames[index].ObserverFromCenterPlanetMetres);
                    CHECK(frame.ObserverFromCenterPlanetMetres ==
                          restored.Frames[index].ObserverFromCenterPlanetMetres);
                    CHECK(frame.PlanetToObserver.Components() == previewFrames[index].PlanetToObserver.Components());
                    CHECK(frame.PlanetToObserver.Components() == restored.Frames[index].PlanetToObserver.Components());
                    CHECK(frame.SnapshotTime == epoch);
                }
                auto weather = inherited;
                ApplyPlanetEnvironmentOverrides(*selected, weather.Atmosphere, weather.Clouds, weather.Fog);
                CHECK(weather.Clouds == (visited.Planet == mars.Planet ? martianClouds : inherited.Clouds));
                CHECK(weather.Fog == (visited.Planet == mars.Planet ? martianFog : inherited.Fog));
            }
        }
    }
}

TEST_CASE("Scene owner transfer advances kinematic roots while dynamic bodies remain inertial")
{
    using namespace Keire;
    using namespace Keire::Detail;
    PlanetRuntimeFixture fixture;
    auto planet = TestPlanet();
    planet.ParentCenter = SectorPosition({1, 1}, {}, {});
    planet.AngularVelocity = {0.0, 0.5, 0.0};
    SceneRuntimePlanetAccess::Configure(*fixture.World, {planet});
    auto scene = CreateRef<Scene>(AssetId::Generate(), SceneAsset::EmptyDefinition());
    auto support = scene->CreateEntity("Kinematic support");
    support.GetComponent<TransformComponent>()->SetLocalPosition({10.0F, 0.0F, 0.0F});
    support.AddComponent<ColliderComponent>();
    SUBCASE("Authored kinematic support")
    {
        support.AddComponent<RigidBodyComponent>()->SetMotion(PhysicsMotionType::Kinematic);
    }
    SUBCASE("Authored static support without a rigid body") {}
    auto moving = scene->CreateEntity("Inertial body");
    moving.GetComponent<TransformComponent>()->SetLocalPosition({0.0F, 10.0F, 0.0F});
    moving.AddComponent<ColliderComponent>();
    moving.AddComponent<RigidBodyComponent>()->SetUseGravity(false);
    const auto marker = scene->CreateEntity("Frame marker");
    marker.GetComponent<TransformComponent>()->SetLocalPosition({20.0F, 0.0F, 0.0F});
    auto session = CreateRef<SceneRuntimeSession>(scene, fixture.Assets, Ref<AudioSystem>{}, fixture.Physics);
    const auto handle = fixture.World->Adopt(session);
    session->Play();
    REQUIRE(session->State() == ScenePlayState::Playing);
    fixture.World->Pause();
    const auto runtimeSupport = session->RuntimeScene()->FindEntity(support.Id());
    const auto runtimeMoving = session->RuntimeScene()->FindEntity(moving.Id());
    auto runtimeMarker = session->RuntimeScene()->FindEntity(marker.Id());
    const auto& frames = SceneRuntimeSpatialAccess::Frames(*fixture.World);
    const auto original = runtimeSupport.GetComponent<TransformComponent>()->WorldPositionPrecise();
    const auto hits = session->Physics()->RayCast({.Origin = {10.0F, 4.0F, 0.0F}, .Direction = {0.0F, -1.0F, 0.0F}});
    REQUIRE(hits.size() == 1);
    const auto body = hits.front().Body;
    SceneRuntimeSpatialAccess::TransferSceneOwner(*fixture.World, handle, frames.Find(2), 0.0);
    CHECK(runtimeSupport.GetComponent<TransformComponent>()->WorldPositionPrecise() == original);
    CHECK(frames.Identity(SceneRuntimeSpatialAccess::OwnerFrameForEntity(*fixture.World, runtimeSupport)).Frame == 2);
    CHECK(frames.Identity(SceneRuntimeSpatialAccess::FrameForEntity(*fixture.World, runtimeSupport)).Frame == 1);
    REQUIRE(fixture.World->Step(0.25F));
    const auto position = runtimeSupport.GetComponent<TransformComponent>()->WorldPositionPrecise();
    CHECK(position.X == doctest::Approx(10.0 * std::cos(0.125)));
    CHECK(position.Z == doctest::Approx(-10.0 * std::sin(0.125)));
    CHECK(runtimeMarker.GetComponent<TransformComponent>()->WorldPositionPrecise().X ==
          doctest::Approx(position.X * 2.0));
    CHECK(runtimeMoving.GetComponent<TransformComponent>()->WorldPositionPrecise().Y == doctest::Approx(10.0));
    const auto state = session->Physics()->TryGetBody(body);
    REQUIRE(state);
    CHECK(state->Position.X == doctest::Approx(position.X).epsilon(1e-5));
    CHECK(state->Position.Z == doctest::Approx(position.Z).epsilon(1e-5));
    CHECK(state->LinearVelocity.Z < -4.0F);

    const auto checkpoint = PlanetRuntimeCheckpointRecord::Decode(
        PlanetRuntimeCheckpointRecord::Encode(SceneRuntimePlanetAccess::CaptureCheckpoint(*fixture.World)));
    REQUIRE(checkpoint.SceneOwners.size() == 1);
    CHECK(checkpoint.SceneOwners.front().Frame == 2);
    auto legacy = PlanetRuntimeCheckpointRecord::Encode(checkpoint);
    legacy["schemaVersion"] = 1;
    legacy.erase("sceneOwners");
    CHECK(PlanetRuntimeCheckpointRecord::Decode(legacy).SceneOwners.empty());
    legacy["schemaVersion"] = 3;
    CHECK_THROWS_AS(PlanetRuntimeCheckpointRecord::Decode(legacy), std::invalid_argument);
    SceneRuntimeSpatialAccess::TransferSceneOwner(*fixture.World, handle, frames.Find(1), 0.25);
    SceneRuntimePlanetAccess::RestoreCheckpoint(*fixture.World, checkpoint);
    CHECK(frames.Identity(SceneRuntimeSpatialAccess::OwnerFrameForEntity(*fixture.World, runtimeSupport)).Frame == 2);
    auto invalid = checkpoint;
    invalid.SceneOwners.front().Frame = 999;
    CHECK_THROWS(SceneRuntimePlanetAccess::RestoreCheckpoint(*fixture.World, invalid));
    CHECK(frames.Identity(SceneRuntimeSpatialAccess::OwnerFrameForEntity(*fixture.World, runtimeSupport)).Frame == 2);

    CHECK_THROWS_AS(SceneRuntimeSpatialAccess::TransferSceneOwner(*fixture.World, handle, frames.Find(1), 0.0),
                    std::invalid_argument);
    const auto unsupportedController = runtimeMarker.AddComponent<CharacterControllerComponent>();
    CHECK_THROWS_AS(SceneRuntimeSpatialAccess::TransferSceneOwner(*fixture.World, handle, frames.Find(2), 0.25),
                    std::invalid_argument);
    CHECK_NOTHROW(SceneRuntimeSpatialAccess::TransferSceneOwner(*fixture.World, handle, frames.Find(1), 0.25));
    unsupportedController->SetEnabled(false);
    REQUIRE(fixture.World->Step(0.25F));
    CHECK(runtimeSupport.GetComponent<TransformComponent>()->WorldPositionPrecise() == position);
    CHECK(session->Physics()->TryGetBody(body)->LinearVelocity == Vector3{});
}

TEST_CASE("Scene owner transfer preserves queued inertial force and torque")
{
    using namespace Keire;
    using namespace Keire::Detail;
    const auto run = [](const bool transfer)
    {
        PlanetRuntimeFixture fixture;
        auto planet = TestPlanet();
        planet.ParentCenter = SectorPosition({1, 1}, {}, {});
        planet.AngularVelocity = {0.0, 0.5, 0.0};
        SceneRuntimePlanetAccess::Configure(*fixture.World, {planet});
        auto scene = CreateRef<Scene>(AssetId::Generate(), SceneAsset::EmptyDefinition());
        auto entity = scene->CreateEntity("Force-driven traveler");
        entity.GetComponent<TransformComponent>()->SetLocalPosition({0.0F, 10.0F, 0.0F});
        entity.AddComponent<ColliderComponent>();
        entity.AddComponent<RigidBodyComponent>()->SetUseGravity(false);
        auto session = CreateRef<SceneRuntimeSession>(scene, fixture.Assets, Ref<AudioSystem>{}, fixture.Physics);
        const auto handle = fixture.World->Adopt(session);
        session->Play();
        REQUIRE(session->State() == ScenePlayState::Playing);
        fixture.World->Pause();
        const auto physics = session->Physics();
        const auto hits = physics->RayCast({.Origin = {0.0F, 14.0F, 0.0F}, .Direction = {0.0F, -1.0F, 0.0F}});
        REQUIRE(hits.size() == 1);
        const auto body = hits.front().Body;
        const auto& frames = SceneRuntimeSpatialAccess::Frames(*fixture.World);
        std::array<PhysicsBodyState, 2> result;
        for (std::size_t step = 0; step < result.size(); ++step)
        {
            physics->AddForce(body, {100.0F, 0.0F, 0.0F});
            physics->AddTorque(body, {0.0F, 0.0F, 10.0F});
            if (transfer)
            {
                SceneRuntimeSpatialAccess::TransferSceneOwner(*fixture.World, handle, frames.Find(step == 0 ? 2 : 1),
                                                              step == 0 ? 0.0 : 0.25);
            }
            REQUIRE(fixture.World->Step(0.25F));
            const auto state = physics->TryGetBody(body);
            REQUIRE(state);
            result[step] = *state;
        }
        return result;
    };
    const auto control = run(false);
    const auto transferred = run(true);
    for (std::size_t step = 0; step < control.size(); ++step)
    {
        CAPTURE(step);
        CHECK(control[step].LinearVelocity.X > 0.0F);
        CHECK(control[step].AngularVelocity.Z > 0.0F);
        CHECK(transferred[step].Position.X == doctest::Approx(control[step].Position.X));
        CHECK(transferred[step].Position.Y == doctest::Approx(control[step].Position.Y));
        CHECK(transferred[step].Position.Z == doctest::Approx(control[step].Position.Z));
        CHECK(transferred[step].LinearVelocity.X == doctest::Approx(control[step].LinearVelocity.X));
        CHECK(transferred[step].LinearVelocity.Y == doctest::Approx(control[step].LinearVelocity.Y));
        CHECK(transferred[step].LinearVelocity.Z == doctest::Approx(control[step].LinearVelocity.Z));
        CHECK(transferred[step].AngularVelocity.X == doctest::Approx(control[step].AngularVelocity.X));
        CHECK(transferred[step].AngularVelocity.Y == doctest::Approx(control[step].AngularVelocity.Y));
        CHECK(transferred[step].AngularVelocity.Z == doctest::Approx(control[step].AngularVelocity.Z));
    }
}

TEST_CASE("Scene owner transfer rejects unsupported content before changing attachments")
{
    using namespace Keire;
    using namespace Keire::Detail;
    PlanetRuntimeFixture fixture;
    auto planet = TestPlanet();
    planet.ParentCenter = SectorPosition({1, 1}, {}, {});
    SceneRuntimePlanetAccess::Configure(*fixture.World, {planet});
    auto scene = CreateRef<Scene>(AssetId::Generate(), SceneAsset::EmptyDefinition());
    auto entity = scene->CreateEntity("Static support");
    entity.AddComponent<ColliderComponent>();
    auto parent = scene->CreateEntity("Unsupported physics parent");
    entity.GetComponent<TransformComponent>()->SetParent(parent);
    auto session = CreateRef<SceneRuntimeSession>(scene, fixture.Assets, Ref<AudioSystem>{}, fixture.Physics);
    const auto handle = fixture.World->Adopt(session);
    session->Play();
    const auto& frames = SceneRuntimeSpatialAccess::Frames(*fixture.World);
    const auto count = frames.AttachmentCount(frames.Find(2));
    CHECK_THROWS_AS(SceneRuntimeSpatialAccess::TransferSceneOwner(*fixture.World, handle, frames.Find(2), 0.0),
                    std::invalid_argument);
    CHECK(frames.AttachmentCount(frames.Find(2)) == count);
    const auto live = session->RuntimeScene()->FindEntity(entity.Id());
    CHECK(frames.Identity(SceneRuntimeSpatialAccess::OwnerFrameForEntity(*fixture.World, live)).Frame == 1);
    FrameRegistry other(1);
    CHECK_THROWS_AS(SceneRuntimeSpatialAccess::TransferSceneOwner(*fixture.World, handle, other.Find(1), 0.0),
                    std::invalid_argument);
}

TEST_CASE("New and replacement static supports join an already following scene")
{
    using namespace Keire;
    using namespace Keire::Detail;
    PlanetRuntimeFixture fixture;
    auto planet = TestPlanet();
    planet.ParentCenter = SectorPosition({1, 1}, {}, {});
    planet.AngularVelocity = {0.0, 0.5, 0.0};
    SceneRuntimePlanetAccess::Configure(*fixture.World, {planet});
    auto scene = CreateRef<Scene>(AssetId::Generate(), SceneAsset::EmptyDefinition());
    auto session = CreateRef<SceneRuntimeSession>(scene, fixture.Assets, Ref<AudioSystem>{}, fixture.Physics);
    const auto handle = fixture.World->Adopt(session);
    session->Play();
    fixture.World->Pause();
    const auto& frames = SceneRuntimeSpatialAccess::Frames(*fixture.World);
    SceneRuntimeSpatialAccess::TransferSceneOwner(*fixture.World, handle, frames.Find(2), 0.0);
    auto support = session->RuntimeScene()->CreateEntity("Live added support");
    support.GetComponent<TransformComponent>()->SetLocalPosition({10.0F, 0.0F, 0.0F});
    const auto collider = support.AddComponent<ColliderComponent>();
    REQUIRE(fixture.World->Step(0.25F));
    const auto first = support.GetComponent<TransformComponent>()->WorldPositionPrecise();
    CHECK(first.X == doctest::Approx(10.0 * std::cos(0.125)));
    collider->SetHalfExtent({1.0F, 0.5F, 1.0F});
    REQUIRE(fixture.World->Step(0.25F));
    const auto second = support.GetComponent<TransformComponent>()->WorldPositionPrecise();
    CHECK(second.X == doctest::Approx(10.0 * std::cos(0.25)));
    CHECK(second.Z == doctest::Approx(-10.0 * std::sin(0.25)));
    const auto hits =
        session->Physics()->RayCast({.Origin = {static_cast<float>(second.X), 4.0F, static_cast<float>(second.Z)},
                                     .Direction = {0.0F, -1.0F, 0.0F}});
    REQUIRE(hits.size() == 1);
    CHECK(session->Physics()->TryGetBody(hits.front().Body)->LinearVelocity.Z < -4.0F);
}

TEST_CASE("Reconstructed planet owner restores static support motion before body velocity checkpoint")
{
    using namespace Keire;
    using namespace Keire::Detail;
    PlanetRuntimeFixture source;
    auto planet = TestPlanet();
    planet.ParentCenter = SectorPosition({1, 1}, {}, {});
    planet.AngularVelocity = {0.0, 0.5, 0.0};
    SceneRuntimePlanetAccess::Configure(*source.World, {planet});
    auto scene = CreateRef<Scene>(AssetId::Generate(), SceneAsset::EmptyDefinition());
    auto support = scene->CreateEntity("Static support");
    support.GetComponent<TransformComponent>()->SetLocalPosition({10.0F, 0.0F, 0.0F});
    support.AddComponent<ColliderComponent>();
    auto session = CreateRef<SceneRuntimeSession>(scene, source.Assets, Ref<AudioSystem>{}, source.Physics);
    const auto handle = source.World->Adopt(session);
    session->Play();
    source.World->Pause();
    const auto& frames = SceneRuntimeSpatialAccess::Frames(*source.World);
    SceneRuntimeSpatialAccess::TransferSceneOwner(*source.World, handle, frames.Find(2), 0.0);
    REQUIRE(source.World->Step(0.25F));
    const auto planets = PlanetRuntimeCheckpointRecord::Decode(
        PlanetRuntimeCheckpointRecord::Encode(SceneRuntimePlanetAccess::CaptureCheckpoint(*source.World)));
    const auto physics = session->CapturePhysicsWorldCheckpoint();
    REQUIRE(physics.Bodies.size() == 1);
    CHECK(physics.Bodies.front().LinearVelocity.Z < -4.0F);

    PlanetRuntimeFixture restored;
    SceneRuntimePlanetAccess::Configure(*restored.World, {planet});
    auto restoredSession = CreateRef<SceneRuntimeSession>(scene, restored.Assets, Ref<AudioSystem>{}, restored.Physics);
    const auto restoredHandle = restored.World->Adopt(restoredSession);
    restoredSession->Play();
    restored.World->Pause();
    const std::array identities{SceneRuntimeIdentityBinding{restoredHandle, planets.SceneOwners.front().Instance}};
    SceneRuntimeIdentityAccess::Rebind(*restored.World, identities);
    SceneRuntimePlanetAccess::RestoreCheckpoint(*restored.World, planets);
    restoredSession->RestorePhysicsWorldCheckpoint(physics);
    const auto recaptured = restoredSession->CapturePhysicsWorldCheckpoint();
    REQUIRE(recaptured.Bodies.size() == 1);
    CHECK(recaptured.Bodies.front().Position == physics.Bodies.front().Position);
    CHECK(recaptured.Bodies.front().LinearVelocity == physics.Bodies.front().LinearVelocity);
    CHECK(recaptured.Bodies.front().AngularVelocity == physics.Bodies.front().AngularVelocity);
}

namespace
{
    struct PlanetContactObservations
    {
        Keire::EntityId Support;
        int Enter = 0;
        int Stay = 0;
    };

    class PlanetContactProbe final : public Keire::Component
    {
      public:
        explicit PlanetContactProbe(std::shared_ptr<PlanetContactObservations> observations)
            : Component(StaticType()), m_Observations(std::move(observations))
        {
        }

        [[nodiscard]] static constexpr Keire::ComponentTypeId StaticType() noexcept
        {
            return Keire::ComponentTypeId(Keire::AssetId(0x74657374706c616eULL, 0x6574636f6e746163ULL));
        }

      protected:
        void OnCollisionEnter(const Keire::PhysicsContactMessage& contact) override
        {
            if (contact.Other == m_Observations->Support)
            {
                ++m_Observations->Enter;
            }
        }

        void OnCollisionStay(const Keire::PhysicsContactMessage& contact) override
        {
            if (contact.Other == m_Observations->Support)
            {
                ++m_Observations->Stay;
            }
        }

      private:
        std::shared_ptr<PlanetContactObservations> m_Observations;
    };

    Keire::Ref<Keire::ComponentRegistry>
    PlanetContactRegistry(const std::shared_ptr<PlanetContactObservations>& observations)
    {
        auto registry = Keire::ComponentRegistry::CreateDefault();
        Keire::ComponentRegistration registration;
        registration.Type = PlanetContactProbe::StaticType();
        registration.Name = "Planet Contact Probe";
        registration.Factory = [observations]
        { return Keire::Ref<Keire::Component>(Keire::CreateRef<PlanetContactProbe>(observations)); };
        registration.Serialize = [](const Keire::Component&) { return Keire::ComponentPropertyBag{}; };
        registration.Deserialize = [](Keire::Component&, const Keire::ComponentPropertyBag&, std::uint32_t) {};
        registry->Register(std::move(registration));
        return registry;
    }

    Keire::PhysicsBodyId FindPlanetTestBody(const Keire::SceneRuntimeSession& session, const Keire::EntityId entity,
                                            const double horizontalOffset)
    {
        auto position = session.RuntimeScene()
                            ->FindEntity(entity)
                            .GetComponent<Keire::TransformComponent>()
                            ->WorldPositionPrecise();
        position.X += horizontalOffset;
        position.Y += 4.0;
        const auto hits =
            session.RayCastPrecise({.Origin = position, .Direction = {0.0F, -1.0F, 0.0F}, .MaximumDistance = 8.0F});
        const auto hit = std::ranges::find(hits, entity, &Keire::ScenePhysicsQueryHitPrecise::Entity);
        REQUIRE(hit != hits.end());
        return hit->Body;
    }

    void CheckPlanetPosition(const Keire::Vector3d actual, const Keire::Vector3d expected, const double tolerance)
    {
        CHECK(std::abs(actual.X - expected.X) <= tolerance);
        CHECK(std::abs(actual.Y - expected.Y) <= tolerance);
        CHECK(std::abs(actual.Z - expected.Z) <= tolerance);
    }
} // namespace

TEST_CASE("Far-origin frame-following scene resumes support contacts after full reconstruction")
{
    using namespace Keire;
    using namespace Keire::Detail;
    constexpr float stepSeconds = 1.0F / 60.0F;
    for (const double originX : {0.0, 1.0e12})
    {
        CAPTURE(originX);
        PlanetRuntimeFixture source;
        auto planet = TestPlanet();
        planet.ParentCenter = SectorPosition({1, 1}, {}, {originX, 0.0, 0.0});
        planet.AngularVelocity = {0.0, 0.1, 0.0};
        SceneRuntimePlanetAccess::Configure(*source.World, {planet});
        auto sourceContacts = std::make_shared<PlanetContactObservations>();
        auto scene =
            CreateRef<Scene>(AssetId::Generate(), SceneAsset::EmptyDefinition(), PlanetContactRegistry(sourceContacts));
        auto support = scene->CreateEntity("Frame support");
        support.GetComponent<TransformComponent>()->SetWorldPositionPrecise({originX + 10.0, -0.5, 0.0});
        support.AddComponent<ColliderComponent>()->SetHalfExtent({4.0F, 0.5F, 4.0F});
        sourceContacts->Support = support.Id();
        auto rider = scene->CreateEntity("Supported rider");
        rider.GetComponent<TransformComponent>()->SetWorldPositionPrecise({originX + 10.0, 0.5, 0.0});
        rider.AddComponent<ColliderComponent>();
        rider.AddComponent<RigidBodyComponent>();
        rider.AddComponent<PlanetContactProbe>();
        auto marker = scene->CreateEntity("Frame marker");
        marker.GetComponent<TransformComponent>()->SetWorldPositionPrecise({originX + 12.0, 0.0, 0.0});
        auto session = CreateRef<SceneRuntimeSession>(scene, source.Assets, Ref<AudioSystem>{}, source.Physics);
        const auto handle = source.World->Adopt(session);
        session->Play();
        source.World->Pause();
        for (int tick = 0; tick < 180; ++tick)
        {
            REQUIRE(source.World->Step(stepSeconds));
        }
        REQUIRE(sourceContacts->Enter > 0);
        const auto supportBody = FindPlanetTestBody(*session, support.Id(), 3.0);
        const auto riderBody = FindPlanetTestBody(*session, rider.Id(), 0.0);
        REQUIRE(supportBody != riderBody);
        const auto time = SceneRuntimePlanetAccess::CaptureCheckpoint(*source.World).SimulationTime;
        const auto& frames = SceneRuntimeSpatialAccess::Frames(*source.World);
        SceneRuntimeSpatialAccess::TransferSceneOwner(*source.World, handle, frames.Find(2), time);
        sourceContacts->Stay = 0;
        for (int tick = 0; tick < 60; ++tick)
        {
            REQUIRE(source.World->Step(stepSeconds));
        }
        REQUIRE(sourceContacts->Stay > 0);
        CHECK(FindPlanetTestBody(*session, support.Id(), 3.0) == supportBody);
        CHECK(FindPlanetTestBody(*session, rider.Id(), 0.0) == riderBody);
        const auto supportState = session->Physics()->TryGetBody(supportBody);
        REQUIRE(supportState);
        CHECK(std::abs(supportState->LinearVelocity.Z) > 0.1F);
        CHECK(std::abs(supportState->AngularVelocity.Y) > 0.05F);

        const auto sceneBytes = SceneAsset::Encode(session->RuntimeScene()->Snapshot());
        const auto planets = PlanetRuntimeCheckpointRecord::Decode(
            PlanetRuntimeCheckpointRecord::Encode(SceneRuntimePlanetAccess::CaptureCheckpoint(*source.World)));
        const auto physics = session->CapturePhysicsWorldCheckpoint();
        PlanetRuntimeFixture restored;
        SceneRuntimePlanetAccess::Configure(*restored.World, {planet});
        auto restoredContacts = std::make_shared<PlanetContactObservations>();
        restoredContacts->Support = support.Id();
        auto restoredScene = CreateRef<Scene>(scene->Asset(), SceneAsset::Decode(sceneBytes)->Definition(),
                                              PlanetContactRegistry(restoredContacts));
        auto restoredSession =
            CreateRef<SceneRuntimeSession>(restoredScene, restored.Assets, Ref<AudioSystem>{}, restored.Physics);
        const auto restoredHandle = restored.World->Adopt(restoredSession);
        restoredSession->Play();
        restored.World->Pause();
        REQUIRE(planets.SceneOwners.size() == 1);
        const std::array identities{SceneRuntimeIdentityBinding{restoredHandle, planets.SceneOwners.front().Instance}};
        SceneRuntimeIdentityAccess::Rebind(*restored.World, identities);
        SceneRuntimePlanetAccess::RestoreCheckpoint(*restored.World, planets);
        restoredSession->RestorePhysicsWorldCheckpoint(physics);
        const auto recaptured = restoredSession->CapturePhysicsWorldCheckpoint();
        REQUIRE(recaptured.Bodies.size() == physics.Bodies.size());
        for (const auto& expected : physics.Bodies)
        {
            const auto actual =
                std::ranges::find(recaptured.Bodies, expected.Entity, &ScenePhysicsCheckpointBody::Entity);
            REQUIRE(actual != recaptured.Bodies.end());
            CheckPlanetPosition({actual->WorldOrigin.X + actual->Position.X, actual->WorldOrigin.Y + actual->Position.Y,
                                 actual->WorldOrigin.Z + actual->Position.Z},
                                {expected.WorldOrigin.X + expected.Position.X,
                                 expected.WorldOrigin.Y + expected.Position.Y,
                                 expected.WorldOrigin.Z + expected.Position.Z},
                                0.001);
            CHECK(actual->LinearVelocity == expected.LinearVelocity);
            CHECK(actual->AngularVelocity == expected.AngularVelocity);
        }
        const auto restoredSupportBody = FindPlanetTestBody(*restoredSession, support.Id(), 3.0);
        const auto restoredRiderBody = FindPlanetTestBody(*restoredSession, rider.Id(), 0.0);
        for (int tick = 0; tick < 60; ++tick)
        {
            CAPTURE(tick);
            REQUIRE(source.World->Step(stepSeconds));
            REQUIRE(restored.World->Step(stepSeconds));
            for (const auto id : {support.Id(), rider.Id(), marker.Id()})
            {
                const auto expected =
                    session->RuntimeScene()->FindEntity(id).GetComponent<TransformComponent>()->WorldPositionPrecise();
                const auto actual = restoredSession->RuntimeScene()
                                        ->FindEntity(id)
                                        .GetComponent<TransformComponent>()
                                        ->WorldPositionPrecise();
                CheckPlanetPosition(actual, expected, id == rider.Id() ? 0.05 : 0.005);
            }
            const auto restoredSupport = restoredSession->Physics()->TryGetBody(restoredSupportBody);
            const auto restoredRider = restoredSession->Physics()->TryGetBody(restoredRiderBody);
            REQUIRE(restoredSupport);
            REQUIRE(restoredRider);
            CHECK(Math::IsFinite(restoredRider->LinearVelocity));
            CHECK(Math::IsFinite(restoredRider->AngularVelocity));
            CHECK(std::abs(restoredRider->Position.Y - restoredSupport->Position.Y - 1.0F) < 0.05F);
            const auto supportPosition = restoredSession->RuntimeScene()
                                             ->FindEntity(support.Id())
                                             .GetComponent<TransformComponent>()
                                             ->WorldPositionPrecise();
            CheckPlanetPosition(restoredSession->Physics()->ToWorldPosition(restoredSupport->Position), supportPosition,
                                0.005);
        }
        CHECK(restoredContacts->Enter + restoredContacts->Stay > 0);
        CHECK(restoredContacts->Stay > 0);
        CHECK(FindPlanetTestBody(*session, support.Id(), 3.0) == supportBody);
        CHECK(FindPlanetTestBody(*session, rider.Id(), 0.0) == riderBody);
        CHECK(FindPlanetTestBody(*restoredSession, support.Id(), 3.0) == restoredSupportBody);
        CHECK(FindPlanetTestBody(*restoredSession, rider.Id(), 0.0) == restoredRiderBody);
    }
}

TEST_CASE("Synchronized characters move with a planet frame and return to inertial control at far origins")
{
    using namespace Keire;
    using namespace Keire::Detail;
    bool supported = false;
    SUBCASE("Free flight") {}
    SUBCASE("Standing on a rotating static support") { supported = true; }
    for (const double originX : {0.0, 1.0e12, -1.0e12})
    {
        for (int rotationAxis = 0; rotationAxis < 3; ++rotationAxis)
        {
            CAPTURE(originX);
            CAPTURE(rotationAxis);
            PlanetRuntimeFixture fixture;
            auto planet = TestPlanet();
            planet.ParentCenter = SectorPosition({1, 1}, {}, {originX, 0.0, 0.0});
            planet.AngularVelocity = {};
            planet.AngularVelocity[rotationAxis] = 0.5;
            SceneRuntimePlanetAccess::Configure(*fixture.World, {planet});
            const auto scene = CreateRef<Scene>(AssetId::Generate(), SceneAsset::EmptyDefinition());
            if (supported)
            {
                auto ground = scene->CreateEntity("Frame support");
                ground.GetComponent<TransformComponent>()->SetWorldPositionPrecise({originX + 10.0, -0.5, 0.0});
                ground.AddComponent<ColliderComponent>()->SetHalfExtent({4.0F, 0.5F, 4.0F});
            }
            auto character = scene->CreateEntity("Frame traveller");
            character.GetComponent<TransformComponent>()->SetWorldPositionPrecise({originX + 10.0, 1.0, 0.0});
            (void)character.AddComponent<CharacterControllerComponent>();
            const auto session =
                CreateRef<SceneRuntimeSession>(scene, fixture.Assets, Ref<AudioSystem>{}, fixture.Physics);
            const auto handle = fixture.World->Adopt(session);
            session->Play();
            REQUIRE(session->State() == ScenePlayState::Playing);
            fixture.World->Pause();
            const auto runtimeCharacter = session->RuntimeScene()->FindEntity(character.Id());
            const auto controller = runtimeCharacter.GetComponent<CharacterControllerComponent>();
            const auto transform = runtimeCharacter.GetComponent<TransformComponent>();
            const auto& frames = SceneRuntimeSpatialAccess::Frames(*fixture.World);
            REQUIRE_NOTHROW(SceneRuntimeSpatialAccess::TransferSceneOwner(*fixture.World, handle, frames.Find(2), 0.0));
            REQUIRE(controller->QueueDesiredMovement({1.0F, 0.0F, 0.0F}));
            constexpr float stepSeconds = 0.1F;
            REQUIRE(fixture.World->Step(stepSeconds));
            REQUIRE(session->State() == ScenePlayState::Paused);
            const double angle = 0.5 * static_cast<double>(stepSeconds);
            const auto expectedPosition = [&](const double rotationAngle) -> Vector3d
            {
                const double cosine = std::cos(rotationAngle);
                const double sine = std::sin(rotationAngle);
                if (rotationAxis == 0)
                {
                    return {originX + 11.0, cosine, sine};
                }
                if (rotationAxis == 1)
                {
                    return {originX + 11.0 * cosine, 1.0, -11.0 * sine};
                }
                return {originX + 11.0 * cosine - sine, 11.0 * sine + cosine, 0.0};
            };
            const auto checkResolvedVelocity = [&](const Vector3d previous, const Vector3d current)
            {
                const auto velocity = controller->RuntimeState().Velocity;
                CHECK(velocity.X == doctest::Approx((current.X - previous.X) / stepSeconds).epsilon(0.005));
                CHECK(velocity.Y == doctest::Approx((current.Y - previous.Y) / stepSeconds).epsilon(0.005));
                CHECK(velocity.Z == doctest::Approx((current.Z - previous.Z) / stepSeconds).epsilon(0.005));
            };
            Vector3d expected = expectedPosition(angle);
            CheckPlanetPosition(transform->WorldPositionPrecise(), expected, 0.002);
            checkResolvedVelocity({originX + 10.0, 1.0, 0.0}, expected);
            CHECK(controller->QueuedMovementCount() == 0);
            CHECK(controller->RuntimeState().Grounded == supported);
            const auto checkpoint = session->CapturePhysicsCheckpoint();
            REQUIRE(checkpoint.size() == (supported ? 2 : 1));
            const auto characterBody =
                std::ranges::find(checkpoint, character.Id(), &ScenePhysicsCheckpointBody::Entity);
            REQUIRE(characterBody != checkpoint.end());
            CheckPlanetPosition(session->Physics()->ToWorldPosition(characterBody->Position), expected, 0.002);

            // No input is needed to remain attached to the rotating ground. Verify several
            // consecutive ticks so a stale support anchor cannot accumulate a second rotation.
            for (int tick = 2; tick <= 5; ++tick)
            {
                const auto previousPosition = transform->WorldPositionPrecise();
                REQUIRE(fixture.World->Step(stepSeconds));
                expected = expectedPosition(angle * tick);
                CheckPlanetPosition(transform->WorldPositionPrecise(), expected, 0.003);
                checkResolvedVelocity(previousPosition, transform->WorldPositionPrecise());
                CHECK(controller->RuntimeState().Grounded == supported);
                CHECK(controller->QueuedMovementCount() == 0);
            }

            const auto time = SceneRuntimePlanetAccess::CaptureCheckpoint(*fixture.World).SimulationTime;
            REQUIRE_NOTHROW(
                SceneRuntimeSpatialAccess::TransferSceneOwner(*fixture.World, handle, frames.Find(1), time));
            const float cosine = static_cast<float>(std::cos(angle * 5));
            const float sine = static_cast<float>(std::sin(angle * 5));
            const Vector3 tangent = rotationAxis == 0   ? Vector3{1.0F, 0.0F, 0.0F}
                                    : rotationAxis == 1 ? Vector3{cosine, 0.0F, -sine}
                                                        : Vector3{cosine, sine, 0.0F};
            REQUIRE(controller->QueueDesiredMovement(tangent));
            REQUIRE(fixture.World->Step(stepSeconds));
            CheckPlanetPosition(transform->WorldPositionPrecise(),
                                {expected.X + tangent.X, expected.Y + tangent.Y, expected.Z + tangent.Z}, 0.003);
            CHECK(controller->QueuedMovementCount() == 0);
            CHECK(controller->RuntimeState().Grounded == supported);
        }
    }
}
