#include "KeireClient/Editor/GraphNavigation.h"

#include <algorithm>
#include <string>

namespace KeireEditor
{

    bool DrawGraphBookmarkMenu(Keire::UiFrame& ui, GraphBookmarkSet& bookmarks, StableNodeGraphCanvas& canvas,
                               GraphBookmarkMenuState& state)
    {
        const auto label = bookmarks.Bookmarks().empty()
                               ? std::string("No saved views")
                               : std::to_string(bookmarks.Bookmarks().size()) +
                                     (bookmarks.Diagnostic().empty() ? " saved" : " views (session only)");
        if (ui.Button("Bookmarks"))
        {
            ui.OpenPopup("GraphBookmarkActions");
        }
        ui.SameLine();
        ui.Text(label);
        if (auto menu = ui.BeginPopup("GraphBookmarkActions"); menu)
        {
            ui.SetNextItemWidth(240.0F);
            (void)ui.InputTextWithHint("Name", "Name this view (optional)...", state.Name);
            const auto saveName = state.Name.empty() ? bookmarks.NextName() : state.Name;
            const bool canSave =
                GraphBookmarkSet::ValidName(saveName) &&
                (bookmarks.Find(saveName) || bookmarks.Bookmarks().size() < GraphBookmarkSet::MaximumBookmarks);
            {
                auto disabled = ui.BeginDisabled(!canSave);
                if (ui.Selectable(bookmarks.Find(saveName) ? "Update Current View" : "Save Current View"))
                {
                    bookmarks.Save(saveName, canvas.Viewport());
                    state.Name = saveName;
                    state.Selected = saveName;
                }
            }
            if (!bookmarks.Bookmarks().empty())
                ui.Separator();
            {
                auto entries = ui.PushId("SavedViews");
                for (const auto& bookmark : bookmarks.Bookmarks())
                {
                    if (ui.Selectable(bookmark.Name, state.Selected == bookmark.Name))
                    {
                        state.Selected = bookmark.Name;
                        state.Name = bookmark.Name;
                        canvas.RestoreViewport(bookmark.Viewport);
                        return true;
                    }
                }
            }
            const bool selected = bookmarks.Find(state.Selected).has_value();
            const bool canRename = selected && GraphBookmarkSet::ValidName(state.Name) &&
                                   state.Name != state.Selected && !bookmarks.Find(state.Name);
            {
                auto disabled = ui.BeginDisabled(!canRename);
                if (ui.Selectable("Rename Selected"))
                {
                    (void)bookmarks.Rename(state.Selected, state.Name);
                    state.Selected = state.Name;
                }
            }
            {
                auto disabled = ui.BeginDisabled(!selected);
                if (ui.Selectable("Remove Selected"))
                {
                    (void)bookmarks.Remove(state.Selected);
                    state.Selected.clear();
                    state.Name.clear();
                }
            }
        }
        if (!bookmarks.Diagnostic().empty())
        {
            ui.TextWrapped(bookmarks.Diagnostic());
        }
        return false;
    }

    std::optional<Keire::AssetId>
    ResolveShaderGraphDiagnosticNode(const Keire::ShaderGraphDefinition& definition,
                                     const Keire::ShaderGraphDiagnostic& diagnostic) noexcept
    {
        if (diagnostic.Node &&
            std::ranges::find(definition.Nodes, diagnostic.Node, &Keire::ShaderGraphNode::Id) != definition.Nodes.end())
            return diagnostic.Node;
        if (!diagnostic.Pin)
            return std::nullopt;
        const auto node =
            std::ranges::find_if(definition.Nodes,
                                 [&](const auto& candidate)
                                 {
                                     return std::ranges::find(candidate.Pins, diagnostic.Pin,
                                                              &Keire::ShaderGraphPin::Id) != candidate.Pins.end();
                                 });
        return node == definition.Nodes.end() ? std::nullopt : std::optional(node->Id);
    }

    std::optional<Keire::AssetId>
    ResolveMaterialGraphDiagnosticNode(const Keire::MaterialGraphDefinition& definition,
                                       const Keire::MaterialGraphDiagnostic& diagnostic) noexcept
    {
        if (diagnostic.Node)
        {
            if (diagnostic.Node == definition.OutputNode ||
                std::ranges::find(definition.Nodes, diagnostic.Node, &Keire::MaterialGraphValueNode::Id) !=
                    definition.Nodes.end() ||
                std::ranges::find(definition.SurfaceGraph.Nodes, diagnostic.Node, &Keire::ShaderGraphNode::Id) !=
                    definition.SurfaceGraph.Nodes.end())
                return diagnostic.Node;
        }
        if (!diagnostic.Pin)
            return std::nullopt;
        if (std::ranges::find(definition.Properties, diagnostic.Pin, &Keire::MaterialGraphPropertyBinding::Pin) !=
            definition.Properties.end())
            return definition.OutputNode;
        if (const auto value =
                std::ranges::find(definition.Nodes, diagnostic.Pin, &Keire::MaterialGraphValueNode::OutputPin);
            value != definition.Nodes.end())
            return value->Id;
        const auto expression =
            std::ranges::find_if(definition.SurfaceGraph.Nodes,
                                 [&](const auto& candidate)
                                 {
                                     return std::ranges::find(candidate.Pins, diagnostic.Pin,
                                                              &Keire::ShaderGraphPin::Id) != candidate.Pins.end();
                                 });
        return expression == definition.SurfaceGraph.Nodes.end() ? std::nullopt : std::optional(expression->Id);
    }
} // namespace KeireEditor
