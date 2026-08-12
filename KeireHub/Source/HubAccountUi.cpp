#include "KeireHub/HubProductUi.h"

#include "KeireHub/HubModalUi.h"

#include <algorithm>
#include <ranges>

namespace KeireHub
{
    namespace
    {
        void DrawAccountMessage(Keire::UiFrame& ui, const HubDesignTokens& tokens, const Keire::UiColor color,
                                const std::string_view eyebrow, const std::string_view message)
        {
            [[maybe_unused]] const auto background =
                ui.PushStyleColor(Keire::UiStyleColorRole::ChildBackground, tokens.Elevated);
            if (auto notice = ui.BeginChild("AccountMessage", {0.0F, 72.0F}, true); notice)
            {
                ui.TextColored(color, eyebrow);
                ui.TextColoredWrapped(tokens.SecondaryText, message);
            }
        }

        void DrawBrowserSignInCard(Keire::UiFrame& ui, const HubDesignTokens& tokens,
                                   const HubProductSnapshot& snapshot, HubUiCommand& command)
        {
            [[maybe_unused]] const auto background =
                ui.PushStyleColor(Keire::UiStyleColorRole::ChildBackground, tokens.Elevated);
            const auto height = snapshot.AccountBrowserSignInPending ? 224.0F : 190.0F;
            if (auto browser = ui.BeginChild("BrowserAccountSignIn", {0.0F, height}, true); browser)
            {
                ui.TextColored(snapshot.AccountBrowserSignInPending ? tokens.Success : tokens.Accent,
                               snapshot.AccountBrowserSignInPending ? "BROWSER CHECKPOINT" : "RECOMMENDED");
                {
                    [[maybe_unused]] const auto heading = ui.PushFont(Keire::UiFontRole::Heading);
                    ui.TextColored(tokens.PrimaryText, snapshot.AccountBrowserSignInPending
                                                           ? "Finish in your browser"
                                                           : "Continue securely in browser");
                }
                ui.TextColoredWrapped(
                    tokens.SecondaryText,
                    snapshot.AccountBrowserSignInPending
                        ? "Approve access on the Kéire website, then choose Open Kéire Hub. The Hub will validate the "
                          "state and exchange the single-use code itself."
                        : "Sign in with GitHub or your Kéire account on the website. Your browser password and cookies "
                          "never enter the Hub.");
                ui.Spacing();

                if (snapshot.AccountBrowserSignInPending)
                {
                    ui.TextColored(tokens.Success, "1  Secure authorization request created");
                    ui.TextColored(tokens.SecondaryText, "2  Approve access in the browser");
                    ui.TextColored(tokens.SecondaryText, "3  Choose Open Kéire Hub on the return page");
                    ui.Spacing();
                    if (HubSecondaryButton(ui, tokens, "Cancel browser sign-in", {174.0F, 36.0F}))
                        command.Type = HubUiCommandType::AccountCancelBrowserSignIn;
                }
                else if (auto disabled = ui.BeginDisabled(snapshot.AccountBusy); disabled)
                {
                    if (HubPrimaryButton(ui, tokens,
                                         snapshot.AccountBusy ? "Preparing secure sign-in..." : "Continue in browser",
                                         {ui.ContentAvailable().Width, 44.0F}))
                    {
                        command.Type = HubUiCommandType::AccountBeginBrowserSignIn;
                    }
                    ui.TextColored(tokens.MutedText, "PKCE protected  ·  Independent session  ·  Remotely revocable");
                }
            }
        }

        void DrawEmailFallback(Keire::UiFrame& ui, const HubDesignTokens& tokens, const HubProductSnapshot& snapshot,
                               std::string& email, std::string& password, HubUiCommand& command)
        {
            if (auto fallback = ui.BeginTreeNode("Use email and password instead"); fallback)
            {
                ui.TextColoredWrapped(tokens.MutedText,
                                      "Fallback access for staged environments and browser troubleshooting.");
                ui.Spacing();
                ui.TextColored(tokens.SecondaryText, "Email address");
                ui.SetNextItemWidth(ui.ContentAvailable().Width);
                (void)ui.InputText("##AccountEmail", email);
                ui.TextColored(tokens.SecondaryText, "Password");
                ui.SetNextItemWidth(ui.ContentAvailable().Width);
                (void)ui.InputPassword("##AccountPassword", password);
                if (snapshot.AccountConfirmationRequired)
                {
                    ui.TextColoredWrapped(tokens.Warning,
                                          "Email confirmation is required before this account can sign in.");
                }
                ui.Spacing();
                if (auto disabled = ui.BeginDisabled(snapshot.AccountBusy || !snapshot.AccountConfigured); disabled)
                {
                    const auto issueCommand = [&](const HubUiCommandType type)
                    {
                        command = {.Type = type, .AccountEmail = email, .AccountPassword = password};
                        std::ranges::fill(password, '\0');
                        password.clear();
                    };
                    if (HubPrimaryButton(ui, tokens, snapshot.AccountBusy ? "Signing in..." : "Sign in",
                                         {120.0F, 38.0F}))
                    {
                        issueCommand(HubUiCommandType::AccountSignIn);
                    }
                    ui.SameLine();
                    if (HubSecondaryButton(ui, tokens, "Create account", {142.0F, 38.0F}))
                        issueCommand(HubUiCommandType::AccountSignUp);
                }
            }
        }
    } // namespace

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

        const auto modalSize = snapshot.AccountSignedIn               ? Keire::UiSize{620.0F, 490.0F}
                               : snapshot.AccountBrowserSignInPending ? Keire::UiSize{640.0F, 570.0F}
                                                                      : Keire::UiSize{640.0F, 680.0F};
        PrepareHubModal(ui, modalSize);
        HubModalStyleScope modalStyle(ui, m_Tokens);
        auto dialog = ui.BeginPopupModal("Kéire Account", nullptr, HubModalWindowOptions(), false);
        if (!dialog)
            return;

        DrawHubModalHeader(ui, m_Tokens, snapshot.AccountSignedIn ? "Your Kéire profile" : "Connect your Kéire account",
                           snapshot.AccountSignedIn
                               ? "Manage the identity used by this Hub. Projects and editor installations remain local."
                               : "Use one identity for your profile, library, organizations, and publisher access.",
                           "IDENTITY & ACCESS");

        if (!snapshot.AccountConfigured)
        {
            DrawAccountMessage(
                ui, m_Tokens, m_Tokens.Warning, "ACCOUNT SERVICE UNAVAILABLE",
                snapshot.AccountMessage.empty()
                    ? "Accounts are unavailable in this Hub package. Local projects and editors continue "
                      "to work normally."
                    : snapshot.AccountMessage);
        }
        else if (snapshot.AccountSignedIn)
        {
            if (!m_AccountProfileSynchronized)
            {
                m_AccountEmail = snapshot.AccountEmail;
                m_AccountDisplayName = snapshot.AccountDisplayName;
                m_AccountProfileSynchronized = true;
            }

            {
                [[maybe_unused]] const auto background =
                    ui.PushStyleColor(Keire::UiStyleColorRole::ChildBackground, m_Tokens.Elevated);
                if (auto identity = ui.BeginChild("SignedInIdentity", {0.0F, 96.0F}, true); identity)
                {
                    ui.TextColored(m_Tokens.Success, "SIGNED IN");
                    {
                        [[maybe_unused]] const auto heading = ui.PushFont(Keire::UiFontRole::Heading);
                        ui.TextColored(m_Tokens.PrimaryText, snapshot.AccountEmail);
                    }
                    ui.TextColored(m_Tokens.MutedText, "Independent, revocable Kéire Hub session");
                }
            }
            ui.Spacing();
            ui.TextColored(m_Tokens.SecondaryText, "Display name");
            ui.SetNextItemWidth(ui.ContentAvailable().Width);
            (void)ui.InputText("##AccountDisplayName", m_AccountDisplayName);
            if (!snapshot.AccountPersistentSessionAvailable)
            {
                ui.TextColoredWrapped(m_Tokens.Warning,
                                      "Secure session persistence is unavailable on this platform. You will need to "
                                      "sign in again after restarting the Hub.");
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
            if (snapshot.AccountHasError || snapshot.AccountConfirmationRequired)
            {
                DrawAccountMessage(ui, m_Tokens, m_Tokens.Warning,
                                   snapshot.AccountHasError ? "SIGN-IN NEEDS ATTENTION" : "CONFIRM YOUR EMAIL",
                                   snapshot.AccountMessage);
                ui.Spacing();
            }

            if (snapshot.AccountBrowserSignInAvailable)
            {
                DrawBrowserSignInCard(ui, m_Tokens, snapshot, command);
            }
            else
            {
                DrawAccountMessage(ui, m_Tokens, m_Tokens.Warning, "BROWSER SIGN-IN UNAVAILABLE",
                                   "This Hub package does not include the browser authentication adapter. Use the "
                                   "email fallback or install a current Hub package.");
            }

            if (!snapshot.AccountBrowserSignInPending)
            {
                ui.Spacing();
                DrawEmailFallback(ui, m_Tokens, snapshot, m_AccountEmail, m_AccountPassword, command);
            }
        }

        ui.Spacing();
        ui.Separator();
        ui.TextColored(m_Tokens.MutedText,
                       "Signing in is optional. Local project and editor access is never account-locked.");
        if (HubSecondaryButton(ui, m_Tokens, "Close", {88.0F, 36.0F}))
            ui.CloseCurrentPopup();
    }
} // namespace KeireHub
