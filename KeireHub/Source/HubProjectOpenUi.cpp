#include "KeireHub/HubProjectOpenUi.h"

#include "KeireHub/HubModalUi.h"
#include "KeireHub/HubProjectUiSupport.h"
#include "KeireInternal/FileSystem.h"

#include <algorithm>

namespace KeireHub
{
    HubOpenProjectAction DrawHubOpenProjectDialog(Keire::UiFrame& ui, const HubProductSnapshot& snapshot,
                                                  std::string& projectPath, std::string& errorMessage,
                                                  const bool folderDialogPending)
    {
        const auto tokens = HubDesignTokens::For(snapshot.Settings.Appearance, HubSystemPrefersDark());
        PrepareHubModal(ui, {620.0F, 360.0F});
        HubModalStyleScope modalStyle(ui, tokens);
        auto dialog = ui.BeginPopupModal("Open Project", nullptr, HubModalWindowOptions(), false);
        if (!dialog)
            return HubOpenProjectAction::None;

        DrawHubModalHeader(ui, tokens, "Open an existing project",
                           "Select a Kéire project folder to validate and add to the Hub.", "PROJECTS");
        ui.TextColored(tokens.SecondaryText, "Project folder");
        ui.SetNextItemWidth(std::max(1.0F, ui.ContentAvailable().Width - 104.0F));
        if (ui.InputText("##OpenProjectFolder", projectPath))
            errorMessage.clear();
        ui.SameLine();
        if (auto disabled = ui.BeginDisabled(folderDialogPending); disabled)
        {
            if (HubSecondaryButton(ui, tokens, "Browse...", {96.0F, 38.0F}))
                return HubOpenProjectAction::Browse;
        }
        ui.Spacing();
        if (auto disabled =
                ui.BeginDisabled(folderDialogPending || projectPath.find_first_not_of(" \t\r\n") == std::string::npos);
            disabled)
        {
            if (HubPrimaryButton(ui, tokens, "Open project", {124.0F, 38.0F}))
            {
                errorMessage = ValidateHubOpenProjectFolder(Keire::Detail::PathFromUtf8(projectPath));
                if (errorMessage.empty())
                {
                    ui.CloseCurrentPopup();
                    return HubOpenProjectAction::Open;
                }
            }
        }
        ui.SameLine();
        if (HubSecondaryButton(ui, tokens, "Cancel", {88.0F, 38.0F}))
        {
            errorMessage.clear();
            ui.CloseCurrentPopup();
        }
        if (!errorMessage.empty())
        {
            ui.Spacing();
            ui.TextWrapped(errorMessage);
        }
        return HubOpenProjectAction::None;
    }
} // namespace KeireHub
