#pragma once

#include "Keire/Rendering/MaterialGraph.h"
#include "Keire/Rendering/ShaderGraph.h"
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
    [[nodiscard]] std::optional<Keire::AssetId>
    ResolveShaderGraphDiagnosticNode(const Keire::ShaderGraphDefinition& definition,
                                     const Keire::ShaderGraphDiagnostic& diagnostic) noexcept;
    [[nodiscard]] std::optional<Keire::AssetId>
    ResolveMaterialGraphDiagnosticNode(const Keire::MaterialGraphDefinition& definition,
                                       const Keire::MaterialGraphDiagnostic& diagnostic) noexcept;
} // namespace KeireEditor
