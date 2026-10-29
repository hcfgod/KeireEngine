#include "KeireClient/Editor/SceneCameraController.h"

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>

TEST_CASE("Scene camera v5 retains transported orientation and migrates legacy navigation")
{
    struct CameraFile
    {
        std::filesystem::path Path = std::filesystem::temp_directory_path() /
                                     ("KeirePlanetCamera-" + Keire::AssetId::Generate().ToString() + ".state");

        ~CameraFile()
        {
            std::error_code ignored;
            std::filesystem::remove(Path, ignored);
        }
    } file;
    KeireEditor::SceneCameraController camera;
    auto state = camera.State();
    state.Focus = {1.0e12 + 0.25, -1.0e12 + 0.5, 1.0e12 + 0.75};
    state.NavigationToWorld = Keire::Detail::FrameRotation::Normalize(1, 2, 3, 4);
    state.SurfaceNavigation = true;
    camera.SetState(state);
    REQUIRE(camera.Save(file.Path));
    KeireEditor::SceneCameraController restored;
    REQUIRE(restored.Load(file.Path));
    CHECK(restored.State().Focus == state.Focus);
    CHECK(restored.State().SurfaceNavigation);
    for (std::size_t axis = 0; axis < 4; ++axis)
    {
        CHECK(restored.State().NavigationToWorld.Components()[axis] ==
              doctest::Approx(state.NavigationToWorld.Components()[axis]));
    }

    SUBCASE("v4 camera files preserve legacy world-up defaults")
    {
        std::ofstream(file.Path, std::ios::trunc) << "4\n1000000000000 2 3\n40 -20 10\n10 8 0 0\n";
        REQUIRE(restored.Load(file.Path));
        CHECK_FALSE(restored.State().SurfaceNavigation);
        CHECK(restored.State().Focus.X == 1.0e12);
        CHECK(restored.State().NavigationToWorld.Components() == Keire::Detail::FrameRotation{}.Components());
    }
    SUBCASE("malformed v5 orientation leaves the existing document camera and lock intact")
    {
        const auto locked = Keire::EntityId::Generate();
        restored.SetLockedEntity(locked);
        std::ofstream(file.Path, std::ios::trunc) << "5\n0 0 0\n0 0 10\n10 8 0 0\n0 0 0 0 1\n";
        CHECK_FALSE(restored.Load(file.Path));
        CHECK(restored.State().Focus == state.Focus);
        CHECK(restored.LockedEntity() == locked);
    }
}
