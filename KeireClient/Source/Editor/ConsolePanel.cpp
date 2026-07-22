#include "KeireClient/Editor/ConsolePanel.h"

#include <algorithm>
#include <utility>

namespace KeireEditor
{
    void ConsolePanel::Attach(Keire::UiWorkspace& workspace)
    {
        m_Registration = workspace.RegisterPanel({"editor.console", "Console"});
    }

    void ConsolePanel::Add(std::string category, std::string message, const Keire::UiColor color,
                           const std::uint64_t frame, const Keire::LogLevel level)
    {
        constexpr std::size_t maximumMessages = 500;
        m_Messages.push_back({std::move(category), std::move(message), color, frame, level});
        while (m_Messages.size() > maximumMessages)
            m_Messages.pop_front();
    }

    void ConsolePanel::Draw(Keire::UiFrame& ui, const Keire::UiThemeDefinition& theme)
    {
        if (auto console = ui.BeginPanel(m_Registration); console)
        {
            const auto countLevel = [this](const Keire::LogLevel level)
            { return std::ranges::count(m_Messages, level, &Message::Level); };
            if (ui.Checkbox(("Info " + std::to_string(countLevel(Keire::LogLevel::Info))).c_str(), m_ShowInfo))
            {
            }
            ui.SameLine();
            if (ui.Checkbox(("Warnings " + std::to_string(countLevel(Keire::LogLevel::Warn))).c_str(), m_ShowWarnings))
            {
            }
            ui.SameLine();
            if (ui.Checkbox(("Errors " + std::to_string(countLevel(Keire::LogLevel::Error))).c_str(), m_ShowErrors))
            {
            }
            ui.SameLine();
            (void)ui.Checkbox("Collapse", m_Collapse);
            (void)ui.InputTextWithHint("##ConsoleSearch", "Search Console", m_Search);
            ui.SameLine();
            if (ui.Checkbox("Pause", m_Paused))
            {
                if (m_Paused)
                    m_PausedSnapshot.assign(m_Messages.begin(), m_Messages.end());
                else
                    m_PausedSnapshot.clear();
            }
            ui.SameLine();
            if (ui.Button("Clear"))
            {
                m_Messages.clear();
                m_PausedSnapshot.clear();
            }
            if (m_Messages.empty())
            {
                ui.TextColored(theme.Success, "Ready");
                return;
            }
            const auto drawEntries = [&](const auto& entries)
            {
                std::string previousCategory;
                std::string previousText;
                for (const auto& entry : entries)
                {
                    if (!m_Search.empty() && entry.Category.find(m_Search) == std::string::npos &&
                        entry.Text.find(m_Search) == std::string::npos)
                        continue;
                    if ((entry.Level == Keire::LogLevel::Info && !m_ShowInfo) ||
                        (entry.Level == Keire::LogLevel::Warn && !m_ShowWarnings) ||
                        (entry.Level >= Keire::LogLevel::Error && !m_ShowErrors))
                        continue;
                    if (m_Collapse && entry.Category == previousCategory && entry.Text == previousText)
                        continue;
                    ui.TextColored(entry.Color,
                                   "[" + std::to_string(entry.Frame) + "] [" + entry.Category + "] " + entry.Text);
                    previousCategory = entry.Category;
                    previousText = entry.Text;
                }
            };
            if (m_Paused)
                drawEntries(m_PausedSnapshot);
            else
                drawEntries(m_Messages);
        }
    }
} // namespace KeireEditor
