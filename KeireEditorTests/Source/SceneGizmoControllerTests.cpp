#include "KeireClient/Editor/SceneGizmoController.h"

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>

TEST_CASE("scene physics and occlusion gizmo settings migrate version one state and round trip version four")
{
    const auto root =
        std::filesystem::temp_directory_path() / ("Keire-SceneTools-" + Keire::AssetId::Generate().ToString());
    const auto state = root / "Library/Editor/SceneTools.state";
    std::filesystem::create_directories(state.parent_path());
    {
        std::ofstream output(state);
        output << "1\n"
               << static_cast<std::uint32_t>(KeireEditor::SceneTool::Rotate) << '\n'
               << "0.25 0.5 1\n"
               << "30 0.2\n"
               << "1 0 1 0 1\n";
    }

    KeireEditor::SceneGizmoController migrated;
    migrated.SetColliderEditing(true);
    migrated.Load(root);
    CHECK(migrated.ActiveTool() == KeireEditor::SceneTool::Rotate);
    CHECK(migrated.Settings().Snapping);
    CHECK_FALSE(migrated.Settings().LocalSpace);
    CHECK_FALSE(migrated.Settings().ShowCameraFrustums);
    CHECK_FALSE(migrated.Settings().EditColliders);
    CHECK(migrated.Settings().ShowPhysicsGizmos);
    CHECK(migrated.Settings().OcclusionDebugView == Keire::GpuOcclusionDebugView::None);
    CHECK(migrated.Settings().OcclusionDebugMip == 0U);
    CHECK_FALSE(migrated.Settings().ShowOcclusionMetadata);

    migrated.SetColliderEditing(true);
    migrated.SetOcclusionDebugView(Keire::GpuOcclusionDebugView::HierarchicalDepth);
    migrated.SetOcclusionDebugMip(5U);
    migrated.SetShowOcclusionMetadata(true);
    migrated.Save(root);

    KeireEditor::SceneGizmoController restored;
    restored.Load(root);
    CHECK(restored.ActiveTool() == KeireEditor::SceneTool::Rotate);
    CHECK(restored.Settings().EditColliders);
    CHECK(restored.Settings().OcclusionDebugView == Keire::GpuOcclusionDebugView::None);
    CHECK(restored.Settings().OcclusionDebugMip == 0U);
    CHECK(restored.Settings().ShowOcclusionMetadata);
    {
        std::ifstream input(state);
        std::uint32_t version = 0;
        input >> version;
        CHECK(version == 4);
    }

    std::error_code cleanupError;
    std::filesystem::remove_all(root, cleanupError);
    CHECK_FALSE(cleanupError);
}

TEST_CASE("truncated occlusion gizmo state resets all debug controls safely")
{
    const auto root =
        std::filesystem::temp_directory_path() / ("Keire-SceneTools-" + Keire::AssetId::Generate().ToString());
    const auto state = root / "Library/Editor/SceneTools.state";
    std::filesystem::create_directories(state.parent_path());
    {
        std::ofstream output(state);
        output << "4\n"
               << static_cast<std::uint32_t>(KeireEditor::SceneTool::Scale) << '\n'
               << "0.5 0.5 0.5\n"
               << "15 0.1\n"
               << "0 1 1 1 1 0 1\n";
    }

    KeireEditor::SceneGizmoController controller;
    controller.SetOcclusionDebugView(Keire::GpuOcclusionDebugView::HierarchicalDepth);
    controller.SetOcclusionDebugMip(4U);
    controller.SetShowOcclusionMetadata(true);
    controller.Load(root);
    CHECK(controller.ActiveTool() == KeireEditor::SceneTool::Translate);
    CHECK(controller.Settings().OcclusionDebugView == Keire::GpuOcclusionDebugView::None);
    CHECK(controller.Settings().OcclusionDebugMip == 0U);
    CHECK_FALSE(controller.Settings().ShowOcclusionMetadata);

    std::error_code cleanupError;
    std::filesystem::remove_all(root, cleanupError);
    CHECK_FALSE(cleanupError);
}

TEST_CASE("occlusion debug mip selection clamps to live per-surface availability")
{
    KeireEditor::SceneGizmoController controller;
    controller.SetOcclusionDebugView(Keire::GpuOcclusionDebugView::HierarchicalDepth);
    controller.SetOcclusionDebugMip(9U);
    controller.ClampOcclusionDebugMip(4U);
    CHECK(controller.Settings().OcclusionDebugMip == 3U);

    controller.ClampOcclusionDebugMip(0U);
    CHECK(controller.Settings().OcclusionDebugMip == 0U);

    controller.SetOcclusionDebugMip(2U);
    controller.SetOcclusionDebugView(Keire::GpuOcclusionDebugView::VisibilityBounds);
    CHECK(controller.Settings().OcclusionDebugMip == 0U);
}
