#include "KeireClient/Editor/EditorPanels.h"

#include "KeireClient/Editor/AssetBrowserPanel.h"
#include "KeireClient/Editor/ProjectSettingsDocument.h"

#include <bit>
#include <cstdint>
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
        if (!m_EnvironmentEditing && m_EnvironmentAsset != settings.Environment)
        {
            m_EnvironmentAsset = settings.Environment;
            m_EnvironmentText = settings.Environment ? settings.Environment.ToString() : std::string{};
        }
        (void)ui.InputText("Environment Texture", m_EnvironmentText);
        const auto environmentState = ui.LastItemState();
        m_EnvironmentEditing = environmentState.Active;
        if (environmentState.DeactivatedAfterEdit)
        {
            try
            {
                settings.Environment =
                    m_EnvironmentText.empty() ? Keire::AssetId{} : Keire::AssetId::Parse(m_EnvironmentText);
                m_EnvironmentAsset = settings.Environment;
                changed = true;
                commit = true;
            }
            catch (const std::exception& error)
            {
                m_Error = std::string("Environment asset ID is invalid: ") + error.what();
            }
        }
        if (auto target = ui.BeginDragTarget(); target)
        {
            std::vector<std::byte> payload;
            if (ui.AcceptDragPayload("KEIRE_ASSETS", payload))
            {
                const auto assets = AssetBrowserPanel::DecodeDragPayload(payload);
                if (!assets.empty())
                {
                    settings.Environment = assets.front();
                    m_EnvironmentAsset = assets.front();
                    m_EnvironmentText = assets.front().ToString();
                    changed = true;
                    commit = true;
                }
            }
        }
        changed |= ui.SliderFloat("Environment Rotation", settings.EnvironmentRotationDegrees, -180.0F, 180.0F);
        commit |= ui.LastItemState().DeactivatedAfterEdit;
        changed |= ui.SliderFloat("IBL Diffuse", settings.EnvironmentDiffuseIntensity, 0.0F, 8.0F);
        commit |= ui.LastItemState().DeactivatedAfterEdit;
        changed |= ui.SliderFloat("IBL Specular", settings.EnvironmentSpecularIntensity, 0.0F, 8.0F);
        commit |= ui.LastItemState().DeactivatedAfterEdit;
        changed |= ui.Checkbox("Show Environment Sky", settings.SkyVisible);
        commit |= changed;
        ui.Spacing();
        ui.Text("Directional Shadows");
        changed |= ui.SliderFloat("Shadow Distance", settings.DirectionalShadowDistance, 1.0F, 1000.0F);
        commit |= ui.LastItemState().DeactivatedAfterEdit;
        auto cascades = static_cast<std::int32_t>(settings.DirectionalShadowCascadeCount);
        if (ui.SliderInt("Cascade Count", cascades, 1, 4))
        {
            settings.DirectionalShadowCascadeCount = static_cast<std::uint32_t>(cascades);
            changed = true;
        }
        commit |= ui.LastItemState().DeactivatedAfterEdit;
        auto resolutionPower = static_cast<std::int32_t>(std::bit_width(settings.DirectionalShadowResolution) - 1U);
        if (ui.SliderInt("Resolution (2^n)", resolutionPower, 8, 13))
        {
            settings.DirectionalShadowResolution = 1U << resolutionPower;
            changed = true;
        }
        commit |= ui.LastItemState().DeactivatedAfterEdit;
        changed |= ui.SliderFloat("Cascade Split Lambda", settings.DirectionalShadowSplitLambda, 0.0F, 1.0F);
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
