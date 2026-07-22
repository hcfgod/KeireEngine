#include "KeireClient/Editor/EditorWindowPlacement.h"

#include "KeireInternal/FileSystem.h"

#include <cmath>
#include <fstream>
#include <sstream>
#include <string>

namespace KeireEditor
{
    namespace
    {
        constexpr std::string_view Header = "KEIRE_EDITOR_WINDOW";

        [[nodiscard]] bool Valid(const EditorWindowPlacement& placement) noexcept
        {
            constexpr std::int32_t maximumPositionMagnitude = 100000;
            return std::abs(static_cast<std::int64_t>(placement.Position.X)) <= maximumPositionMagnitude &&
                   std::abs(static_cast<std::int64_t>(placement.Position.Y)) <= maximumPositionMagnitude &&
                   placement.WindowedSize.Width >= 640 && placement.WindowedSize.Width <= 16384 &&
                   placement.WindowedSize.Height >= 480 && placement.WindowedSize.Height <= 16384 &&
                   !(placement.Maximized && placement.Mode == Keire::WindowMode::BorderlessFullscreen);
        }
    } // namespace

    std::optional<EditorWindowPlacement> LoadEditorWindowPlacement(const std::filesystem::path& path) noexcept
    {
        try
        {
            std::ifstream input(path);
            std::string header;
            std::uint32_t version = 0;
            std::int64_t x = 0;
            std::int64_t y = 0;
            std::uint64_t width = 0;
            std::uint64_t height = 0;
            std::uint32_t mode = 0;
            std::uint32_t maximized = 0;
            if (!(input >> header >> version >> x >> y >> width >> height >> mode >> maximized) || header != Header ||
                version != 1 || mode > 1 || maximized > 1)
                return std::nullopt;
            input >> std::ws;
            constexpr std::int64_t maximumPositionMagnitude = 100000;
            if (!input.eof() || x < -maximumPositionMagnitude || x > maximumPositionMagnitude ||
                y < -maximumPositionMagnitude || y > maximumPositionMagnitude || width > 16384 || height > 16384)
                return std::nullopt;
            EditorWindowPlacement placement;
            placement.Position = {static_cast<std::int32_t>(x), static_cast<std::int32_t>(y)};
            placement.WindowedSize = {static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height)};
            placement.Mode = static_cast<Keire::WindowMode>(mode);
            placement.Maximized = maximized != 0;
            return Valid(placement) ? std::optional<EditorWindowPlacement>(placement) : std::nullopt;
        }
        catch (...)
        {
            return std::nullopt;
        }
    }

    bool SaveEditorWindowPlacement(const std::filesystem::path& path, const EditorWindowPlacement& placement) noexcept
    {
        if (!Valid(placement))
            return false;
        try
        {
            std::ostringstream output;
            output << Header << " 1\n"
                   << placement.Position.X << ' ' << placement.Position.Y << ' ' << placement.WindowedSize.Width << ' '
                   << placement.WindowedSize.Height << '\n'
                   << static_cast<std::uint32_t>(placement.Mode) << ' '
                   << static_cast<std::uint32_t>(placement.Maximized) << '\n';
            std::filesystem::create_directories(path.parent_path());
            Keire::Detail::WriteTextFileAtomically(path, output.str());
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    void PrepareEditorWindow(const EditorWindowPlacement& placement, Keire::WindowSpecification& specification)
    {
        if (!Valid(placement))
            throw std::invalid_argument("Editor window placement is invalid.");
        specification.Width = placement.WindowedSize.Width;
        specification.Height = placement.WindowedSize.Height;
        specification.Visible = false;
        specification.Maximized = false;
        specification.Mode = Keire::WindowMode::Windowed;
    }
} // namespace KeireEditor
