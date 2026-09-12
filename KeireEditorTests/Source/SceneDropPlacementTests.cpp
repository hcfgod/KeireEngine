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
