#include "KeireClient/Editor/SceneGizmoController.h"

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>

TEST_CASE("scene physics gizmo settings migrate version one state and round trip version three")
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

    migrated.SetColliderEditing(true);
    migrated.Save(root);

    KeireEditor::SceneGizmoController restored;
    restored.Load(root);
    CHECK(restored.ActiveTool() == KeireEditor::SceneTool::Rotate);
    CHECK(restored.Settings().EditColliders);
    {
        std::ifstream input(state);
        std::uint32_t version = 0;
        input >> version;
        CHECK(version == 3);
    }

    std::error_code cleanupError;
    std::filesystem::remove_all(root, cleanupError);
    CHECK_FALSE(cleanupError);
}
