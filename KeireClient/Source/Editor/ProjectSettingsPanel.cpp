#include "KeireClient/Editor/EditorPanels.h"

#include "KeireClient/Editor/AssetPicker.h"
#include "KeireClient/Editor/ProjectSettingsDocument.h"

#include <bit>
#include <cstdint>
#include <exception>
#include <memory>

namespace KeireEditor
{
    ProjectSettingsPanel::ProjectSettingsPanel(ProjectSettingsDocument& document,
                                               IProjectSettingsController& controller)
        : m_Document(document), m_Controller(controller), m_AssetPicker(std::make_unique<AssetPicker>())
    {
    }

    ProjectSettingsPanel::~ProjectSettingsPanel() = default;

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
        ui.Text("Scripting");
        ui.TextColored(theme.MutedText, "Select the .NET 10 SDK used to compile project scripts.");
        try
        {
            auto sdk = m_Controller.ProjectManagedSdk();
            if (!m_SdkInitialized)
            {
                m_CustomSdkPath = sdk.CustomExecutable.string();
                m_SdkInitialized = true;
            }
            if (ui.Button("Bundled SDK"))
            {
                sdk.Selection = Keire::ManagedSdkSelection::Bundled;
                m_Controller.SetProjectManagedSdk(sdk);
            }
            ui.SameLine();
            if (ui.Button("System PATH"))
            {
                sdk.Selection = Keire::ManagedSdkSelection::SystemPath;
                m_Controller.SetProjectManagedSdk(sdk);
            }
            ui.SameLine();
            if (ui.Button("Custom SDK"))
            {
                sdk.Selection = Keire::ManagedSdkSelection::Custom;
                sdk.CustomExecutable = m_CustomSdkPath;
                m_Controller.SetProjectManagedSdk(sdk);
            }
            if (sdk.Selection == Keire::ManagedSdkSelection::Custom)
            {
                if (ui.InputText("dotnet executable", m_CustomSdkPath))
                {
                    sdk.CustomExecutable = m_CustomSdkPath;
                    m_Controller.SetProjectManagedSdk(sdk);
                }
                ui.TextColored(theme.MutedText, "Choose the dotnet executable beside the SDK directory.");
            }
            const auto selection = sdk.Selection == Keire::ManagedSdkSelection::Bundled ? "Active: bundled engine SDK"
                                   : sdk.Selection == Keire::ManagedSdkSelection::SystemPath
                                       ? "Active: DOTNET_ROOT / PATH"
                                       : "Active: custom executable";
            ui.TextColored(theme.MutedText, selection);
            m_Error.clear();
        }
        catch (const std::exception& error)
        {
            m_Error = error.what();
        }
        if (!m_Error.empty())
            ui.TextColored(theme.Warning, m_Error);
        ui.Spacing();
        ui.Separator();
        ui.Text("Audio / Physics Authoring");
        ui.TextColored(theme.MutedText,
                       "Project assets persist during Play; collision-matrix changes require no active physics world.");
        auto authoring = m_Document.AuthoringSettings();
        bool authoringChanged = false;
        bool authoringCommit = false;
        bool matrixChanged = false;
        AssetPickerOptions mixerOptions;
        mixerOptions.Label = "Default Mixer";
        mixerOptions.EmptyLabel = "No project mixer";
        mixerOptions.ExpectedType = Keire::AudioMixerAsset::StaticType();
        mixerOptions.Reveal = [this](const Keire::AssetId asset) { m_Controller.RevealProjectSettingsAsset(asset); };
        if (m_AssetPicker->Draw(ui, m_Controller.ProjectSettingsAssetRecords(), authoring.DefaultMixer, mixerOptions))
        {
            authoringChanged = true;
            authoringCommit = true;
        }
        if (auto layers = ui.BeginTreeNode("Physics Layers (32)"); layers)
        {
            for (std::size_t index = 0; index < authoring.PhysicsLayerNames.size(); ++index)
            {
                if (ui.InputText("Layer " + std::to_string(index) + "###PhysicsLayer" + std::to_string(index),
                                 authoring.PhysicsLayerNames[index]))
                {
                    authoringChanged = true;
                }
                authoringCommit |= ui.LastItemState().DeactivatedAfterEdit;
            }
        }
        if (auto matrix = ui.BeginTreeNode("Collision Matrix"); matrix)
        {
            ui.TextColored(theme.MutedText, "Each row is mirrored automatically to keep filtering deterministic.");
            for (std::size_t first = 0; first < authoring.PhysicsLayerNames.size(); ++first)
            {
                const auto rowLabel =
                    authoring.PhysicsLayerNames[first] + "###PhysicsCollisionRow" + std::to_string(first);
                if (auto row = ui.BeginTreeNode(rowLabel); row)
                {
                    for (std::size_t second = 0; second < authoring.PhysicsLayerNames.size(); ++second)
                    {
                        bool collides = (authoring.PhysicsCollisionMatrix[first] & (1U << second)) != 0;
                        const auto label = authoring.PhysicsLayerNames[second] + "###PhysicsCollision" +
                                           std::to_string(first) + "_" + std::to_string(second);
                        if (ui.Checkbox(label, collides))
                        {
                            const auto firstBit = 1U << second;
                            const auto secondBit = 1U << first;
                            if (collides)
                            {
                                authoring.PhysicsCollisionMatrix[first] |= firstBit;
                                authoring.PhysicsCollisionMatrix[second] |= secondBit;
                            }
                            else
                            {
                                authoring.PhysicsCollisionMatrix[first] &= ~firstBit;
                                authoring.PhysicsCollisionMatrix[second] &= ~secondBit;
                            }
                            authoringChanged = true;
                            authoringCommit = true;
                            matrixChanged = true;
                        }
                    }
                }
            }
        }
        if (ui.Button("Reset Audio / Physics"))
        {
            m_Document.ResetAuthoring();
            authoringCommit = true;
            matrixChanged = true;
        }
        else if (authoringChanged)
        {
            try
            {
                m_Document.UpdateAuthoring(std::move(authoring));
            }
            catch (const std::exception& error)
            {
                m_Error = error.what();
                authoringCommit = false;
            }
        }
        ui.Spacing();
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
        ui.Spacing();
        ui.Text("Skybox");
        ui.TextColored(theme.MutedText,
                       settings.Environment ? "Custom project environment" : "Built-in Kéire studio sky");
        KeireEditor::AssetPickerOptions skyOptions;
        skyOptions.Label = "Skybox Asset";
        skyOptions.EmptyLabel = "Kéire Default Sky";
        skyOptions.ExpectedType = Keire::Texture2DAsset::StaticType();
        skyOptions.Filter = &KeireEditor::AssetPicker::AcceptsEnvironmentTexture;
        skyOptions.Reveal = [this](const Keire::AssetId asset) { m_Controller.RevealProjectSettingsAsset(asset); };
        if (m_AssetPicker->Draw(ui, m_Controller.ProjectSettingsAssetRecords(), settings.Environment, skyOptions))
        {
            changed = true;
            commit = true;
        }
        if (!m_AssetPicker->Diagnostic().empty())
            ui.TextColored(theme.Warning, m_AssetPicker->Diagnostic());
        changed |= ui.SliderFloat("Sky Rotation", settings.EnvironmentRotationDegrees, -180.0F, 180.0F);
        commit |= ui.LastItemState().DeactivatedAfterEdit;
        changed |= ui.SliderFloat("Environment Diffuse", settings.EnvironmentDiffuseIntensity, 0.0F, 8.0F);
        commit |= ui.LastItemState().DeactivatedAfterEdit;
        changed |= ui.SliderFloat("Sky / Specular Intensity", settings.EnvironmentSpecularIntensity, 0.0F, 8.0F);
        commit |= ui.LastItemState().DeactivatedAfterEdit;
        changed |= ui.Checkbox("Render Skybox", settings.SkyVisible);
        commit |= changed;
        ui.TextColored(theme.MutedText, "Skyboxes provide the background and environment response.");
        ui.TextColored(theme.MutedText, "Shadows require a Directional Light; rotate it to match a custom sky's sun.");
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
        auto resolutionPower = static_cast<std::int32_t>(std::bit_width(settings.DirectionalShadowResolution)) - 1;
        if (ui.SliderInt("Resolution (2^n)", resolutionPower, 8, 13))
        {
            settings.DirectionalShadowResolution = 1U << static_cast<std::uint32_t>(resolutionPower);
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
                if (matrixChanged)
                    m_Controller.ApplyProjectAuthoringSettings(m_Document.AuthoringSettings());
                m_Document.CommitEdit();
                m_Document.Save();
                m_Error.clear();
            }
            catch (const std::exception& error)
            {
                m_Error = error.what();
            }
        }
        else if (authoringCommit && m_Document.Dirty())
        {
            try
            {
                if (matrixChanged)
                    m_Controller.ApplyProjectAuthoringSettings(m_Document.AuthoringSettings());
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
