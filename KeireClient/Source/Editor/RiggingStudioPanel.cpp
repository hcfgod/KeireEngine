#include "KeireClient/Editor/RiggingStudioPanel.h"

#include "KeireClient/Editor/RiggingStudioValidation.h"
#include "KeireClient/EditorWorkspaceLayer.h"
#include "KeireClientInternal/Editor/RetargetMappingActions.h"

#include <algorithm>
#include <array>
#include <ranges>
#include <utility>

namespace KeireEditor
{
    namespace
    {
        [[nodiscard]] std::string_view RigOptionLabel(const std::string_view value) noexcept
        {
            constexpr std::pair<std::string_view, std::string_view> labels[]{
                {"embedded", "Keep imported skeleton"},
                {"generate", "Generate a skeleton"},
                {"none", "None"},
                {"humanoid", "Humanoid"},
                {"biped", "Biped"},
                {"quadruped", "Quadruped"},
                {"custom", "Custom / imported names"},
                {"linearBlend", "Linear blend"},
                {"dualQuaternion", "Dual quaternion"},
                {"light", "Light"},
                {"balanced", "Balanced"},
                {"aggressive", "Aggressive"},
                {"rootMotion", "Extract root motion"},
                {"authored", "Keep authored motion"},
                {"inPlaceHorizontal", "In place (horizontal)"},
                {"inPlace", "In place (all axes)"}};
            for (const auto& [key, label] : labels)
                if (key == value)
                    return label;
            return value;
        }

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
            case Keire::AnimationRetargetMatch::Manual:
                return "manual";
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
                if (!model && m_DraftAsset)
                    model = FindModelRecord(records, m_DraftAsset);
            }
            const auto& theme = m_Controller.RiggingStudioTheme();
            ui.TextColored(theme.Accent, "RIGGING STUDIO");
            if (m_Registration.Locked())
            {
                ui.SameLine();
                ui.TextColored(theme.MutedText, "PINNED");
            }
            ui.TextColored(theme.MutedText, "Choose a model, configure its rig, then review animation compatibility.");
            ui.Separator();
            if (auto picker = ui.BeginCombo("Model", model ? model->RelativePath.generic_string() : "Choose a model");
                picker)
            {
                for (const auto& candidate : records)
                {
                    if (!IsModelRecord(candidate))
                        continue;
                    auto id = ui.PushId(candidate.Id.ToString());
                    if (ui.Selectable(candidate.RelativePath.generic_string(), model && model->Id == candidate.Id))
                    {
                        m_Controller.RevealRiggingStudioAsset(candidate.Id);
                        model = &candidate;
                        if (m_Registration.Locked())
                            m_LockedAsset = candidate.Id;
                    }
                }
            }
            if (!model)
            {
                ui.Text("Import an FBX, glTF, or GLB model into the Project panel, then choose it above.");
                ui.TextColored(theme.MutedText,
                               "Keep an existing skeleton, or generate a humanoid, biped, or quadruped rig.");
                ui.TextColored(theme.MutedText, "For spiders and other custom creatures, import an authored skeleton.");
                return;
            }

            if (m_DraftAsset != model->Id)
            {
                m_DraftAsset = model->Id;
                m_Message.clear();
                m_MessageError = false;
                m_ReviewedPartialMapping = false;
                m_RetargetDiagnostics.reset();
                m_DiagnosticSourceClip = {};
                m_DiagnosticSourceSkeleton = {};
                m_DiagnosticSourceRig = {};
                m_DiagnosticTargetSkeleton = {};
                m_DiagnosticTargetRig = {};
            }
            auto& draft = m_Drafts.Select(model->Id, model->ImportSettings);
            auto& draftValues = draft.Values;
            auto& draftDirty = draft.Dirty;
            bool importFailed = false;
            if (const auto database = m_Controller.RiggingStudioDatabase())
            {
                const auto status = database->ImportStatus(model->Id);
                importFailed = status.State == Keire::AssetImportState::Failed;
                if (importFailed)
                {
                    ui.TextColoredWrapped(theme.Error,
                                          "Import failed. The preview may show the last good result. Correct the "
                                          "source or settings, then regenerate before baking.");
                    for (const auto& diagnostic : status.Diagnostics)
                        ui.TextColoredWrapped(
                            diagnostic.Severity == Keire::AssetDiagnosticSeverity::Error ? theme.Error : theme.Warning,
                            diagnostic.Message);
                }
            }

            ui.Text(model->RelativePath.generic_string());
            if (draftDirty)
                ui.TextColored(theme.Warning, "Unapplied changes — retained while you inspect other models.");
            if (draftDirty && draft.Baseline != model->ImportSettings)
                ui.TextColoredWrapped(theme.Warning, "Import settings changed elsewhere. Apply replaces them with this "
                                                     "draft; Revert loads the current settings.");
            ui.Separator();

            auto rigSource = ReadChoice(draftValues, "rigSource", "embedded");
            constexpr std::array RigSources{"embedded", "generate", "none"};
            if (auto combo = ui.BeginCombo("Rig Source", RigOptionLabel(rigSource)); combo)
            {
                for (const auto value : RigSources)
                {
                    if (ui.Selectable(RigOptionLabel(value), rigSource == value))
                    {
                        rigSource = value;
                        draftValues["rigSource"] = rigSource;
                        draftDirty = true;
                    }
                }
            }

            if (rigSource != "none")
            {
                auto profile = ReadChoice(draftValues, "rigProfile", "humanoid");
                constexpr std::array Profiles{"humanoid", "biped", "quadruped", "custom"};
                if (auto combo = ui.BeginCombo("Mapping Profile", RigOptionLabel(profile)); combo)
                {
                    for (const auto value : Profiles)
                    {
                        if (ui.Selectable(RigOptionLabel(value), profile == value))
                        {
                            profile = value;
                            draftValues["rigProfile"] = profile;
                            draftDirty = true;
                        }
                    }
                }

                auto influences = ReadChoice(draftValues, "maximumInfluences", "4");
                if (auto combo = ui.BeginCombo("Maximum Influences", RigOptionLabel(influences)); combo)
                {
                    for (const auto value : {"4", "8"})
                    {
                        if (ui.Selectable(RigOptionLabel(value), influences == value))
                        {
                            influences = value;
                            draftValues["maximumInfluences"] = influences;
                            draftDirty = true;
                        }
                    }
                }

                auto method = ReadChoice(draftValues, "skinningMethod", "linearBlend");
                if (auto combo = ui.BeginCombo("Skinning", RigOptionLabel(method)); combo)
                {
                    for (const auto value : {"linearBlend", "dualQuaternion"})
                    {
                        if (ui.Selectable(RigOptionLabel(value), method == value))
                        {
                            method = value;
                            draftValues["skinningMethod"] = method;
                            draftDirty = true;
                        }
                    }
                }
                ui.TextColored(theme.MutedText,
                               "Linear blend uses the GPU skin cache. Dual quaternion preserves twisting volume.");
            }
            if (rigSource == "embedded")
            {
                ui.TextColored(theme.MutedText,
                               "The imported hierarchy, bind pose, weights, and clips remain authoritative.");
                ui.TextColoredWrapped(
                    theme.MutedText,
                    "Mapping profiles suggest semantic roles; they do not convert a custom creature into a humanoid.");
            }
            else if (rigSource == "none")
            {
                ui.TextColored(theme.Warning, "Rigging is disabled; this model imports as static geometry.");
            }

            if (rigSource != "none")
            {
                auto compression = ReadChoice(draftValues, "animationCompression", "balanced");
                if (auto combo = ui.BeginCombo("Animation Compression", RigOptionLabel(compression)); combo)
                {
                    for (const auto value : {"none", "light", "balanced", "aggressive"})
                    {
                        if (ui.Selectable(RigOptionLabel(value), compression == value))
                        {
                            compression = value;
                            draftValues["animationCompression"] = compression;
                            draftDirty = true;
                        }
                    }
                }
                ui.TextColored(theme.MutedText,
                               "Balanced preserves millimeter-scale translation and quarter-degree rotation error.");

                auto motion = ReadChoice(draftValues, "animationMotion", "rootMotion");
                if (auto combo = ui.BeginCombo("Animation Motion", RigOptionLabel(motion)); combo)
                {
                    for (const auto value : {"rootMotion", "authored", "inPlaceHorizontal", "inPlace"})
                    {
                        if (ui.Selectable(RigOptionLabel(value), motion == value))
                        {
                            motion = value;
                            draftValues["animationMotion"] = motion;
                            draftDirty = true;
                        }
                    }
                }
                ui.TextColored(theme.MutedText,
                               "In-place modes lock semantic pelvis/root translation for scripted controllers.");
            }

            const bool invalidCustomGeneration =
                rigSource == "generate" && ReadChoice(draftValues, "rigProfile", "humanoid") == "custom";
            if (invalidCustomGeneration)
                ui.TextColoredWrapped(theme.Warning, "Custom mapping requires an imported skeleton. Choose Keep "
                                                     "imported skeleton, or select a supported generation profile.");
            ui.Separator();
            {
                auto invalidGenerationScope = ui.BeginDisabled(invalidCustomGeneration);
                if (ui.Button(draftDirty ? "Apply & Regenerate" : "Regenerate"))
                {
                    try
                    {
                        m_Controller.ApplyRiggingStudioSettings(model->Id, draftValues);
                        draftDirty = false;
                        m_Message.clear();
                        m_MessageError = false;
                    }
                    catch (const std::exception& error)
                    {
                        m_Message = error.what();
                        m_MessageError = true;
                        m_Controller.ReportRiggingStudioError(m_Message);
                    }
                    // Applying refreshes the controller's record storage. Reacquire
                    // the selected model next frame before using any record views.
                    return;
                }
            }
            ui.SameLine();
            if (ui.Button("Revert"))
            {
                m_Drafts.Revert(model->Id, model->ImportSettings);
                m_Message = "Draft settings reverted.";
                m_MessageError = false;
            }
            if (!m_Message.empty())
                ui.TextColored(m_MessageError ? theme.Error : theme.MutedText, m_Message);
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
                    if (assets)
                        if (const auto metadata = assets->TryGetMetadata(subAsset);
                            metadata && !metadata->DisplayName.empty())
                        {
                            ui.SameLine();
                            ui.Text(metadata->DisplayName);
                        }
                    ui.SameLine();
                    if (ui.Button(record != records.end() ? "Reveal" : "Reveal Source"))
                        m_Controller.RevealRiggingStudioAsset(subAsset);
                    if (type && *type == Keire::AnimationClipAsset::StaticType())
                    {
                        ui.SameLine();
                        if (ui.Button("Preview"))
                        {
                            try
                            {
                                m_Controller.PreviewRiggingStudioClip(subAsset);
                            }
                            catch (const std::exception& error)
                            {
                                m_Controller.ReportRiggingStudioError(error.what());
                            }
                        }
                    }

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
                                if (rig->Definition().Chains.empty())
                                    ui.TextColoredWrapped(
                                        theme.Warning,
                                        "No semantic chains were inferred. Imported animation is preserved; "
                                        "use explicit bone names for custom IK.");
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
                    std::string Name;
                };
                std::vector<ClipChoice> clips;
                for (const auto& candidate : records)
                {
                    if (!IsModelRecord(candidate))
                        continue;
                    const auto animation = DescribeModelAnimation(candidate, *assets);
                    for (const auto clip : animation.Clips)
                    {
                        const auto metadata = assets->TryGetMetadata(clip);
                        const auto name = metadata && !metadata->DisplayName.empty()
                                              ? metadata->DisplayName
                                              : "Unnamed clip " + clip.ToString().substr(0, 8);
                        clips.push_back({clip, &candidate, name});
                    }
                }
                const auto selected =
                    std::ranges::find_if(clips, [this](const auto& value) { return value.Clip == m_SourceClip; });
                const auto preview =
                    selected == clips.end()
                        ? std::string("Select source clip")
                        : selected->Model->RelativePath.filename().generic_string() + " / " + selected->Name;
                if (auto combo = ui.BeginCombo("Source Clip", preview); combo)
                {
                    for (const auto& choice : clips)
                    {
                        auto id = ui.PushId(choice.Clip.ToString());
                        const auto label = choice.Model->RelativePath.filename().generic_string() + " / " + choice.Name;
                        if (ui.Selectable(label, choice.Clip == m_SourceClip))
                        {
                            m_SourceClip = choice.Clip;
                            m_RetargetName =
                                SuggestedRetargetName(choice.Model->RelativePath.stem().string(), choice.Name);
                        }
                    }
                }
                (void)ui.InputText("Output Name", m_RetargetName);
                const auto nameError = RetargetOutputNameError(m_RetargetName);
                if (!nameError.empty())
                    ui.TextColoredWrapped(theme.Warning, nameError);
                if (!m_SourceClip)
                {
                    ui.TextColored(theme.MutedText, "Choose a generated animation clip to inspect compatibility.");
                    return;
                }

                const auto* sourceModel = FindParentModel(records, m_SourceClip);
                bool sourceImportFailed = false;
                if (const auto database = m_Controller.RiggingStudioDatabase(); database && sourceModel)
                {
                    const auto sourceStatus = database->ImportStatus(sourceModel->Id);
                    sourceImportFailed = sourceStatus.State == Keire::AssetImportState::Failed;
                    if (sourceImportFailed)
                    {
                        ui.TextColoredWrapped(theme.Error,
                                              "Source model import failed. The clip may be the last good result. "
                                              "Repair and reimport the source model before baking.");
                        for (const auto& diagnostic : sourceStatus.Diagnostics)
                            ui.TextColoredWrapped(diagnostic.Severity == Keire::AssetDiagnosticSeverity::Error
                                                      ? theme.Error
                                                      : theme.Warning,
                                                  diagnostic.Message);
                    }
                }
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

                const auto sourceClipHandle =
                    assets->Load<Keire::AnimationClipAsset>(m_SourceClip, Keire::AssetPriority::Normal);
                const auto sourceSkeletonHandle =
                    assets->Load<Keire::SkeletonAsset>(sourceAnimation.Skeleton, Keire::AssetPriority::Normal);
                const auto sourceRigHandle =
                    assets->Load<Keire::RigDefinitionAsset>(sourceAnimation.Rig, Keire::AssetPriority::Normal);
                const auto targetSkeletonHandle =
                    assets->Load<Keire::SkeletonAsset>(targetAnimation.Skeleton, Keire::AssetPriority::Normal);
                const auto targetRigHandle =
                    assets->Load<Keire::RigDefinitionAsset>(targetAnimation.Rig, Keire::AssetPriority::Normal);
                bool loadFailed = false;
                const auto reportLoadFailure = [&](const auto& handle, const std::string_view label)
                {
                    if (const auto error = RetargetAssetLoadError(handle, label); !error.empty())
                    {
                        ui.TextColoredWrapped(theme.Error, error);
                        loadFailed = true;
                    }
                };
                reportLoadFailure(sourceClipHandle, "Source clip");
                reportLoadFailure(sourceSkeletonHandle, "Source skeleton");
                reportLoadFailure(sourceRigHandle, "Source rig");
                reportLoadFailure(targetSkeletonHandle, "Target skeleton");
                reportLoadFailure(targetRigHandle, "Target rig");
                if (loadFailed)
                    return;
                const auto sourceClip = sourceClipHandle.TryGetLoaded();
                const auto sourceSkeleton = sourceSkeletonHandle.TryGetLoaded();
                const auto sourceRig = sourceRigHandle.TryGetLoaded();
                const auto targetSkeleton = targetSkeletonHandle.TryGetLoaded();
                const auto targetRig = targetRigHandle.TryGetLoaded();
                if (!sourceClip || !sourceSkeleton || !sourceRig || !targetSkeleton || !targetRig)
                {
                    ui.TextColored(theme.MutedText, "Loading source and target rig data...");
                    return;
                }

                ui.TextColored(theme.MutedText,
                               std::to_string(sourceRig->Definition().Bones.size()) + " source bones  ->  " +
                                   std::to_string(targetRig->Definition().Bones.size()) + " target bones");
                const bool inputsChanged =
                    m_DiagnosticSourceClip != sourceClip || m_DiagnosticSourceSkeleton != sourceSkeleton ||
                    m_DiagnosticSourceRig != sourceRig || m_DiagnosticTargetSkeleton != targetSkeleton ||
                    m_DiagnosticTargetRig != targetRig;
                bool mappingChanged = m_MappingDraft.Select(m_SourceClip, model->Id);
                if (mappingChanged)
                    m_MappingMessage.clear();
                if (auto mappingEditor = ui.BeginTreeNode("Edit bone mappings"); mappingEditor)
                {
                    ui.TextWrapped("Choose a target bone for each source track that needs repair. Automatic restores "
                                   "name and semantic matching. Overrides apply to this source/target selection; "
                                   "baking saves the resulting animation.");
                    (void)ui.InputText("Filter source bones", m_MappingDraft.SourceFilter);
                    for (const auto& track : sourceClip->Tracks())
                    {
                        if (track.Bone >= sourceSkeleton->Bones().size())
                            continue;
                        const auto& sourceName = sourceSkeleton->Bones()[track.Bone].Name;
                        if (!RetargetBoneMatchesFilter(sourceName, m_MappingDraft.SourceFilter))
                            continue;
                        auto id = ui.PushId(sourceName);
                        const auto found = std::ranges::find(m_MappingDraft.Overrides, sourceName,
                                                             &Keire::AnimationRetargetOverride::SourceBone);
                        const auto current =
                            found == m_MappingDraft.Overrides.end() ? std::string("Automatic") : found->TargetBone;
                        auto mappingPreview = current;
                        if (found == m_MappingDraft.Overrides.end() && m_RetargetDiagnostics && !inputsChanged)
                        {
                            const auto mapped = std::ranges::find(m_RetargetDiagnostics->Mappings, track.Bone,
                                                                  &Keire::AnimationRetargetBoneMapping::SourceBone);
                            mappingPreview += mapped != m_RetargetDiagnostics->Mappings.end() && mapped->TargetBone
                                                  ? ": " + mapped->TargetName
                                                  : ": no matching target";
                        }
                        if (auto picker = ui.BeginCombo(sourceName, mappingPreview); picker)
                        {
                            (void)ui.InputText("Find target bone", m_MappingDraft.TargetFilter);
                            if (ui.Selectable("Automatic", found == m_MappingDraft.Overrides.end()))
                            {
                                std::erase_if(m_MappingDraft.Overrides, [&sourceName](const auto& item)
                                              { return item.SourceBone == sourceName; });
                                mappingChanged = true;
                            }
                            for (const auto& targetBone : targetSkeleton->Bones())
                                if (RetargetBoneMatchesFilter(targetBone.Name, m_MappingDraft.TargetFilter) &&
                                    ui.Selectable(targetBone.Name, current == targetBone.Name))
                                {
                                    std::erase_if(m_MappingDraft.Overrides, [&sourceName](const auto& item)
                                                  { return item.SourceBone == sourceName; });
                                    m_MappingDraft.Overrides.push_back({sourceName, targetBone.Name});
                                    mappingChanged = true;
                                }
                        }
                    }
                    if (mappingChanged)
                        m_MappingMessage.clear();
                    if (const auto database = m_Controller.RiggingStudioDatabase())
                        mappingChanged |= Detail::DrawRetargetMappingActions(
                            ui, theme, database->Specification().ProjectRoot, sourceAnimation.Skeleton,
                            targetAnimation.Skeleton, *sourceSkeleton, *targetSkeleton, m_MappingDraft.Overrides,
                            m_MappingMessage, m_MappingMessageError);
                }
                if (inputsChanged || mappingChanged)
                {
                    m_ReviewedPartialMapping = false;
                    m_DiagnosticSourceClip = sourceClip;
                    m_DiagnosticSourceSkeleton = sourceSkeleton;
                    m_DiagnosticSourceRig = sourceRig;
                    m_DiagnosticTargetSkeleton = targetSkeleton;
                    m_DiagnosticTargetRig = targetRig;
                    try
                    {
                        m_RetargetDiagnostics = Keire::DiagnoseAnimationRetargeting(
                            *sourceSkeleton, sourceRig->Definition(), *sourceClip, *targetSkeleton,
                            targetRig->Definition(), m_MappingDraft.Overrides);
                        m_Message.clear();
                        m_MessageError = false;
                    }
                    catch (const std::exception& error)
                    {
                        m_RetargetDiagnostics.reset();
                        m_Message = error.what();
                        m_MessageError = true;
                    }
                }

                const bool compatible = m_RetargetDiagnostics && m_RetargetDiagnostics->Compatible();
                if (m_RetargetDiagnostics)
                {
                    const auto& diagnostics = *m_RetargetDiagnostics;
                    const bool partial = HasPartialRetargetMapping(diagnostics);
                    ui.TextColored(!compatible ? theme.Error
                                   : partial   ? theme.Warning
                                               : theme.Success,
                                   std::to_string(diagnostics.MappedTrackCount) + " / " +
                                       std::to_string(diagnostics.SourceTrackCount) + " tracks mapped  |  " +
                                       std::to_string(diagnostics.ExactNameMatchCount) + " exact  |  " +
                                       std::to_string(diagnostics.HierarchyMatchCount) + " hierarchy  |  " +
                                       std::to_string(diagnostics.SemanticMatchCount) + " semantic  |  " +
                                       std::to_string(diagnostics.ManualMatchCount) + " manual");
                    if (!compatible)
                    {
                        ui.TextColoredWrapped(theme.Error,
                                              "Cannot bake: no animation tracks map to this rig. Choose a compatible "
                                              "Source Clip or use Edit bone mappings to assign matching target bones.");
                    }
                    if (partial)
                    {
                        ui.TextColoredWrapped(theme.Warning,
                                              "Partial mapping: unmapped animation tracks will be omitted. "
                                              "Review the diagnostics before baking.");
                        (void)ui.Checkbox("I reviewed the omitted tracks", m_ReviewedPartialMapping);
                    }
                    if (compatible)
                        ui.TextColored(diagnostics.RootMotionMapped ? theme.Success : theme.Warning,
                                       diagnostics.RootMotionMapped ? "Root motion mapping is compatible."
                                                                    : "Root motion will be disabled for this bake.");
                    std::size_t scaleFallbacks = 0;
                    for (const auto& mapping : diagnostics.Mappings)
                        scaleFallbacks += mapping.ScaleFallbackKeyCount;
                    if (scaleFallbacks != 0)
                        ui.TextColored(theme.Warning, std::to_string(scaleFallbacks) +
                                                          " animated scale components will use target bind scale.");
                    if (auto details = ui.BeginTreeNode("Retarget diagnostics", !compatible || partial); details)
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
                if (draftDirty)
                    ui.TextColored(theme.Warning, "Apply or revert the pending import settings before baking.");
                if (auto disabled = ui.BeginDisabled(!nameError.empty() || !m_RetargetDiagnostics ||
                                                     !CanBakeRetarget(*m_RetargetDiagnostics, m_ReviewedPartialMapping,
                                                                      draftDirty, sourceImportFailed, importFailed));
                    disabled)
                {
                    if (ui.Button("Bake Retargeted Clip"))
                    {
                        try
                        {
                            const auto baked = Keire::RetargetAnimationClipWithDiagnostics(
                                *sourceSkeleton, sourceRig->Definition(), *sourceClip, targetAnimation.Skeleton,
                                *targetSkeleton, targetRig->Definition(), m_MappingDraft.Overrides);
                            m_Controller.CreateRiggingStudioRetarget(
                                m_RetargetName,
                                Keire::AnimationClipAsset::Encode(baked.Clip->Skeleton(), baked.Clip->Duration(),
                                                                  baked.Clip->Tracks(), baked.Clip->Events(),
                                                                  baked.Clip->RootMotion()));
                            m_Message.clear();
                            m_MessageError = false;
                        }
                        catch (const std::exception& error)
                        {
                            m_Message = error.what();
                            m_MessageError = true;
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
    if (const auto error = KeireEditor::RetargetOutputNameError(name); !error.empty())
        throw std::invalid_argument(std::string(error));
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
    const auto source = KeireEditor::ResolveRiggingStudioRevealAsset(RiggingStudioRecords(), asset);
    if (!source)
    {
        SetAssetError("The asset's source is no longer available. Refresh the Project panel or reimport the model.");
        return;
    }
    m_SelectedAsset = source;
    if (m_AssetBrowserPanel)
        m_AssetBrowserPanel->RevealAsset(source);
}

void EditorWorkspaceLayer::ReportRiggingStudioError(std::string message) noexcept { SetAssetError(std::move(message)); }

void EditorWorkspaceLayer::PreviewRiggingStudioClip(const Keire::AssetId asset)
{
    auto name = KeireEditor::RiggingStudioClipPreviewName(asset, RiggingStudioAssets());
    m_AnimatorControllerPanel->OpenClip(asset, std::move(name));
}
