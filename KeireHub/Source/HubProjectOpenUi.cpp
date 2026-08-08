#include "KeireHub/HubProjectOpenUi.h"

#include "KeireHub/HubModalUi.h"

#include <algorithm>

namespace KeireHub
{
    HubOpenProjectAction DrawHubOpenProjectDialog(Keire::UiFrame& ui, const HubProductSnapshot& snapshot,
                                                  std::string& projectPath, const bool folderDialogPending)
    {
        const auto tokens = HubDesignTokens::For(snapshot.Settings.Appearance, HubSystemPrefersDark());
        PrepareHubModal(ui, {620.0F, 300.0F});
        HubModalStyleScope modalStyle(ui, tokens);
        auto dialog = ui.BeginPopupModal("Open Project", nullptr, HubModalWindowOptions(), false);
        if (!dialog)
            return HubOpenProjectAction::None;

        DrawHubModalHeader(ui, tokens, "Open an existing project",
                           "Select a Kéire project folder to validate and add to the Hub.", "PROJECTS");
        ui.TextColored(tokens.SecondaryText, "Project folder");
        ui.SetNextItemWidth(std::max(1.0F, ui.ContentAvailable().Width - 104.0F));
        (void)ui.InputText("##OpenProjectFolder", projectPath);
        ui.SameLine();
        if (auto disabled = ui.BeginDisabled(folderDialogPending); disabled)
        {
            if (HubSecondaryButton(ui, tokens, "Browse...", {96.0F, 38.0F}))
                return HubOpenProjectAction::Browse;
        }
        ui.Spacing();
        if (HubPrimaryButton(ui, tokens, "Open project", {124.0F, 38.0F}))
        {
            ui.CloseCurrentPopup();
            return HubOpenProjectAction::Open;
        }
        ui.SameLine();
        if (HubSecondaryButton(ui, tokens, "Cancel", {88.0F, 38.0F}))
            ui.CloseCurrentPopup();
        return HubOpenProjectAction::None;
    }
} // namespace KeireHub
