#include "Keire/Core.h"
#include "KeireInternal/Environment/SceneRuntimeEnvironmentAccess.h"
#include "KeireInternal/Scenes/CharacterMovementAccess.h"
#include "KeireInternal/Spatial/SceneRuntimePlanetAccess.h"

#include <doctest/doctest.h>

#include <limits>
#include <stdexcept>

TEST_CASE("Planet tick clock overflow rejects before gravity and character input change")
{
    using namespace Keire;
    using namespace Keire::Detail;
    auto assets = CreateRef<AssetSystem>(AssetSystemSpecification{.Mode = AssetMode::Development});
    auto events = CreateRef<EventBus>();
    auto scenes = CreateRef<SceneSystem>(SceneSystemSpecification{.Mode = SceneMode::Enabled}, assets, events);
    auto physics = CreateRef<PhysicsSystem>(PhysicsSystemSpecification{.Mode = PhysicsMode::Enabled});
    auto world = CreateRef<SceneRuntimeWorld>(
        SceneRuntimeWorldSpecification{.Scenes = scenes, .Assets = assets, .Physics = physics});
    PlanetDefinition planet;
    planet.Planet = AssetId(1, 2);
    planet.Frame = 2;
    planet.RadiusMetres = 100.0;
    planet.GravitationalParameter = 1000.0;
    PlanetProjectSettings settings;
    settings.Gravity = PlanetGravityMode::Radial;
    settings.SelectedPlanet = planet.Planet;
    settings.Planets = {planet};
    SceneRuntimePlanetAccess::ConfigureProject(*world, settings);
    auto scene = CreateRef<Scene>(AssetId::Generate(), SceneAsset::EmptyDefinition());
    auto walker = scene->CreateEntity("Queued walker");
    (void)walker.AddComponent<CharacterControllerComponent>();
    auto session = CreateRef<SceneRuntimeSession>(scene, assets, Ref<AudioSystem>{}, physics);
    (void)world->Adopt(session);
    session->Play();
    REQUIRE(session->State() == ScenePlayState::Playing);
    world->Pause();
    const auto controller =
        session->RuntimeScene()->FindEntity(walker.Id()).GetComponent<CharacterControllerComponent>();
    REQUIRE(controller->QueueDesiredMovement({1.0F, 0.0F, 0.0F}));
    const auto input = CharacterMovementAccess::Capture(*controller);
    const Vector3 customGravity{1.0F, 2.0F, 3.0F};
    session->Physics()->SetGravity(customGravity);
    const auto originalSources = session->Physics()->GravitySources();
    auto clock = SceneRuntimeEnvironmentAccess::State(*world);
    clock.Day = std::numeric_limits<std::int64_t>::max();
    clock.DayFraction = 0.75;
    clock.SecondsPerDay = 1.0;
    SceneRuntimeEnvironmentAccess::Restore(*world, clock);
    const auto before = SceneRuntimePlanetAccess::CaptureCheckpoint(*world);
    CHECK_THROWS_AS((void)world->Step(0.5F), std::overflow_error);
    const auto after = SceneRuntimePlanetAccess::CaptureCheckpoint(*world);
    CHECK(after.Clock == before.Clock);
    CHECK(after.SimulationTime == before.SimulationTime);
    CHECK(session->Physics()->Gravity() == customGravity);
    CHECK(session->Physics()->GravitySources() == originalSources);
    CHECK(session->State() == ScenePlayState::Paused);
    CHECK(CharacterMovementAccess::Capture(*controller).Count == input.Count);
    CHECK(CharacterMovementAccess::Capture(*controller).Commands == input.Commands);
    world->Close();
    scene->Close();
    physics->Close();
    scenes->Close();
    assets->Close();
    events->Close();
}
