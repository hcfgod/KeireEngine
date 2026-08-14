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

TEST_CASE("Editor session state round trips the last scene")
{
    TemporaryDirectory directory;
    const auto path = directory.Path / "EditorSession.state";
    const KeireEditor::EditorSessionState state{.LastScene =
                                                    Keire::AssetId::Parse("60000000-0000-4000-8000-000000000006")};
    REQUIRE(KeireEditor::SaveEditorSessionState(path, state));
    CHECK(KeireEditor::LoadEditorSessionState(path) == state);
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
