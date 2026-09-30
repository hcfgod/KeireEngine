#include "KeireClient/Editor/EditorSessionState.h"

#include <doctest/doctest.h>

#include <chrono>
#include <filesystem>
#include <fstream>

namespace
{
    class TemporaryDirectory final
    {
      public:
        TemporaryDirectory()
            : Path(std::filesystem::temp_directory_path() /
                   ("Keire-EditorSession-" +
                    std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())))
        {
            std::filesystem::create_directories(Path);
        }

        ~TemporaryDirectory()
        {
            std::error_code ignored;
            std::filesystem::remove_all(Path, ignored);
        }

        std::filesystem::path Path;
    };
} // namespace

TEST_CASE("Editor session state round trips the last scene and Play Mode view preference")
{
    TemporaryDirectory directory;
    const auto path = directory.Path / "EditorSession.state";
    const KeireEditor::EditorSessionState state{
        .LastScene = Keire::AssetId::Parse("60000000-0000-4000-8000-000000000006"), .MaximizeGameOnPlay = true};
    REQUIRE(KeireEditor::SaveEditorSessionState(path, state));
    CHECK(KeireEditor::LoadEditorSessionState(path) == state);
}

TEST_CASE("Editor session preference updates preserve the last scene after document closure")
{
    TemporaryDirectory directory;
    const auto path = directory.Path / "EditorSession.state";
    REQUIRE(KeireEditor::SaveEditorSessionViewPreference(path, true));
    CHECK_FALSE(KeireEditor::LoadEditorSessionState(path).LastScene);
    CHECK(KeireEditor::LoadEditorSessionState(path).MaximizeGameOnPlay);

    const auto scene = Keire::AssetId::Parse("60000000-0000-4000-8000-000000000006");
    REQUIRE(KeireEditor::SaveEditorSessionState(path, {.LastScene = scene}));
    for (const bool maximize : {true, false, true})
    {
        REQUIRE(KeireEditor::SaveEditorSessionViewPreference(path, maximize));
        const auto state = KeireEditor::LoadEditorSessionState(path);
        CHECK(state.LastScene == scene);
        CHECK(state.MaximizeGameOnPlay == maximize);
    }
    CHECK_FALSE(KeireEditor::SaveEditorSessionViewPreference({}, true));
    CHECK_FALSE(KeireEditor::SaveEditorSessionViewPreference(path / "blocked.state", true));
    CHECK(KeireEditor::LoadEditorSessionState(path).LastScene == scene);
}

TEST_CASE("Editor session state migrates the original scene-only format")
{
    TemporaryDirectory directory;
    const auto path = directory.Path / "EditorSession.state";
    {
        std::ofstream output(path);
        output << "KEIRE_EDITOR_SESSION 1\n60000000-0000-4000-8000-000000000006\n";
    }
    const auto state = KeireEditor::LoadEditorSessionState(path);
    CHECK(state.LastScene == Keire::AssetId::Parse("60000000-0000-4000-8000-000000000006"));
    CHECK_FALSE(state.MaximizeGameOnPlay);
}

TEST_CASE("Editor session state fails closed for malformed or unsupported files")
{
    TemporaryDirectory directory;
    const auto path = directory.Path / "EditorSession.state";
    CHECK(KeireEditor::LoadEditorSessionState(path) == KeireEditor::EditorSessionState{});

    {
        std::ofstream output(path);
        output << "KEIRE_EDITOR_SESSION 99\nnot-an-asset\n";
    }
    CHECK(KeireEditor::LoadEditorSessionState(path) == KeireEditor::EditorSessionState{});
}

TEST_CASE("Editor presentation preferences preserve scene view settings and reject invalid modes")
{
    TemporaryDirectory directory;
    const auto path = directory.Path / "EditorSession.state";
    const auto scene = Keire::AssetId::Parse("60000000-0000-4000-8000-000000000006");
    REQUIRE(KeireEditor::SaveEditorSessionState(path, {.LastScene = scene, .MaximizeGameOnPlay = true}));
    for (std::uint8_t mode = 0; mode < 3; ++mode)
    {
        REQUIRE(KeireEditor::SaveEditorSessionPresentMode(path, mode));
        REQUIRE(KeireEditor::SaveEditorSessionViewPreference(path, false));
        const auto state = KeireEditor::LoadEditorSessionState(path);
        CHECK(state.LastScene == scene);
        CHECK(state.PresentMode == mode);
        CHECK_FALSE(state.MaximizeGameOnPlay);
    }
    const auto previous = KeireEditor::LoadEditorSessionState(path);
    CHECK_FALSE(KeireEditor::SaveEditorSessionPresentMode(path, 3));
    CHECK_FALSE(KeireEditor::SaveEditorSessionState(path, {.PresentMode = 42}));
    CHECK(KeireEditor::LoadEditorSessionState(path) == previous);
    CHECK_FALSE(KeireEditor::SaveEditorSessionPresentMode({}, 0));
    CHECK_FALSE(KeireEditor::SaveEditorSessionPresentMode(path / "blocked", 0));
}

TEST_CASE("Editor presentation preferences migrate schema two without inventing a requested mode")
{
    TemporaryDirectory directory;
    const auto path = directory.Path / "EditorSession.state";
    {
        std::ofstream output(path);
        output << "KEIRE_EDITOR_SESSION 2\nnone\n1\n";
    }
    const auto state = KeireEditor::LoadEditorSessionState(path);
    CHECK(state.MaximizeGameOnPlay);
    CHECK(state.PresentMode == 255);
    {
        std::ofstream output(path);
        output << "KEIRE_EDITOR_SESSION 3\nnone\n1\n3\n";
    }
    CHECK(KeireEditor::LoadEditorSessionState(path) == KeireEditor::EditorSessionState{});
}
