#include "KeireClient/Editor/ConsolePanel.h"

#include "KeireClient/EditorWorkspaceLayer.h"

namespace KeireEditor
{
    void ConsolePanel::Draw(Keire::UiFrame& ui, EditorWorkspaceLayer& editor)
    {
        if (auto console = ui.BeginPanel(editor.m_Console); console)
        {
            ui.TextColored(editor.m_Theme.Accent, "CONSOLE");
            ui.Separator();
            (void)ui.InputText("Search", editor.m_ConsoleSearch);
            ui.SameLine();
            if (ui.Checkbox("Pause", editor.m_ConsolePaused))
            {
                if (editor.m_ConsolePaused)
                    editor.m_PausedConsoleSnapshot.assign(editor.m_ConsoleMessages.begin(),
                                                          editor.m_ConsoleMessages.end());
                else
                    editor.m_PausedConsoleSnapshot.clear();
            }
            ui.SameLine();
            if (ui.Button("Clear"))
            {
                editor.m_ConsoleMessages.clear();
                editor.m_PausedConsoleSnapshot.clear();
            }
            if (editor.m_ConsoleMessages.empty())
            {
                ui.TextColored(editor.m_Theme.Success, "Ready");
                return;
            }
            const auto drawEntries = [&](const auto& entries)
            {
                for (const auto& entry : entries)
                {
                    if (!editor.m_ConsoleSearch.empty() &&
                        entry.Category.find(editor.m_ConsoleSearch) == std::string::npos &&
                        entry.Message.find(editor.m_ConsoleSearch) == std::string::npos)
                        continue;
                    ui.TextColored(entry.Color,
                                   "[" + std::to_string(entry.Frame) + "] [" + entry.Category + "] " + entry.Message);
                }
            };
            if (editor.m_ConsolePaused)
                drawEntries(editor.m_PausedConsoleSnapshot);
            else
                drawEntries(editor.m_ConsoleMessages);
        }
    }
} // namespace KeireEditor
