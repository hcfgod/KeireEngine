#include "KeireClient/Editor/EditorPanels.h"

#include "KeireClient/Editor/ProjectSettingsDocument.h"
#include "KeireClientInternal/Editor/AtmosphereControls.h"
#include "KeireClientInternal/Editor/BloomControls.h"
#include "KeireClientInternal/Editor/CloudControls.h"
#include "KeireClientInternal/Editor/EnvironmentLightingControls.h"
#include "KeireClientInternal/Editor/EnvironmentPreviewControls.h"
#include "KeireClientInternal/Editor/EnvironmentSkyControls.h"
#include "KeireClientInternal/Editor/EnvironmentTimelineControls.h"
#include "KeireClientInternal/Editor/FogControls.h"
#include "KeireClientInternal/Editor/PlanetControls.h"
#include "KeireClientInternal/Editor/StarFieldControls.h"
#include "KeireClientInternal/Editor/VolumetricCloudControls.h"

#include <algorithm>
#include <array>
#include <exception>
#include <string_view>

namespace KeireEditor
{
    void ProjectSettingsPanel::DrawPanels(Keire::UiFrame& ui, const Keire::UiThemeDefinition& theme)
    {
        Draw(ui, theme);
        DrawEnvironment(ui, theme);
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
            const auto history = m_Document.UndoContext();
            ui.SameLine();
            {
                auto disabled = ui.BeginDisabled(!history || (!history->CanUndo() && !m_Document.Dirty()));
                if (ui.Button("Undo##Environment"))
                {
                    m_Document.CommitEdit("Edit Environment");
                    (void)history->Undo();
                }
            }
            ui.SameLine();
            {
                auto disabled = ui.BeginDisabled(!history || !history->CanRedo());
                if (ui.Button("Redo##Environment"))
                {
                    m_Document.CommitEdit("Edit Environment");
                    (void)history->Redo();
                }
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

            constexpr std::array<std::string_view, 9> sections{"Time & Lighting",   "Sky Colors", "Atmosphere",
                                                               "Planets",           "Stars",      "Layered Clouds",
                                                               "Volumetric Clouds", "Fog",        "Bloom"};
            constexpr std::array<std::string_view, 9> descriptions{
                "Author the daily lighting cycle and preview its transitions.",
                "Shape the sky and horizon palette throughout the day.",
                "Tune atmospheric scattering and the view toward space.",
                "Configure planetary environments and reference frames.",
                "Set the density, brightness, and character of the star field.",
                "Compose broad cloud coverage and layered weather.",
                "Sculpt cloud volume, lighting, and quality.",
                "Control aerial depth, haze, and volumetric fog.",
                "Set the environment's baseline highlight glow."};
            int activeCategory = std::clamp(m_EnvironmentCategory, 0, static_cast<int>(sections.size()) - 1);
            auto settings = m_Document.Settings();
            bool changed = false;
            bool commit = false;
            const auto drawSettings = [&]
            {
                {
                    auto heading = ui.PushFont(Keire::UiFontRole::Heading);
                    ui.Text(sections[activeCategory]);
                }
                ui.TextColoredWrapped(theme.MutedText, descriptions[activeCategory]);
                ui.Separator();
                switch (activeCategory)
                {
                case 0:
                    Detail::DrawEnvironmentTimeline(ui, theme, m_Document.EnvironmentLighting(),
                                                    m_Controller.ProjectEnvironmentPreview(), m_EnvironmentSelectedKey);
                    commit |= Detail::DrawEnvironmentLightingControls(
                        ui, theme, m_Document, m_Controller.ProjectEnvironmentPreview(), &m_EnvironmentSelectedKey);
                    break;
                case 1:
                    commit |= Detail::DrawSkyColorControls(ui, theme, m_Document,
                                                           m_Controller.ProjectEnvironmentPreview(), m_SkySelectedKeys);
                    break;
                case 2:
                    changed |= Detail::DrawAtmosphereControls(ui, theme, settings.Atmosphere, commit);
                    break;
                case 3:
                    commit |= Detail::DrawPlanetControls(ui, theme, m_Document);
                    break;
                case 4:
                    changed |= Detail::DrawStarFieldControls(ui, theme, settings.Stars, commit);
                    break;
                case 5:
                    changed |= Detail::DrawCloudControls(ui, theme, settings.Clouds, commit);
                    break;
                case 6:
                    changed |= Detail::DrawVolumetricCloudControls(ui, theme, settings.Clouds.Volume, commit);
                    break;
                case 7:
                    changed |= Detail::DrawFogControls(ui, theme, settings.Fog, commit);
                    break;
                case 8:
                    changed |= Detail::DrawBloomControls(ui, theme, settings.Bloom, commit);
                    break;
                }
            };
            const auto available = ui.ContentAvailable();
            if (available.Width < 680.0F)
            {
                if (auto navigation = ui.BeginCombo("Section", sections[activeCategory]); navigation)
                {
                    for (std::size_t index = 0; index < sections.size(); ++index)
                    {
                        if (ui.Selectable(sections[index], activeCategory == static_cast<int>(index)))
                        {
                            activeCategory = static_cast<int>(index);
                        }
                    }
                }
                if (auto content = ui.BeginChild("EnvironmentInspectorCompact", {}, true); content)
                {
                    drawSettings();
                }
            }
            else
            {
                Keire::UiTableOptions layout;
                layout.Borders = false;
                layout.Resizable = false;
                layout.RowBackground = false;
                layout.PersistSettings = false;
                if (auto workspace = ui.BeginTable("EnvironmentWorkspace", 2, layout); workspace)
                {
                    ui.TableSetupColumn("Sections", Keire::UiTableColumnSizing::Fixed, 190.0F);
                    ui.TableSetupColumn("Settings", Keire::UiTableColumnSizing::Stretch);
                    (void)ui.TableNextColumn();
                    const float height = std::max(160.0F, available.Height - 16.0F);
                    if (auto navigation = ui.BeginChild("EnvironmentNavigation", {0.0F, height}, true); navigation)
                    {
                        ui.TextColored(theme.MutedText, "ENVIRONMENT");
                        ui.Separator();
                        for (std::size_t index = 0; index < sections.size(); ++index)
                        {
                            if (ui.Selectable(sections[index], activeCategory == static_cast<int>(index)))
                            {
                                activeCategory = static_cast<int>(index);
                            }
                        }
                    }
                    (void)ui.TableNextColumn();
                    if (auto content = ui.BeginChild("EnvironmentInspector", {0.0F, height}, true); content)
                    {
                        drawSettings();
                    }
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
