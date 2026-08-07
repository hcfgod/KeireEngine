#include "KeireHub/HubProductUi.h"

#include "KeireHub/HubModalUi.h"

#include <algorithm>
#include <ranges>

namespace KeireHub
{
    void HubProductUi::DrawAccountDialog(Keire::UiFrame& ui, const HubProductSnapshot& snapshot, HubUiCommand& command)
    {
        if (!snapshot.AccountSignedIn)
            m_AccountProfileSynchronized = false;
        if (m_RequestAccountDialog)
        {
            m_RequestAccountDialog = false;
            if (m_AccountEmail.empty())
                m_AccountEmail = snapshot.AccountEmail;
            ui.OpenPopup("Kéire Account");
        }
        PrepareHubModal(ui, snapshot.AccountSignedIn ? Keire::UiSize{560.0F, 410.0F} : Keire::UiSize{560.0F, 480.0F});
        HubModalStyleScope modalStyle(ui, m_Tokens);
        auto dialog = ui.BeginPopupModal("Kéire Account", nullptr, HubModalWindowOptions(), false);
        if (!dialog)
            return;

        DrawHubModalHeader(ui, m_Tokens, snapshot.AccountSignedIn ? "Your Kéire profile" : "Sign in to Kéire",
                           snapshot.AccountSignedIn
                               ? "Manage the profile used by this Hub. Projects and installs remain local."
                               : "Use your Kéire account across Hub sessions. An account is optional.",
                           "KÉIRE ACCOUNT");

        const auto statusColor = !snapshot.AccountConfigured            ? m_Tokens.Warning
                                 : snapshot.AccountConfirmationRequired ? m_Tokens.Warning
                                 : snapshot.AccountSignedIn             ? m_Tokens.Success
                                                                        : m_Tokens.Accent;
        [[maybe_unused]] const auto statusBackground =
            ui.PushStyleColor(Keire::UiStyleColorRole::ChildBackground, m_Tokens.Elevated);
        if (auto status = ui.BeginChild("AccountStatus", {0.0F, 68.0F}, true); status)
        {
            ui.TextColored(statusColor, snapshot.AccountSignedIn ? "Signed in" : "Account status");
            ui.TextColoredWrapped(m_Tokens.SecondaryText,
                                  snapshot.AccountMessage.empty()
                                      ? "Authentication is ready. Your editor installations are never account-locked."
                                      : snapshot.AccountMessage);
        }
        ui.Spacing();

        if (!snapshot.AccountConfigured)
        {
            ui.TextColoredWrapped(m_Tokens.Warning,
                                  "Accounts are unavailable in this Hub package. Projects and editor installs "
                                  "continue to work without an account.");
        }
        else if (snapshot.AccountSignedIn)
        {
            if (!m_AccountProfileSynchronized)
            {
                m_AccountEmail = snapshot.AccountEmail;
                m_AccountDisplayName = snapshot.AccountDisplayName;
                m_AccountProfileSynchronized = true;
            }
            ui.TextColored(m_Tokens.MutedText, "EMAIL");
            ui.TextColored(m_Tokens.PrimaryText, snapshot.AccountEmail);
            ui.Spacing();
            ui.TextColored(m_Tokens.SecondaryText, "Display name");
            ui.SetNextItemWidth(ui.ContentAvailable().Width);
            (void)ui.InputText("##AccountDisplayName", m_AccountDisplayName);
            if (!snapshot.AccountPersistentSessionAvailable)
            {
                ui.TextColoredWrapped(m_Tokens.Warning,
                                      "Secure session persistence is unavailable on this platform. You will need "
                                      "to sign in again after restarting the Hub.");
            }
            ui.Spacing();
            if (auto disabled = ui.BeginDisabled(snapshot.AccountBusy); disabled)
            {
                if (HubPrimaryButton(ui, m_Tokens, snapshot.AccountBusy ? "Saving..." : "Save profile",
                                     {126.0F, 38.0F}))
                {
                    command = {.Type = HubUiCommandType::SaveAccountProfile,
                               .AccountDisplayName = m_AccountDisplayName};
                }
                ui.SameLine();
                if (HubSecondaryButton(ui, m_Tokens, "Sign out", {96.0F, 38.0F}))
                    command.Type = HubUiCommandType::AccountSignOut;
            }
        }
        else
        {
            ui.TextColored(m_Tokens.SecondaryText, "Email address");
            ui.SetNextItemWidth(ui.ContentAvailable().Width);
            (void)ui.InputText("##AccountEmail", m_AccountEmail);
            ui.TextColored(m_Tokens.SecondaryText, "Password");
            ui.SetNextItemWidth(ui.ContentAvailable().Width);
            (void)ui.InputPassword("##AccountPassword", m_AccountPassword);
            if (snapshot.AccountConfirmationRequired)
            {
                ui.TextColoredWrapped(m_Tokens.Warning,
                                      "Email confirmation is required before this account can sign in.");
            }
            ui.Spacing();
            if (auto disabled = ui.BeginDisabled(snapshot.AccountBusy || !snapshot.AccountConfigured); disabled)
            {
                const auto issueCommand = [&](const HubUiCommandType type)
                {
                    command = {.Type = type, .AccountEmail = m_AccountEmail, .AccountPassword = m_AccountPassword};
                    std::ranges::fill(m_AccountPassword, '\0');
                    m_AccountPassword.clear();
                };
                if (HubPrimaryButton(ui, m_Tokens, snapshot.AccountBusy ? "Signing in..." : "Sign in", {120.0F, 38.0F}))
                    issueCommand(HubUiCommandType::AccountSignIn);
                ui.SameLine();
                if (HubSecondaryButton(ui, m_Tokens, "Create account", {142.0F, 38.0F}))
                    issueCommand(HubUiCommandType::AccountSignUp);
            }
        }
        ui.Spacing();
        ui.Separator();
        if (HubSecondaryButton(ui, m_Tokens, "Close", {88.0F, 36.0F}))
            ui.CloseCurrentPopup();
    }
} // namespace KeireHub
