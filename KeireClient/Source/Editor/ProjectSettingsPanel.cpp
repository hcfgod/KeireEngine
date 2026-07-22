#include "KeireClient/Editor/EditorPanels.h"

#include "KeireClient/Editor/ProjectSettingsDocument.h"

#include <exception>

namespace KeireEditor
{
    ProjectSettingsPanel::ProjectSettingsPanel(ProjectSettingsDocument& document) noexcept : m_Document(document) {}

    void ProjectSettingsPanel::Attach(Keire::UiWorkspace& workspace)
    {
        m_Registration = workspace.RegisterPanel({"editor.project-settings", "Project Settings", false});
    }

    void ProjectSettingsPanel::Draw(Keire::UiFrame& ui, const Keire::UiThemeDefinition& theme)
    {
        auto panel = ui.BeginPanel(m_Registration);
        if (!panel)
            return;

        ui.TextColored(theme.Accent, "PROJECT SETTINGS");
        ui.Separator();
        ui.Text("Rendering / Environment");
        ui.TextColored(theme.MutedText, "These values are project-owned and light both the Scene and Game views.");
        ui.Spacing();

        auto settings = m_Document.Settings();
        Keire::UiColor ambient{settings.AmbientColor.Red, settings.AmbientColor.Green, settings.AmbientColor.Blue,
                               settings.AmbientColor.Alpha};
        bool changed = ui.ColorEdit("Ambient Color", ambient);
        bool commit = ui.LastItemState().DeactivatedAfterEdit;
        changed |= ui.SliderFloat("Ambient Intensity", settings.AmbientIntensity, 0.0F, 8.0F);
        commit |= ui.LastItemState().DeactivatedAfterEdit;
        changed |= ui.SliderFloat("Exposure", settings.Exposure, 0.1F, 4.0F);
        commit |= ui.LastItemState().DeactivatedAfterEdit;
        if (ambient != Keire::UiColor{settings.AmbientColor.Red, settings.AmbientColor.Green,
                                      settings.AmbientColor.Blue, settings.AmbientColor.Alpha})
        {
            settings.AmbientColor = {ambient.Red, ambient.Green, ambient.Blue, ambient.Alpha};
            changed = true;
        }

        ui.Spacing();
        if (ui.Button("Reset Environment"))
        {
            m_Document.Reset();
            commit = true;
        }
        else if (changed)
            m_Document.Update(settings);

        if (commit && m_Document.Dirty())
        {
            try
            {
                m_Document.CommitEdit();
                m_Document.Save();
                m_Error.clear();
            }
            catch (const std::exception& error)
            {
                m_Error = error.what();
            }
        }
        if (!m_Error.empty())
            ui.TextColored(theme.Error, "Could not save project settings: " + m_Error);
    }
} // namespace KeireEditor
