#include "Keire/Core.h"
#include "KeireInternal/Scenes/SceneRuntimeRenderingInternal.h"
#include "KeireInternal/Scenes/SceneRuntimeIdentityAccess.h"
#include "KeireInternal/Spatial/PlanetProjectFrameResolver.h"
#include "KeireInternal/Spatial/PlanetRuntimeCheckpointRecord.h"
#include "KeireInternal/Spatial/SceneRuntimePlanetAccess.h"
#include "KeireInternal/Spatial/SceneRuntimeSpatialAccess.h"

#include <doctest/doctest.h>

#include <atomic>
#include <cmath>
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
    SUBCASE("Authored static support without a rigid body")
    {
    }
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
    const auto hits = session->Physics()->RayCast({.Origin = {static_cast<float>(second.X), 4.0F, static_cast<float>(second.Z)},
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
