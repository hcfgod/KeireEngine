#include "KeireClient/Editor/RiggingStudioPanel.h"

#include "KeireClient/EditorWorkspaceLayer.h"

#include <algorithm>
#include <array>
#include <ranges>
#include <utility>

namespace KeireEditor
{
    namespace
    {
        [[nodiscard]] bool IsModelRecord(const Keire::AssetSourceRecord& record) noexcept
        {
            return record.Importer == "Keire.Mesh" || record.Importer == "Keire.Model";
        }

        [[nodiscard]] const Keire::AssetSourceRecord*
        FindModelRecord(const std::span<const Keire::AssetSourceRecord> records, const Keire::AssetId selected)
        {
            const auto direct = std::ranges::find(records, selected, &Keire::AssetSourceRecord::Id);
            if (direct != records.end() && IsModelRecord(*direct))
                return &*direct;
            const auto parent =
                std::ranges::find_if(records,
                                     [selected](const auto& record)
                                     {
                                         return IsModelRecord(record) &&
                                                std::ranges::find(record.SubAssets, selected) != record.SubAssets.end();
                                     });
            return parent == records.end() ? nullptr : &*parent;
        }

        [[nodiscard]] std::string ReadChoice(const Keire::AssetImportSettings& settings, const std::string_view key,
                                             std::string fallback)
        {
            const auto found = settings.find(key);
            if (found != settings.end())
                if (const auto* value = std::get_if<std::string>(&found->second))
                    return *value;
            return fallback;
        }

        [[nodiscard]] std::string_view AssetTypeLabel(const Keire::AssetTypeId type) noexcept
        {
            if (type == Keire::SkeletonAsset::StaticType())
                return "Skeleton";
            if (type == Keire::RigDefinitionAsset::StaticType())
                return "Rig Definition";
            if (type == Keire::SkinnedMeshAsset::StaticType())
                return "Skin Weights";
            if (type == Keire::AnimationClipAsset::StaticType())
                return "Animation Clip";
            return "Generated Asset";
        }

        struct ModelAnimationAssets final
        {
            Keire::AssetId Skeleton;
            Keire::AssetId Rig;
            std::vector<Keire::AssetId> Clips;
        };

        [[nodiscard]] ModelAnimationAssets DescribeModelAnimation(const Keire::AssetSourceRecord& model,
                                                                  const Keire::AssetSystem& assets)
        {
            ModelAnimationAssets result;
            for (const auto subAsset : model.SubAssets)
            {
                const auto type = assets.TryGetType(subAsset);
                if (!type)
                    continue;
                if (*type == Keire::SkeletonAsset::StaticType())
                    result.Skeleton = subAsset;
                else if (*type == Keire::RigDefinitionAsset::StaticType())
                    result.Rig = subAsset;
                else if (*type == Keire::AnimationClipAsset::StaticType())
                    result.Clips.push_back(subAsset);
            }
            return result;
        }

        [[nodiscard]] const Keire::AssetSourceRecord*
        FindParentModel(const std::span<const Keire::AssetSourceRecord> records, const Keire::AssetId subAsset)
        {
            const auto found =
                std::ranges::find_if(records,
                                     [subAsset](const auto& record)
                                     {
                                         return IsModelRecord(record) &&
                                                std::ranges::find(record.SubAssets, subAsset) != record.SubAssets.end();
                                     });
            return found == records.end() ? nullptr : &*found;
        }

        [[nodiscard]] std::string_view RetargetMatchLabel(const Keire::AnimationRetargetMatch match) noexcept
        {
            switch (match)
            {
            case Keire::AnimationRetargetMatch::Unmapped:
                return "unmapped";
            case Keire::AnimationRetargetMatch::ExactName:
                return "exact name";
            case Keire::AnimationRetargetMatch::Semantic:
                return "semantic";
            case Keire::AnimationRetargetMatch::TargetConflict:
                return "target conflict";
            case Keire::AnimationRetargetMatch::Hierarchy:
                return "hierarchy";
            }
            return "unknown";
        }
    } // namespace

    void RiggingStudioPanel::Attach(Keire::UiWorkspace& workspace)
    {
        m_Registration = workspace.RegisterPanel({"editor.rigging-studio", "Rigging Studio", false});
    }

    void RiggingStudioPanel::Draw(Keire::UiFrame& ui)
    {
        if (auto panel = ui.BeginPanel(m_Registration); panel)
        {
            const auto records = m_Controller.RiggingStudioRecords();
            const auto selectedAsset = m_Controller.RiggingStudioSelectedAsset();
            const auto* selectedModel = FindModelRecord(records, selectedAsset);
            const Keire::AssetSourceRecord* model = nullptr;
            if (m_Registration.Locked())
            {
                if (!m_LockedAsset)
                    m_LockedAsset = selectedModel ? selectedModel->Id : m_DraftAsset;
                model = FindModelRecord(records, m_LockedAsset);
                if (!model)
                {
                    m_Registration.SetLocked(false);
                    m_LockedAsset = {};
                }
            }
            if (!m_Registration.Locked())
            {
                m_LockedAsset = {};
                model = selectedModel;
                if (!model && !selectedAsset && ui.WindowFocused() && m_DraftAsset)
                    model = FindModelRecord(records, m_DraftAsset);
                if (!model && !ui.WindowFocused())
                {
                    m_DraftAsset = {};
                    m_Draft.clear();
                    m_Dirty = false;
                    m_Message.clear();
                }
            }
            const auto& theme = m_Controller.RiggingStudioTheme();
            ui.TextColored(theme.Accent, "RIGGING STUDIO");
            if (m_Registration.Locked())
            {
                ui.SameLine();
                ui.TextColored(theme.MutedText, "PINNED");
            }
            ui.TextColored(theme.MutedText,
                           "Generate, validate, and publish a deterministic runtime skeleton and skin cache.");
            ui.Separator();
            if (!model)
            {
                ui.TextColored(theme.Warning, "Select an imported model or one of its generated rig assets.");
                ui.Text("Supported profiles: humanoid, biped, quadruped, and authored custom rigs.");
                return;
            }

            if (m_DraftAsset != model->Id)
            {
                m_DraftAsset = model->Id;
                m_Draft = model->ImportSettings;
                m_Dirty = false;
                m_Message.clear();
                m_RetargetDiagnostics.reset();
                m_DiagnosticSourceClip = {};
                m_DiagnosticSourceSkeleton = {};
                m_DiagnosticSourceRig = {};
                m_DiagnosticTargetSkeleton = {};
                m_DiagnosticTargetRig = {};
            }

            ui.Text(model->RelativePath.generic_string());
            ui.TextColored(theme.MutedText, "Stable model ID  " + model->Id.ToString());
            ui.Separator();

            auto rigSource = ReadChoice(m_Draft, "rigSource", "embedded");
            constexpr std::array RigSources{"embedded", "generate", "none"};
            if (auto combo = ui.BeginCombo("Rig Source", rigSource); combo)
            {
                for (const auto value : RigSources)
                {
                    if (ui.Selectable(value, rigSource == value))
                    {
                        rigSource = value;
                        m_Draft["rigSource"] = rigSource;
                        m_Dirty = true;
                    }
                }
            }

            if (rigSource == "generate")
            {
                auto profile = ReadChoice(m_Draft, "rigProfile", "humanoid");
                constexpr std::array Profiles{"humanoid", "biped", "quadruped"};
                if (auto combo = ui.BeginCombo("Profile", profile); combo)
                {
                    for (const auto value : Profiles)
                    {
                        if (ui.Selectable(value, profile == value))
                        {
                            profile = value;
                            m_Draft["rigProfile"] = profile;
                            m_Dirty = true;
                        }
                    }
                }

                auto influences = ReadChoice(m_Draft, "maximumInfluences", "4");
                if (auto combo = ui.BeginCombo("Maximum Influences", influences); combo)
                {
                    for (const auto value : {"4", "8"})
                    {
                        if (ui.Selectable(value, influences == value))
                        {
                            influences = value;
                            m_Draft["maximumInfluences"] = influences;
                            m_Dirty = true;
                        }
                    }
                }

                auto method = ReadChoice(m_Draft, "skinningMethod", "linearBlend");
                if (auto combo = ui.BeginCombo("Skinning", method); combo)
                {
                    for (const auto value : {"linearBlend", "dualQuaternion"})
                    {
                        if (ui.Selectable(value, method == value))
                        {
                            method = value;
                            m_Draft["skinningMethod"] = method;
                            m_Dirty = true;
                        }
                    }
                }
                ui.TextColored(theme.MutedText,
                               "Linear blend uses the GPU skin cache. Dual quaternion preserves twisting volume.");
            }
            else if (rigSource == "embedded")
            {
                ui.TextColored(theme.MutedText,
                               "The imported hierarchy, bind pose, weights, and clips remain authoritative.");
            }
            else
            {
                ui.TextColored(theme.Warning, "Rigging is disabled; this model imports as static geometry.");
            }

            if (rigSource != "none")
            {
                auto compression = ReadChoice(m_Draft, "animationCompression", "balanced");
                if (auto combo = ui.BeginCombo("Animation Compression", compression); combo)
                {
                    for (const auto value : {"none", "light", "balanced", "aggressive"})
                    {
                        if (ui.Selectable(value, compression == value))
                        {
                            compression = value;
                            m_Draft["animationCompression"] = compression;
                            m_Dirty = true;
                        }
                    }
                }
                ui.TextColored(theme.MutedText,
                               "Balanced preserves millimeter-scale translation and quarter-degree rotation error.");

                auto motion = ReadChoice(m_Draft, "animationMotion", "rootMotion");
                if (auto combo = ui.BeginCombo("Animation Motion", motion); combo)
                {
                    for (const auto value : {"rootMotion", "authored", "inPlaceHorizontal", "inPlace"})
                    {
                        if (ui.Selectable(value, motion == value))
                        {
                            motion = value;
                            m_Draft["animationMotion"] = motion;
                            m_Dirty = true;
                        }
                    }
                }
                ui.TextColored(theme.MutedText,
                               "In-place modes lock semantic pelvis/root translation for scripted controllers.");
            }

            ui.Separator();
            if (ui.Button(m_Dirty ? "Apply & Regenerate" : "Regenerate"))
            {
                try
                {
                    m_Controller.ApplyRiggingStudioSettings(model->Id, m_Draft);
                    m_Dirty = false;
                    m_Message = "Rig regeneration queued in the isolated asset worker.";
                }
                catch (const std::exception& error)
                {
                    m_Message = error.what();
                    m_Controller.ReportRiggingStudioError(m_Message);
                }
            }
            ui.SameLine();
            if (ui.Button("Revert"))
            {
                m_Draft = model->ImportSettings;
                m_Dirty = false;
                m_Message = "Draft settings reverted.";
            }
            if (!m_Message.empty())
                ui.TextColored(theme.MutedText, m_Message);
            if (!m_Controller.RiggingStudioStatus().empty())
                ui.TextColored(theme.MutedText, m_Controller.RiggingStudioStatus());

            ui.Separator();
            if (auto generated = ui.BeginTreeNode(
                    "Generated runtime assets (" + std::to_string(model->SubAssets.size()) + ")", true);
                generated)
            {
                for (const auto subAsset : model->SubAssets)
                {
                    const auto record = std::ranges::find(records, subAsset, &Keire::AssetSourceRecord::Id);
                    const auto assets = m_Controller.RiggingStudioAssets();
                    std::optional<Keire::AssetTypeId> type;
                    if (record != records.end())
                        type = record->Type;
                    else if (assets)
                        type = assets->TryGetType(subAsset);
                    auto id = ui.PushId(subAsset.ToString());
                    ui.Text(type ? AssetTypeLabel(*type) : std::string_view("Generated Asset"));
                    ui.SameLine();
                    if (ui.Button("Reveal"))
                        m_Controller.RevealRiggingStudioAsset(subAsset);

                    if (type && *type == Keire::RigDefinitionAsset::StaticType())
                    {
                        if (assets)
                        {
                            const auto rig =
                                assets->Load<Keire::RigDefinitionAsset>(subAsset, Keire::AssetPriority::Normal)
                                    .TryGetLoaded();
                            if (rig)
                            {
                                ui.TextColored(theme.MutedText,
                                               std::to_string(rig->Definition().Bones.size()) + " bones  |  " +
                                                   std::to_string(rig->Definition().Chains.size()) + " IK chains");
                                if (auto mapping = ui.BeginTreeNode("Semantic bone map", false); mapping)
                                {
                                    for (const auto& bone : rig->Definition().Bones)
                                    {
                                        ui.Text(std::string(Keire::RigBoneSemanticName(bone.Semantic)) + "  ->  " +
                                                bone.Name);
                                    }
                                }
                            }
                        }
                    }
                }
            }

            ui.Separator();
            if (auto retarget = ui.BeginTreeNode("Animation Retargeting", false); retarget)
            {
                const auto assets = m_Controller.RiggingStudioAssets();
                if (!assets)
                {
                    ui.TextColored(theme.Warning, "The runtime asset catalog is unavailable.");
                    return;
                }

                struct ClipChoice final
                {
                    Keire::AssetId Clip;
                    const Keire::AssetSourceRecord* Model = nullptr;
                };
                std::vector<ClipChoice> clips;
                for (const auto& candidate : records)
                {
                    if (!IsModelRecord(candidate))
                        continue;
                    const auto animation = DescribeModelAnimation(candidate, *assets);
                    for (const auto clip : animation.Clips)
                        clips.push_back({clip, &candidate});
                }
                const auto selected =
                    std::ranges::find_if(clips, [this](const auto& value) { return value.Clip == m_SourceClip; });
                const auto preview = selected == clips.end()
                                         ? std::string("Select source clip")
                                         : selected->Model->RelativePath.filename().generic_string() + " / " +
                                               selected->Clip.ToString().substr(0, 8);
                if (auto combo = ui.BeginCombo("Source Clip", preview); combo)
                {
                    for (const auto& choice : clips)
                    {
                        auto id = ui.PushId(choice.Clip.ToString());
                        const auto label = choice.Model->RelativePath.filename().generic_string() + " / " +
                                           choice.Clip.ToString().substr(0, 8);
                        if (ui.Selectable(label, choice.Clip == m_SourceClip))
                        {
                            m_SourceClip = choice.Clip;
                            m_RetargetName = choice.Model->RelativePath.stem().string() + " Retargeted";
                        }
                    }
                }
                (void)ui.InputText("Output Name", m_RetargetName);
                if (!m_SourceClip)
                {
                    ui.TextColored(theme.MutedText, "Choose a generated animation clip to inspect compatibility.");
                    return;
                }

                const auto* sourceModel = FindParentModel(records, m_SourceClip);
                const auto sourceAnimation =
                    sourceModel ? DescribeModelAnimation(*sourceModel, *assets) : ModelAnimationAssets{};
                const auto targetAnimation = DescribeModelAnimation(*model, *assets);
                if (!sourceModel || !sourceAnimation.Skeleton || !sourceAnimation.Rig || !targetAnimation.Skeleton ||
                    !targetAnimation.Rig)
                {
                    ui.TextColored(theme.Warning,
                                   "Both source and target models need generated or embedded skeleton and rig assets.");
                    return;
                }

                const auto sourceClip =
                    assets->Load<Keire::AnimationClipAsset>(m_SourceClip, Keire::AssetPriority::Normal).TryGetLoaded();
                const auto sourceSkeleton =
                    assets->Load<Keire::SkeletonAsset>(sourceAnimation.Skeleton, Keire::AssetPriority::Normal)
                        .TryGetLoaded();
                const auto sourceRig =
                    assets->Load<Keire::RigDefinitionAsset>(sourceAnimation.Rig, Keire::AssetPriority::Normal)
                        .TryGetLoaded();
                const auto targetSkeleton =
                    assets->Load<Keire::SkeletonAsset>(targetAnimation.Skeleton, Keire::AssetPriority::Normal)
                        .TryGetLoaded();
                const auto targetRig =
                    assets->Load<Keire::RigDefinitionAsset>(targetAnimation.Rig, Keire::AssetPriority::Normal)
                        .TryGetLoaded();
                if (!sourceClip || !sourceSkeleton || !sourceRig || !targetSkeleton || !targetRig)
                {
                    ui.TextColored(theme.MutedText, "Loading source and target rig data...");
                    return;
                }

                ui.TextColored(theme.MutedText,
                               std::to_string(sourceRig->Definition().Bones.size()) + " source bones  ->  " +
                                   std::to_string(targetRig->Definition().Bones.size()) + " target bones");
                if (m_DiagnosticSourceClip != sourceClip || m_DiagnosticSourceSkeleton != sourceSkeleton ||
                    m_DiagnosticSourceRig != sourceRig || m_DiagnosticTargetSkeleton != targetSkeleton ||
                    m_DiagnosticTargetRig != targetRig)
                {
                    m_DiagnosticSourceClip = sourceClip;
                    m_DiagnosticSourceSkeleton = sourceSkeleton;
                    m_DiagnosticSourceRig = sourceRig;
                    m_DiagnosticTargetSkeleton = targetSkeleton;
                    m_DiagnosticTargetRig = targetRig;
                    try
                    {
                        m_RetargetDiagnostics =
                            Keire::DiagnoseAnimationRetargeting(*sourceSkeleton, sourceRig->Definition(), *sourceClip,
                                                                *targetSkeleton, targetRig->Definition());
                    }
                    catch (const std::exception& error)
                    {
                        m_RetargetDiagnostics.reset();
                        m_Message = error.what();
                    }
                }

                const bool compatible = m_RetargetDiagnostics && m_RetargetDiagnostics->Compatible();
                if (m_RetargetDiagnostics)
                {
                    const auto& diagnostics = *m_RetargetDiagnostics;
                    ui.TextColored(compatible ? theme.Success : theme.Error,
                                   std::to_string(diagnostics.MappedTrackCount) + " / " +
                                       std::to_string(diagnostics.SourceTrackCount) + " tracks mapped  |  " +
                                       std::to_string(diagnostics.ExactNameMatchCount) + " exact  |  " +
                                       std::to_string(diagnostics.HierarchyMatchCount) + " hierarchy  |  " +
                                       std::to_string(diagnostics.SemanticMatchCount) + " semantic");
                    ui.TextColored(diagnostics.RootMotionMapped ? theme.Success : theme.Warning,
                                   diagnostics.RootMotionMapped ? "Root motion mapping is compatible."
                                                                : "Root motion will be disabled for this bake.");
                    std::size_t scaleFallbacks = 0;
                    for (const auto& mapping : diagnostics.Mappings)
                        scaleFallbacks += mapping.ScaleFallbackKeyCount;
                    if (scaleFallbacks != 0)
                        ui.TextColored(theme.Warning, std::to_string(scaleFallbacks) +
                                                          " animated scale components will use target bind scale.");
                    if (auto details = ui.BeginTreeNode("Retarget diagnostics", !compatible); details)
                    {
                        for (const auto& mapping : diagnostics.Mappings)
                        {
                            const auto target = mapping.TargetBone ? mapping.TargetName : std::string("(none)");
                            ui.Text(mapping.SourceName + "  ->  " + target + "  [" +
                                    std::string(RetargetMatchLabel(mapping.Match)) + "]  scale " +
                                    std::to_string(mapping.TranslationScale));
                        }
                        for (const auto& diagnostic : diagnostics.Messages)
                        {
                            const auto color = diagnostic.Severity == Keire::RigDiagnosticSeverity::Error ? theme.Error
                                               : diagnostic.Severity == Keire::RigDiagnosticSeverity::Warning
                                                   ? theme.Warning
                                                   : theme.MutedText;
                            ui.TextColored(color, diagnostic.Code + "  " + diagnostic.Message);
                        }
                    }
                }
                if (auto disabled = ui.BeginDisabled(!compatible); disabled)
                {
                    if (ui.Button("Bake Retargeted Clip"))
                    {
                        try
                        {
                            const auto baked = Keire::RetargetAnimationClipWithDiagnostics(
                                *sourceSkeleton, sourceRig->Definition(), *sourceClip, targetAnimation.Skeleton,
                                *targetSkeleton, targetRig->Definition());
                            m_Controller.CreateRiggingStudioRetarget(
                                m_RetargetName,
                                Keire::AnimationClipAsset::Encode(baked.Clip->Skeleton(), baked.Clip->Duration(),
                                                                  baked.Clip->Tracks(), baked.Clip->Events(),
                                                                  baked.Clip->RootMotion()));
                            m_Message = "Retargeted clip creation queued in the isolated asset worker.";
                        }
                        catch (const std::exception& error)
                        {
                            m_Message = error.what();
                            m_Controller.ReportRiggingStudioError(m_Message);
                        }
                    }
                }
            }
        }
    }
} // namespace KeireEditor

const Keire::UiThemeDefinition& EditorWorkspaceLayer::RiggingStudioTheme() const noexcept { return m_Theme; }

Keire::Ref<Keire::AssetDatabase> EditorWorkspaceLayer::RiggingStudioDatabase() const noexcept
{
    return m_AssetDatabase;
}

Keire::Ref<Keire::AssetSystem> EditorWorkspaceLayer::RiggingStudioAssets() const noexcept { return Owner().Assets(); }

std::span<const Keire::AssetSourceRecord> EditorWorkspaceLayer::RiggingStudioRecords() const noexcept
{
    return m_AssetRecords;
}

Keire::AssetId EditorWorkspaceLayer::RiggingStudioSelectedAsset() const noexcept { return m_SelectedAsset; }

std::string_view EditorWorkspaceLayer::RiggingStudioStatus() const noexcept { return m_AssetStatus; }

void EditorWorkspaceLayer::ApplyRiggingStudioSettings(const Keire::AssetId asset,
                                                      const Keire::AssetImportSettings& settings)
{
    if (!m_AssetDatabase)
        throw std::runtime_error("The project asset database is unavailable.");
    m_AssetDatabase->SetImportSettings(asset, settings);
    m_AssetDatabase->RequestReimport(asset);
    RefreshAssetBrowserRecords();
    m_SelectedAsset = asset;
    ImportAssets();
}

void EditorWorkspaceLayer::CreateRiggingStudioRetarget(const std::string_view name, std::vector<std::byte> bytes)
{
    if (!m_AssetDatabase || !m_AssetOperations)
        throw std::runtime_error("Asset creation services are unavailable.");
    if (m_AssetOperations->Busy())
        throw std::runtime_error("Wait for the active asset operation before creating a retargeted clip.");
    if (name.empty() || name == "." || name == ".." || name.find_first_of("/\\") != std::string_view::npos)
        throw std::invalid_argument("Retargeted clip name must be one non-empty path component.");
    const auto directory = m_AssetBrowserPanel ? m_AssetBrowserPanel->CurrentFolder() : std::filesystem::path{};
    auto destination = directory / (std::string(name) + ".keireanim");
    for (std::size_t copy = 2; m_AssetDatabase->Find(destination); ++copy)
        destination = directory / (std::string(name) + " " + std::to_string(copy) + ".keireanim");
    m_AssetOperations->QueueCreateAsset(
        destination, std::move(bytes), {},
        {.FollowUp = KeireEditor::AssetOperationFollowUp::Reveal, .UndoName = "Create Retargeted Animation Clip"});
    m_AssetStatus = "Creating " + destination.generic_string() + " in the isolated asset worker.";
}

void EditorWorkspaceLayer::RevealRiggingStudioAsset(const Keire::AssetId asset)
{
    m_SelectedAsset = asset;
    if (m_AssetBrowserPanel)
        m_AssetBrowserPanel->RevealAsset(asset);
}

void EditorWorkspaceLayer::ReportRiggingStudioError(std::string message) noexcept { SetAssetError(std::move(message)); }
