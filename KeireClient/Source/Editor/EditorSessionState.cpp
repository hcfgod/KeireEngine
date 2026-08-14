#include "KeireClient/Editor/EditorSessionState.h"

#include "KeireInternal/FileSystem.h"

#include <fstream>
#include <sstream>
#include <string>

namespace KeireEditor
{
    namespace
    {
        constexpr std::string_view Header = "KEIRE_EDITOR_SESSION";
        constexpr std::uint32_t SchemaVersion = 1;
    } // namespace

    EditorSessionState LoadEditorSessionState(const std::filesystem::path& path) noexcept
    {
        if (path.empty())
            return {};
        try
        {
            std::ifstream input(path);
            std::string header;
            std::string scene;
            std::uint32_t version = 0;
            if (!(input >> header >> version >> scene) || header != Header || version != SchemaVersion)
                return {};
            input >> std::ws;
            if (!input.eof())
                return {};
            if (scene == "none")
                return {};
            return {.LastScene = Keire::AssetId::Parse(scene)};
        }
        catch (...)
        {
            return {};
        }
    }

    bool SaveEditorSessionState(const std::filesystem::path& path, const EditorSessionState& state) noexcept
    {
        if (path.empty())
            return false;
        try
        {
            std::ostringstream output;
            output << Header << ' ' << SchemaVersion << '\n'
                   << (state.LastScene ? state.LastScene.ToString() : std::string("none")) << '\n';
            std::filesystem::create_directories(path.parent_path());
            Keire::Detail::WriteTextFileAtomically(path, output.str());
            return true;
        }
        catch (...)
        {
            return false;
        }
    }
} // namespace KeireEditor
