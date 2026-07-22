#include "KeireClient/Editor/ConsolePanel.h"

#include <utility>

namespace KeireEditor
{
    void ConsolePanel::Attach(Keire::UiWorkspace& workspace)
    {
        m_Registration = workspace.RegisterPanel({"editor.console", "Console"});
    }

    void ConsolePanel::Add(std::string category, std::string message, const Keire::UiColor color,
                           const std::uint64_t frame)
    {
        constexpr std::size_t maximumMessages = 500;
        m_Messages.push_back({std::move(category), std::move(message), color, frame});
        while (m_Messages.size() > maximumMessages)
            m_Messages.pop_front();
    }

    void ConsolePanel::Draw(Keire::UiFrame& ui, const Keire::UiThemeDefinition& theme)
    {
        if (auto console = ui.BeginPanel(m_Registration); console)
        {
            ui.TextColored(theme.Accent, "CONSOLE");
            ui.Separator();
            (void)ui.InputText("Search", m_Search);
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
                for (const auto& entry : entries)
                {
                    if (!m_Search.empty() && entry.Category.find(m_Search) == std::string::npos &&
                        entry.Text.find(m_Search) == std::string::npos)
                        continue;
                    ui.TextColored(entry.Color,
                                   "[" + std::to_string(entry.Frame) + "] [" + entry.Category + "] " + entry.Text);
                }
            };
            if (m_Paused)
                drawEntries(m_PausedSnapshot);
            else
                drawEntries(m_Messages);
        }
    }
} // namespace KeireEditor
