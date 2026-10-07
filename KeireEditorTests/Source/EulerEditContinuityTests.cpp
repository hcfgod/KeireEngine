#include "Keire/Scenes/Scene.h"
#include "Keire/Scenes/SceneAsset.h"
#include "KeireClient/Editor/EulerEditContinuity.h"

#include <cmath>
#include <doctest/doctest.h>

TEST_CASE("Continuous Euler editing crosses positive yaw 180 without changing the other axes")
{
    Keire::Vector3 displayed{10.0F, 170.0F, 20.0F};
    for (float yaw = 171.0F; yaw <= 190.0F; yaw += 1.0F)
    {
        displayed =
            KeireEditor::ContinuousEulerAngles(Keire::Math::EulerDegreesToQuaternion({10.0F, yaw, 20.0F}), displayed);
        CHECK(displayed.X == doctest::Approx(10.0F).epsilon(0.001));
        CHECK(displayed.Y == doctest::Approx(yaw).epsilon(0.001));
        CHECK(displayed.Z == doctest::Approx(20.0F).epsilon(0.001));
    }
}

TEST_CASE("Continuous Euler editing crosses negative yaw 180 without changing the other axes")
{
    Keire::Vector3 displayed{-12.0F, -170.0F, 35.0F};
    for (float yaw = -171.0F; yaw >= -190.0F; yaw -= 1.0F)
    {
        displayed =
            KeireEditor::ContinuousEulerAngles(Keire::Math::EulerDegreesToQuaternion({-12.0F, yaw, 35.0F}), displayed);
        CHECK(displayed.X == doctest::Approx(-12.0F).epsilon(0.001));
        CHECK(displayed.Y == doctest::Approx(yaw).epsilon(0.001));
        CHECK(displayed.Z == doctest::Approx(35.0F).epsilon(0.001));
    }
}

TEST_CASE("Quaternion sign does not look like an external rotation change")
{
    const auto rotation = Keire::Math::EulerDegreesToQuaternion({23.0F, 181.0F, -7.0F});
    CHECK(KeireEditor::SameRotation(rotation, {-rotation.X, -rotation.Y, -rotation.Z, -rotation.W}));
}

TEST_CASE("Inspector Euler display resets when Play replaces a transform with the same entity ID")
{
    const auto scene =
        Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(), Keire::SceneAsset::EmptyDefinition("Euler display"),
                                       Keire::ComponentRegistry::CreateDefault());
    const auto entity = scene->CreateEntity("Spinner");
    const auto authored = entity.GetComponent<Keire::TransformComponent>();
    KeireEditor::InspectorEulerState display;
    CHECK(display.Read(authored) == Keire::Vector3{});
    const auto session = Keire::CreateRef<Keire::SceneRuntimeSession>(scene);
    session->Play();
    const auto runtime = session->RuntimeScene()->FindEntity(entity.Id()).GetComponent<Keire::TransformComponent>();
    for (int yaw = 0; yaw <= 1440; yaw += 10)
    {
        runtime->SetLocalEulerAngles({0.0F, static_cast<float>(yaw), 0.0F});
        CHECK(display.Read(runtime).Y == doctest::Approx(static_cast<float>(yaw)).epsilon(0.001));
    }
    display.SetEditing(true);
    session->Stop();
    CHECK(display.Read(authored) == Keire::Vector3{});
    CHECK(authored->LocalEulerAngles() == Keire::Vector3{});
}

TEST_CASE("Inspector Euler display retains edited full turns without keeping its transform alive")
{
    KeireEditor::InspectorEulerState display;
    Keire::WeakRef<Keire::TransformComponent> observed;
    {
        const auto scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(),
                                                          Keire::SceneAsset::EmptyDefinition("Euler lifetime"),
                                                          Keire::ComponentRegistry::CreateDefault());
        const auto transform = scene->CreateEntity("Edited").GetComponent<Keire::TransformComponent>();
        observed = transform;
        (void)display.Read(transform);
        const Keire::Vector3 edited{0.0F, 720.0F, 0.0F};
        transform->SetLocalEulerAngles(edited);
        display.SetValue(edited, transform->LocalRotation());
        display.SetEditing(true);
        CHECK(display.Read(transform) == edited);
        display.EndEdit(transform->LocalRotation());
        CHECK(display.Read(transform) == edited);
        display.Reset();
        CHECK(std::abs(display.Read(transform).Y) < 0.001F);
    }
    CHECK_FALSE(observed.Lock());
    CHECK(display.Read({}) == Keire::Vector3{});
}
