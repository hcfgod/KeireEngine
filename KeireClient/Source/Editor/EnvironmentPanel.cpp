#include "KeireClient/Editor/EditorPanels.h"

#include "KeireClient/Editor/ProjectSettingsDocument.h"
#include "KeireClientInternal/Editor/AtmosphereControls.h"
#include "KeireClientInternal/Editor/BloomControls.h"
#include "KeireClientInternal/Editor/CloudControls.h"
#include "KeireClientInternal/Editor/EnvironmentLightingControls.h"
#include "KeireClientInternal/Editor/EnvironmentPreviewControls.h"
#include "KeireClientInternal/Editor/EnvironmentSkyControls.h"
#include "KeireClientInternal/Editor/EnvironmentTimelineControls.h"
#include "KeireClientInternal/Editor/StarFieldControls.h"
#include "KeireClientInternal/Editor/VolumetricCloudControls.h"

#include <exception>

namespace KeireEditor
{
    void ProjectSettingsPanel::DrawPanels(Keire::UiFrame& ui, const Keire::UiThemeDefinition& theme)
    {
        Draw(ui, theme);
        DrawEnvironment(ui, theme);
    }

    void ProjectSettingsPanel::ObserveEditSurface(const Detail::SettingsEditSurface surface, const bool visible,
                                                  const bool focused)
    {
        if (!m_EditSurfaces.Observe(surface, visible, focused))
        {
            return;
        }
        try
        {
            // Finish the gesture without silently retrying a failed save or saving an Undo operation.
            // Both surfaces retain their visible dirty indicator and explicit Save action.
            m_Document.CommitEdit("Edit Project Environment");
        }
        catch (const std::exception& error)
        {
            m_Error = error.what();
        }
    }

    void ProjectSettingsPanel::DrawEnvironment(Keire::UiFrame& ui, const Keire::UiThemeDefinition& theme)
    {
        ui.SetNextWindowSize({720.0F, 800.0F});
        auto panel = ui.BeginPanel(m_EnvironmentRegistration);
        ObserveEditSurface(Detail::SettingsEditSurface::Environment, static_cast<bool>(panel),
                           panel && ui.WindowFocused(true));
        if (!panel)
        {
            return;
        }
        if (!m_Document.Opened())
        {
            ui.TextWrapped("Open a project to author its environment.");
            return;
        }
        if (ui.WindowFocused())
        {
            m_Controller.ActivateProjectSettingsHistory();
        }

        ui.TextColored(theme.Accent, "ENVIRONMENT");
        ui.TextColoredWrapped(theme.MutedText,
                              "Project-wide sky and daily lighting. Changes also appear in Project Settings.");
        ui.TextColoredWrapped(m_Document.Dirty() ? theme.Warning : theme.MutedText,
                              m_Document.Dirty() ? "Unsaved changes - save after Undo/Redo." : "All settings saved");
        try
        {
            if (ui.Button("Save Environment"))
            {
                m_Document.CommitEdit("Edit Environment");
                m_Document.Save();
                m_Error.clear();
            }
            if (!m_Error.empty())
            {
                ui.TextColoredWrapped(theme.Error, m_Error);
                ui.TextWrapped("The operation did not complete. Review the message and correct the input or "
                               "underlying problem. If saving failed, edits are retained; retry Save Environment.");
                if (ui.Button("Dismiss environment message"))
                {
                    m_Error.clear();
                }
            }
            ui.Separator();
            if (auto* preview = m_Controller.ProjectEnvironmentPreview())
            {
                Detail::DrawEnvironmentPreviewControls(ui, theme, *preview, m_Document);
            }
            else
            {
                ui.TextColoredWrapped(theme.Warning, "Edit-mode time preview is unavailable during Play Mode.");
            }

            auto tabs = ui.BeginTabBar("EnvironmentSections");
            if (!tabs)
            {
                return;
            }
            auto settings = m_Document.Settings();
            bool changed = false;
            bool commit = false;
            int activeCategory = -1;
            if (auto tab = ui.BeginTabItem("Lighting"); tab)
            {
                activeCategory = 0;
                if (auto content = ui.BeginChild("EnvironmentLightingContent", {0.0F, 0.0F}); content)
                {
                    Detail::DrawEnvironmentTimeline(ui, theme, m_Document.EnvironmentLighting(),
                                                    m_Controller.ProjectEnvironmentPreview(), m_EnvironmentSelectedKey);
                    commit |= Detail::DrawEnvironmentLightingControls(
                        ui, theme, m_Document, m_Controller.ProjectEnvironmentPreview(), &m_EnvironmentSelectedKey);
                }
            }
            if (auto tab = ui.BeginTabItem("Sky"); tab)
            {
                activeCategory = 1;
                if (auto content = ui.BeginChild("EnvironmentSkyContent", {0.0F, 0.0F}); content)
                {
                    commit |= Detail::DrawSkyColorControls(ui, theme, m_Document,
                                                           m_Controller.ProjectEnvironmentPreview(), m_SkySelectedKeys);
                    changed |= Detail::DrawAtmosphereControls(ui, theme, settings.Atmosphere, commit);
                    changed |= Detail::DrawStarFieldControls(ui, theme, settings.Stars, commit);
                }
            }
            if (auto tab = ui.BeginTabItem("Clouds"); tab)
            {
                activeCategory = 2;
                if (auto content = ui.BeginChild("EnvironmentCloudContent", {0.0F, 0.0F}); content)
                {
                    changed |= Detail::DrawCloudControls(ui, theme, settings.Clouds, commit);
                    changed |= Detail::DrawVolumetricCloudControls(ui, theme, settings.Clouds.Volume, commit);
                }
            }
            if (auto tab = ui.BeginTabItem("Bloom"); tab)
            {
                activeCategory = 3;
                if (auto content = ui.BeginChild("EnvironmentBloomContent", {0.0F, 0.0F}); content)
                {
                    changed |= Detail::DrawBloomControls(ui, theme, settings.Bloom, commit);
                }
            }
            if (changed)
            {
                m_Document.Update(settings);
            }
            const bool categoryChanged = m_EnvironmentCategory >= 0 && activeCategory != m_EnvironmentCategory;
            m_EnvironmentCategory = activeCategory;
            if ((commit || categoryChanged) && m_Document.Dirty())
            {
                m_Document.CommitEdit("Edit Environment");
                m_Document.Save();
                m_Error.clear();
            }
        }
        catch (const std::exception& error)
        {
            m_Error = error.what();
        }
    }
} // namespace KeireEditor
