#include "Keire/Core.h"
#include "KeireInternal/Scenes/CharacterMovementAccess.h"

#include <doctest/doctest.h>

#include <cmath>

TEST_CASE("Prepared child controller inherits accepted parent motion and drops blocked parent motion")
{
    using namespace Keire;
    bool blocked = false;
    bool childMoves = true;
    SUBCASE("parent translation and child command") {}
    SUBCASE("parent translation with no child command") { childMoves = false; }
    SUBCASE("crossing blocks parent and dependent child") { blocked = true; }
    for (const double offset : {0.0, 1.0e12, -1.0e12})
    {
        CAPTURE(offset);
        const auto scene = CreateRef<Scene>(AssetId::Generate(), SceneAsset::EmptyDefinition());
        auto parent = scene->CreateEntity("Parent walker");
        auto child = scene->CreateEntity("Child walker");
        const Vector3d origin{offset + 0.125, 0.25, offset - 0.5};
        parent.GetComponent<TransformComponent>()->SetWorldPositionPrecise(origin);
        child.GetComponent<TransformComponent>()->SetParent(parent, false);
        child.GetComponent<TransformComponent>()->SetLocalPositionPrecise({0.0, 4.0, 0.0});
        (void)parent.AddComponent<CharacterControllerComponent>();
        (void)child.AddComponent<CharacterControllerComponent>();
        Entity obstacle;
        if (blocked)
        {
            obstacle = scene->CreateEntity("Crossing walker");
            obstacle.GetComponent<TransformComponent>()->SetWorldPositionPrecise(
                {origin.X + 1.0, origin.Y, origin.Z - 1.0});
            (void)obstacle.AddComponent<CharacterControllerComponent>();
        }
        const auto physics = CreateRef<PhysicsSystem>(PhysicsSystemSpecification{.Mode = PhysicsMode::Enabled});
        const auto session = CreateRef<SceneRuntimeSession>(scene, Ref<AssetSystem>{}, Ref<AudioSystem>{}, physics);
        session->Play();
        REQUIRE(session->State() == ScenePlayState::Playing);
        session->Pause();
        const auto runtimeParent = session->RuntimeScene()->FindEntity(parent.Id());
        const auto runtimeChild = session->RuntimeScene()->FindEntity(child.Id());
        const auto parentController = runtimeParent.GetComponent<CharacterControllerComponent>();
        const auto childController = runtimeChild.GetComponent<CharacterControllerComponent>();
        REQUIRE(parentController->QueueDesiredMovement({2.0F, 0.0F, 0.0F}));
        if (childMoves)
        {
            REQUIRE(childController->QueueDesiredMovement({0.0F, 0.0F, 1.0F}));
        }
        if (blocked)
        {
            const auto controller =
                session->RuntimeScene()->FindEntity(obstacle.Id()).GetComponent<CharacterControllerComponent>();
            REQUIRE(controller->QueueDesiredMovement({0.0F, 0.0F, 2.0F}));
        }
        REQUIRE(session->Step(1.0F / 60.0F));
        const Vector3d expectedParent{origin.X + (blocked ? 0.0 : 2.0), origin.Y, origin.Z};
        const Vector3d expectedChild{expectedParent.X, origin.Y + 4.0, origin.Z + (!blocked && childMoves ? 1.0 : 0.0)};
        CHECK(runtimeParent.GetComponent<TransformComponent>()->WorldPositionPrecise() == expectedParent);
        CHECK(runtimeChild.GetComponent<TransformComponent>()->WorldPositionPrecise() == expectedChild);
        CHECK(Detail::CharacterMovementAccess::Capture(*parentController).Count == 0);
        CHECK(Detail::CharacterMovementAccess::Capture(*childController).Count == 0);
        for (const auto& body : session->CapturePhysicsCheckpoint())
        {
            const auto transform = session->RuntimeScene()->FindEntity(body.Entity).GetComponent<TransformComponent>();
            const auto authored = transform->WorldPositionPrecise();
            CHECK(std::abs(static_cast<double>(body.Position.X) - (authored.X - body.WorldOrigin.X)) <= 0.001);
            CHECK(std::abs(static_cast<double>(body.Position.Y) - (authored.Y - body.WorldOrigin.Y)) <= 0.001);
            CHECK(std::abs(static_cast<double>(body.Position.Z) - (authored.Z - body.WorldOrigin.Z)) <= 0.001);
        }
        REQUIRE(session->Step(1.0F / 60.0F));
        CHECK(runtimeChild.GetComponent<TransformComponent>()->WorldPositionPrecise() == expectedChild);
        session->Stop();
        physics->Close();
        scene->Close();
    }
}
