#include "KeireClient/Editor/GraphNavigation.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace KeireEditor
{
    void GraphBookmarkSet::Save(const std::string_view name, const NodeGraphViewport viewport)
    {
        if (name.empty() || name.size() > 64U)
            throw std::invalid_argument("Graph bookmark name must contain 1 to 64 characters.");
        if (!std::isfinite(viewport.Pan.X) || !std::isfinite(viewport.Pan.Y) || !std::isfinite(viewport.Zoom) ||
            viewport.Zoom < 0.2F || viewport.Zoom > 2.5F)
            throw std::invalid_argument("Graph bookmark viewport is invalid.");
        if (const auto found = std::ranges::find(m_Bookmarks, name, &GraphBookmark::Name); found != m_Bookmarks.end())
        {
            found->Viewport = viewport;
            return;
        }
        if (m_Bookmarks.size() >= MaximumBookmarks)
            throw std::invalid_argument("Graph bookmark limit is 9.");
        m_Bookmarks.push_back({std::string(name), viewport});
    }

    std::optional<NodeGraphViewport> GraphBookmarkSet::Find(const std::string_view name) const noexcept
    {
        const auto found = std::ranges::find(m_Bookmarks, name, &GraphBookmark::Name);
        return found == m_Bookmarks.end() ? std::nullopt : std::optional(found->Viewport);
    }

    bool DrawGraphBookmarkMenu(Keire::UiFrame& ui, GraphBookmarkSet& bookmarks, StableNodeGraphCanvas& canvas)
    {
        const auto label = bookmarks.Bookmarks().empty() ? std::string("No saved views")
                                                         : std::to_string(bookmarks.Bookmarks().size()) + " saved";
        if (auto menu = ui.BeginCombo("Bookmarks", label); menu)
        {
            if (auto disabled = ui.BeginDisabled(bookmarks.Bookmarks().size() >= GraphBookmarkSet::MaximumBookmarks);
                disabled)
                if (ui.Selectable("Save Current View"))
                {
                    const auto name = "Bookmark " + std::to_string(bookmarks.Bookmarks().size() + 1U);
                    bookmarks.Save(name, canvas.Viewport());
                }
            if (!bookmarks.Bookmarks().empty())
                ui.Separator();
            for (const auto& bookmark : bookmarks.Bookmarks())
                if (ui.Selectable(bookmark.Name))
                {
                    canvas.RestoreViewport(bookmark.Viewport);
                    return true;
                }
        }
        return false;
    }
} // namespace KeireEditor
