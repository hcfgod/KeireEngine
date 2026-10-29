#include "KeireInternal/Rendering/PresentationCameraFrame.h"
#include "KeireInternal/Scenes/SceneRuntimeRenderingInternal.h"
#include "KeireTests/TestSupport.h"

#include <doctest/doctest.h>

#include <limits>
#include <stdexcept>
#include <variant>

TEST_CASE("Precise camera frames preserve local view coordinates at distant origins")
{
    for (const double distance : {0.0, 1e15, 1e40})
    {
        Keire::TransformComponent camera;
        camera.SetLocalPositionPrecise({distance, 0.0, 0.0});
        camera.SetLocalRotation({0.0F, 0.0F, 1.0F, 0.0F});
        const auto frame = Keire::Internal::BuildPresentationCameraFrame(camera);
        CHECK(frame.Origin == camera.PresentationWorldPositionPrecise());
        Keire::TransformComponent object;
        object.SetLocalPositionPrecise({distance, 0.25, 8.0});
        const auto viewPosition =
            Keire::Math::TransformPoint(Keire::Math::Multiply(frame.View, frame.RelativeTransform(object, 0.0)), {});
        CHECK(viewPosition == Keire::Vector3{0.0F, -0.25F, 8.0F});
    }
}

TEST_CASE("Precise camera frames keep previous and current origins independent")
{
    Keire::TransformComponent camera;
    camera.SetLocalPositionPrecise({1e15, 0.0, 0.0});
    Keire::TransformComponent object;
    object.SetLocalPositionPrecise({1e15 + 0.5, 0.0, 8.0});
    const auto previous = Keire::Internal::BuildPresentationCameraFrame(camera);
    const auto previousObject = previous.RelativeTransform(object, 0.0);
    camera.SetLocalPositionPrecise({1e15 + 0.25, 0.0, 0.0});
    const auto current = Keire::Internal::BuildPresentationCameraFrame(camera);
    const auto currentObject = current.RelativeTransform(object, 0.0);
    CHECK(Keire::Math::TransformPoint(previous.View, Keire::Math::TransformPoint(previousObject, {})).X == 0.5F);
    CHECK(Keire::Math::TransformPoint(current.View, Keire::Math::TransformPoint(currentObject, {})).X == 0.25F);
    CHECK(previous.Origin.X == 1e15);
    CHECK_THROWS_AS(current.RelativeTransform(object, -1.0), std::invalid_argument);
    CHECK(camera.WorldPositionPrecise().X == 1e15 + 0.25);
}

TEST_CASE("Precise presented scene cameras retain settings and runtime presentation offsets")
{
    Keire::CameraComponent settings;
    settings.SetClipPlanes(0.25F, 50000.0F);
    settings.SetVerticalFieldOfViewDegrees(75.0F);
    settings.SetOrthographicSize(32.0F);
    settings.SetClearColor({0.2F, 0.3F, 0.4F, 1.0F});
    const auto effect = Keire::AssetId::Generate();
    settings.SetFullscreenEffect(Keire::CameraEffectStage::AfterUi, effect);

    Keire::TransformComponent transform;
    transform.SetLocalPositionPrecise({1e15, 2.0, -1e15});
    const Keire::Vector3d presentedPosition{1e15 + 0.25, 2.5, -1e15 + 0.5};
    transform.SetRuntimePresentationWorldPositionPrecise(presentedPosition);
    for (const auto projection : {Keire::CameraProjection::Perspective, Keire::CameraProjection::Orthographic})
    {
        settings.SetProjection(projection);
        const auto camera = Keire::Internal::BuildPresentedSceneCamera(settings, transform, 16.0F / 9.0F);
        CHECK(camera.WorldOrigin == presentedPosition);
        CHECK(camera.WorldOrigin != transform.WorldPositionPrecise());
        CHECK(camera.View == Keire::Matrix4{});
        CHECK(camera.Projection == settings.ProjectionMatrix(16.0F / 9.0F));
        CHECK(camera.ClearColor == settings.ClearColor());
        CHECK(camera.NearPlane == settings.NearPlane());
        CHECK(camera.FarPlane == settings.FarPlane());
        CHECK(camera.FullscreenEffects == settings.FullscreenEffects());

        Keire::TransformComponent neighbor;
        neighbor.SetLocalPositionPrecise({presentedPosition.X + 0.125, presentedPosition.Y, presentedPosition.Z + 8.0});
        const auto localPosition = Keire::Math::TransformPoint(
            Keire::Math::Multiply(camera.View, neighbor.PresentationRelativeMatrix(camera.WorldOrigin, 0.0)), {});
        CHECK(localPosition == Keire::Vector3{0.125F, 0.0F, 8.0F});
    }
}

TEST_CASE("Precise transforms preserve distant local coordinates and legacy scene migration")
{
    const auto registration = Keire::CreateTransformComponentRegistration();
    auto transform = Keire::CreateRef<Keire::TransformComponent>();
    const Keire::Vector3d position{1e15 + 0.125, -1e15 - 0.25, 6371000.001};
    transform->SetLocalPositionPrecise(position);
    CHECK(transform->LocalPositionPrecise() == position);
    CHECK(transform->WorldPositionPrecise() == position);

    const auto properties = registration.Serialize(*transform);
    CHECK(std::get<Keire::Vector3d>(properties.at("position")) == position);
    auto restored = Keire::CreateRef<Keire::TransformComponent>();
    registration.Deserialize(*restored, properties, registration.SchemaVersion);
    CHECK(restored->LocalPositionPrecise() == position);

    const Keire::ComponentPropertyBag legacy{{"position", Keire::Vector3{1.25F, -2.5F, 3.75F}}};
    const auto migrated = registration.Migrate(legacy, 1);
    registration.Deserialize(*restored, migrated, registration.SchemaVersion);
    CHECK(restored->LocalPositionPrecise() == Keire::Vector3d{1.25, -2.5, 3.75});
    CHECK_THROWS_AS(registration.Migrate(legacy, 99), std::invalid_argument);

    CHECK_THROWS_AS(transform->SetLocalPositionPrecise({std::numeric_limits<double>::infinity(), 0.0, 0.0}),
                    std::invalid_argument);
    CHECK(transform->LocalPositionPrecise() == position);

    auto distant = Keire::CreateRef<Keire::TransformComponent>();
    distant->SetLocalPositionPrecise({std::numeric_limits<double>::max(), 0, 0});
    CHECK_THROWS_AS((void)distant->InverseTransformPointPrecise({-std::numeric_limits<double>::max(), 0, 0}),
                    std::out_of_range);
}

TEST_CASE("Precise simulation matrices retain hierarchy and ignore presentation overrides")
{
    auto scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(), Keire::SceneAsset::EmptyDefinition());
    const auto parent = scene->CreateEntity("Simulation reference");
    const auto parentTransform = parent.GetComponent<Keire::TransformComponent>();
    const Keire::Vector3d origin{1e15, -1e15, 6371000.0};
    parentTransform->SetLocalPositionPrecise(origin);
    parentTransform->SetLocalScale({2.0F, 4.0F, 0.5F});
    parentTransform->SetLocalRotation({0.0F, 0.0F, 1.0F, 0.0F});
    const auto child = scene->CreateEntity("Simulated body", parent).GetComponent<Keire::TransformComponent>();
    child->SetLocalPositionPrecise({0.125, -0.25, 2.0});
    const auto matrix = child->WorldRelativeMatrix(origin, 0.0);
    CHECK(Keire::Math::TransformPoint(matrix, {}) == Keire::Vector3{-0.25F, 1.0F, 1.0F});
    CHECK(Keire::Math::TransformDirection(matrix, {1.0F, 1.0F, 1.0F}) == Keire::Vector3{-2.0F, -4.0F, 0.5F});
    child->SetRuntimePresentationWorldPositionPrecise({origin.X + 8.0, origin.Y, origin.Z});
    CHECK(child->WorldRelativeMatrix(origin, 0.0) == matrix);
    CHECK(child->PresentationRelativeMatrix(origin, 0.0) != matrix);
    CHECK_THROWS_AS((void)child->WorldRelativeMatrix(origin, -1.0), std::invalid_argument);
    CHECK_THROWS_AS((void)child->WorldRelativeMatrix({}, 0.0), std::out_of_range);
}

TEST_CASE("Precise presentation matrices preserve distant positions and reject invalid replacements atomically")
{
    Keire::TransformComponent transform;
    transform.SetLocalPositionPrecise({1e15, 0.0, 0.0});
    const Keire::Vector3d position{1e15 + 0.25, 2.0, 3.0};
    const auto matrix = Keire::Math::ComposeTransform({99.0F, 0.0F, 0.0F}, {}, {2.0F, 3.0F, 4.0F});
    transform.SetRuntimePresentationWorldMatrix(matrix, position);
    CHECK(transform.PresentationWorldPositionPrecise() == position);
    CHECK(transform.WorldPositionPrecise() == Keire::Vector3d{1e15, 0.0, 0.0});
    const auto relative = transform.PresentationRelativeMatrix({1e15, 0.0, 0.0}, 0.0);
    CHECK(Keire::Math::TransformPoint(relative, {}) == Keire::Vector3{0.25F, 2.0F, 3.0F});
    CHECK(Keire::Math::TransformDirection(relative, {1.0F, 1.0F, 1.0F}) == Keire::Vector3{2.0F, 3.0F, 4.0F});
    CHECK_THROWS_AS(
        transform.SetRuntimePresentationWorldMatrix(matrix, {std::numeric_limits<double>::infinity(), 0.0, 0.0}),
        std::invalid_argument);
    CHECK(transform.PresentationRelativeMatrix({1e15, 0.0, 0.0}, 0.0) == relative);
    transform.SetRuntimePresentationWorldMatrix(matrix);
    CHECK(transform.PresentationWorldPositionPrecise() == Keire::Vector3d{99.0, 0.0, 0.0});
}

TEST_CASE("Precise hierarchy world edits and preserved reparent retain sub-metre offsets")
{
    auto scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(), Keire::SceneAsset::EmptyDefinition());
    auto parent = scene->CreateEntity("Distant reference");
    auto parentTransform = parent.GetComponent<Keire::TransformComponent>();
    parentTransform->SetLocalPositionPrecise({1e15, -1e15, 6371000.0});
    parentTransform->SetLocalScale({2.0F, 4.0F, 0.5F});

    auto child = scene->CreateEntity("Traveller");
    auto transform = child.GetComponent<Keire::TransformComponent>();
    child.SetParent(parent, false);
    transform->SetLocalPositionPrecise({0.125, -0.25, 2.0});
    const Keire::Vector3d initial{1e15 + 0.25, -1e15 - 1.0, 6371001.0};
    CHECK(transform->WorldPositionPrecise() == initial);

    const Keire::Vector3d moved{1e15 + 0.5, -1e15 - 2.0, 6371002.0};
    transform->SetWorldPositionPrecise(moved);
    CHECK(transform->WorldPositionPrecise() == moved);
    CHECK(transform->LocalPositionPrecise() == Keire::Vector3d{0.25, -0.5, 4.0});

    parentTransform->SetLocalRotation({0, 0, 1, 0});
    CHECK(transform->WorldPositionPrecise() == Keire::Vector3d{1e15 - 0.5, -1e15 + 2.0, 6371002.0});
    transform->SetWorldPositionPrecise(moved);
    CHECK(transform->LocalPositionPrecise() == Keire::Vector3d{-0.25, 0.5, 4.0});
    CHECK(transform->WorldPositionPrecise() == moved);

    child.SetParent({}, true);
    CHECK(transform->LocalPositionPrecise() == moved);
    child.SetParent(parent, true);
    CHECK(transform->WorldPositionPrecise() == moved);
    CHECK(transform->LocalPositionPrecise() == Keire::Vector3d{-0.25, 0.5, 4.0});

    const auto encoded = Keire::SceneAsset::Encode(scene->Snapshot());
    auto loaded =
        Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(), Keire::SceneAsset::Decode(encoded)->Definition());
    CHECK(loaded->FindEntity(child.Id()).GetComponent<Keire::TransformComponent>()->WorldPositionPrecise() == moved);
    loaded->Close();
    scene->Close();
}

TEST_CASE("Precise scene snapshots retain authoring coordinates beyond float range")
{
    for (const Keire::Vector3d position :
         {Keire::Vector3d{1e15 + 0.125, -1e15 - 0.25, 6371000.001}, Keire::Vector3d{1e40, -1e40, 0.125}})
    {
        CAPTURE(position.X);
        auto scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(), Keire::SceneAsset::EmptyDefinition());
        const auto entity = scene->CreateEntity("Distant traveller");
        entity.GetComponent<Keire::TransformComponent>()->SetLocalPositionPrecise(position);
        const auto snapshot = scene->Snapshot();
        REQUIRE(snapshot.Objects.size() == 1);
        CHECK(snapshot.Objects.front().Transform.Position.X == position.X);
        CHECK(snapshot.Objects.front().Transform.Position.Y == position.Y);
        CHECK(snapshot.Objects.front().Transform.Position.Z == position.Z);

        const auto encoded = Keire::SceneAsset::Encode(snapshot);
        const auto decoded = Keire::SceneAsset::Decode(encoded);
        REQUIRE(decoded->Definition().Objects.size() == 1);
        CHECK(decoded->Definition().Objects.front().Transform.Position.X == position.X);
        CHECK(decoded->Definition().Objects.front().Transform.Position.Y == position.Y);
        CHECK(decoded->Definition().Objects.front().Transform.Position.Z == position.Z);
        auto restored = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(), decoded->Definition());
        CHECK(restored->FindEntity(entity.Id()).GetComponent<Keire::TransformComponent>()->LocalPositionPrecise() ==
              position);
        restored->Close();
        scene->Close();
    }
}

TEST_CASE("Precise presentation preserves distant fractional interpolation and relative matrices")
{
    Keire::TransformComponent transform;
    const Keire::Vector3d origin{1e15, -1e15, 6371000.0};
    transform.SetLocalPositionPrecise(origin);
    transform.SetFixedPresentationInterpolation(true);
    transform.BeginRuntimeFixedPresentationSample();
    transform.SetLocalPositionPrecise({origin.X + 0.5, origin.Y + 1.0, origin.Z + 2.0});
    transform.EndRuntimeFixedPresentationSample();
    transform.ApplyRuntimeFixedPresentation(0.5F);
    CHECK(transform.PresentationWorldPositionPrecise() ==
          Keire::Vector3d{origin.X + 0.25, origin.Y + 0.5, origin.Z + 1.0});
    const auto relative = transform.PresentationRelativeMatrix(origin, 0.0);
    CHECK(relative.Elements[12] == 0.25F);
    CHECK(relative.Elements[13] == 0.5F);
    CHECK(relative.Elements[14] == 1.0F);
    transform.ResetPresentationInterpolation();
    CHECK(transform.PresentationWorldPositionPrecise() == transform.WorldPositionPrecise());
    CHECK_THROWS_AS(transform.PresentationRelativeMatrix({std::numeric_limits<double>::infinity(), 0, 0}, 0.0),
                    std::invalid_argument);
}

TEST_CASE("Precise presentation composes distant rotated scaled parents before narrowing")
{
    auto scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(), Keire::SceneAsset::EmptyDefinition());
    auto parent = scene->CreateEntity("Reference");
    auto root = parent.GetComponent<Keire::TransformComponent>();
    const Keire::Vector3d origin{1e15, 0.0, 0.0};
    root->SetLocalPositionPrecise(origin);
    root->SetLocalScale({2.0F, 4.0F, 1.0F});
    root->SetLocalRotation({0.0F, 0.0F, 1.0F, 0.0F});
    auto child = scene->CreateEntity("Traveller", parent).GetComponent<Keire::TransformComponent>();
    child->SetLocalPositionPrecise({0.125, 0.25, 0.5});
    CHECK(child->PresentationWorldPositionPrecise() == Keire::Vector3d{origin.X - 0.25, -1.0, 0.5});
    const auto relative = child->PresentationRelativeMatrix(origin, 0.0);
    CHECK(relative.Elements[12] == -0.25F);
    CHECK(relative.Elements[13] == -1.0F);
    CHECK(relative.Elements[14] == 0.5F);
    scene->Close();
}

TEST_CASE("Precise fixed presentation supports coordinates beyond float range")
{
    Keire::TransformComponent transform;
    transform.SetLocalPositionPrecise({1e40, -1e40, 0.0});
    transform.SetFixedPresentationInterpolation(true);
    transform.BeginRuntimeFixedPresentationSample();
    transform.SetLocalPositionPrecise({1e40, -1e40, 2.0});
    transform.EndRuntimeFixedPresentationSample();
    REQUIRE_NOTHROW(transform.ApplyRuntimeFixedPresentation(0.5F));
    const auto relative = transform.PresentationRelativeMatrix({1e40, -1e40, 0.0}, 0.0);
    CHECK(relative.Elements[12] == 0.0F);
    CHECK(relative.Elements[13] == 0.0F);
    CHECK(relative.Elements[14] == 1.0F);
    CHECK_THROWS_AS(transform.PresentationWorldMatrix(), std::out_of_range);
}

TEST_CASE("Precise presentation overrides preserve gameplay pose and reject invalid updates")
{
    Keire::TransformComponent transform;
    transform.SetLocalPositionPrecise({1e40, 0.0, 0.0});
    transform.SetLocalRotation({0.0F, 0.0F, 1.0F, 0.0F});
    transform.SetLocalScale({2.0F, 3.0F, 4.0F});
    const Keire::Vector3d presented{1e40, 0.25, 0.5};
    transform.SetRuntimePresentationWorldPositionPrecise(presented);
    CHECK(transform.PresentationWorldPositionPrecise() == presented);
    const auto relative = transform.PresentationRelativeMatrix({1e40, 0.0, 0.0}, 0.0);
    CHECK(relative.Elements[0] == -2.0F);
    CHECK(relative.Elements[5] == -3.0F);
    CHECK(relative.Elements[10] == 4.0F);
    CHECK(relative.Elements[13] == 0.25F);
    CHECK_NOTHROW(transform.PresentationWorldRotation());
    CHECK_THROWS_AS(
        transform.SetRuntimePresentationWorldPositionPrecise({std::numeric_limits<double>::quiet_NaN(), 0.0, 0.0}),
        std::invalid_argument);
    CHECK(transform.PresentationWorldPositionPrecise() == presented);
    CHECK(transform.WorldPositionPrecise() == Keire::Vector3d{1e40, 0.0, 0.0});
    transform.ResetPresentationInterpolation();
    CHECK(transform.PresentationWorldPositionPrecise() == transform.WorldPositionPrecise());
}

TEST_CASE("Precise presentation follows interpolated parents and reset invalidates descendants")
{
    auto scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(), Keire::SceneAsset::EmptyDefinition());
    auto parent = scene->CreateEntity("Moving reference");
    auto root = parent.GetComponent<Keire::TransformComponent>();
    auto child = scene->CreateEntity("Traveller", parent).GetComponent<Keire::TransformComponent>();
    const Keire::Vector3d origin{1e15, 0.0, 0.0};
    root->SetLocalPositionPrecise(origin);
    child->SetLocalPositionPrecise({0.125, 0.0, 0.0});
    root->SetFixedPresentationInterpolation(true);
    root->BeginRuntimeFixedPresentationSample();
    root->SetLocalPositionPrecise({origin.X + 1.0, 0.0, 0.0});
    root->EndRuntimeFixedPresentationSample();
    for (const auto alpha : {0.0F, 0.5F, 1.0F})
    {
        root->ApplyRuntimeFixedPresentation(alpha);
        const auto relative = child->PresentationRelativeMatrix(origin, 0.0);
        CHECK(relative.Elements[12] == alpha + 0.125F);
    }
    root->ApplyRuntimeFixedPresentation(0.5F);
    const auto revision = child->PresentationResetRevision();
    root->ResetPresentationInterpolation();
    CHECK(child->PresentationResetRevision() > revision);
    CHECK(child->PresentationRelativeMatrix(origin, 0.0).Elements[12] == 1.125F);
    scene->Close();
}
