#include "Keire/Math/Math.h"
#include "KeireClient/Editor/ScenePicker.h"
#include <doctest/doctest.h>
#include <stdexcept>

TEST_CASE("viewport asset placement projects the mouse onto the ground with a finite horizon fallback")
{
    Keire::RenderCamera camera;
    camera.View = Keire::Math::LookAt({0.0F, 5.0F, 5.0F}, {}, {0.0F, 1.0F, 0.0F});
    camera.Projection = Keire::Math::Perspective(60.0F, 1.0F, 0.1F, 100.0F);
    const Keire::UiItemRect viewport{{50.0F, 30.0F}, {250.0F, 230.0F}};
    const auto center = KeireEditor::ResolveSceneDropPosition(viewport, {150.0F, 130.0F}, camera);
    CHECK(center.X == doctest::Approx(0.0F).epsilon(0.001));
    CHECK(center.Y == doctest::Approx(0.0F).epsilon(0.001));
    CHECK(center.Z == doctest::Approx(0.0F).epsilon(0.001));
    const auto right = KeireEditor::ResolveSceneDropPosition(viewport, {200.0F, 130.0F}, camera);
    const auto viewProjection = Keire::Math::Multiply(camera.Projection, camera.View);
    const auto clip = Keire::Math::TransformPoint(viewProjection, right);
    const auto& matrix = viewProjection.Elements;
    const float w = matrix[3] * right.X + matrix[7] * right.Y + matrix[11] * right.Z + matrix[15];
    CHECK(clip.X / w == doctest::Approx(0.5F));
    CHECK(right.Y == doctest::Approx(0.0F));
    CHECK_THROWS_AS((void)KeireEditor::ResolveSceneDropPosition({}, {}, camera), std::invalid_argument);
    CHECK_THROWS_AS((void)KeireEditor::ResolveSceneDropPosition(viewport, {}, camera), std::invalid_argument);
    camera.View = Keire::Math::LookAt({0.0F, 2.0F, 5.0F}, {0.0F, 2.0F, 0.0F}, {0.0F, 1.0F, 0.0F});
    const auto horizon = KeireEditor::ResolveSceneDropPosition(viewport, {150.0F, 130.0F}, camera);
    CHECK(horizon.Y == doctest::Approx(2.0F));
    CHECK(horizon.Z == doctest::Approx(-5.0F));
}

TEST_CASE("precise viewport placement retains distant offsets and the absolute ground plane")
{
    Keire::RenderCamera camera;
    camera.WorldOrigin = {1.0e15, 5.0, -1.0e15};
    camera.View = Keire::Math::LookAt({0.25F, 0.0F, 5.0F}, {0.25F, -5.0F, 0.0F}, {0.0F, 1.0F, 0.0F});
    camera.Projection = Keire::Math::Perspective(60.0F, 1.0F, 0.1F, 100.0F);
    const Keire::UiItemRect viewport{{0.0F, 0.0F}, {200.0F, 200.0F}};
    const auto ground = KeireEditor::ResolveSceneDropPositionPrecise(viewport, {100.0F, 100.0F}, camera);
    CHECK(ground.X - camera.WorldOrigin.X == 0.25);
    CHECK(ground.Y == doctest::Approx(0.0).epsilon(0.001));
    CHECK(ground.Z - camera.WorldOrigin.Z == 0.0);

    camera.View = Keire::Math::LookAt({0.25F, 0.0F, 5.0F}, {0.25F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F});
    const auto horizon = KeireEditor::ResolveSceneDropPositionPrecise(viewport, {100.0F, 100.0F}, camera);
    CHECK(horizon.X - camera.WorldOrigin.X == 0.25);
    CHECK(horizon.Y == 5.0);
    CHECK(horizon.Z - camera.WorldOrigin.Z == -5.0);
}

TEST_CASE("viewport prefab placement follows elevated visible mesh bounds instead of global ground")
{
    auto scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(), Keire::SceneAsset::EmptyDefinition());
    auto floor = scene->CreateEntity("Elevated floor");
    auto transform = floor.GetComponent<Keire::TransformComponent>();
    const Keire::Vector3d origin{1.0e9, 100.0, -1.0e9};
    transform->SetLocalPositionPrecise(origin);
    transform->SetLocalScale({20.0F, 1.0F, 20.0F});
    auto renderer = floor.AddComponent<Keire::MeshRendererComponent>();
    Keire::RenderCamera camera;
    camera.WorldOrigin = origin;
    camera.View = Keire::Math::LookAt({0.0F, 10.0F, 10.0F}, {}, {0.0F, 1.0F, 0.0F});
    camera.Projection = Keire::Math::Perspective(60.0F, 1.0F, 0.1F, 100.0F);
    const Keire::UiItemRect viewport{{0.0F, 0.0F}, {200.0F, 200.0F}};
    const auto hit = KeireEditor::ResolveSceneDropPositionPrecise(viewport, {100.0F, 100.0F}, camera, scene, {});
    CHECK(hit.X == doctest::Approx(origin.X));
    CHECK(hit.Y == doctest::Approx(origin.Y + 0.5).epsilon(0.00001));
    CHECK(hit.Z - origin.Z == doctest::Approx(0.5).epsilon(0.001));
    renderer->SetVisible(false);
    const auto hidden = KeireEditor::ResolveSceneDropPositionPrecise(viewport, {100.0F, 100.0F}, camera, scene, {});
    const auto fallback = KeireEditor::ResolveSceneDropPositionPrecise(viewport, {100.0F, 100.0F}, camera);
    CHECK(hidden == fallback);
    scene->Close();
}
