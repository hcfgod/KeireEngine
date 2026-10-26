#include "KeireClient/Editor/GraphNavigation.h"
#include "KeireInternal/FileSystem.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <locale>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace KeireEditor
{
    bool GraphBookmarkSet::ValidName(const std::string_view name) noexcept
    {
        return !name.empty() && name.size() <= 64U && name.find("##") == std::string_view::npos &&
               std::ranges::all_of(name,
                                   [](const unsigned char character) { return character >= 32 && character != 127; }) &&
               std::ranges::any_of(name, [](const unsigned char character) { return character != ' '; });
    }

    void GraphBookmarkSet::Save(const std::string_view name, const NodeGraphViewport viewport)
    {
        if (!ValidName(name))
        {
            throw std::invalid_argument(
                "Graph bookmark names require 1 to 64 UTF-8 bytes without controls or ## markers.");
        }
        if (!std::isfinite(viewport.Pan.X) || !std::isfinite(viewport.Pan.Y) || !std::isfinite(viewport.Zoom) ||
            viewport.Zoom < 0.2F || viewport.Zoom > 2.5F)
            throw std::invalid_argument("Graph bookmark viewport is invalid.");
        if (const auto found = std::ranges::find(m_Bookmarks, name, &GraphBookmark::Name); found != m_Bookmarks.end())
        {
            found->Viewport = viewport;
            Persist();
            return;
        }
        if (m_Bookmarks.size() >= MaximumBookmarks)
            throw std::invalid_argument("Graph bookmark limit is 9.");
        m_Bookmarks.push_back({std::string(name), viewport});
        Persist();
    }

    bool GraphBookmarkSet::Remove(const std::string_view name)
    {
        const auto found = std::ranges::find(m_Bookmarks, name, &GraphBookmark::Name);
        if (found == m_Bookmarks.end())
        {
            return false;
        }
        m_Bookmarks.erase(found);
        Persist();
        return true;
    }

    bool GraphBookmarkSet::Rename(const std::string_view name, const std::string_view replacement)
    {
        if (!ValidName(replacement))
        {
            throw std::invalid_argument("Graph bookmark replacement name is invalid.");
        }
        const auto found = std::ranges::find(m_Bookmarks, name, &GraphBookmark::Name);
        if (found == m_Bookmarks.end() || name == replacement)
        {
            return false;
        }
        if (Find(replacement))
        {
            throw std::invalid_argument("A graph bookmark already uses that name.");
        }
        found->Name = replacement;
        Persist();
        return true;
    }

    std::string GraphBookmarkSet::NextName() const
    {
        for (std::size_t index = 1; index <= MaximumBookmarks; ++index)
        {
            auto name = "Bookmark " + std::to_string(index);
            if (!Find(name))
            {
                return name;
            }
        }
        return {};
    }

    void GraphBookmarkSet::Clear() noexcept
    {
        m_Bookmarks.clear();
        m_Directory.clear();
        m_Path.clear();
        m_Asset = {};
        m_System = {};
        m_Diagnostic.clear();
        m_StorageBlocked = false;
    }

    bool GraphBookmarkSet::Open(const std::filesystem::path& directory, const Keire::AssetId asset,
                                const Keire::AssetId system)
    {
        if (m_Directory == directory && m_Asset == asset && m_System == system)
        {
            return false;
        }
        Clear();
        m_Directory = directory;
        m_Asset = asset;
        m_System = system;
        if (directory.empty() || !asset)
        {
            return true;
        }
        m_Path = directory / (asset.ToString() + (system ? "." + system.ToString() : std::string{}) + ".views");
        try
        {
            if (!std::filesystem::exists(directory))
            {
                return true;
            }
            const Keire::Detail::AnchoredFileSystem storage(directory);
            if (!storage.Exists(m_Path.filename()))
            {
                return true;
            }
            constexpr std::size_t maximumBytes = 16U * 1024U;
            const auto bytes = storage.Read(m_Path.filename(), maximumBytes);
            if (bytes.empty())
            {
                throw std::invalid_argument("Saved graph views are empty.");
            }
            std::istringstream input(std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
            input.imbue(std::locale::classic());
            std::string header;
            unsigned int version = 0;
            std::size_t count = 0;
            if (!(input >> header >> version >> count) || header != "KEIRE_GRAPH_VIEWS" || version != 1 ||
                count > MaximumBookmarks)
            {
                throw std::invalid_argument("Saved graph view header/count is invalid.");
            }
            GraphBookmarkSet loaded;
            for (std::size_t index = 0; index < count; ++index)
            {
                std::string name;
                NodeGraphViewport viewport;
                if (!(input >> std::quoted(name) >> viewport.Pan.X >> viewport.Pan.Y >> viewport.Zoom) ||
                    loaded.Find(name))
                {
                    throw std::invalid_argument("Saved graph views contain invalid or duplicate entries.");
                }
                loaded.Save(name, viewport);
            }
            input >> std::ws;
            if (!input.eof())
            {
                throw std::invalid_argument("Saved graph views contain unexpected trailing content.");
            }
            m_Bookmarks = std::move(loaded.m_Bookmarks);
        }
        catch (const std::exception& error)
        {
            m_StorageBlocked = true;
            m_Diagnostic = "Saved graph views could not be read; the original file is preserved. New views are "
                           "session-only. Move it aside to "
                           "enable saving again, then reopen the graph: " +
                           Keire::Detail::PathToUtf8(m_Path) + " (" + error.what() + ")";
        }
        return true;
    }

    void GraphBookmarkSet::Persist()
    {
        if (m_Path.empty() || m_StorageBlocked)
        {
            return;
        }
        try
        {
            std::ostringstream output;
            output.imbue(std::locale::classic());
            output << "KEIRE_GRAPH_VIEWS 1 " << m_Bookmarks.size() << '\n';
            output << std::setprecision(std::numeric_limits<float>::max_digits10);
            for (const auto& bookmark : m_Bookmarks)
            {
                output << std::quoted(bookmark.Name) << ' ' << bookmark.Viewport.Pan.X << ' ' << bookmark.Viewport.Pan.Y
                       << ' ' << bookmark.Viewport.Zoom << '\n';
            }
            std::filesystem::create_directories(m_Directory);
            const Keire::Detail::AnchoredFileSystem storage(m_Directory);
            const auto text = output.str();
            storage.WriteFileAtomically(m_Path.filename(), std::as_bytes(std::span(text.data(), text.size())));
            m_Diagnostic.clear();
        }
        catch (const std::exception& error)
        {
            m_Diagnostic = "Views are available for this session but could not be saved: " + std::string(error.what());
        }
    }

    std::optional<NodeGraphViewport> GraphBookmarkSet::Find(const std::string_view name) const noexcept
    {
        const auto found = std::ranges::find(m_Bookmarks, name, &GraphBookmark::Name);
        return found == m_Bookmarks.end() ? std::nullopt : std::optional(found->Viewport);
    }
} // namespace KeireEditor
