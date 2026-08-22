#pragma once

#include "KeireClient/Editor/AuthoringWidgets.h"

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace KeireEditor
{
    struct GraphBookmark
    {
        std::string Name;
        NodeGraphViewport Viewport;

        bool operator==(const GraphBookmark&) const = default;
    };

    class GraphBookmarkSet final
    {
      public:
        static constexpr std::size_t MaximumBookmarks = 9;

        void Save(std::string_view name, NodeGraphViewport viewport);
        [[nodiscard]] std::optional<NodeGraphViewport> Find(std::string_view name) const noexcept;
        [[nodiscard]] std::span<const GraphBookmark> Bookmarks() const noexcept { return m_Bookmarks; }
        void Clear() noexcept { m_Bookmarks.clear(); }

      private:
        std::vector<GraphBookmark> m_Bookmarks;
    };

    [[nodiscard]] bool DrawGraphBookmarkMenu(Keire::UiFrame& ui, GraphBookmarkSet& bookmarks,
                                             StableNodeGraphCanvas& canvas);
} // namespace KeireEditor
