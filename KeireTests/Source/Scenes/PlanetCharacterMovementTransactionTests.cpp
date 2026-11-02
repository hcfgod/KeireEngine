#include "Keire/Core.h"
#include "KeireInternal/Scenes/CharacterMovementAccess.h"

#include <doctest/doctest.h>

#include <algorithm>

TEST_CASE("Crossing character commands block without faulting or stopping unrelated movement")
{
    using namespace Keire;
    bool collide = true;
    SUBCASE("reciprocal layers collide") {}
    SUBCASE("one reciprocal mask excludes the other character") { collide = false; }

    for (const double offset : {0.0, 1.0e12, -1.0e12})
    {
        CAPTURE(offset);
        const Vector3d origin{offset + 0.125, -offset + 0.25, offset - 0.5};
        const auto worldPoint = [&](const double x, const double y, const double z)
        { return Vector3d{origin.X + x, origin.Y + y, origin.Z + z}; };
        const auto scene = CreateRef<Scene>(AssetId::Generate(), SceneAsset::EmptyDefinition());
        auto first = scene->CreateEntity("First walker");
        auto second = scene->CreateEntity("Second walker");
        auto third = scene->CreateEntity("Independent walker");
        third.GetComponent<TransformComponent>()->SetWorldPositionPrecise(worldPoint(10.0, 0.0, 0.0));
        (void)third.AddComponent<CharacterControllerComponent>();
        first.GetComponent<TransformComponent>()->SetWorldPositionPrecise(worldPoint(-2.0, 0.0, 0.0));
        second.GetComponent<TransformComponent>()->SetWorldPositionPrecise(worldPoint(0.0, 0.0, -2.0));
        (void)first.AddComponent<CharacterControllerComponent>();
        (void)second.AddComponent<CharacterControllerComponent>();
        if (!collide)
        {
            first.GetComponent<CharacterControllerComponent>()->SetLayer(2);
            second.GetComponent<CharacterControllerComponent>()->SetMask(1);
        }
        const auto physics = CreateRef<PhysicsSystem>(PhysicsSystemSpecification{.Mode = PhysicsMode::Enabled});
        const auto session = CreateRef<SceneRuntimeSession>(scene, Ref<AssetSystem>{}, Ref<AudioSystem>{}, physics);
        session->Play();
        REQUIRE(session->State() == ScenePlayState::Playing);
        session->Pause();
        const auto runtimeFirst = session->RuntimeScene()->FindEntity(first.Id());
        const auto runtimeSecond = session->RuntimeScene()->FindEntity(second.Id());
        const auto runtimeThird = session->RuntimeScene()->FindEntity(third.Id());
        const auto thirdController = runtimeThird.GetComponent<CharacterControllerComponent>();
        REQUIRE(thirdController->QueueDesiredMovement({1.0F, 0.0F, 0.0F}));
        const auto firstController = runtimeFirst.GetComponent<CharacterControllerComponent>();
        const auto secondController = runtimeSecond.GetComponent<CharacterControllerComponent>();
        REQUIRE(firstController->QueueDesiredMovement({4.0F, 0.0F, 0.0F}));
        REQUIRE(secondController->QueueDesiredMovement({0.0F, 0.0F, 4.0F}));
        using Access = Detail::CharacterMovementAccess;
        const auto firstPosition = runtimeFirst.GetComponent<TransformComponent>()->WorldPositionPrecise();
        const auto secondPosition = runtimeSecond.GetComponent<TransformComponent>()->WorldPositionPrecise();
        const auto originalBodies = session->CapturePhysicsCheckpoint();
        REQUIRE(originalBodies.size() == 3);

        CHECK(session->Step(1.0F / 60.0F));
        CHECK(session->State() == ScenePlayState::Paused);
        const Vector3d firstTarget = collide ? firstPosition : worldPoint(2.0, 0.0, 0.0);
        const Vector3d secondTarget = collide ? secondPosition : worldPoint(0.0, 0.0, 2.0);
        CHECK(runtimeFirst.GetComponent<TransformComponent>()->WorldPositionPrecise() == firstTarget);
        CHECK(runtimeSecond.GetComponent<TransformComponent>()->WorldPositionPrecise() == secondTarget);
        CHECK(Access::Capture(*firstController).Count == 0);
        CHECK(Access::Capture(*secondController).Count == 0);
        CHECK(Access::Capture(*thirdController).Count == 0);
        const auto thirdTarget = worldPoint(11.0, 0.0, 0.0);
        CHECK(runtimeThird.GetComponent<TransformComponent>()->WorldPositionPrecise() == thirdTarget);

        const auto blockedBodies = session->CapturePhysicsCheckpoint();
        REQUIRE(blockedBodies.size() == originalBodies.size());
        for (const auto& original : originalBodies)
        {
            if (!collide || original.Entity == third.Id())
            {
                continue;
            }
            const auto blocked =
                std::find_if(blockedBodies.begin(), blockedBodies.end(),
                             [&](const ScenePhysicsCheckpointBody& body) { return body.Entity == original.Entity; });
            REQUIRE(blocked != blockedBodies.end());
            CHECK(blocked->Position == original.Position);
            CHECK(blocked->Rotation == original.Rotation);
            CHECK(blocked->LinearVelocity == original.LinearVelocity);
            CHECK(blocked->AngularVelocity == original.AngularVelocity);
            CHECK(blocked->WorldOrigin == original.WorldOrigin);
            CHECK(blocked->MotionSettings == original.MotionSettings);
            CHECK(blocked->PendingForce == original.PendingForce);
            CHECK(blocked->PendingTorque == original.PendingTorque);
        }

        const auto resolvedFirst = runtimeFirst.GetComponent<TransformComponent>()->WorldPositionPrecise();
        const auto resolvedSecond = runtimeSecond.GetComponent<TransformComponent>()->WorldPositionPrecise();
        CHECK(resolvedFirst.X - origin.X == (collide ? -2.0 : 2.0));
        CHECK(resolvedSecond.Z - origin.Z == (collide ? -2.0 : 2.0));

        // Rebasing between ticks must neither replay consumed input nor change accepted world poses.
        session->RebasePhysicsOrigin(worldPoint(128.0, 64.0, -128.0));
        CHECK(session->Step(1.0F / 60.0F));
        CHECK(runtimeFirst.GetComponent<TransformComponent>()->WorldPositionPrecise() == firstTarget);
        CHECK(runtimeSecond.GetComponent<TransformComponent>()->WorldPositionPrecise() == secondTarget);
        CHECK(runtimeThird.GetComponent<TransformComponent>()->WorldPositionPrecise() == thirdTarget);
        CHECK(Access::Capture(*firstController).Count == 0);
        CHECK(Access::Capture(*secondController).Count == 0);
        CHECK(Access::Capture(*thirdController).Count == 0);
        session->Stop();
        physics->Close();
        scene->Close();
    }
}
