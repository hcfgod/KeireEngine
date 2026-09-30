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
        constexpr std::uint32_t SchemaVersion = 3;
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
            if (!(input >> header >> version >> scene) || header != Header || (version < 1 || version > SchemaVersion))
                return {};
            bool maximizeGameOnPlay = false;
            if (version >= 2)
            {
                std::uint32_t value = 0;
                if (!(input >> value) || value > 1)
                    return {};
                maximizeGameOnPlay = value != 0;
            }
            std::uint32_t presentMode = 255;
            if (version >= 3 && (!(input >> presentMode) || (presentMode > 2 && presentMode != 255)))
                return {};
            input >> std::ws;
            if (!input.eof())
                return {};
            return {.LastScene = scene == "none" ? Keire::AssetId{} : Keire::AssetId::Parse(scene),
                    .MaximizeGameOnPlay = maximizeGameOnPlay,
                    .PresentMode = static_cast<std::uint8_t>(presentMode)};
        }
        catch (...)
        {
            return {};
        }
    }

    bool SaveEditorSessionPresentMode(const std::filesystem::path& path, const std::uint8_t mode) noexcept
    {
        if (mode > 2)
            return false;
        auto state = LoadEditorSessionState(path);
        state.PresentMode = mode;
        return SaveEditorSessionState(path, state);
    }

    bool SaveEditorSessionViewPreference(const std::filesystem::path& path, const bool maximizeGameOnPlay) noexcept
    {
        auto state = LoadEditorSessionState(path);
        state.MaximizeGameOnPlay = maximizeGameOnPlay;
        return SaveEditorSessionState(path, state);
    }

    bool SaveEditorSessionState(const std::filesystem::path& path, const EditorSessionState& state) noexcept
    {
        if (path.empty() || (state.PresentMode > 2 && state.PresentMode != 255))
            return false;
        try
        {
            std::ostringstream output;
            output << Header << ' ' << SchemaVersion << '\n'
                   << (state.LastScene ? state.LastScene.ToString() : std::string("none")) << '\n'
                   << static_cast<std::uint32_t>(state.MaximizeGameOnPlay) << '\n'
                   << static_cast<std::uint32_t>(state.PresentMode) << '\n';
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
