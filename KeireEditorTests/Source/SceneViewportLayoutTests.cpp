#include "KeireClient/Editor/SceneCameraController.h"
#include "KeireClient/Editor/ScenePicker.h"
#include "KeireClient/Editor/SceneViewportLayout.h"
#include "KeireClientInternal/Editor/ScenePoseBounds.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <limits>

TEST_CASE("Scene click and rectangle selection track animated bounds and stop fallback")
{
    auto scene =
        Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(), Keire::SceneAsset::EmptyDefinition("Pick pose"));
    auto entity = scene->CreateEntity("Offset pose");
    entity.AddComponent<Keire::MeshRendererComponent>()->SetMesh(Keire::AssetId::Generate());
    const KeireEditor::MeshBoundsResolver bind = [](Keire::AssetId)
    { return Keire::MeshBounds{{-0.25F, -0.25F, -0.25F}, {0.25F, 0.25F, 0.25F}}; };
    bool preview = true;
    const KeireEditor::PoseBoundsResolver pose = [&preview](const Keire::Entity&) -> std::optional<Keire::MeshBounds>
    {
        if (!preview)
            return std::nullopt;
        return Keire::MeshBounds{{-0.25F, -2.25F, -0.25F}, {0.25F, -1.75F, 0.25F}};
    };
    Keire::RenderCamera camera;
    camera.View = Keire::Math::LookAt({0.0F, 0.0F, -5.0F}, {}, {0.0F, 1.0F, 0.0F});
    camera.Projection = Keire::Math::Perspective(60.0F, 1.0F, 0.1F, 100.0F);
    const Keire::UiItemRect viewport{{0.0F, 0.0F}, {200.0F, 200.0F}};
    const Keire::UiItemRect poseRectangle{{85.0F, 155.0F}, {115.0F, 185.0F}};
    CHECK(KeireEditor::PickSceneEntity(scene, viewport, {100.0F, 169.0F}, camera, bind, pose) == entity.Id());
    CHECK_FALSE(KeireEditor::PickSceneEntity(scene, viewport, {100.0F, 100.0F}, camera, bind, pose));
    CHECK(KeireEditor::SelectSceneEntitiesInRectangle(scene, viewport, poseRectangle, camera, bind, pose) ==
          std::vector{entity.Id()});
    preview = false;
    CHECK_FALSE(KeireEditor::PickSceneEntity(scene, viewport, {100.0F, 169.0F}, camera, bind, pose));
    CHECK(KeireEditor::PickSceneEntity(scene, viewport, {100.0F, 100.0F}, camera, bind, pose) == entity.Id());
    CHECK(KeireEditor::SelectSceneEntitiesInRectangle(scene, viewport, poseRectangle, camera, bind, pose).empty());
    scene->Close();
}

TEST_CASE("Scene pose bounds follow translated skinning and reject stale palettes")
{
    const Keire::MeshAsset mesh(Keire::BuiltinMesh::Cube);
    const auto meshId = Keire::AssetId::Generate();
    const auto skeletonId = Keire::AssetId::Generate();
    std::vector<Keire::SkinVertexInfluence8> influences(mesh.Vertices().size());
    for (auto& influence : influences)
    {
        influence.Count = 1;
        influence.Bones[0] = 0;
        influence.Weights[0] = 1.0F;
    }
    for (const auto method : {Keire::SkinningMethod::LinearBlend, Keire::SkinningMethod::DualQuaternion})
    {
        const auto influenceBounds =
            Keire::CalculateBindSpaceSkinInfluenceBounds(mesh.Vertices(), mesh.Indices(), mesh.Submeshes(), influences);
        const auto skin = Keire::SkinnedMeshAsset::Decode(
            Keire::SkinnedMeshAsset::Encode(meshId, skeletonId, influences, method,
                                            static_cast<std::uint32_t>(mesh.Submeshes().size()), influenceBounds));
        std::vector palette{Keire::Math::ComposeTransform({0.0F, -5.0F, 2.0F}, {}, {1.0F, 1.0F, 1.0F})};
        const auto bounds = KeireEditor::CalculateScenePoseBounds(mesh, *skin, palette);
        REQUIRE(bounds);
        CHECK(bounds->Minimum.Y == doctest::Approx(mesh.Bounds().Minimum.Y - 5.0F));
        CHECK(bounds->Maximum.Z == doctest::Approx(mesh.Bounds().Maximum.Z + 2.0F));
        CHECK_FALSE(KeireEditor::CalculateScenePoseBounds(mesh, *skin, {}));
        palette[0].Elements[0] = std::numeric_limits<float>::quiet_NaN();
        CHECK_FALSE(KeireEditor::CalculateScenePoseBounds(mesh, *skin, palette));
    }
    for (auto& influence : influences)
        influence.Bones[0] = 1;
    const Keire::SkinnedMeshAsset staleSkin(meshId, skeletonId, influences, Keire::SkinningMethod::LinearBlend);
    const std::vector palette{Keire::Math::ComposeTransform({}, {}, {1.0F, 1.0F, 1.0F})};
    CHECK_FALSE(KeireEditor::CalculateScenePoseBounds(mesh, staleSkin, palette));
}

TEST_CASE("Scene framing resolves posed descendants and falls back after preview stops")
{
    auto scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(), Keire::SceneAsset::EmptyDefinition("Pose"));
    auto entity = scene->CreateEntity("Animated model");
    auto renderer = entity.AddComponent<Keire::MeshRendererComponent>();
    renderer->SetMesh(Keire::AssetId::Generate());
    entity.GetComponent<Keire::TransformComponent>()->SetLocalPosition({10.0F, 0.0F, 0.0F});
    const KeireEditor::MeshBoundsResolver bind = [](Keire::AssetId)
    { return Keire::MeshBounds{{-1.0F, -1.0F, -1.0F}, {1.0F, 1.0F, 1.0F}}; };
    bool preview = true;
    const KeireEditor::PoseBoundsResolver pose = [&preview](const Keire::Entity&) -> std::optional<Keire::MeshBounds>
    {
        if (!preview)
            return std::nullopt;
        return Keire::MeshBounds{{-1.0F, -6.0F, -1.0F}, {1.0F, -4.0F, 1.0F}};
    };
    CHECK(KeireEditor::CalculateSceneEntityBounds(entity, bind, pose).Center() == Keire::Vector3{10.0F, -5.0F, 0.0F});
    preview = false;
    CHECK(KeireEditor::CalculateSceneEntityBounds(entity, bind, pose).Center() == Keire::Vector3{10.0F, 0.0F, 0.0F});
    preview = true;
    auto parent = scene->CreateEntity("Group");
    entity.SetParent(parent, false);
    CHECK(KeireEditor::CalculateSceneEntityBounds(parent, bind, pose).Minimum.Y == -6.0F);
    renderer->SetEnabled(false);
    CHECK(KeireEditor::CalculateSceneEntityBounds(entity, bind, pose).Minimum.Y == doctest::Approx(-0.15F));
    CHECK_FALSE(KeireEditor::ResolveScenePoseBounds({}, entity));
    scene->Close();
}

TEST_CASE("Scene framing keeps small creature bounds visible at authored scale")
{
    const KeireEditor::SceneEntityBounds bounds{
        {-0.016335F, 0.002263F, -0.010132F}, {0.016339F, 0.028187F, -0.000563F}, true};
    REQUIRE(bounds.Radius() < 0.025F);
    REQUIRE(bounds.Radius() > 0.02F);
    KeireEditor::SceneCameraController camera;
    camera.Frame(bounds.Center(), bounds.Radius(), 1.0F);
    CHECK(camera.State().Distance < 0.07F);
    for (const auto projection :
         {Keire::Detail::EditorCameraProjection::Perspective, Keire::Detail::EditorCameraProjection::Orthographic})
    {
        if (camera.State().Projection != projection)
            camera.ToggleProjection();
        const auto matrix = Keire::Math::Multiply(camera.ProjectionMatrix(1.0F), camera.ViewMatrix());
        const auto& m = matrix.Elements;
        float extent = 0.0F;
        for (int corner = 0; corner < 8; ++corner)
        {
            const Keire::Vector3 p{corner & 1 ? bounds.Maximum.X : bounds.Minimum.X,
                                   corner & 2 ? bounds.Maximum.Y : bounds.Minimum.Y,
                                   corner & 4 ? bounds.Maximum.Z : bounds.Minimum.Z};
            const float x = m[0] * p.X + m[4] * p.Y + m[8] * p.Z + m[12];
            const float y = m[1] * p.X + m[5] * p.Y + m[9] * p.Z + m[13];
            const float z = m[2] * p.X + m[6] * p.Y + m[10] * p.Z + m[14];
            const float w = m[3] * p.X + m[7] * p.Y + m[11] * p.Z + m[15];
            REQUIRE(w > 0.0F);
            CHECK(z >= 0.0F);
            CHECK(z <= w);
            CHECK(std::abs(x / w) <= 0.8F);
            CHECK(std::abs(y / w) <= 0.8F);
            extent = std::max({extent, std::abs(x / w), std::abs(y / w)});
        }
        CHECK(extent > 0.3F);
    }
    CHECK(KeireEditor::SceneEntityBounds{}.Radius() == 0.0F);
    const KeireEditor::SceneEntityBounds point{{}, {}, true};
    CHECK(point.Radius() == 0.25F);
}

TEST_CASE("Scene viewport right toolbar drops optional controls before overlapping the gizmo toolbar")
{
    const Keire::UiItemRect leftToolbar{{8.0F, 8.0F}, {284.0F, 36.0F}};

    const auto wide =
        KeireEditor::CalculateSceneViewportRightToolbarLayout({{0.0F, 0.0F}, {520.0F, 550.0F}}, leftToolbar);
    CHECK(wide.ButtonCount == 7U);
    CHECK(wide.ShowAxes);
    CHECK(wide.ShowOcclusionMetadata);
    CHECK(wide.Rectangle.Minimum.X > leftToolbar.Maximum.X);

    const auto ordinaryNarrow =
        KeireEditor::CalculateSceneViewportRightToolbarLayout({{0.0F, 0.0F}, {480.0F, 550.0F}}, leftToolbar);
    CHECK(ordinaryNarrow.ButtonCount == 6U);
    CHECK(ordinaryNarrow.ShowAxes);
    CHECK(ordinaryNarrow.ShowOcclusionVisibility);
    CHECK_FALSE(ordinaryNarrow.ShowOcclusionMetadata);
    CHECK(ordinaryNarrow.Rectangle.Minimum.X > leftToolbar.Maximum.X);

    const auto narrow =
        KeireEditor::CalculateSceneViewportRightToolbarLayout({{0.0F, 0.0F}, {440.0F, 550.0F}}, leftToolbar);
    CHECK(narrow.ButtonCount == 4U);
    CHECK_FALSE(narrow.ShowAxes);
    CHECK(narrow.ShowCameraPreview);
    CHECK(narrow.ShowOcclusionVisibility);
    CHECK(narrow.ShowOcclusionMetadata);
    CHECK(narrow.Rectangle.Minimum.X > leftToolbar.Maximum.X);
}

TEST_CASE("Scene occlusion diagnostics avoid performance and preview reservations")
{
    const Keire::UiItemRect viewport{{0.0F, 0.0F}, {1200.0F, 550.0F}};
    const Keire::UiItemRect performance{{870.0F, 48.0F}, {1188.0F, 285.0F}};
    const auto diagnostics = KeireEditor::PlaceSceneOcclusionDiagnostics(viewport, 328.0F, performance);
    REQUIRE(diagnostics);
    CHECK(diagnostics->Maximum.X <= performance.Minimum.X - 8.0F);
    CHECK(diagnostics->Minimum.Y >= viewport.Minimum.Y + 48.0F);
    CHECK(diagnostics->Maximum.Y <= 328.0F);

    const auto tooShort = KeireEditor::PlaceSceneOcclusionDiagnostics({{0.0F, 0.0F}, {440.0F, 180.0F}}, 48.0F);
    CHECK_FALSE(tooShort);

    const auto noAvailableSlot = KeireEditor::PlaceSceneOcclusionDiagnostics(
        {{0.0F, 0.0F}, {440.0F, 350.0F}}, 218.0F, Keire::UiItemRect{{110.0F, 48.0F}, {428.0F, 285.0F}});
    CHECK_FALSE(noAvailableSlot);
}

TEST_CASE("Scene performance overlays avoid the main camera preview")
{
    const Keire::UiItemRect viewport{{0.0F, 0.0F}, {1200.0F, 400.0F}};
    const Keire::UiItemRect preview{{868.0F, 186.0F}, {1188.0F, 366.0F}};
    const auto advanced =
        KeireEditor::PlaceViewportPerformanceOverlay(viewport, {318.0F, 237.0F}, 48.0F, true, preview);
    REQUIRE(advanced);
    CHECK_FALSE(KeireEditor::SceneViewportRectanglesOverlap(*advanced, preview));
    CHECK(advanced->Maximum.X < preview.Minimum.X);

    const Keire::UiItemRect narrowViewport{{0.0F, 0.0F}, {380.0F, 400.0F}};
    const Keire::UiItemRect narrowPreview{{208.0F, 276.0F}, {368.0F, 366.0F}};
    CHECK_FALSE(
        KeireEditor::PlaceViewportPerformanceOverlay(narrowViewport, {318.0F, 237.0F}, 48.0F, true, narrowPreview));
    const auto compact =
        KeireEditor::PlaceViewportPerformanceOverlay(narrowViewport, {166.0F, 44.0F}, 48.0F, false, narrowPreview);
    REQUIRE(compact);
    CHECK_FALSE(KeireEditor::SceneViewportRectanglesOverlap(*compact, narrowPreview));

    const auto statusReserved =
        KeireEditor::PlaceViewportPerformanceOverlay(viewport, {318.0F, 319.0F}, 48.0F, true, preview, 34.0F);
    CHECK_FALSE(statusReserved);
}

TEST_CASE("Scene camera preview fits between viewport toolbars and status")
{
    const auto ordinary = KeireEditor::PlaceSceneCameraPreview({{0.0F, 0.0F}, {1200.0F, 400.0F}});
    REQUIRE(ordinary);
    CHECK(ordinary->Minimum.Y >= 48.0F);
    CHECK(ordinary->Maximum.Y <= 366.0F);

    CHECK_FALSE(KeireEditor::PlaceSceneCameraPreview({{0.0F, 0.0F}, {440.0F, 150.0F}}));
    CHECK_FALSE(KeireEditor::PlaceSceneCameraPreview({{0.0F, 0.0F}, {440.0F, 100.0F}}));
}

TEST_CASE("Empty Scene actions force a non-overlapping compact performance overlay")
{
    const Keire::UiItemRect viewport{{0.0F, 0.0F}, {900.0F, 500.0F}};
    const auto centered = KeireEditor::CalculateSceneViewportCenteredStateLayout(viewport, 400.0F, true);
    CHECK(centered.ShowActions);
    CHECK_FALSE(KeireEditor::PlaceViewportPerformanceOverlay(viewport, {318.0F, 289.0F}, 48.0F, true,
                                                             centered.Reservation, 34.0F));
    const auto compact = KeireEditor::PlaceViewportPerformanceOverlay(viewport, {166.0F, 44.0F}, 48.0F, false,
                                                                      centered.Reservation, 34.0F);
    REQUIRE(compact);
    CHECK_FALSE(KeireEditor::SceneViewportRectanglesOverlap(*compact, centered.Reservation));

    const auto shortState =
        KeireEditor::CalculateSceneViewportCenteredStateLayout({{0.0F, 0.0F}, {440.0F, 100.0F}}, 400.0F, true);
    CHECK_FALSE(shortState.ShowActions);
    CHECK(shortState.Reservation.Minimum.Y >= 8.0F);
    CHECK(shortState.Reservation.Maximum.Y <= 92.0F);

    const Keire::UiItemRect unavailableViewport{{0.0F, 0.0F}, {700.0F, 550.0F}};
    const auto rendererUnavailable =
        KeireEditor::CalculateSceneViewportCenteredStateLayout(unavailableViewport, 440.0F, false);
    CHECK_FALSE(KeireEditor::PlaceViewportPerformanceOverlay(unavailableViewport, {318.0F, 234.0F}, 48.0F, true,
                                                             rendererUnavailable.Reservation, 34.0F));
}

TEST_CASE("Scene status hides before overlapping the top toolbar")
{
    CHECK_FALSE(KeireEditor::CanPlaceSceneViewportStatus({{0.0F, 0.0F}, {440.0F, 60.0F}}));
    CHECK(KeireEditor::CanPlaceSceneViewportStatus({{0.0F, 0.0F}, {440.0F, 100.0F}}));
}
