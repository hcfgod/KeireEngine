#include "KeireHub/HubProductUi.h"

#include <algorithm>
#include <cctype>
#include <ranges>

namespace KeireHub
{
    namespace
    {
        [[nodiscard]] std::string Lower(std::string value)
        {
            std::ranges::transform(value, value.begin(), [](const char character)
                                   { return static_cast<char>(std::tolower(static_cast<unsigned char>(character))); });
            return value;
        }

        void PageHeader(Keire::UiFrame& ui, const HubDesignTokens& tokens, const std::string_view title,
                        const std::string_view description)
        {
            {
                const auto heading = ui.PushFont(Keire::UiFontRole::Heading);
                ui.TextColored(tokens.PrimaryText, title);
            }
            ui.TextColoredWrapped(tokens.SecondaryText, description);
            ui.Spacing();
        }

        [[nodiscard]] HubUiCommand OpenContent(const HubContentUiRecord& item)
        {
            if (!item.LocalPath.empty())
                return {.Type = HubUiCommandType::OpenLocalContent, .ItemId = item.Id, .Path = item.LocalPath};
            return {.Type = HubUiCommandType::OpenUrl, .ItemId = item.Id, .Url = item.Url};
        }
    } // namespace

    void HubProductUi::DrawLearn(Keire::UiFrame& ui, const HubProductSnapshot& snapshot, HubUiCommand& command)
    {
        PageHeader(ui, m_Tokens, "Learn", "Open the documentation and samples packaged with this build.");
        if (!snapshot.ContentMessage.empty())
            ui.TextColoredWrapped(m_Tokens.Warning, snapshot.ContentMessage);
        (void)ui.InputTextWithHint("##LearnSearch", "Search learning content", m_ContentSearch);
        const auto search = Lower(m_ContentSearch);
        std::size_t shown = 0;
        for (const auto& item : snapshot.Learn)
        {
            if (!search.empty() &&
                Lower(item.Title + " " + item.Summary + " " + item.Category).find(search) == std::string::npos)
                continue;
            ++shown;
            auto id = ui.PushId(item.Id);
            if (auto card = ui.BeginChild("LearnItem", {0.0F, 140.0F}, true); card)
            {
                ui.TextColored(m_Tokens.Accent,
                               item.Category + (item.Difficulty.empty() ? "" : "  •  " + item.Difficulty));
                ui.TextColored(m_Tokens.PrimaryText, item.Title);
                ui.TextColoredWrapped(m_Tokens.SecondaryText, item.Summary);
                if (ui.Button("Open", {78.0F, 28.0F}))
                    command = OpenContent(item);
            }
            ui.Spacing();
        }
        if (shown == 0)
            ui.TextColored(m_Tokens.SecondaryText, snapshot.Learn.empty() ? "No learning content is installed."
                                                                          : "No learning content matches this search.");
    }

    void HubProductUi::DrawResources(Keire::UiFrame& ui, const HubProductSnapshot& snapshot, HubUiCommand& command)
    {
        PageHeader(ui, m_Tokens, "Resources",
                   "Documentation, API references, release notes, and project links that exist today.");
        if (!snapshot.ContentMessage.empty())
            ui.TextColoredWrapped(m_Tokens.Warning, snapshot.ContentMessage);
        if (snapshot.Resources.empty())
        {
            ui.TextColored(m_Tokens.SecondaryText, "No resources are configured for this package.");
            return;
        }
        for (const auto& item : snapshot.Resources)
        {
            auto id = ui.PushId(item.Id);
            ui.TextColored(m_Tokens.PrimaryText, item.Title);
            ui.TextColoredWrapped(m_Tokens.SecondaryText, item.Summary);
            if (ui.Button(item.LocalPath.empty() ? "Open link" : "Open", {88.0F, 30.0F}))
                command = OpenContent(item);
            ui.Separator();
        }
    }

    void HubProductUi::DrawLicenses(Keire::UiFrame& ui, const HubProductSnapshot& snapshot, HubUiCommand& command)
    {
        PageHeader(ui, m_Tokens, "Licenses",
                   "Review the MIT license and notices for the Hub, editor, components, templates, and content.");
        if (!snapshot.LocalLicenseMessage.empty())
            ui.TextColoredWrapped(m_Tokens.Warning, snapshot.LocalLicenseMessage);
        if (!snapshot.LicenseMessage.empty())
            ui.TextColoredWrapped(m_Tokens.Warning, snapshot.LicenseMessage);
        (void)ui.InputTextWithHint("##LicenseSearch", "Search licenses", m_LicenseSearch);
        const auto search = Lower(m_LicenseSearch);
        std::size_t shown = 0;
        for (const auto& license : snapshot.Licenses)
        {
            if (!search.empty() &&
                Lower(license.Name + " " + license.Group + " " + license.Text).find(search) == std::string::npos)
                continue;
            ++shown;
            auto id = ui.PushId(license.Id);
            if (auto node = ui.BeginTreeNode(license.Name + "  —  " + license.Group); node)
            {
                ui.TextColoredWrapped(m_Tokens.SecondaryText, license.Text);
                if (ui.Button("Copy text", {92.0F, 30.0F}))
                    command = {.Type = HubUiCommandType::CopyText, .ItemId = license.Id, .Text = license.Text};
                if (!license.Path.empty())
                {
                    ui.SameLine();
                    if (ui.Button("Reveal source", {108.0F, 30.0F}))
                        command = {.Type = HubUiCommandType::RevealPath, .ItemId = license.Id, .Path = license.Path};
                }
            }
        }
        if (shown == 0)
            ui.TextColored(m_Tokens.SecondaryText, snapshot.Licenses.empty()
                                                       ? "No license files were found in this package."
                                                       : "No licenses match this search.");
    }
} // namespace KeireHub
