#include "Keire/Core.h"
#include "KeireInternal/Scripting/ManagedRuntimePhysics.h"

#include <doctest/doctest.h>

#include <stdexcept>

TEST_CASE("managed character gravity samples its owning distant runtime and rejects stale contexts")
{
    auto scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(), Keire::SceneAsset::EmptyDefinition());
    auto character = scene->CreateEntity("Gravity context");
    REQUIRE(character.AddComponent<Keire::CharacterControllerComponent>());
    character.GetComponent<Keire::TransformComponent>()->SetWorldPositionPrecise({1.0e12, 5.0, -1.0e12});
    Keire::PhysicsSystemSpecification specification;
    specification.Mode = Keire::PhysicsMode::Enabled;
    const auto physics = Keire::CreateRef<Keire::PhysicsSystem>(specification);
    const auto session = Keire::CreateRef<Keire::SceneRuntimeSession>(scene, Keire::Ref<Keire::AssetSystem>{},
                                                                      Keire::Ref<Keire::AudioSystem>{}, physics);
    session->Play();
    REQUIRE(session->State() == Keire::ScenePlayState::Playing);
    auto runtimeCharacter = session->RuntimeScene()->FindEntity(character.Id());
    const auto world = runtimeCharacter.World();
    const auto sample = [&]
    { return Keire::Detail::QueryManagedCharacterGravity(session, world, character.Id().Value()); };
    REQUIRE(session->Physics());
    session->Physics()->RebaseOrigin({1.0e12, 0.0, -1.0e12});
    session->Physics()->SetGravity({1.0F, -3.0F, 2.0F});
    REQUIRE(sample());
    CHECK(*sample() == Keire::Vector3{1.0F, -3.0F, 2.0F});
    const auto second = Keire::CreateRef<Keire::SceneRuntimeSession>(scene, Keire::Ref<Keire::AssetSystem>{},
                                                                     Keire::Ref<Keire::AudioSystem>{}, physics);
    second->Play();
    REQUIRE(second->Physics());
    const auto otherWorld = second->RuntimeScene()->FindEntity(character.Id()).World();
    second->Physics()->RebaseOrigin({1.0e12, 0.0, -1.0e12});
    second->Physics()->SetGravity({0.0F, 6.0F, 0.0F});
    CHECK_FALSE(Keire::Detail::QueryManagedCharacterGravity(second, world, character.Id().Value()));
    const auto otherGravity = Keire::Detail::QueryManagedCharacterGravity(second, otherWorld, character.Id().Value());
    REQUIRE(otherGravity);
    CHECK(*otherGravity == Keire::Vector3{0.0F, 6.0F, 0.0F});
    CHECK(*sample() == Keire::Vector3{1.0F, -3.0F, 2.0F});
    second->Stop();
    CHECK_FALSE(Keire::Detail::QueryManagedCharacterGravity(session, world + 1, character.Id().Value()));
    CHECK_FALSE(Keire::Detail::QueryManagedCharacterGravity(session, world, Keire::AssetId::Generate()));
    CHECK_FALSE(Keire::Detail::QueryManagedCharacterGravity({}, world, character.Id().Value()));
    runtimeCharacter.GetComponent<Keire::CharacterControllerComponent>()->SetEnabled(false);
    CHECK_FALSE(sample());
    runtimeCharacter.GetComponent<Keire::CharacterControllerComponent>()->SetEnabled(true);
    runtimeCharacter.SetActive(false);
    CHECK_FALSE(sample());
    runtimeCharacter.SetActive(true);
    session->Physics()->SetGravity({});
    REQUIRE(sample());
    CHECK(*sample() == Keire::Vector3{});
    runtimeCharacter.GetComponent<Keire::TransformComponent>()->SetWorldPositionPrecise(
        {1.0e12 + 100000000.125, 5.0, -1.0e12});
    CHECK_THROWS_AS((void)sample(), std::out_of_range);
    session->Stop();
    CHECK_FALSE(sample());
    physics->Close();
    scene->Close();
}
