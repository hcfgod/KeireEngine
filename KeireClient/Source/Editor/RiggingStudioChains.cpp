#include "KeireClient/Editor/RiggingStudioPanel.h"

#include "KeireClient/Editor/RigChainInspection.h"

#include <limits>

namespace KeireEditor
{
    void RiggingStudioPanel::DrawChainInspector(Keire::UiFrame& ui, const Keire::AssetSourceRecord& model)
    {
        auto tree = ui.BeginTreeNode("Custom limb chain inspector", false);
        if (!tree)
            return;
        const auto& theme = m_Controller.RiggingStudioTheme();
        ui.TextColoredWrapped(theme.MutedText,
                              "Inspect any imported limb, including spider legs. Selection reads the bind pose. "
                              "Use Author reusable limb rig below to save explicit chains for runtime IK.");
        const auto assets = m_Controller.RiggingStudioAssets();
        if (!assets)
        {
            ui.TextColoredWrapped(theme.Warning, "The runtime asset catalog is unavailable.");
            return;
        }
        Keire::AssetId skeletonId;
        for (const auto id : model.SubAssets)
            if (assets->TryGetType(id) == Keire::SkeletonAsset::StaticType())
            {
                skeletonId = id;
                break;
            }
        if (!skeletonId)
        {
            ui.TextColoredWrapped(theme.Warning,
                                  "No skeleton is available. Apply the model's rig import settings first.");
            return;
        }
        const auto handle = assets->Load<Keire::SkeletonAsset>(skeletonId, Keire::AssetPriority::Normal);
        if (const auto error = RetargetAssetLoadError(handle, "Skeleton"); !error.empty())
        {
            ui.TextColoredWrapped(theme.Error, error);
            return;
        }
        const auto skeleton = handle.TryGetLoaded();
        if (!skeleton)
        {
            ui.TextColored(theme.MutedText, "Loading skeleton...");
            return;
        }
        if (m_ChainSkeleton != skeleton)
        {
            if (m_PendingLimbRig.Asset())
            {
                m_PendingLimbRig.Cancel();
                m_PendingLimbRigHandle = {};
                m_LimbAuthoringMessage = "Rig loading cancelled because the skeleton changed. Your draft is unchanged.";
                m_LimbAuthoringError = false;
            }
            m_ChainSkeleton = skeleton;
            m_ChainSkeletonId = skeletonId;
            m_ShowChainOverlay = false;
            m_ChainRoot.clear();
            m_ChainTip.clear();
            m_ChainFilter.clear();
        }
        (void)ui.InputText("Bone filter", m_ChainFilter);
        const auto bones = skeleton->Bones();
        const auto pick = [&](const std::string_view label, std::string& selected)
        {
            if (auto combo = ui.BeginCombo(label, selected.empty() ? "Choose a bone" : selected); combo)
                for (const auto& bone : bones)
                    if (RetargetBoneMatchesFilter(bone.Name, m_ChainFilter) &&
                        ui.Selectable(bone.Name, selected == bone.Name))
                        selected = bone.Name;
        };
        pick("Chain root", m_ChainRoot);
        pick("Chain tip", m_ChainTip);
        const auto find = [&](const std::string& name)
        {
            for (std::size_t index = 0; index < bones.size(); ++index)
                if (bones[index].Name == name)
                    return index;
            return std::numeric_limits<std::size_t>::max();
        };
        const auto inspection = InspectRigChain(*skeleton, find(m_ChainRoot), find(m_ChainTip));
        if (!inspection.Valid())
        {
            ui.TextColoredWrapped(theme.Warning, inspection.Error);
            return;
        }
        (void)ui.Checkbox("Show chain in Scene view", m_ShowChainOverlay);
        if (m_ShowChainOverlay)
        {
            ui.TextColoredWrapped(theme.MutedText,
                                  "Select a scene entity using this skeleton. Cyan joints follow its published pose; "
                                  "the yellow line shows root-to-tip distance. Use Animation Studio's Step 1/60 s "
                                  "to inspect paused clip poses. Overlay is visible through surfaces.");
            if (!m_ChainOverlayDiagnostic.empty())
                ui.TextColoredWrapped(theme.Warning, m_ChainOverlayDiagnostic);
        }
        ui.Text("Segments: " + std::to_string(inspection.SegmentLengths.size()));
        ui.Text("Maximum bind reach: " + std::to_string(inspection.MaximumReach) + " model units");
        ui.TextColoredWrapped(theme.MutedText,
                              "Reach includes skeleton ancestor scale, but excludes the scene entity transform. "
                              "Joint limits and terrain may reduce usable reach.");
        ui.TextColoredWrapped(theme.MutedText, inspection.Bones.size() == 3
                                                   ? "Three bones: suitable for a two-bone IK chain."
                                                   : "Use a multi-bone solver such as FABRIK for this chain.");
        for (std::size_t index = 0; index < inspection.Bones.size(); ++index)
        {
            std::string line = std::to_string(index + 1) + ". " + bones[inspection.Bones[index]].Name;
            if (index > 0)
                line += "  |  segment " + std::to_string(inspection.SegmentLengths[index - 1]);
            ui.Text(line);
        }
        DrawLimbAuthoring(ui, inspection.Bones);
    }
} // namespace KeireEditor
