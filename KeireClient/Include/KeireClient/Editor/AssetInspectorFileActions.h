#pragma once

#include "Keire/Ui.h"

#include <algorithm>
#include <string>

namespace KeireEditor
{
    enum class AssetInspectorFileAction
    {
        None,
        Rename,
        Duplicate,
        Trash
    };

    [[nodiscard]] inline AssetInspectorFileAction DrawAssetInspectorFileActions(Keire::UiFrame& ui, std::string& name)
    {
        auto action = AssetInspectorFileAction::None;
        ui.Text("Name");
        const float width = std::max(1.0F, ui.ContentAvailable().Width);
        ui.SetNextItemWidth(width);
        (void)ui.InputText("##AssetName", name);
        if (auto disabled = ui.BeginDisabled(name.empty()); disabled)
            if (ui.Button("Rename", {width, 0.0F}))
                action = AssetInspectorFileAction::Rename;
        if (ui.Button("Duplicate", {width, 0.0F}))
            action = AssetInspectorFileAction::Duplicate;
        if (ui.Button("Move to Trash", {width, 0.0F}))
            action = AssetInspectorFileAction::Trash;
        return action;
    }
} // namespace KeireEditor
