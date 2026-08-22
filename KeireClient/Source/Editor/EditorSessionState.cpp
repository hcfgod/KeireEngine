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
        constexpr std::uint32_t SchemaVersion = 2;
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
            if (!(input >> header >> version >> scene) || header != Header ||
                (version != 1 && version != SchemaVersion))
                return {};
            bool maximizeGameOnPlay = false;
            if (version >= 2)
            {
                std::uint32_t value = 0;
                if (!(input >> value) || value > 1)
                    return {};
                maximizeGameOnPlay = value != 0;
            }
            input >> std::ws;
            if (!input.eof())
                return {};
            return {.LastScene = scene == "none" ? Keire::AssetId{} : Keire::AssetId::Parse(scene),
                    .MaximizeGameOnPlay = maximizeGameOnPlay};
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
                   << (state.LastScene ? state.LastScene.ToString() : std::string("none")) << '\n'
                   << static_cast<std::uint32_t>(state.MaximizeGameOnPlay) << '\n';
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
