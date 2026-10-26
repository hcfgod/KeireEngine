#include "KeireClient/Editor/EditorPanels.h"

#include "KeireClient/Editor/AssetPicker.h"
#include "KeireClient/Editor/GpuOcclusionDiagnostics.h"
#include "KeireClient/Editor/ProjectSettingsDocument.h"
#include "KeireClient/Editor/RenderFeatureDiagnostics.h"
#include "KeireClientInternal/Editor/AtmosphereControls.h"
#include "KeireClientInternal/Editor/BloomControls.h"
#include "KeireClientInternal/Editor/CloudControls.h"
#include "KeireClientInternal/Editor/EnvironmentLightingControls.h"
#include "KeireClientInternal/Editor/EnvironmentPreviewControls.h"
#include "KeireClientInternal/Editor/ExternalEditorSettings.h"
#include "KeireClientInternal/Editor/StarFieldControls.h"
#include "KeireClientInternal/Editor/VolumetricCloudControls.h"
#include "KeireInternal/FileSystem.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <exception>
#include <memory>
#include <ranges>

namespace KeireEditor
{
    namespace
    {
        [[nodiscard]] constexpr const char* RenderPathName(const Keire::RenderPath path) noexcept
        {
            switch (path)
            {
            case Keire::RenderPath::ForwardPlus:
                return "Forward+";
            case Keire::RenderPath::DeferredHybrid:
                return "Deferred Hybrid";
            case Keire::RenderPath::Automatic:
                return "Automatic";
            }
            return "Unsupported";
        }

        [[nodiscard]] constexpr const char* AntiAliasingName(const Keire::RenderAntiAliasingMode mode) noexcept
        {
            switch (mode)
            {
            case Keire::RenderAntiAliasingMode::None:
                return "None";
            case Keire::RenderAntiAliasingMode::Fxaa:
                return "FXAA";
            case Keire::RenderAntiAliasingMode::Taa:
                return "TAA";
            case Keire::RenderAntiAliasingMode::Msaa2:
                return "MSAA 2x";
            case Keire::RenderAntiAliasingMode::Msaa4:
                return "MSAA 4x";
            }
            return "Unsupported";
        }

        [[nodiscard]] constexpr const char* DynamicResolutionName(const Keire::DynamicResolutionMode mode) noexcept
        {
            switch (mode)
            {
            case Keire::DynamicResolutionMode::Disabled:
                return "Disabled";
            case Keire::DynamicResolutionMode::Automatic:
                return "Automatic";
            }
            return "Unsupported";
        }

        [[nodiscard]] constexpr const char* GlobalIlluminationName(const Keire::GlobalIlluminationMode mode) noexcept
        {
            switch (mode)
            {
            case Keire::GlobalIlluminationMode::Disabled:
                return "Disabled (Direct Only)";
            case Keire::GlobalIlluminationMode::Baked:
                return "Baked Indirect";
            case Keire::GlobalIlluminationMode::Realtime:
                return "Realtime Environment";
            case Keire::GlobalIlluminationMode::Irradyn:
                return "Irradyn Dynamic GI";
            case Keire::GlobalIlluminationMode::Hybrid:
                return "Hybrid (Baked + Irradyn)";
            }
            return "Unsupported";
        }

        [[nodiscard]] constexpr const char* IrradynQualityName(const Keire::IrradynQuality quality) noexcept
        {
            switch (quality)
            {
            case Keire::IrradynQuality::Performance:
                return "Performance";
            case Keire::IrradynQuality::Balanced:
                return "Balanced";
            case Keire::IrradynQuality::Quality:
                return "Quality";
            }
            return "Unsupported";
        }
    } // namespace

    ProjectSettingsPanel::ProjectSettingsPanel(ProjectSettingsDocument& document,
                                               IProjectSettingsController& controller)
        : m_Document(document), m_Controller(controller), m_AssetPicker(std::make_unique<AssetPicker>())
    {
    }

    ProjectSettingsPanel::~ProjectSettingsPanel() = default;

    void ProjectSettingsPanel::Attach(Keire::UiWorkspace& workspace)
    {
        m_Registration = workspace.RegisterPanel({"editor.project-settings", "Project Settings", false});
        m_EnvironmentRegistration = workspace.RegisterPanel({"editor.environment", "Environment", false});
    }

    void ProjectSettingsPanel::Draw(Keire::UiFrame& ui, const Keire::UiThemeDefinition& theme)
    {
        ui.SetNextWindowSize({900.0F, 700.0F});
        auto panel = ui.BeginPanel(m_Registration);
        ObserveEditSurface(Detail::SettingsEditSurface::Project, static_cast<bool>(panel),
                           panel && ui.WindowFocused(true));
        if (!panel)
        {
            return;
        }

        if (ui.WindowFocused())
        {
            m_Controller.ActivateProjectSettingsHistory();
        }

        ui.TextColored(theme.Accent, "PROJECT SETTINGS");
        ui.TextColoredWrapped(m_Document.Dirty() ? theme.Warning : theme.MutedText,
                              m_Document.Dirty() ? "Unsaved settings - save after Undo/Redo or to retry a failed save."
                                                 : "All settings saved");
        {
            auto disabled = ui.BeginDisabled(!m_Document.Dirty());
            if (ui.Button("Save Settings"))
            {
                try
                {
                    m_Document.Save();
                    m_Error.clear();
                }
                catch (const std::exception& error)
                {
                    m_Error = error.what();
                }
            }
        }
        if (!m_Error.empty())
        {
            ui.TextColoredWrapped(theme.Error, "Project settings need attention.");
            if (m_Document.Dirty())
            {
                ui.TextWrapped("Unsaved edits are retained. Correct the reported input or underlying problem. "
                               "If saving failed, retry Save Settings before closing.");
            }
            {
                auto details = ui.BeginChild("ProjectSettingsErrorDetails", {0.0F, 100.0F}, true);
                if (details)
                {
                    ui.TextColoredWrapped(theme.Error, m_Error);
                }
            }
            if (ui.Button("Dismiss settings message"))
            {
                m_Error.clear();
            }
        }
        ui.Separator();
        auto tabs = ui.BeginTabBar("ProjectSettingsCategories");
        if (!tabs)
        {
            return;
        }

        bool authoringCommit = false;
        bool matrixChanged = false;
        bool commit = false;
        int activeCategory = -1;
        if (auto category = ui.BeginTabItem("Scripting"); category)
        {
            activeCategory = 0;
            if (auto content = ui.BeginChild("ScriptingSettings", {0.0F, -80.0F}); content)
            {
                ui.Text("Scripting");
                ui.TextColoredWrapped(theme.MutedText, "Select the .NET 10 SDK used to compile project scripts.");
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
                        ui.TextColoredWrapped(theme.MutedText,
                                              "Choose the dotnet executable beside the SDK directory.");
                    }
                    const auto selection =
                        sdk.Selection == Keire::ManagedSdkSelection::Bundled      ? "Active: bundled engine SDK"
                        : sdk.Selection == Keire::ManagedSdkSelection::SystemPath ? "Active: DOTNET_ROOT / PATH"
                                                                                  : "Active: custom executable";
                    ui.TextColoredWrapped(theme.MutedText, selection);
                }
                catch (const std::exception& error)
                {
                    m_Error = error.what();
                }
                ui.Spacing();
                ui.Separator();
            }
        }
        if (auto category = ui.BeginTabItem("Input"); category)
        {
            activeCategory = 1;
            if (auto content = ui.BeginChild("InputSettings", {0.0F, -80.0F}); content)
            {
                ui.Text("Input");
                ui.TextColoredWrapped(
                    theme.MutedText,
                    "Choose the action asset and map enabled when Play starts. Changes apply on the next Play.");
                try
                {
                    auto [defaultInput, defaultMap] = m_Controller.ProjectDefaultInput();
                    AssetPickerOptions inputOptions;
                    inputOptions.Label = "Default Input Actions";
                    inputOptions.EmptyLabel = "No default input";
                    inputOptions.ExpectedType = Keire::InputActionAsset::StaticType();
                    inputOptions.Reveal = [this](const Keire::AssetId asset)
                    { m_Controller.RevealProjectSettingsAsset(asset); };
                    if (m_AssetPicker->Draw(ui, m_Controller.ProjectSettingsAssetRecords(), defaultInput, inputOptions))
                    {
                        const auto maps = m_Controller.ProjectInputActionMaps(defaultInput);
                        defaultMap = maps.empty() ? Keire::AssetId{} : maps.front().Id;
                        m_Controller.SetProjectDefaultInput(defaultInput, defaultMap);
                    }
                    const auto maps = m_Controller.ProjectInputActionMaps(defaultInput);
                    const auto selected = std::ranges::find(maps, defaultMap, &Keire::InputActionMapDefinition::Id);
                    const auto label = selected != maps.end() ? selected->Name
                                       : defaultInput         ? "Select a map"
                                                              : "No map";
                    ui.Text("Default Action Map");
                    ui.SetNextItemWidth(-1.0F);
                    if (auto combo = ui.BeginCombo("##Default Action Map", label); combo)
                    {
                        for (const auto& map : maps)
                        {
                            if (ui.Selectable(map.Name, map.Id == defaultMap))
                                m_Controller.SetProjectDefaultInput(defaultInput, map.Id);
                        }
                    }
                    if (defaultInput && maps.empty())
                        ui.TextColoredWrapped(theme.Warning,
                                              "The selected input action asset does not contain an action map.");
                }
                catch (const std::exception& error)
                {
                    m_Error = error.what();
                }
                ui.Spacing();
                ui.Separator();
            }
        }
        if (auto category = ui.BeginTabItem("Code Editor"); category)
        {
            activeCategory = 2;
            if (auto content = ui.BeginChild("CodeEditorSettings", {0.0F, -80.0F}); content)
            {
                ui.Text("Code Editor");
                ui.TextColoredWrapped(theme.MutedText,
                                      "Choose the project-wide editor used for C#, C++, shaders, and text assets.");
                if (!m_EditorsInitialized)
                {
                    m_ExternalEditorProfiles = DiscoverExternalEditorProfiles();
                    m_EditorsInitialized = true;
                }
                auto editorSettings = m_Document.AuthoringSettings();
                if (m_ObservedExternalEditorId != editorSettings.ExternalEditorId ||
                    m_ObservedExternalEditorExecutable != editorSettings.ExternalEditorExecutable)
                {
                    // History and document reloads can change the preference without using these controls.
                    // Unchanged preferences must not overwrite a custom path the user is still drafting.
                    m_SelectedExternalEditorId = editorSettings.ExternalEditorId;
                    m_CustomEditorPath = Keire::Detail::PathToUtf8(editorSettings.ExternalEditorExecutable);
                    m_ObservedExternalEditorId = editorSettings.ExternalEditorId;
                    m_ObservedExternalEditorExecutable = editorSettings.ExternalEditorExecutable;
                    ++m_ExternalEditorDraftRevision;
                }
                const auto activeProfile =
                    std::ranges::find(m_ExternalEditorProfiles, m_SelectedExternalEditorId, &ExternalEditorProfile::Id);
                const auto activeName = activeProfile != m_ExternalEditorProfiles.end() ? activeProfile->DisplayName
                                        : m_SelectedExternalEditorId == "custom"        ? "Custom Executable"
                                                                                        : "Unavailable Editor";
                bool editorChanged = false;
                ui.Text("External Editor");
                ui.SetNextItemWidth(-1.0F);
                if (auto combo = ui.BeginCombo("##External Editor", activeName); combo)
                {
                    for (const auto& profile : m_ExternalEditorProfiles)
                    {
                        const auto label = profile.DisplayName + (profile.Installed ? "" : " (not detected)");
                        if (auto disabled = ui.BeginDisabled(!profile.Installed); disabled)
                            if (ui.Selectable(label, profile.Id == m_SelectedExternalEditorId))
                            {
                                m_SelectedExternalEditorId = profile.Id;
                                editorSettings.ExternalEditorId = profile.Id;
                                editorSettings.ExternalEditorExecutable = profile.Id.starts_with("visual-studio:")
                                                                              ? profile.Executable
                                                                              : std::filesystem::path{};
                                editorChanged = true;
                            }
                    }
                    if (ui.Selectable("Custom Executable", m_SelectedExternalEditorId == "custom"))
                    {
                        m_SelectedExternalEditorId = "custom";
                    }
                }
                if (m_SelectedExternalEditorId == "custom")
                {
                    // Replacing the preference must also retire the UI toolkit's cached input/deactivation state.
                    auto preferenceId = ui.PushId(std::to_string(m_ExternalEditorDraftRevision));
                    if (Detail::DrawCustomExternalEditor(ui, m_CustomEditorPath,
                                                         editorSettings.ExternalEditorExecutable, m_Error))
                    {
                        editorSettings.ExternalEditorId = "custom";
                        editorChanged = true;
                    }
                }
                if (ui.Button("Rescan Editors"))
                    m_ExternalEditorProfiles = DiscoverExternalEditorProfiles();
                if (editorChanged)
                {
                    try
                    {
                        m_Document.UpdateAuthoring(std::move(editorSettings));
                        // Publish the observed preference in the same frame as the edit. Otherwise an immediate
                        // Undo can match the old observation and leave a stale draft that reapplies the change.
                        m_ObservedExternalEditorId = m_Document.AuthoringSettings().ExternalEditorId;
                        m_ObservedExternalEditorExecutable = m_Document.AuthoringSettings().ExternalEditorExecutable;
                        m_Document.CommitEdit("Change External Editor");
                        m_Document.Save();
                        m_Error.clear();
                    }
                    catch (const std::exception& error)
                    {
                        m_Error = error.what();
                    }
                }
                ui.Spacing();
                ui.Separator();
            }
        }
        if (auto category = ui.BeginTabItem("Audio / Physics"); category)
        {
            activeCategory = 3;
            if (auto content = ui.BeginChild("AudioPhysicsSettings", {0.0F, -80.0F}); content)
            {
                ui.Text("Audio / Physics Authoring");
                ui.TextColoredWrapped(
                    theme.MutedText,
                    "Project assets persist during Play; collision-matrix changes require no active physics world.");
                auto authoring = m_Document.AuthoringSettings();
                bool authoringChanged = false;
                AssetPickerOptions mixerOptions;
                mixerOptions.Label = "Default Mixer";
                mixerOptions.EmptyLabel = "No project mixer";
                mixerOptions.ExpectedType = Keire::AudioMixerAsset::StaticType();
                mixerOptions.Reveal = [this](const Keire::AssetId asset)
                { m_Controller.RevealProjectSettingsAsset(asset); };
                if (m_AssetPicker->Draw(ui, m_Controller.ProjectSettingsAssetRecords(), authoring.DefaultMixer,
                                        mixerOptions))
                {
                    authoringChanged = true;
                    authoringCommit = true;
                }
                if (auto audioSettings = ui.BeginTreeNode("Audio Runtime"); audioSettings)
                {
                    ui.TextColoredWrapped(
                        theme.MutedText,
                        "Output changes apply on the next editor launch. Existing projects migrate automatically.");
                    const auto scanAudioDevices = [this]
                    {
                        try
                        {
                            m_AudioPlaybackDevices = Keire::EnumerateAudioPlaybackDevices();
                            m_AudioDeviceError.clear();
                        }
                        catch (const std::exception& error)
                        {
                            m_AudioPlaybackDevices.clear();
                            m_AudioDeviceError = error.what();
                        }
                        m_AudioDevicesInitialized = true;
                    };
                    if (!m_AudioDevicesInitialized)
                        scanAudioDevices();
                    const auto selectedDevice = std::ranges::find(
                        m_AudioPlaybackDevices, authoring.Audio.PlaybackDeviceId, &Keire::AudioDeviceInfo::Id);
                    const auto selectedDeviceName = authoring.Audio.PlaybackDeviceId.empty()
                                                        ? std::string("System Default")
                                                    : selectedDevice != m_AudioPlaybackDevices.end()
                                                        ? selectedDevice->Name
                                                        : std::string("Unavailable (using system default)");
                    if (auto combo = ui.BeginCombo("Playback Device", selectedDeviceName); combo)
                    {
                        if (ui.Selectable("System Default", authoring.Audio.PlaybackDeviceId.empty()))
                        {
                            authoring.Audio.PlaybackDeviceId.clear();
                            authoringChanged = true;
                            authoringCommit = true;
                        }
                        for (const auto& device : m_AudioPlaybackDevices)
                        {
                            const auto label = device.Name + (device.Default ? " (default)" : "");
                            if (ui.Selectable(label, authoring.Audio.PlaybackDeviceId == device.Id))
                            {
                                authoring.Audio.PlaybackDeviceId = device.Id;
                                authoringChanged = true;
                                authoringCommit = true;
                            }
                        }
                    }
                    if (ui.Button("Rescan Audio Devices"))
                        scanAudioDevices();
                    if (!m_AudioDeviceError.empty())
                        ui.TextColoredWrapped(theme.Warning, m_AudioDeviceError);
                    constexpr std::array sampleRates{22050U, 32000U, 44100U, 48000U, 88200U, 96000U, 192000U};
                    const auto sampleRateLabel = std::to_string(authoring.Audio.MixSampleRate) + " Hz";
                    if (auto combo = ui.BeginCombo("Mix Sample Rate", sampleRateLabel); combo)
                    {
                        for (const auto sampleRate : sampleRates)
                        {
                            const auto label = std::to_string(sampleRate) + " Hz";
                            if (ui.Selectable(label, authoring.Audio.MixSampleRate == sampleRate))
                            {
                                authoring.Audio.MixSampleRate = sampleRate;
                                authoringChanged = true;
                                authoringCommit = true;
                            }
                        }
                    }

                    constexpr std::array periods{128U, 256U, 512U, 1024U};
                    const auto periodLabel = std::to_string(authoring.Audio.PeriodFrames) + " frames";
                    if (auto combo = ui.BeginCombo("Buffer Size", periodLabel); combo)
                    {
                        for (const auto period : periods)
                        {
                            const auto label = std::to_string(period) + " frames";
                            if (ui.Selectable(label, authoring.Audio.PeriodFrames == period))
                            {
                                authoring.Audio.PeriodFrames = period;
                                authoringChanged = true;
                                authoringCommit = true;
                            }
                        }
                    }

                    const auto layoutName = [](const Keire::AudioChannelLayout layout)
                    {
                        switch (layout)
                        {
                        case Keire::AudioChannelLayout::Mono:
                            return "Mono";
                        case Keire::AudioChannelLayout::Stereo:
                            return "Stereo";
                        case Keire::AudioChannelLayout::Surround51:
                            return "Surround 5.1";
                        case Keire::AudioChannelLayout::Surround71:
                            return "Surround 7.1";
                        }
                        return "Unsupported";
                    };
                    constexpr std::array layouts{Keire::AudioChannelLayout::Mono, Keire::AudioChannelLayout::Stereo,
                                                 Keire::AudioChannelLayout::Surround51,
                                                 Keire::AudioChannelLayout::Surround71};
                    if (auto combo = ui.BeginCombo("Speaker Layout", layoutName(authoring.Audio.OutputLayout)); combo)
                    {
                        for (const auto layout : layouts)
                        {
                            if (ui.Selectable(layoutName(layout), authoring.Audio.OutputLayout == layout))
                            {
                                authoring.Audio.OutputLayout = layout;
                                authoringChanged = true;
                                authoringCommit = true;
                            }
                        }
                    }

                    auto maximumVoices = static_cast<std::int32_t>(authoring.Audio.MaximumVoices);
                    if (ui.SliderInt("Audible Voice Budget", maximumVoices, 16, 4096))
                    {
                        authoring.Audio.MaximumVoices = static_cast<std::uint32_t>(maximumVoices);
                        authoring.Audio.MaximumVirtualVoices =
                            std::max(authoring.Audio.MaximumVirtualVoices, authoring.Audio.MaximumVoices);
                        authoringChanged = true;
                    }
                    authoringCommit |= ui.LastItemState().DeactivatedAfterEdit;
                    auto maximumVirtualVoices = static_cast<std::int32_t>(authoring.Audio.MaximumVirtualVoices);
                    if (ui.SliderInt("Virtual Voice Budget", maximumVirtualVoices, maximumVoices, 16384))
                    {
                        authoring.Audio.MaximumVirtualVoices = static_cast<std::uint32_t>(maximumVirtualVoices);
                        authoringChanged = true;
                    }
                    authoringCommit |= ui.LastItemState().DeactivatedAfterEdit;
                    ui.TextColoredWrapped(theme.MutedText,
                                          "256 audible / 1024 virtual voices is the balanced desktop default.");
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
                    ui.TextColoredWrapped(theme.MutedText,
                                          "Each row is mirrored automatically to keep filtering deterministic.");
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
            }
        }
        auto settings = m_Document.Settings();
        bool changed = false;
        const auto drawRenderReset = [&]
        {
            if (ui.Button("Reset Rendering / Environment"))
            {
                m_Document.Reset();
                settings = m_Document.Settings();
                commit = true;
            }
            ui.TextColoredWrapped(
                theme.MutedText, "Reset affects render settings on both tabs, including sky and shadow cascades. "
                                 "Daily time, lighting keys and daily-light shadows are preserved. Undo is available.");
            ui.Separator();
        };
        if (auto category = ui.BeginTabItem("Rendering"); category)
        {
            activeCategory = 4;
            drawRenderReset();
            if (auto content = ui.BeginChild("RenderingSettings", {0.0F, -80.0F}); content)
            {
                ui.Text("Rendering");
                ui.TextColoredWrapped(theme.MutedText,
                                      "These values are project-owned and light both the Scene and Game views.");
                ui.Spacing();

                ui.Text("Render Pipeline");
                constexpr std::array renderPaths{Keire::RenderPath::Automatic, Keire::RenderPath::ForwardPlus,
                                                 Keire::RenderPath::DeferredHybrid};
                if (auto combo = ui.BeginCombo("Render Path", RenderPathName(settings.RequestedRenderPath)); combo)
                {
                    for (const auto path : renderPaths)
                    {
                        if (ui.Selectable(RenderPathName(path), settings.RequestedRenderPath == path))
                        {
                            settings.RequestedRenderPath = path;
                            changed = true;
                            commit = true;
                        }
                    }
                }
                if (settings.RequestedRenderPath == Keire::RenderPath::Automatic)
                    ui.TextColoredWrapped(theme.MutedText,
                                          "Automatic selects the best production path supported by the device.");
                else if (settings.RequestedRenderPath == Keire::RenderPath::ForwardPlus)
                    ui.TextColoredWrapped(theme.MutedText, "Forward+ supports every material fallback.");
                else
                    ui.TextColoredWrapped(
                        theme.MutedText,
                        "Deferred Hybrid uses GBuffer passes and a forward tail for specialized materials.");

                constexpr std::array antiAliasingModes{
                    Keire::RenderAntiAliasingMode::None, Keire::RenderAntiAliasingMode::Fxaa,
                    Keire::RenderAntiAliasingMode::Taa, Keire::RenderAntiAliasingMode::Msaa2,
                    Keire::RenderAntiAliasingMode::Msaa4};
                if (auto combo = ui.BeginCombo("Anti-Aliasing", AntiAliasingName(settings.RequestedAntiAliasing));
                    combo)
                {
                    for (const auto mode : antiAliasingModes)
                    {
                        if (ui.Selectable(AntiAliasingName(mode), settings.RequestedAntiAliasing == mode))
                        {
                            settings.RequestedAntiAliasing = mode;
                            changed = true;
                            commit = true;
                        }
                    }
                }
                ui.TextColoredWrapped(
                    theme.MutedText,
                    "Every AA mode works with Forward+ and Deferred Hybrid. TAA uses motion vectors; MSAA uses "
                    "hardware coverage while Deferred Hybrid retains its deferred lighting and GI data.");
                ui.Text("Render Scale");
                ui.SetNextItemWidth(-1.0F);
                changed |= ui.SliderFloat("##Render Scale", settings.RenderScale, 0.5F, 1.0F);
                commit |= ui.LastItemState().DeactivatedAfterEdit;
                constexpr std::array dynamicResolutionModes{Keire::DynamicResolutionMode::Disabled,
                                                            Keire::DynamicResolutionMode::Automatic};
                ui.Text("Dynamic Resolution");
                ui.SetNextItemWidth(-1.0F);
                if (auto combo = ui.BeginCombo("##Dynamic Resolution",
                                               DynamicResolutionName(settings.RequestedDynamicResolution));
                    combo)
                {
                    for (const auto mode : dynamicResolutionModes)
                    {
                        if (ui.Selectable(DynamicResolutionName(mode), settings.RequestedDynamicResolution == mode))
                        {
                            settings.RequestedDynamicResolution = mode;
                            changed = true;
                            commit = true;
                        }
                    }
                }
                if (settings.RequestedDynamicResolution == Keire::DynamicResolutionMode::Automatic)
                {
                    const bool minimumChanged =
                        ui.SliderFloat("Minimum Scale", settings.MinimumDynamicResolutionScale, 0.5F, 1.0F);
                    if (minimumChanged)
                    {
                        settings.MaximumDynamicResolutionScale =
                            std::max(settings.MinimumDynamicResolutionScale, settings.MaximumDynamicResolutionScale);
                        changed = true;
                    }
                    commit |= ui.LastItemState().DeactivatedAfterEdit;
                    changed |= ui.SliderFloat("Maximum Scale", settings.MaximumDynamicResolutionScale,
                                              settings.MinimumDynamicResolutionScale, 1.0F);
                    commit |= ui.LastItemState().DeactivatedAfterEdit;
                    changed |= ui.SliderFloat("Target Frame Time (ms)", settings.DynamicResolutionTargetMilliseconds,
                                              8.0F, 50.0F);
                    commit |= ui.LastItemState().DeactivatedAfterEdit;
                }
                ui.TextColoredWrapped(
                    theme.MutedText,
                    settings.RequestedDynamicResolution == Keire::DynamicResolutionMode::Automatic
                        ? "Automatic resolution uses spatial presentation upscaling with every supported anti-aliasing "
                          "mode."
                        : "Render Scale changes the internal 3D resolution; presentation remains at the viewport "
                          "size.");

                constexpr std::array illuminationModes{
                    Keire::GlobalIlluminationMode::Disabled, Keire::GlobalIlluminationMode::Baked,
                    Keire::GlobalIlluminationMode::Realtime, Keire::GlobalIlluminationMode::Irradyn,
                    Keire::GlobalIlluminationMode::Hybrid};
                ui.Text("Global Illumination");
                ui.SetNextItemWidth(-1.0F);
                if (auto combo = ui.BeginCombo("##Global Illumination",
                                               GlobalIlluminationName(settings.RequestedGlobalIllumination));
                    combo)
                {
                    for (const auto mode : illuminationModes)
                    {
                        if (ui.Selectable(GlobalIlluminationName(mode), settings.RequestedGlobalIllumination == mode))
                        {
                            settings.RequestedGlobalIllumination = mode;
                            changed = true;
                            commit = true;
                        }
                    }
                }
                if (settings.RequestedGlobalIllumination == Keire::GlobalIlluminationMode::Irradyn ||
                    settings.RequestedGlobalIllumination == Keire::GlobalIlluminationMode::Hybrid)
                {
                    constexpr std::array qualities{Keire::IrradynQuality::Performance, Keire::IrradynQuality::Balanced,
                                                   Keire::IrradynQuality::Quality};
                    if (auto combo =
                            ui.BeginCombo("Irradyn Quality", IrradynQualityName(settings.RequestedIrradynQuality));
                        combo)
                    {
                        for (const auto quality : qualities)
                        {
                            if (ui.Selectable(IrradynQualityName(quality), settings.RequestedIrradynQuality == quality))
                            {
                                settings.RequestedIrradynQuality = quality;
                                changed = true;
                                commit = true;
                            }
                        }
                    }
                }
                ui.TextColoredWrapped(
                    theme.MutedText,
                    "GI controls indirect light and reflections. Each Light's Bake Mode controls direct light "
                    "and whether its shadow can move.");
                ui.TextColoredWrapped(
                    theme.MutedText,
                    "Rendering changes apply immediately and are saved when the edited control is committed.");
                const auto currentRuntime =
                    Keire::ResolveRenderFeatureSelection(settings, m_Controller.ProjectRenderFeatureCapabilities());
                ui.TextColoredWrapped(
                    theme.MutedText,
                    "Active: " + std::string(RenderPathName(currentRuntime.EffectivePath)) + " | " +
                        std::string(AntiAliasingName(currentRuntime.EffectiveAntiAliasing)) + " | " +
                        std::string(GlobalIlluminationName(currentRuntime.EffectiveGlobalIllumination)));
                if (currentRuntime.EffectivePath == Keire::RenderPath::DeferredHybrid &&
                    (currentRuntime.EffectiveAntiAliasing == Keire::RenderAntiAliasingMode::Msaa2 ||
                     currentRuntime.EffectiveAntiAliasing == Keire::RenderAntiAliasingMode::Msaa4))
                {
                    ui.TextColoredWrapped(
                        theme.MutedText,
                        "Deferred Hybrid remains active; MSAA uses its forward coverage lane while deferred data "
                        "and GI passes stay enabled.");
                }
                if (currentRuntime.PathFallback != Keire::RenderPathFallbackReason::None)
                    ui.TextColoredWrapped(theme.Warning,
                                          "Deferred Hybrid is not available in this runtime; Forward+ will be used.");
                if (currentRuntime.AntiAliasingFallback != Keire::AntiAliasingFallbackReason::None)
                {
                    ui.TextColoredWrapped(
                        theme.Warning,
                        "The requested anti-aliasing mode is not available with the effective render path; using " +
                            std::string(AntiAliasingName(currentRuntime.EffectiveAntiAliasing)) + '.');
                }
                if (currentRuntime.DynamicResolutionFallback != Keire::DynamicResolutionFallbackReason::None)
                    ui.TextColoredWrapped(theme.Warning,
                                          "Dynamic resolution is not available with the effective renderer settings.");
                if (currentRuntime.GlobalIlluminationFallback != Keire::GlobalIlluminationFallbackReason::None)
                {
                    ui.TextColoredWrapped(theme.Warning,
                                          BuildGlobalIlluminationFallbackMessage(
                                              currentRuntime.GlobalIlluminationFallback,
                                              GlobalIlluminationName(currentRuntime.EffectiveGlobalIllumination)));
                }
                ui.TextColoredWrapped(
                    theme.MutedText,
                    "Requested and effective modes remain separate so capability fallback is explicit and safe.");
                ui.Spacing();

                ui.Text("GPU Occlusion Culling");
                constexpr std::array modes{Keire::GpuOcclusionMode::Disabled, Keire::GpuOcclusionMode::Automatic,
                                           Keire::GpuOcclusionMode::Forced};
                if (auto combo = ui.BeginCombo("Occlusion Mode", GpuOcclusionModeName(settings.GpuOcclusion)); combo)
                {
                    for (const auto mode : modes)
                    {
                        if (ui.Selectable(GpuOcclusionModeName(mode), settings.GpuOcclusion == mode))
                        {
                            settings.GpuOcclusion = mode;
                            changed = true;
                            commit = true;
                        }
                    }
                }
                ui.TextColoredWrapped(theme.MutedText, GpuOcclusionModeDescription(settings.GpuOcclusion));
                ui.TextColoredWrapped(
                    theme.MutedText,
                    "Visibility counters use asynchronous readback and may describe an older completed frame.");
                ui.Spacing();
            }
        }
        if (auto category = ui.BeginTabItem("Environment"); category)
        {
            activeCategory = 5;
            drawRenderReset();
            if (auto content = ui.BeginChild("EnvironmentSettings", {0.0F, -80.0F}); content)
            {
                try
                {
                    if (auto* preview = m_Controller.ProjectEnvironmentPreview())
                    {
                        Detail::DrawEnvironmentPreviewControls(ui, theme, *preview, m_Document);
                    }
                    commit |= Detail::DrawEnvironmentLightingControls(ui, theme, m_Document,
                                                                      m_Controller.ProjectEnvironmentPreview());
                }
                catch (const std::exception& error)
                {
                    m_Error = error.what();
                }
                changed |= Detail::DrawAtmosphereControls(ui, theme, settings.Atmosphere, commit);
                changed |= Detail::DrawStarFieldControls(ui, theme, settings.Stars, commit);
                changed |= Detail::DrawCloudControls(ui, theme, settings.Clouds, commit);
                changed |= Detail::DrawVolumetricCloudControls(ui, theme, settings.Clouds.Volume, commit);
                changed |= Detail::DrawBloomControls(ui, theme, settings.Bloom, commit);
                Keire::UiColor ambient{settings.AmbientColor.Red, settings.AmbientColor.Green,
                                       settings.AmbientColor.Blue, settings.AmbientColor.Alpha};
                changed |= ui.ColorEdit("Ambient Color", ambient);
                commit |= ui.LastItemState().DeactivatedAfterEdit;
                changed |= ui.SliderFloat("Ambient Intensity", settings.AmbientIntensity, 0.0F, 8.0F);
                commit |= ui.LastItemState().DeactivatedAfterEdit;
                changed |= ui.SliderFloat("Exposure", settings.Exposure, 0.1F, 4.0F);
                commit |= ui.LastItemState().DeactivatedAfterEdit;
                ui.Spacing();
                ui.Text("Skybox");
                ui.TextColoredWrapped(theme.MutedText, settings.Environment ? "Custom project environment"
                                                                            : "Built-in Kéire studio sky");
                KeireEditor::AssetPickerOptions skyOptions;
                skyOptions.Label = "Skybox Asset";
                skyOptions.EmptyLabel = "Kéire Default Sky";
                skyOptions.ExpectedType = Keire::Texture2DAsset::StaticType();
                skyOptions.Filter = &KeireEditor::AssetPicker::AcceptsEnvironmentTexture;
                skyOptions.Reveal = [this](const Keire::AssetId asset)
                { m_Controller.RevealProjectSettingsAsset(asset); };
                if (m_AssetPicker->Draw(ui, m_Controller.ProjectSettingsAssetRecords(), settings.Environment,
                                        skyOptions))
                {
                    changed = true;
                    commit = true;
                }
                if (!m_AssetPicker->Diagnostic().empty())
                    ui.TextColoredWrapped(theme.Warning, m_AssetPicker->Diagnostic());
                changed |= ui.SliderFloat("Sky Rotation", settings.EnvironmentRotationDegrees, -180.0F, 180.0F);
                commit |= ui.LastItemState().DeactivatedAfterEdit;
                changed |= ui.SliderFloat("Environment Diffuse", settings.EnvironmentDiffuseIntensity, 0.0F, 8.0F);
                commit |= ui.LastItemState().DeactivatedAfterEdit;
                changed |=
                    ui.SliderFloat("Sky / Specular Intensity", settings.EnvironmentSpecularIntensity, 0.0F, 8.0F);
                commit |= ui.LastItemState().DeactivatedAfterEdit;
                changed |= ui.Checkbox("Render Skybox", settings.SkyVisible);
                commit |= changed;
                ui.TextColoredWrapped(theme.MutedText, "Skyboxes provide the background and environment response.");
                ui.TextColoredWrapped(theme.MutedText,
                                      "Use Daily Lighting or an authored Directional Light for directional shadows. "
                                      "Match its direction to your sky; the skybox does not position the light.");
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
                auto resolutionPower =
                    static_cast<std::int32_t>(std::bit_width(settings.DirectionalShadowResolution)) - 1;
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
            }
            ui.Spacing();
        }
        if (changed)
        {
            m_Document.Update(settings);
        }

        // A disappearing tab cannot report the previous control's deactivation next frame.
        // Finish its pending edit before navigation loses that commit signal.
        const bool categoryChanged = m_SettingsCategory >= 0 && activeCategory != m_SettingsCategory;
        m_SettingsCategory = activeCategory;
        if ((commit || authoringCommit || categoryChanged) && m_Document.Dirty())
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
    }
} // namespace KeireEditor
