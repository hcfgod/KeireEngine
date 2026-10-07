#include "Keire/Assets/Asset.h"
#include "KeireClient/Editor/EditorProjectPicker.h"

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <system_error>

namespace
{
    struct ProjectPickerFixture final
    {
        std::filesystem::path Root =
            std::filesystem::temp_directory_path() / ("Keire Project Picker " + Keire::AssetId::Generate().ToString());

        ProjectPickerFixture() { std::filesystem::create_directories(Root); }
        ~ProjectPickerFixture()
        {
            std::error_code error;
            std::filesystem::remove_all(Root, error);
        }
    };

    std::filesystem::path ExecutableName(const char* name)
    {
        std::filesystem::path result(name);
#if defined(_WIN32)
        result += ".exe";
#endif
        return result;
    }
} // namespace

TEST_CASE("Editor project picker resolves installed and development companions with a stable working directory")
{
    ProjectPickerFixture fixture;
    auto editor = fixture.Root / "bin" / ExecutableName("KeireClient");
    auto hub = fixture.Root / "bin" / ExecutableName("KeireHub");
    SUBCASE("Installed package") {}
    SUBCASE("Development build")
    {
        editor = fixture.Root / "KeireClient" / ExecutableName("KeireClient");
        hub = fixture.Root / "KeireHub" / ExecutableName("KeireHub");
    }
    std::filesystem::create_directories(hub.parent_path());
    std::ofstream(hub) << "Companion fixture; never executed";
    REQUIRE(std::filesystem::is_regular_file(hub));
    int calls = 0;
    KeireEditor::OpenEditorProjectPicker(
        editor,
        [&](const auto& executable, const auto arguments, const auto& workingDirectory, auto&)
        {
            ++calls;
            CHECK(executable == hub);
            CHECK(workingDirectory == hub.parent_path());
            REQUIRE(arguments.size() == 2);
            CHECK(arguments[0] == "--navigate");
            CHECK(arguments[1] == "projects");
            return true;
        });
    CHECK(calls == 1);
}

TEST_CASE("Editor project picker reports missing companions before trying to launch")
{
    ProjectPickerFixture fixture;
    bool launched = false;
    CHECK_THROWS_WITH_AS(KeireEditor::OpenEditorProjectPicker(fixture.Root / "bin" / ExecutableName("KeireClient"),
                                                              [&](const auto&, const auto, const auto&, auto&)
                                                              {
                                                                  launched = true;
                                                                  return true;
                                                              }),
                         doctest::Contains("Repair the Editor installation"), std::runtime_error);
    CHECK_FALSE(launched);
}

TEST_CASE("Editor project picker preserves launch failure guidance")
{
    ProjectPickerFixture fixture;
    const auto hub = fixture.Root / ExecutableName("KeireHub");
    std::ofstream(hub) << "Companion fixture; never executed";
    CHECK_THROWS_WITH_AS(KeireEditor::OpenEditorProjectPicker(fixture.Root / ExecutableName("KeireClient"),
                                                              [](const auto&, const auto, const auto&, auto& diagnostic)
                                                              {
                                                                  diagnostic = "Fixture launch refusal";
                                                                  return false;
                                                              }),
                         doctest::Contains("Open Kéire Hub and select your project. Fixture launch refusal"),
                         std::runtime_error);
}
