#include "KeireClient/Editor/ScenePlayChangesPanel.h"

namespace KeireEditor
{
    ScenePlayDecision ScenePlayChangesPanel::Draw(Keire::UiFrame& ui, ScenePlayChangeSet& changes)
    {
        if (!m_ReviewPending)
            return ScenePlayDecision::None;
        if (m_OpenRequested)
            ui.OpenPopup("Play Mode Changes");
        auto popup = ui.BeginPopupModal("Play Mode Changes");
        if (!popup)
            return ScenePlayDecision::None;
        m_OpenRequested = false;

        ui.Text("Choose the runtime changes to keep in the edit scene.");
        ui.TextColored({0.65F, 0.68F, 0.75F, 1.0F},
                       "Editor changes are selected by default; simulation-only changes are not.");
        ui.Separator();
        if (ui.Button("Select All"))
            changes.SetAllSelected(true);
        ui.SameLine();
        if (ui.Button("Select None"))
            changes.SetAllSelected(false);
        ui.Separator();

        Keire::AssetId currentEntity;
        Keire::ComponentTypeId currentComponent;
        for (const auto& change : changes.Changes())
        {
            if (change.Entity != currentEntity)
            {
                currentEntity = change.Entity;
                currentComponent = {};
                ui.TextColored({0.35F, 0.75F, 1.0F, 1.0F}, change.Entity ? change.EntityName : std::string("Scene"));
            }
            if (change.Component && change.Component != currentComponent)
            {
                currentComponent = change.Component;
                ui.TextColored({0.72F, 0.74F, 0.80F, 1.0F}, "  " + change.ComponentName);
            }
            bool selected = change.Selected;
            const auto origin = change.Origin == ScenePlayChangeOrigin::Editor  ? " [Editor]"
                                : change.Origin == ScenePlayChangeOrigin::Mixed ? " [Mixed]"
                                                                                : " [Runtime]";
            const auto label = "    " + change.Label + ": " + change.Before + " -> " + change.After + origin;
            if (ui.Checkbox(label, selected) && !change.Locked)
                changes.SetSelected(change.Id, selected);
            if (change.Locked && !change.LockReason.empty())
                ui.TextColored({0.88F, 0.68F, 0.30F, 1.0F}, "      " + change.LockReason);
            if (!change.Conflict.empty())
                ui.TextColored({1.0F, 0.38F, 0.34F, 1.0F}, "      " + change.Conflict);
            if (change.CanKeepAtRoot && change.Selected)
            {
                bool keepAtRoot = change.KeepAtRoot;
                if (ui.Checkbox("      Keep child at scene root##" + std::to_string(change.Id), keepAtRoot))
                    changes.KeepCreatedEntityAtRoot(change.Entity, keepAtRoot);
            }
        }
        ui.Separator();
        if (auto disabled = ui.BeginDisabled(!changes.HasSelectedChanges()); disabled)
        {
            if (ui.Button("Apply Selected and Stop"))
            {
                Close();
                ui.CloseCurrentPopup();
                return ScenePlayDecision::Apply;
            }
        }
        ui.SameLine();
        if (ui.Button("Discard and Stop"))
        {
            Close();
            ui.CloseCurrentPopup();
            return ScenePlayDecision::Discard;
        }
        ui.SameLine();
        if (ui.Button("Cancel"))
        {
            Close();
            ui.CloseCurrentPopup();
            return ScenePlayDecision::Cancel;
        }
        return ScenePlayDecision::None;
    }
} // namespace KeireEditor
