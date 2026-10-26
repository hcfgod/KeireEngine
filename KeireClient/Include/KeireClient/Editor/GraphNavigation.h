#pragma once

#include "Keire/Rendering/MaterialGraph.h"
#include "Keire/Rendering/ShaderGraph.h"
#include "KeireClient/Editor/AuthoringWidgets.h"

#include <filesystem>
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
        [[nodiscard]] bool Remove(std::string_view name);
        [[nodiscard]] bool Rename(std::string_view name, std::string_view replacement);
        [[nodiscard]] static bool ValidName(std::string_view name) noexcept;
        [[nodiscard]] std::string NextName() const;
        /// Loads local project preferences once per asset/system; never writes graph source data.
        [[nodiscard]] bool Open(const std::filesystem::path& directory, Keire::AssetId asset,
                                Keire::AssetId system = {});
        [[nodiscard]] std::string_view Diagnostic() const noexcept { return m_Diagnostic; }
        [[nodiscard]] std::optional<NodeGraphViewport> Find(std::string_view name) const noexcept;
        [[nodiscard]] std::span<const GraphBookmark> Bookmarks() const noexcept { return m_Bookmarks; }
        /// Drops transient binding/state without deleting saved preferences.
        void Clear() noexcept;

      private:
        void Persist();

        std::vector<GraphBookmark> m_Bookmarks;
        std::filesystem::path m_Directory;
        std::filesystem::path m_Path;
        Keire::AssetId m_Asset;
        Keire::AssetId m_System;
        std::string m_Diagnostic;
        bool m_StorageBlocked = false;
    };

    struct GraphBookmarkMenuState
    {
        std::string Name;
        std::string Selected;
    };

    [[nodiscard]] bool DrawGraphBookmarkMenu(Keire::UiFrame& ui, GraphBookmarkSet& bookmarks,
                                             StableNodeGraphCanvas& canvas, GraphBookmarkMenuState& state);
    [[nodiscard]] std::optional<Keire::AssetId>
    ResolveShaderGraphDiagnosticNode(const Keire::ShaderGraphDefinition& definition,
                                     const Keire::ShaderGraphDiagnostic& diagnostic) noexcept;
    [[nodiscard]] std::optional<Keire::AssetId>
    ResolveMaterialGraphDiagnosticNode(const Keire::MaterialGraphDefinition& definition,
                                       const Keire::MaterialGraphDiagnostic& diagnostic) noexcept;
} // namespace KeireEditor
