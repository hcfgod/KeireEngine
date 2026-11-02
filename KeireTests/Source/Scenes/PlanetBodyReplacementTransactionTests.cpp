#include "Keire/Core.h"
#include "KeireInternal/Scenes/CharacterMovementAccess.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>

TEST_CASE("Later invalid character replacement preserves earlier native body identities and input")
{
    using namespace Keire;
    const auto scene = CreateRef<Scene>(AssetId::Generate(), SceneAsset::EmptyDefinition());
    std::array entities{scene->CreateEntity("First replacement"), scene->CreateEntity("Later replacement")};
    std::ranges::sort(entities, {}, &Entity::Id);
    for (std::size_t index = 0; index < entities.size(); ++index)
    {
        entities[index].GetComponent<TransformComponent>()->SetWorldPositionPrecise(
            {static_cast<double>(index) * 6.0, 0.0, 0.0});
        (void)entities[index].AddComponent<CharacterControllerComponent>();
    }
    const auto physics = CreateRef<PhysicsSystem>(PhysicsSystemSpecification{.Mode = PhysicsMode::Enabled});
    const auto session = CreateRef<SceneRuntimeSession>(scene, Ref<AssetSystem>{}, Ref<AudioSystem>{}, physics);
    session->Play();
    session->Pause();
    const PhysicsRayQuery query{
        .Origin = {-5.0F, 0.0F, 0.0F}, .Direction = {1.0F, 0.0F, 0.0F}, .MaximumDistance = 20.0F};
    const auto originalHits = session->Physics()->RayCast(query);
    REQUIRE(originalHits.size() == 2);
    const auto first = session->RuntimeScene()->FindEntity(entities[0].Id());
    const auto later = session->RuntimeScene()->FindEntity(entities[1].Id());
    const auto firstController = first.GetComponent<CharacterControllerComponent>();
    const auto laterController = later.GetComponent<CharacterControllerComponent>();
    REQUIRE(firstController->QueueDesiredMovement({0.0F, 0.0F, 1.0F}));
    REQUIRE(laterController->QueueDesiredMovement({0.0F, 0.0F, 1.0F}));
    using Access = Detail::CharacterMovementAccess;
    const auto firstInput = Access::Capture(*firstController);
    const auto laterInput = Access::Capture(*laterController);

    // Both authored transforms decompose. Only the second derived capsule is invalid:
    // its scaled diameter exceeds its unscaled height at native shape creation.
    first.GetComponent<TransformComponent>()->SetLocalScale({1.1F, 1.0F, 1.0F});
    later.GetComponent<TransformComponent>()->SetLocalScale({10.0F, 1.0F, 1.0F});
    CHECK_FALSE(session->Step(1.0F / 60.0F));
    CHECK(session->State() == ScenePlayState::Faulted);
    const auto remainingHits = session->Physics()->RayCast(query);
    REQUIRE(remainingHits.size() == originalHits.size());
    for (std::size_t index = 0; index < originalHits.size(); ++index)
    {
        CHECK(remainingHits[index].Body == originalHits[index].Body);
        CHECK(remainingHits[index].Distance == originalHits[index].Distance);
        CHECK(session->Physics()->TryGetBody(originalHits[index].Body).has_value());
    }
    CHECK(Access::Capture(*firstController).Count == firstInput.Count);
    CHECK(Access::Capture(*firstController).Commands == firstInput.Commands);
    CHECK(Access::Capture(*laterController).Count == laterInput.Count);
    CHECK(Access::Capture(*laterController).Commands == laterInput.Commands);
    session->Stop();
    physics->Close();
    scene->Close();
}
