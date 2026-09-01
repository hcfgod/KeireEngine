#include "KeireClient/Editor/EditorPanels.h"

#include "KeireClient/Editor/AssetPicker.h"
#include "KeireClient/Editor/AuthoringWidgets.h"
#include "KeireClient/Editor/InputActionsDocument.h"
#include "KeireClient/Editor/InspectorPropertyEditor.h"
#include "KeireClient/Editor/ManagedDataInspectorPanel.h"
#include "KeireClient/Editor/MaterialDocument.h"
#include "KeireClient/Editor/MaterialInspectorPanel.h"
#include "KeireClient/Editor/SceneDocument.h"
#include "KeireClient/Editor/ThumbnailService.h"

#include "Keire/Audio/AudioAssets.h"
#include "Keire/Rendering/ShaderGraph.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <ranges>
#include <span>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace
{
    [[nodiscard]] std::vector<std::byte> ReadBytes(const std::filesystem::path& path)
    {
        std::ifstream input(path, std::ios::binary);
        if (!input)
            throw std::runtime_error("Cannot open asset: " + path.string());
        const std::vector<char> characters{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
        std::vector<std::byte> bytes(characters.size());
        std::ranges::transform(characters, bytes.begin(), [](const char value) { return std::byte(value); });
        return bytes;
    }
    [[nodiscard]] std::string FormatAssetDiagnostic(const Keire::AssetImportDiagnostic& diagnostic)
    {
        auto result = diagnostic.RelativePath.generic_string();
        if (diagnostic.Line != 0)
        {
            result += ':' + std::to_string(diagnostic.Line);
            if (diagnostic.Column != 0)
                result += ':' + std::to_string(diagnostic.Column);
        }
        if (!result.empty())
            result += ": ";
        result += diagnostic.Message;
        return result;
    }

    [[nodiscard]] Keire::AssetImportSettings EditableImportSettings(const Keire::AssetImporterRegistration& importer,
                                                                    const Keire::AssetImportSettings& persisted)
    {
        Keire::AssetImportSettings result;
        for (const auto& option : importer.ImportOptions)
            result.emplace(option.Key, option.DefaultValue);
        for (const auto& [key, value] : persisted)
            if (std::ranges::find(importer.ImportOptions, key, &Keire::AssetImportOptionDescriptor::Key) !=
                importer.ImportOptions.end())
                result[key] = value;
        return result;
    }

    bool DrawImportOption(Keire::UiFrame& ui, const Keire::AssetImportOptionDescriptor& option,
                          Keire::AssetImportOptionValue& value)
    {
        if (auto* boolean = std::get_if<bool>(&value))
            return ui.Checkbox(option.DisplayName, *boolean);
        if (auto* integer = std::get_if<std::int64_t>(&value))
        {
            return ui.DragInteger(
                option.DisplayName, *integer, option.Step,
                option.Minimum ? std::optional<std::int64_t>(static_cast<std::int64_t>(*option.Minimum)) : std::nullopt,
                option.Maximum ? std::optional<std::int64_t>(static_cast<std::int64_t>(*option.Maximum))
                               : std::nullopt);
        }
        if (auto* scalar = std::get_if<double>(&value))
            return ui.DragScalar(option.DisplayName, *scalar, option.Step, option.Minimum, option.Maximum);
        auto* choice = std::get_if<std::string>(&value);
        if (!choice)
            return false;
        bool changed = false;
        if (auto combo = ui.BeginCombo(option.DisplayName, *choice); combo)
            for (const auto& candidate : option.Choices)
                if (ui.Selectable(candidate, candidate == *choice))
                {
                    *choice = candidate;
                    changed = true;
                }
        return changed;
    }
} // namespace

KeireEditor::AssetInspectorPanel::AssetInspectorPanel(IInspectorController& controller)
    : m_Controller(controller), m_AssetPicker(std::make_unique<AssetPicker>()),
      m_ManagedDataInspector(std::make_unique<ManagedDataInspectorPanel>(controller))
{
}

KeireEditor::AssetInspectorPanel::~AssetInspectorPanel() = default;

void KeireEditor::AssetInspectorPanel::ClearState() noexcept
{
    m_EditingAsset = {};
    m_OriginalImportSettings.clear();
    m_ImportSettings.clear();
    m_PreviewImage.Reset();
    m_PreviewDigest.clear();
    m_AssetName.clear();
    m_MaterialParameterCollection.reset();
    m_ProceduralMotionProfile.reset();
    m_MaterialParameterCollectionDirty = false;
    m_ProceduralMotionProfileDirty = false;
    m_ManagedDataInspector->Clear();
}

void KeireEditor::AssetInspectorPanel::Draw(Keire::UiFrame& ui, Keire::AssetId selectedAsset, const bool pinned)
{
    auto& inputDocument = m_Controller.InspectorInputDocument();
    auto& materialDocument = m_Controller.InspectorMaterialDocument();
    const auto& theme = m_Controller.InspectorTheme();
    const auto database = m_Controller.InspectorAssetDatabase();
    const auto assets = m_Controller.InspectorAssetSystem();
    const auto records = m_Controller.InspectorAssetRecords();
    const auto assetStatus = m_Controller.InspectorAssetStatus();
    const auto scene = m_Controller.InspectorSceneDocument().ActiveScene();
    if (!selectedAsset || !database)
    {
        ui.Text("Nothing selected");
        ui.TextColored(theme.MutedText, "Select an asset in the Project panel.");
        return;
    }
    const auto record = database->Find(selectedAsset);
    if (!record)
    {
        selectedAsset = {};
        if (!pinned)
            m_Controller.SetInspectorSelectedAsset({});
        ui.TextColored(theme.Warning, "The selected asset no longer exists.");
        return;
    }
    const auto importer = database->FindImporterForPath(record->RelativePath);
    if (m_EditingAsset != record->Id)
    {
        m_EditingAsset = record->Id;
        m_AssetName = record->RelativePath.filename().string();
        m_OriginalImportSettings = importer && importer->Name == record->Importer
                                       ? EditableImportSettings(*importer, record->ImportSettings)
                                       : Keire::AssetImportSettings{};
        m_ImportSettings = m_OriginalImportSettings;
        m_PreviewImage.Reset();
        m_PreviewDigest.clear();
        m_MaterialParameterCollection.reset();
        m_ProceduralMotionProfile.reset();
        m_MaterialParameterCollectionDirty = false;
        m_ProceduralMotionProfileDirty = false;
    }

    const auto projectRoot = database->Specification().ProjectRoot;
    if (!m_Thumbnails || m_PreviewProjectRoot != projectRoot)
    {
        m_Thumbnails = std::make_unique<KeireEditor::ThumbnailService>(projectRoot / "Library" / "Thumbnails", 8);
        m_PreviewProjectRoot = projectRoot;
        m_PreviewImage.Reset();
        m_PreviewDigest.clear();
    }
    for (auto& completed : m_Thumbnails->DrainCompleted(8))
        if (completed.Asset == record->Id)
            m_PreviewImage = ui.CreateImage(completed.Width, completed.Height, completed.Pixels);
    if (!m_PreviewImage)
    {
        const auto fallback = KeireEditor::MakeAssetFallbackThumbnail(record->Type, 96, 96);
        m_PreviewImage = ui.CreateImage(96, 96, fallback);
    }
    const auto previewDigest = record->Id.ToString() + record->SourceDigest + record->MetadataDigest;
    if (m_PreviewDigest != previewDigest)
    {
        KeireEditor::ThumbnailRequest request{
            .Asset = record->Id, .Type = record->Type, .RelativePath = record->RelativePath, .Digest = previewDigest};
        bool ready = true;
        if (assets && record->Type == Keire::Texture2DAsset::StaticType())
        {
            const auto handle = assets->Load<Keire::Texture2DAsset>(record->Id, Keire::AssetPriority::Low);
            request.PreviewAsset = handle.TryGetLoaded();
            request.Missing = handle.State() == Keire::AssetState::Failed;
            if (!request.PreviewAsset && request.Missing)
                request.PreviewAsset = handle.Get();
            ready = static_cast<bool>(request.PreviewAsset);
        }
        else if (assets && record->Type == Keire::MeshAsset::StaticType())
        {
            const auto handle = assets->Load<Keire::MeshAsset>(record->Id, Keire::AssetPriority::Low);
            request.PreviewAsset = handle.TryGetLoaded();
            request.Missing = handle.State() == Keire::AssetState::Failed;
            if (!request.PreviewAsset && request.Missing)
                request.PreviewAsset = handle.Get();
            ready = static_cast<bool>(request.PreviewAsset);
        }
        else if (assets && record->Type == Keire::AudioClipAsset::StaticType())
        {
            const auto handle = assets->Load<Keire::AudioClipAsset>(record->Id, Keire::AssetPriority::Low);
            request.PreviewAsset = handle.TryGetLoaded();
            request.Missing = handle.State() == Keire::AssetState::Failed;
            if (!request.PreviewAsset && request.Missing)
                request.PreviewAsset = handle.Get();
            ready = static_cast<bool>(request.PreviewAsset);
        }
        else if (const auto generated = KeireEditor::PrepareGeneratedAssetThumbnail(assets, *record, request))
        {
            ready = *generated;
        }
        if (ready && m_Thumbnails->Request(std::move(request)))
            m_PreviewDigest = previewDigest;
    }

    ui.TextColored(theme.Accent, "PREVIEW");
    const float previewSize = std::clamp(ui.ContentAvailable().Width, 96.0F, 220.0F);
    ui.Image(m_PreviewImage, {previewSize, previewSize});
    ui.Text(record->RelativePath.generic_string());
    ui.TextColored(theme.MutedText, "Asset ID");
    ui.Text(record->Id.ToString());
    ui.TextColored(theme.MutedText, "Importer");
    ui.Text(record->Importer + " v" + std::to_string(record->ImporterVersion));
    ui.TextColored(theme.MutedText, "Content SHA-256");
    ui.Text(record->SourceDigest);
    if (importer && importer->Name == record->Importer && !importer->ImportOptions.empty())
    {
        ui.Separator();
        ui.TextColored(theme.Accent, "IMPORT SETTINGS");
        ui.TextColored(theme.MutedText, "Changes are stored with this source asset and applied on reimport.");
        std::string activeGroup;
        for (const auto& option : importer->ImportOptions)
        {
            if (option.Group != activeGroup)
            {
                activeGroup = option.Group;
                if (!activeGroup.empty())
                    ui.TextColored(theme.MutedText, activeGroup);
            }
            (void)DrawImportOption(ui, option, m_ImportSettings.at(option.Key));
        }
        const bool changed = m_ImportSettings != m_OriginalImportSettings;
        if (auto disabled = ui.BeginDisabled(!changed); disabled)
        {
            if (ui.Button("Apply and Reimport"))
            {
                try
                {
                    m_Controller.ApplyInspectorImportSettings(record->Id, m_ImportSettings);
                    m_OriginalImportSettings = m_ImportSettings;
                    m_PreviewDigest.clear();
                }
                catch (const std::exception& error)
                {
                    m_Controller.ReportInspectorAssetError(std::string("Import settings failed: ") + error.what());
                }
            }
        }
        ui.SameLine();
        if (auto disabled = ui.BeginDisabled(!changed); disabled)
            if (ui.Button("Revert Import Settings"))
                m_ImportSettings = m_OriginalImportSettings;
    }
    if (record->RelativePath.extension() == ".keireinput")
    {
        ui.Separator();
        ui.TextColored(theme.Accent, "INPUT ACTION ASSET");
        ui.Text("Action maps, bindings, control schemes, and runtime overrides.");
        if (ui.Button("Edit Input Actions"))
        {
            try
            {
                if (inputDocument.Dirty() && inputDocument.Asset() != record->Id)
                    throw std::runtime_error("Save or Revert the currently edited input asset before switching.");
                m_Controller.OpenInspectorInputActions(record->Id);
            }
            catch (const std::exception& error)
            {
                m_Controller.ReportInspectorAssetError(std::string("Input editor failed to open: ") + error.what());
            }
        }
    }
    else if (record->Type == Keire::AudioClipAsset::StaticType())
    {
        ui.Separator();
        ui.TextColored(theme.Accent, "AUDIO CLIP");
        if (assets)
        {
            const auto handle = assets->Load<Keire::AudioClipAsset>(record->Id, Keire::AssetPriority::High);
            if (const auto clip = handle.TryGetLoaded())
            {
                const auto& data = *clip->Clip();
                ui.Text("Duration: " + std::to_string(clip->DurationSeconds()) + " seconds");
                ui.Text("Sample rate: " + std::to_string(data.SampleRate) + " Hz");
                ui.Text("Channels: " + std::to_string(data.Channels));
                ui.Text("Frames: " + std::to_string(clip->FrameCount()));
                ui.TextColored(theme.MutedText,
                               data.Streaming ? "Storage: streamed encoded source" : "Storage: resident PCM");
            }
            else
            {
                ui.TextColored(theme.MutedText, "Loading decoded audio metadata...");
            }
        }
        if (ui.Button("Preview"))
        {
            try
            {
                m_Controller.PreviewInspectorAudio(record->Id);
                m_Controller.SetInspectorAssetStatus("Playing audio preview through the EditorPreview bus.");
            }
            catch (const std::exception& error)
            {
                m_Controller.ReportInspectorAssetError(std::string("Audio preview failed: ") + error.what());
            }
        }
        ui.SameLine();
        if (ui.Button("Stop Preview"))
            m_Controller.StopInspectorAudioPreview();
        ui.SameLine();
        if (ui.Button("Reimport Audio"))
            m_Controller.ImportInspectorAssets();
        if (!assetStatus.empty())
            ui.TextColored(theme.MutedText, assetStatus);
    }
    else if (record->Type == Keire::ProceduralMotionProfileAsset::StaticType())
    {
        ui.Separator();
        ui.TextColored(theme.Accent, "PROCEDURAL MOTION PROFILE");
        ui.Text("Normalized zero-clip humanoid gait, grounding, airborne, and response settings.");
        try
        {
            const auto sourceRoot = database->Specification().ProjectRoot / database->Specification().SourceDirectory;
            const auto source = sourceRoot / record->RelativePath;
            if (!m_ProceduralMotionProfile)
                m_ProceduralMotionProfile = Keire::ProceduralMotionProfileAsset::Decode(ReadBytes(source))->Profile();
            auto& profile = *m_ProceduralMotionProfile;
            const auto scalar = [&](const std::string_view label, float& value, const double step, const double minimum,
                                    const double maximum)
            {
                double candidate = value;
                if (ui.DragScalar(label, candidate, step, minimum, maximum))
                {
                    value = static_cast<float>(candidate);
                    m_ProceduralMotionProfileDirty = true;
                }
            };

            if (auto gait = ui.BeginTreeNode("Gait", true); gait)
            {
                scalar("Walk Speed", profile.WalkSpeed, 0.05, 0.01, 100.0);
                scalar("Sprint Speed", profile.SprintSpeed, 0.05, 0.01, 100.0);
                scalar("Walk Cadence", profile.WalkCadence, 0.01, 0.05, 10.0);
                scalar("Sprint Cadence", profile.SprintCadence, 0.01, 0.05, 12.0);
                scalar("Stride Length / Leg", profile.StrideLengthRatio, 0.005, 0.01, 2.0);
                scalar("Lateral Stride / Leg", profile.LateralStrideRatio, 0.005, 0.01, 2.0);
                scalar("Backward Stride / Leg", profile.BackwardStrideRatio, 0.005, 0.01, 2.0);
                scalar("Foot Spacing / Leg", profile.FootSpacingRatio, 0.005, 0.01, 1.0);
                scalar("Minimum Movement Speed", profile.MinimumMovementSpeed, 0.01, 0.0, 5.0);
                scalar("Stop Settle Time", profile.StopSettleTime, 0.01, 0.0, 2.0);
                scalar("Turn Threshold (degrees/s)", profile.TurnInPlaceThresholdDegrees, 1.0, 0.0, 180.0);
                scalar("Turn Step (degrees)", profile.TurnStepDegrees, 1.0, 1.0, 180.0);
            }
            if (auto body = ui.BeginTreeNode("Body and Upper Body", false); body)
            {
                scalar("Pelvis Bob / Leg", profile.PelvisBobRatio, 0.001, 0.0, 0.25);
                scalar("Pelvis Sway / Leg", profile.PelvisSwayRatio, 0.001, 0.0, 0.25);
                scalar("Crouch Depth / Leg", profile.CrouchDepthRatio, 0.005, 0.0, 0.6);
                scalar("Acceleration Lean", profile.MaximumAccelerationLeanDegrees, 0.5, 0.0, 45.0);
                scalar("Turn Lean", profile.MaximumTurnLeanDegrees, 0.5, 0.0, 45.0);
                scalar("Spine Counter Rotation", profile.SpineCounterRotationDegrees, 0.5, 0.0, 45.0);
                scalar("Arm Rest Drop", profile.ArmRestDropDegrees, 0.5, 0.0, 90.0);
                scalar("Arm Swing", profile.ArmSwingDegrees, 0.5, 0.0, 90.0);
                scalar("Elbow Bend", profile.ElbowBendDegrees, 0.5, 0.0, 150.0);
                scalar("Breathing Amplitude", profile.BreathingAmplitudeDegrees, 0.05, 0.0, 15.0);
                scalar("Breathing Frequency", profile.BreathingFrequency, 0.01, 0.0, 5.0);
            }
            if (auto grounding = ui.BeginTreeNode("Grounding and Joint Limits", true); grounding)
            {
                scalar("Probe Height / Stature", profile.ProbeHeightRatio, 0.005, 0.01, 2.0);
                scalar("Probe Distance / Stature", profile.ProbeDistanceRatio, 0.005, 0.01, 3.0);
                scalar("Sole Offset / Stature", profile.SoleOffsetRatio, 0.001, 0.0, 0.25);
                scalar("Vertical Pelvis Range / Stature", profile.MaximumPelvisAdjustmentRatio, 0.005, 0.0, 1.0);
                scalar("Horizontal Pelvis Range / Stature", profile.MaximumHorizontalPelvisAdjustmentRatio, 0.005, 0.0,
                       1.0);
                scalar("Plant Distance / Stature", profile.PlantDistanceRatio, 0.002, 0.0, 0.5);
                scalar("Release Distance / Stature", profile.ReleaseDistanceRatio, 0.002, 0.0, 1.0);
                scalar("Step Clearance / Leg", profile.StepClearanceRatio, 0.005, 0.0, 1.0);
                scalar("Maximum Slope", profile.MaximumSlopeDegrees, 0.5, 0.0, 89.0);
                scalar("Maximum Ankle Slope", profile.MaximumAnkleSlopeDegrees, 0.5, 0.0, 89.0);
                scalar("Minimum Knee Bend", profile.MinimumKneeBendDegrees, 0.5, 0.0, 90.0);
                scalar("Maximum Knee Bend", profile.MaximumKneeBendDegrees, 0.5, 0.0, 179.0);
            }
            if (auto airborne = ui.BeginTreeNode("Airborne and Landing", false); airborne)
            {
                scalar("Takeoff Compression / Leg", profile.TakeoffCompressionRatio, 0.005, 0.0, 0.5);
                scalar("Airborne Tuck / Leg", profile.AirborneTuckRatio, 0.005, 0.0, 0.6);
                scalar("Falling Extension / Leg", profile.FallingExtensionRatio, 0.005, 0.0, 0.5);
                scalar("Pre-Landing Probe Time", profile.PreLandingProbeTime, 0.01, 0.0, 2.0);
                scalar("Landing Compression / Leg", profile.LandingCompressionRatio, 0.005, 0.0, 0.6);
                scalar("Landing Recovery Time", profile.LandingRecoveryTime, 0.01, 0.01, 2.0);
                scalar("Maximum Landing Speed", profile.MaximumLandingSpeed, 0.1, 0.1, 100.0);
            }
            if (auto response = ui.BeginTreeNode("Response", false); response)
            {
                scalar("Velocity Response", profile.VelocityResponseTime, 0.005, 0.0, 2.0);
                scalar("Facing Response", profile.FacingResponseTime, 0.005, 0.0, 2.0);
                scalar("Pose Response", profile.PoseResponseTime, 0.005, 0.0, 2.0);
                scalar("Grounding Response", profile.GroundingResponseTime, 0.005, 0.0, 2.0);
            }
            if (auto curves = ui.BeginTreeNode("Normalized Curves", false); curves)
            {
                m_ProceduralMotionProfileDirty |=
                    KeireEditor::AuthoringValueEditors::Curve(ui, "Stride Travel", profile.StrideTravel);
                m_ProceduralMotionProfileDirty |=
                    KeireEditor::AuthoringValueEditors::Curve(ui, "Foot Lift", profile.FootLift);
                m_ProceduralMotionProfileDirty |=
                    KeireEditor::AuthoringValueEditors::Curve(ui, "Foot Roll", profile.FootRoll);
                m_ProceduralMotionProfileDirty |=
                    KeireEditor::AuthoringValueEditors::Curve(ui, "Pelvis Motion", profile.PelvisMotion);
                m_ProceduralMotionProfileDirty |=
                    KeireEditor::AuthoringValueEditors::Curve(ui, "Airborne Tuck", profile.AirborneTuck);
                m_ProceduralMotionProfileDirty |=
                    KeireEditor::AuthoringValueEditors::Curve(ui, "Landing Compression", profile.LandingCompression);
                m_ProceduralMotionProfileDirty |=
                    KeireEditor::AuthoringValueEditors::Curve(ui, "Arm Swing", profile.ArmSwing);
            }

            std::string validationError;
            try
            {
                Keire::ValidateProceduralMotionProfile(profile);
            }
            catch (const std::exception& error)
            {
                validationError = error.what();
                ui.TextColored(theme.Error, validationError);
            }
            if (auto disabled = ui.BeginDisabled(!m_ProceduralMotionProfileDirty || !validationError.empty()); disabled)
            {
                if (ui.Button("Save Profile"))
                {
                    m_Controller.PersistInspectorProceduralMotionProfile(
                        record->Id, Keire::ProceduralMotionProfileAsset::Encode(profile));
                    m_ProceduralMotionProfileDirty = false;
                }
            }
            ui.SameLine();
            if (auto disabled = ui.BeginDisabled(!m_ProceduralMotionProfileDirty); disabled)
            {
                if (ui.Button("Revert Profile"))
                {
                    profile = Keire::ProceduralMotionProfileAsset::Decode(ReadBytes(source))->Profile();
                    m_ProceduralMotionProfileDirty = false;
                }
            }
            ui.TextColored(theme.MutedText,
                           "Use the showcase scene to preview terrain contacts, gait phase, and IK diagnostics live.");
        }
        catch (const std::exception& error)
        {
            ui.TextColored(theme.Error, std::string("Procedural Motion Profile editor unavailable: ") + error.what());
        }
    }
    else if (record->Type == Keire::ManagedDataAsset::StaticType())
    {
        m_ManagedDataInspector->Draw(ui, *record);
    }
    else if (record->RelativePath.extension() == ".keireshader")
    {
        ui.Separator();
        const auto importStatus = database->ImportStatus(record->Id);
        if (importStatus.State == Keire::AssetImportState::Failed)
        {
            ui.TextColored(theme.Error, "SHADER IMPORT FAILED");
            ui.TextColored(theme.Warning, "The last-good compiled revision remains active when available.");
            ui.Separator();
            ui.TextColored(theme.MutedText, "Compiler diagnostics");
            constexpr std::size_t maximumVisibleDiagnostics = 64;
            const auto visibleDiagnostics = std::min(importStatus.Diagnostics.size(), maximumVisibleDiagnostics);
            for (std::size_t index = 0; index < visibleDiagnostics; ++index)
            {
                const auto& diagnostic = importStatus.Diagnostics[index];
                const auto color = diagnostic.Severity == Keire::AssetDiagnosticSeverity::Error     ? theme.Error
                                   : diagnostic.Severity == Keire::AssetDiagnosticSeverity::Warning ? theme.Warning
                                                                                                    : theme.MutedText;
                ui.TextColored(color, FormatAssetDiagnostic(diagnostic));
            }
            if (importStatus.Diagnostics.size() > visibleDiagnostics)
                ui.TextColored(theme.MutedText, std::to_string(importStatus.Diagnostics.size() - visibleDiagnostics) +
                                                    " additional diagnostic(s) are available in the log file.");
        }
        else
        {
            ui.TextColored(theme.Accent, "SHADER");
            ui.TextColored(importStatus.State == Keire::AssetImportState::NotImported ? theme.Warning : theme.Success,
                           importStatus.State == Keire::AssetImportState::NotImported ? "Waiting for first import"
                                                                                      : "Imported graphics shader");
        }
        ui.Text("Stages: Vertex, Fragment");
        ui.Text("Variants: DXIL, SPIR-V, MSL");
        ui.TextColored(theme.MutedText, "Source dependencies");
        if (record->SourceDependencies.empty())
            ui.Text("No dependency records are available; reimport to refresh.");
        for (const auto& dependency : record->SourceDependencies)
            ui.Text(dependency.RelativePath.generic_string() + "  " + dependency.Digest.substr(0, 12));
        if (ui.Button("Reimport Shader"))
            m_Controller.ImportInspectorAssets();
        if (!assetStatus.empty())
            ui.TextColored(theme.MutedText, assetStatus);
    }
    else if (record->RelativePath.extension() == ".keirematerialinstance")
    {
        ui.Separator();
        ui.TextColored(theme.Accent, "MATERIAL INSTANCE");
        ui.Text("Lightweight overrides inherited from a Material, Material Graph, or Material Instance.");
        try
        {
            const auto sourceRoot = database->Specification().ProjectRoot / database->Specification().SourceDirectory;
            const auto sourcePath = sourceRoot / record->RelativePath;
            auto instance = Keire::MaterialInstanceAsset::DecodeSource(ReadBytes(sourcePath));
            const auto parentRecord = database->Find(instance.Parent);
            if (!parentRecord || !assets)
                throw std::runtime_error("The Material Instance parent is unavailable.");
            auto runtimeMaterial = instance.Parent;
            if (parentRecord->Type != Keire::MaterialAsset::StaticType())
            {
                const auto runtime =
                    std::ranges::find_if(parentRecord->SubAssets,
                                         [&](const Keire::AssetId candidate)
                                         {
                                             const auto type = assets->TryGetType(candidate);
                                             return type && *type == Keire::MaterialAsset::StaticType();
                                         });
                if (runtime == parentRecord->SubAssets.end())
                    throw std::runtime_error("The parent runtime material has not been imported yet.");
                runtimeMaterial = *runtime;
            }
            const auto parent =
                assets->Load<Keire::MaterialAsset>(runtimeMaterial, Keire::AssetPriority::High).TryGetLoaded();
            if (!parent)
                throw std::runtime_error("The parent runtime material is still loading.");
            const auto shader =
                assets->Load<Keire::ShaderAsset>(parent->Definition().Shader, Keire::AssetPriority::High)
                    .TryGetLoaded();
            if (!shader)
                throw std::runtime_error("The inherited shader interface is still loading.");

            ui.TextColored(theme.MutedText, "Parent");
            ui.Text(parentRecord->RelativePath.generic_string());
            Keire::MaterialAuthoringDefinition authoring;
            authoring.Shader.Asset = parent->Definition().Shader;
            authoring.Surface = instance.Surface.value_or(parent->Definition().Surface);
            authoring.ContributeEmissionToGI =
                instance.ContributeEmissionToGI.value_or(parent->Definition().ContributeEmissionToGI);
            authoring.EmissiveGIIntensity =
                instance.EmissiveGIIntensity.value_or(parent->Definition().EmissiveGIIntensity);
            authoring.Properties = parent->Definition().Properties;
            for (const auto& [name, value] : instance.Properties)
                authoring.Properties.insert_or_assign(name, value);
            KeireEditor::MaterialDocument editorDocument;
            editorDocument.Open(Keire::MaterialAsset::EncodeAuthoringSource(authoring),
                                [&](const Keire::AssetId candidate) -> std::optional<Keire::ShaderAssetDefinition>
                                {
                                    return candidate == parent->Definition().Shader
                                               ? std::optional(shader->Definition())
                                               : std::nullopt;
                                });
            InspectorPropertyEditor propertyEditor(ui, records, assets, scene, *m_AssetPicker);
            if (KeireEditor::MaterialInspectorPanel{}.Draw(propertyEditor, editorDocument))
            {
                const auto changed = editorDocument.LastChangedProperty();
                if (changed == "$surface")
                    instance.Surface = editorDocument.Surface();
                else if (!changed.empty())
                    instance.Properties.insert_or_assign(std::string(changed), editorDocument.Property(changed));
                m_Controller.PersistInspectorMaterialInstance(record->Id,
                                                              Keire::MaterialInstanceAsset::EncodeSource(instance));
            }

            std::optional<Keire::MaterialGraphDefinition> rootGraph;
            std::vector<std::map<std::string, std::string, std::less<>>> inheritedStaticOverrides;
            auto root = instance.Parent;
            for (std::size_t depth = 0; depth < 16 && root; ++depth)
            {
                const auto rootRecord = database->Find(root);
                if (!rootRecord)
                    break;
                const auto rootPath = sourceRoot / rootRecord->RelativePath;
                if (rootRecord->Type == Keire::MaterialInstanceAsset::StaticType())
                {
                    const auto parentInstance = Keire::MaterialInstanceAsset::DecodeSource(ReadBytes(rootPath));
                    inheritedStaticOverrides.push_back(parentInstance.KeywordOverrides);
                    root = parentInstance.Parent;
                    continue;
                }
                if (rootRecord->Type == Keire::MaterialGraphAsset::StaticType())
                    rootGraph = Keire::MaterialGraphAsset::DecodeSource(ReadBytes(rootPath));
                break;
            }
            if (rootGraph && rootGraph->Shader.Kind == Keire::MaterialShaderSourceKind::ShaderGraph)
            {
                const auto templateRecord = database->Find(rootGraph->Shader.Asset);
                if (templateRecord && templateRecord->Type == Keire::ShaderGraphAsset::StaticType())
                {
                    const auto shaderTemplate =
                        Keire::ShaderGraphAsset::DecodeSource(ReadBytes(sourceRoot / templateRecord->RelativePath));
                    auto staticParameters = shaderTemplate.Keywords;
                    for (const auto& keyword : rootGraph->SurfaceGraph.Keywords)
                        if (std::ranges::none_of(staticParameters, [&](const Keire::ShaderGraphKeyword& candidate)
                                                 { return candidate.Name == keyword.Name; }))
                            staticParameters.push_back(keyword);
                    if (!staticParameters.empty())
                    {
                        std::map<std::string, std::string, std::less<>> inheritedValues;
                        for (const auto& keyword : staticParameters)
                            inheritedValues[keyword.Name] =
                                keyword.DefaultOption.empty()
                                    ? keyword.Options.empty() ? "false" : keyword.Options.front()
                                    : keyword.DefaultOption;
                        for (const auto& [name, value] : rootGraph->Shader.Keywords)
                            inheritedValues.insert_or_assign(name, value);
                        for (auto parentOverrides = inheritedStaticOverrides.rbegin();
                             parentOverrides != inheritedStaticOverrides.rend(); ++parentOverrides)
                            for (const auto& [name, value] : *parentOverrides)
                                inheritedValues.insert_or_assign(name, value);

                        ui.Separator();
                        ui.TextColored(theme.Accent, "STATIC PARAMETERS");
                        ui.TextColored(theme.MutedText,
                                       "Static overrides select a precompiled parent Material Graph variant.");
                        bool staticChanged = false;
                        for (const auto& keyword : staticParameters)
                        {
                            const auto existing = instance.KeywordOverrides.find(keyword.Name);
                            bool overridden = existing != instance.KeywordOverrides.end();
                            if (ui.Checkbox("Override##" + keyword.Name, overridden))
                            {
                                if (overridden)
                                    instance.KeywordOverrides.insert_or_assign(keyword.Name,
                                                                               inheritedValues.at(keyword.Name));
                                else
                                    instance.KeywordOverrides.erase(keyword.Name);
                                staticChanged = true;
                            }
                            ui.SameLine();
                            auto value = overridden ? instance.KeywordOverrides.at(keyword.Name)
                                                    : inheritedValues.at(keyword.Name);
                            if (auto disabled = ui.BeginDisabled(!overridden); disabled)
                            {
                                if (keyword.Options.empty())
                                {
                                    bool enabled = value == "true";
                                    if (ui.Checkbox(keyword.Name, enabled))
                                    {
                                        instance.KeywordOverrides.insert_or_assign(keyword.Name,
                                                                                   enabled ? "true" : "false");
                                        staticChanged = true;
                                    }
                                }
                                else if (auto combo = ui.BeginCombo(keyword.Name, value); combo)
                                    for (const auto& option : keyword.Options)
                                        if (ui.Selectable(option, option == value))
                                        {
                                            instance.KeywordOverrides.insert_or_assign(keyword.Name, option);
                                            staticChanged = true;
                                        }
                            }
                        }
                        if (!instance.KeywordOverrides.empty() && ui.Button("Reset All Static Overrides"))
                        {
                            instance.KeywordOverrides.clear();
                            staticChanged = true;
                        }
                        if (staticChanged)
                            m_Controller.PersistInspectorMaterialInstance(
                                record->Id, Keire::MaterialInstanceAsset::EncodeSource(instance));
                    }
                }
            }
            ui.TextColored(theme.MutedText, std::to_string(instance.Properties.size()) +
                                                " explicit property override(s), " +
                                                std::to_string(instance.KeywordOverrides.size()) +
                                                " static override(s). Shader code is never duplicated.");
            if (!instance.Properties.empty() && ui.Button("Reset All Property Overrides"))
            {
                instance.Properties.clear();
                m_Controller.PersistInspectorMaterialInstance(record->Id,
                                                              Keire::MaterialInstanceAsset::EncodeSource(instance));
            }
            ui.SameLine();
            if (ui.Button("Reimport Material Instance"))
                m_Controller.ImportInspectorAssets();
        }
        catch (const std::exception& error)
        {
            ui.TextColored(theme.Error, std::string("Material Instance editor unavailable: ") + error.what());
        }
    }
    else if (record->Type == Keire::MaterialParameterCollectionAsset::StaticType())
    {
        ui.Separator();
        ui.TextColored(theme.Accent, "MATERIAL PARAMETER COLLECTION");
        ui.TextColored(theme.MutedText,
                       "Project-global scalar, vector, and color values shared by dependent materials.");
        try
        {
            const auto sourceRoot = database->Specification().ProjectRoot / database->Specification().SourceDirectory;
            const auto source = sourceRoot / record->RelativePath;
            if (!m_MaterialParameterCollection)
                m_MaterialParameterCollection =
                    Keire::MaterialParameterCollectionAsset::DecodeSource(ReadBytes(source));
            auto& collection = *m_MaterialParameterCollection;
            constexpr std::array typeNames{std::string_view("Scalar"), std::string_view("Vector2"),
                                           std::string_view("Vector3"), std::string_view("Vector4"),
                                           std::string_view("Color")};
            std::optional<std::size_t> remove;
            for (std::size_t index = 0; index < collection.Parameters.size(); ++index)
            {
                auto& parameter = collection.Parameters[index];
                ui.Separator();
                ui.TextColored(theme.MutedText, parameter.DisplayName.empty() ? parameter.Name : parameter.DisplayName);
                const auto suffix = "##collection-" + parameter.Id.ToString();
                m_MaterialParameterCollectionDirty |= ui.InputText("Name" + suffix, parameter.Name);
                m_MaterialParameterCollectionDirty |= ui.InputText("Display Name" + suffix, parameter.DisplayName);
                m_MaterialParameterCollectionDirty |= ui.InputText("Description" + suffix, parameter.Description);
                m_MaterialParameterCollectionDirty |= ui.InputText("Category" + suffix, parameter.Category);
                auto sortPriority = static_cast<double>(parameter.SortPriority);
                if (ui.DragScalar("Sort Priority" + suffix, sortPriority, 1.0, -10'000.0, 10'000.0))
                {
                    parameter.SortPriority = static_cast<std::int32_t>(std::round(sortPriority));
                    m_MaterialParameterCollectionDirty = true;
                }
                auto typeIndex = static_cast<std::size_t>(parameter.Type);
                if (auto combo = ui.BeginCombo("Type" + suffix, typeNames.at(typeIndex)); combo)
                    for (std::size_t candidate = 0; candidate < typeNames.size(); ++candidate)
                        if (ui.Selectable(typeNames[candidate], candidate == typeIndex))
                        {
                            parameter.Type = static_cast<Keire::ShaderPropertyType>(candidate);
                            parameter.DefaultValue = candidate == 0   ? Keire::MaterialPropertyValue(0.0F)
                                                     : candidate == 1 ? Keire::MaterialPropertyValue(Keire::Vector2{})
                                                     : candidate == 2 ? Keire::MaterialPropertyValue(Keire::Vector3{})
                                                     : candidate == 3 ? Keire::MaterialPropertyValue(Keire::Vector4{})
                                                                      : Keire::MaterialPropertyValue(Keire::Color{});
                            m_MaterialParameterCollectionDirty = true;
                        }
                if (auto* value = std::get_if<float>(&parameter.DefaultValue))
                {
                    double edited = *value;
                    if (ui.DragScalar("Default" + suffix, edited, 0.01))
                    {
                        *value = static_cast<float>(edited);
                        m_MaterialParameterCollectionDirty = true;
                    }
                }
                else if (auto* vector2 = std::get_if<Keire::Vector2>(&parameter.DefaultValue))
                    m_MaterialParameterCollectionDirty |= ui.DragVector2("Default" + suffix, *vector2);
                else if (auto* vector3 = std::get_if<Keire::Vector3>(&parameter.DefaultValue))
                    m_MaterialParameterCollectionDirty |= ui.DragVector3("Default" + suffix, *vector3);
                else if (auto* vector4 = std::get_if<Keire::Vector4>(&parameter.DefaultValue))
                    m_MaterialParameterCollectionDirty |= ui.DragVector4("Default" + suffix, *vector4);
                else if (auto* color = std::get_if<Keire::Color>(&parameter.DefaultValue))
                {
                    Keire::UiColor edited{color->Red, color->Green, color->Blue, color->Alpha};
                    if (ui.ColorEdit("Default" + suffix, edited))
                    {
                        *color = {edited.Red, edited.Green, edited.Blue, edited.Alpha};
                        m_MaterialParameterCollectionDirty = true;
                    }
                }
                if (ui.Button("Remove" + suffix))
                    remove = index;
            }
            if (remove)
            {
                collection.Parameters.erase(collection.Parameters.begin() + static_cast<std::ptrdiff_t>(*remove));
                m_MaterialParameterCollectionDirty = true;
            }
            if (ui.Button("Add Parameter"))
            {
                const auto ordinal = collection.Parameters.size() + 1U;
                collection.Parameters.push_back({.Id = Keire::AssetId::Generate(),
                                                 .Name = "Parameter" + std::to_string(ordinal),
                                                 .DisplayName = "Parameter " + std::to_string(ordinal)});
                m_MaterialParameterCollectionDirty = true;
            }
            ui.SameLine();
            if (auto disabled = ui.BeginDisabled(!m_MaterialParameterCollectionDirty); disabled)
                if (ui.Button("Save Collection"))
                {
                    m_Controller.PersistInspectorMaterialParameterCollection(
                        record->Id, Keire::MaterialParameterCollectionAsset::EncodeSource(collection));
                    m_MaterialParameterCollectionDirty = false;
                }
            ui.SameLine();
            if (auto disabled = ui.BeginDisabled(!m_MaterialParameterCollectionDirty); disabled)
                if (ui.Button("Revert Collection"))
                {
                    collection = Keire::MaterialParameterCollectionAsset::DecodeSource(ReadBytes(source));
                    m_MaterialParameterCollectionDirty = false;
                }
        }
        catch (const std::exception& error)
        {
            ui.TextColored(theme.Error,
                           std::string("Material Parameter Collection editor unavailable: ") + error.what());
        }
    }
    else if (record->RelativePath.extension().string() == Keire::MaterialAssetSourceExtension)
    {
        ui.Separator();
        ui.TextColored(theme.Accent, "MATERIAL");
        ui.Text("OpenPBR surface, closure, and layer authoring in one assignable graph.");
        ui.TextColored(theme.MutedText, "Parameters flow into Material Instances; generated programs stay internal.");
        if (ui.Button("Open Material"))
        {
            try
            {
                m_Controller.OpenInspectorMaterialGraph(record->Id);
            }
            catch (const std::exception& error)
            {
                m_Controller.ReportInspectorAssetError(std::string("Material editor failed to open: ") + error.what());
            }
        }
        ui.SameLine();
        if (ui.Button("Reimport Material"))
            m_Controller.ImportInspectorAssets();
        ui.TextColored(theme.MutedText, "Double-clicking this asset opens its Material document.");
    }
    else if (record->RelativePath.extension().string() == Keire::LegacyMaterialAssetSourceExtension)
    {
        ui.Separator();
        ui.TextColored(theme.Accent, "MATERIAL");
        ui.Text("Inspector-based material authoring with shader-driven properties.");
        try
        {
            const auto sourceRoot = database->Specification().ProjectRoot / database->Specification().SourceDirectory;
            const KeireEditor::MaterialDocument::ShaderReferenceResolver resolveShader =
                [&](const Keire::MaterialShaderReference& shader)
                -> std::optional<KeireEditor::MaterialDocument::ResolvedShader>
            {
                const auto shaderRecord = database->Find(shader.Asset);
                if (!shaderRecord)
                    return std::nullopt;
                try
                {
                    if (shader.Kind != Keire::MaterialShaderSourceKind::ShaderGraph)
                    {
                        if (shaderRecord->Type != Keire::ShaderAsset::StaticType())
                            return std::nullopt;
                        return KeireEditor::MaterialDocument::ResolvedShader{
                            shader.Asset,
                            Keire::ShaderAsset::DecodeManifest(ReadBytes(sourceRoot / shaderRecord->RelativePath))};
                    }
                    if (shader.Target != "default" || shaderRecord->Type != Keire::ShaderGraphAsset::StaticType() ||
                        !assets)
                        return std::nullopt;
                    const auto graph =
                        Keire::ShaderGraphAsset::DecodeSource(ReadBytes(sourceRoot / shaderRecord->RelativePath));
                    Keire::ShaderGraphInstanceDefinition selection;
                    selection.Parent = shader.Asset;
                    selection.KeywordOverrides = shader.Keywords;
                    const std::array ancestry{selection};
                    const auto resolvedSelection = Keire::ResolveShaderGraphInstance(graph, ancestry);
                    const auto variants = Keire::EnumerateShaderGraphKeywordVariants(graph.Keywords);
                    const auto variant =
                        std::ranges::find_if(variants, [&resolvedSelection](const auto& candidate)
                                             { return std::ranges::equal(candidate, resolvedSelection.Keywords); });
                    const auto index = static_cast<std::size_t>(std::distance(variants.begin(), variant));
                    if (variant == variants.end() || index >= shaderRecord->SubAssets.size())
                        return std::nullopt;
                    const auto runtimeShader = shaderRecord->SubAssets[index];
                    const auto loaded = assets->Load<Keire::ShaderAsset>(runtimeShader, Keire::AssetPriority::High);
                    const auto definition = loaded.TryGetLoaded();
                    if (!definition)
                        return std::nullopt;
                    return KeireEditor::MaterialDocument::ResolvedShader{runtimeShader, definition->Definition()};
                }
                catch (...)
                {
                    return std::nullopt;
                }
            };

            const auto sourcePath = sourceRoot / record->RelativePath;
            if (!materialDocument.IsOpen(record->Id))
            {
                m_Controller.CommitInspectorMaterial();
                const auto source = ReadBytes(sourcePath);
                materialDocument.OpenAsset(record->Id, sourcePath, source, resolveShader);
            }
            else
                materialDocument.Open(materialDocument.DraftSource(), resolveShader);
            auto& document = materialDocument;
            InspectorPropertyEditor editor(ui, records, assets, scene, *m_AssetPicker);
            bool changed = false;
            const auto currentShader = document.ShaderReference();
            auto shaderGraph = currentShader.Kind == Keire::MaterialShaderSourceKind::ShaderGraph ? currentShader.Asset
                                                                                                  : Keire::AssetId{};
            if (editor.EditAsset("Shader Graph", shaderGraph, Keire::ShaderGraphAsset::StaticType()))
            {
                Keire::MaterialShaderReference replacement;
                replacement.Kind = shaderGraph ? Keire::MaterialShaderSourceKind::ShaderGraph
                                               : Keire::MaterialShaderSourceKind::ShaderAsset;
                replacement.Asset = shaderGraph;
                changed = document.SetShaderReference(std::move(replacement), resolveShader) || changed;
            }
            auto rawShader = currentShader.Kind == Keire::MaterialShaderSourceKind::ShaderGraph ? Keire::AssetId{}
                                                                                                : currentShader.Asset;
            if (editor.EditAsset("Raw Shader", rawShader, Keire::ShaderAsset::StaticType()))
            {
                Keire::MaterialShaderReference replacement;
                replacement.Asset = rawShader;
                changed = document.SetShaderReference(std::move(replacement), resolveShader) || changed;
            }

            if (document.Properties().empty())
                ui.TextColored(theme.MutedText, "The selected shader declares no material properties.");
            else
            {
                ui.Separator();
                ui.TextColored(theme.MutedText, "SHADER PROPERTIES");
                changed = KeireEditor::MaterialInspectorPanel{}.Draw(editor, document) || changed;
            }
            if (changed)
            {
                document.CaptureDraft();
                if (assets)
                    (void)assets->PublishDevelopmentAsset(
                        record->Id, Keire::CreateRef<Keire::MaterialAsset>(document.Definition()));
                m_Controller.SetInspectorAssetStatus("Previewing material changes live.");
            }
            if (editor.EditBoundary())
                m_Controller.CommitInspectorMaterial();
            ui.TextColored(theme.MutedText,
                           "Names, ranges, categories, texture semantics, and defaults come from the shader.");
        }
        catch (const std::exception& error)
        {
            ui.TextColored(theme.Error, std::string("Material editor unavailable: ") + error.what());
        }
        ui.TextColored(theme.MutedText, "Invalid shaders resolve to the error material at runtime.");
        if (ui.Button("Reimport Material"))
        {
            m_Controller.CommitInspectorMaterial();
            m_Controller.ImportInspectorAssets();
        }
        if (!assetStatus.empty())
            ui.TextColored(theme.MutedText, assetStatus);
    }
    ui.Separator();
    (void)ui.InputText("Name", m_AssetName);
    if (ui.Button("Rename") && !m_AssetName.empty())
    {
        try
        {
            m_Controller.RenameInspectorAsset(record->Id, m_AssetName);
            m_Controller.SetInspectorAssetStatus("Renamed asset and preserved its metadata identity.");
        }
        catch (const std::exception& error)
        {
            m_Controller.ReportInspectorAssetError(std::string("Asset rename failed: ") + error.what());
        }
    }
    ui.SameLine();
    if (ui.Button("Duplicate"))
    {
        try
        {
            const auto stem = record->RelativePath.stem().string();
            const auto extension = record->RelativePath.extension().string();
            auto copyName = stem;
            copyName.append(" Copy").append(extension);
            auto destination = record->RelativePath.parent_path() / copyName;
            for (std::size_t copy = 2; database->Find(destination); ++copy)
            {
                copyName = stem;
                copyName.append(" Copy ").append(std::to_string(copy)).append(extension);
                destination = record->RelativePath.parent_path() / copyName;
            }
            m_Controller.DuplicateInspectorAsset(record->Id, destination);
            m_Controller.SetInspectorAssetStatus("Duplicating asset in the isolated asset worker.");
        }
        catch (const std::exception& error)
        {
            m_Controller.ReportInspectorAssetError(std::string("Asset duplication failed: ") + error.what());
        }
    }
    ui.SameLine();
    if (ui.Button("Move to Trash"))
    {
        try
        {
            m_Controller.TrashInspectorAsset(record->Id);
            selectedAsset = {};
            if (!pinned)
                m_Controller.SetInspectorSelectedAsset({});
            m_EditingAsset = {};
            m_Controller.SetInspectorAssetStatus("Moving asset to recoverable trash in the isolated asset worker.");
        }
        catch (const std::exception& error)
        {
            m_Controller.ReportInspectorAssetError(std::string("Asset trash operation failed: ") + error.what());
        }
    }
}
