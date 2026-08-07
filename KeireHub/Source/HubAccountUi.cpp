#include "KeireHub/HubProductUi.h"

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
        if (auto dialog = ui.BeginPopupModal("Kéire Account"); dialog)
        {
            ui.TextColored(m_Tokens.PrimaryText, snapshot.AccountSignedIn ? "Your Kéire profile" : "Kéire account");
            ui.TextColoredWrapped(m_Tokens.SecondaryText, snapshot.AccountMessage.empty()
                                                              ? "Account identity is provided by Supabase Auth."
                                                              : snapshot.AccountMessage);
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
                ui.TextColored(m_Tokens.MutedText, snapshot.AccountEmail);
                (void)ui.InputText("Display name", m_AccountDisplayName);
                if (!snapshot.AccountPersistentSessionAvailable)
                {
                    ui.TextColoredWrapped(m_Tokens.Warning,
                                          "Secure session persistence is unavailable on this platform. You will "
                                          "need to sign in again after restarting the Hub.");
                }
                if (auto disabled = ui.BeginDisabled(snapshot.AccountBusy); disabled)
                {
                    if (ui.Button("Save profile", {104.0F, 32.0F}))
                    {
                        command = {.Type = HubUiCommandType::SaveAccountProfile,
                                   .AccountDisplayName = m_AccountDisplayName};
                    }
                    ui.SameLine();
                    if (ui.Button("Sign out", {84.0F, 32.0F}))
                        command.Type = HubUiCommandType::AccountSignOut;
                }
            }
            else
            {
                (void)ui.InputText("Email", m_AccountEmail);
                (void)ui.InputPassword("Password", m_AccountPassword);
                if (snapshot.AccountConfirmationRequired)
                {
                    ui.TextColoredWrapped(m_Tokens.Warning,
                                          "Email confirmation is required before this account can sign in.");
                }
                if (auto disabled = ui.BeginDisabled(snapshot.AccountBusy || !snapshot.AccountConfigured); disabled)
                {
                    const auto issueCommand = [&](const HubUiCommandType type)
                    {
                        command = {.Type = type, .AccountEmail = m_AccountEmail, .AccountPassword = m_AccountPassword};
                        std::ranges::fill(m_AccountPassword, '\0');
                        m_AccountPassword.clear();
                    };
                    if (ui.Button(snapshot.AccountBusy ? "Signing in..." : "Sign in", {88.0F, 32.0F}))
                        issueCommand(HubUiCommandType::AccountSignIn);
                    ui.SameLine();
                    if (ui.Button("Create account", {116.0F, 32.0F}))
                        issueCommand(HubUiCommandType::AccountSignUp);
                }
            }
            ui.Spacing();
            if (ui.Button("Close", {76.0F, 30.0F}))
                ui.CloseCurrentPopup();
        }
    }
} // namespace KeireHub
