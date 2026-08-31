#include "KeireHub/HubProductUi.h"

#include "KeireHub/HubModalUi.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <string>
#include <string_view>

namespace KeireHub
{
    namespace
    {
        [[nodiscard]] std::string Utf8Path(const std::filesystem::path& path)
        {
            const auto value = path.generic_u8string();
            return {reinterpret_cast<const char*>(value.data()), value.size()};
        }

        [[nodiscard]] std::filesystem::path PathFromUtf8(const std::string_view value)
        {
            return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(value.data()), value.size()));
        }
    } // namespace

    void HubProductUi::DrawFirstRun(Keire::UiFrame& ui, const HubProductSnapshot& snapshot, HubUiCommand& command)
    {
        if (snapshot.Settings.FirstRunCompleted)
            return;
        constexpr std::array titles{"Welcome",         "Choose locations",  "Detect existing editors",
                                    "Import projects", "Review components", "Ready"};
        SynchronizeSettings(snapshot.Settings);
        auto& settings = *m_EditedSettings;
        ui.OpenPopup("Welcome to Kéire Hub");
        PrepareHubModal(ui, {720.0F, 520.0F});
        HubModalStyleScope modalStyle(ui, m_Tokens);
        if (auto dialog = ui.BeginPopupModal("Welcome to Kéire Hub", nullptr, HubModalWindowOptions(), false); dialog)
        {
            DrawHubModalHeader(ui, m_Tokens, titles[m_FirstRunStep],
                               "Set up the Hub once. Every choice can be changed later in Settings.",
                               "WELCOME  •  STEP " + std::to_string(m_FirstRunStep + 1) + " OF " +
                                   std::to_string(titles.size()));
            const float setupProgress = static_cast<float>(m_FirstRunStep + 1) / static_cast<float>(titles.size());
            const auto setupPercent = std::to_string((m_FirstRunStep + 1) * 100 / titles.size()) + '%';
            const float percentWidth = ui.MeasureText(setupPercent).Width;
            ui.ProgressBar(setupProgress, {std::max(ui.ContentAvailable().Width - percentWidth - 12.0F, 120.0F), 18.0F},
                           " ");
            ui.SameLine();
            ui.TextColored(m_Tokens.SecondaryText, setupPercent);
            ui.Spacing();
            if (m_FirstRunStep == 0)
            {
                ui.TextColoredWrapped(m_Tokens.SecondaryText,
                                      "Kéire Hub keeps projects, editor installations, downloads, and learning "
                                      "content in one place. No account is required.");
            }
            else if (m_FirstRunStep == 1)
            {
                auto projectRoot = Utf8Path(settings.DefaultProjectLocation);
                auto editorRoot = Utf8Path(settings.DefaultEditorRoot);
                if (ui.InputTextWithHint("Project root", "Folder for new projects", projectRoot))
                    settings.DefaultProjectLocation = PathFromUtf8(projectRoot);
                if (ui.InputTextWithHint("Editor root", "Folder for managed editors", editorRoot))
                    settings.DefaultEditorRoot = PathFromUtf8(editorRoot);
                ui.TextColoredWrapped(m_Tokens.MutedText,
                                      "Only roots you choose are considered for discovery. The Hub never scans an "
                                      "entire disk or network share automatically. Missing folders are created when "
                                      "you continue.");
            }
            else if (m_FirstRunStep == 2)
            {
                ui.TextColoredWrapped(m_Tokens.SecondaryText,
                                      "The Hub checks its packaged ancestry and only the editor location you selected. "
                                      "You can locate more editors later from Installs.");
                ui.Text(std::to_string(snapshot.DiscoveredEditors) + " compatible editor installation(s) detected.");
                const auto shownEditors = std::min<std::size_t>(snapshot.DiscoveredEditorItems.size(), 6);
                for (std::size_t index = 0; index < shownEditors; ++index)
                {
                    const auto& editor = snapshot.DiscoveredEditorItems[index];
                    ui.TextColored(m_Tokens.PrimaryText, editor.Name);
                    ui.TextColored(m_Tokens.SecondaryText, editor.Detail);
                    ui.TextColoredWrapped(m_Tokens.MutedText, Utf8Path(editor.Root));
                }
                if (snapshot.DiscoveredEditorItems.size() > shownEditors)
                    ui.TextColored(m_Tokens.MutedText,
                                   "+" + std::to_string(snapshot.DiscoveredEditorItems.size() - shownEditors) +
                                       " additional editor(s)");
            }
            else if (m_FirstRunStep == 3)
            {
                ui.TextColoredWrapped(m_Tokens.SecondaryText,
                                      "Existing recent-project entries and projects found beneath your selected "
                                      "root can be imported. You can add other projects individually later.");
                ui.Text(std::to_string(snapshot.DiscoveredProjects) + " additional project(s) detected; " +
                        std::to_string(snapshot.RecentProjects) + " already registered.");
                const auto shownProjects = std::min<std::size_t>(snapshot.DiscoveredProjectItems.size(), 6);
                for (std::size_t index = 0; index < shownProjects; ++index)
                {
                    const auto& project = snapshot.DiscoveredProjectItems[index];
                    ui.TextColored(m_Tokens.PrimaryText, project.Name);
                    ui.TextColored(m_Tokens.SecondaryText, project.Detail);
                    ui.TextColoredWrapped(m_Tokens.MutedText, Utf8Path(project.Root));
                }
                if (snapshot.DiscoveredProjectItems.size() > shownProjects)
                    ui.TextColored(m_Tokens.MutedText,
                                   "+" + std::to_string(snapshot.DiscoveredProjectItems.size() - shownProjects) +
                                       " additional project(s)");
            }
            else if (m_FirstRunStep == 4)
            {
                ui.TextColoredWrapped(m_Tokens.SecondaryText,
                                      "Build Support already installed beside this editor remains visible in the "
                                      "Installs area.");
                if (snapshot.BuildSupportInventoryLoading)
                    ui.Text("Checking installed component health...");
                else
                    ui.Text(std::to_string(snapshot.HealthyComponents) + " healthy component(s); " +
                            std::to_string(snapshot.UnhealthyComponents) + " need attention.");
            }
            else
            {
                ui.TextColoredWrapped(m_Tokens.SecondaryText,
                                      "Setup is complete. These choices can be changed at any time in Settings.");
            }
            ui.Spacing();
            if (snapshot.FirstRunDiscoveryRunning)
                ui.TextColored(m_Tokens.Accent, "Discovery is running in the background...");
            else if (!snapshot.FirstRunDiscoveryMessage.empty())
                ui.TextColored(snapshot.FirstRunDiscoveryComplete ? m_Tokens.Success : m_Tokens.Warning,
                               snapshot.FirstRunDiscoveryMessage);
            ui.Spacing();
            ui.Separator();
            if (m_FirstRunStep > 0 && HubSecondaryButton(ui, m_Tokens, "Back", {88.0F, 38.0F}))
                --m_FirstRunStep;
            if (m_FirstRunStep > 0)
                ui.SameLine();
            const bool discoveryBlocksNext = snapshot.FirstRunDiscoveryRunning && m_FirstRunStep >= 2;
            if (auto disabled = ui.BeginDisabled(discoveryBlocksNext); disabled)
            {
                if (HubPrimaryButton(ui, m_Tokens, m_FirstRunStep + 1 == titles.size() ? "Finish" : "Continue",
                                     {108.0F, 38.0F}))
                {
                    const auto previousStep = m_FirstRunStep;
                    if (++m_FirstRunStep == titles.size())
                    {
                        settings.FirstRunCompleted = true;
                        command = {.Type = HubUiCommandType::SaveSettings, .Settings = settings};
                        ui.CloseCurrentPopup();
                        m_FirstRunStep = 0;
                    }
                    else if (previousStep == 1)
                    {
                        command = {.Type = HubUiCommandType::BeginFirstRunDiscovery, .Settings = settings};
                    }
                }
            }
            ui.SameLine();
            if (HubSecondaryButton(ui, m_Tokens, "Skip optional discovery", {176.0F, 38.0F}))
            {
                settings.FirstRunCompleted = true;
                command = {.Type = HubUiCommandType::SaveSettings, .ItemId = "skip-discovery", .Settings = settings};
                ui.CloseCurrentPopup();
                m_FirstRunStep = 0;
            }
        }
    }
} // namespace KeireHub
