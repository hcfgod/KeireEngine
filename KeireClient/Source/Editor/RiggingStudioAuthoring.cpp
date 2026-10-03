#include "KeireClient/Editor/RiggingStudioPanel.h"

#include "KeireClient/Editor/AssetBrowserPanel.h"
#include "KeireClient/Editor/LimbRigAuthoring.h"
#include "KeireClient/Editor/SceneDocument.h"
#include "KeireClient/EditorWorkspaceLayer.h"

#include <limits>

namespace KeireEditor
{
    void RiggingStudioPanel::DrawLimbAuthoring(Keire::UiFrame& ui, const std::span<const std::size_t> chain)
    {
        auto tree = ui.BeginTreeNode("Author reusable limb rig", false);
        if (!tree)
            return;
        m_LimbAuthoringWasDrawn = true;
        const auto& theme = m_Controller.RiggingStudioTheme();
        auto& limbs = m_LimbDrafts[m_ChainSkeletonId];
        if (const auto pending = m_PendingLimbRig.Asset(); pending)
        {
            try
            {
                const auto state = m_PendingLimbRigHandle.State();
                if (state == Keire::AssetState::Failed || state == Keire::AssetState::Cancelled)
                {
                    const auto diagnostic = m_PendingLimbRigHandle.Diagnostic();
                    m_LimbAuthoringMessage =
                        state == Keire::AssetState::Cancelled
                            ? "Rig loading was cancelled. Your draft is unchanged."
                            : "Rig failed to load. " + diagnostic.Message + " Your draft is unchanged.";
                    m_LimbAuthoringError = state == Keire::AssetState::Failed;
                    m_PendingLimbRig.Cancel();
                }
                else
                {
                    const auto progress =
                        m_PendingLimbRig.Complete(m_ChainSkeleton, m_PendingLimbRigHandle.TryGetLoaded(), limbs);
                    if (progress == LimbRigLoadProgress::Loaded)
                    {
                        m_LoadedLimbRig = pending;
                        m_LimbAuthoringMessage = "Loaded compatible limb definitions. Saving creates a new asset copy.";
                    }
                    else if (progress == LimbRigLoadProgress::Cancelled)
                        m_LimbAuthoringMessage =
                            "Rig loading cancelled because the skeleton changed. Your draft is unchanged.";
                    else
                        m_LimbAuthoringMessage =
                            "Loading saved limb rig... Draft editing resumes when loading completes.";
                    m_LimbAuthoringError = false;
                }
            }
            catch (const std::exception& error)
            {
                m_LimbAuthoringMessage = std::string(error.what()) + " Your draft is unchanged.";
                m_LimbAuthoringError = true;
                m_PendingLimbRig.Cancel();
            }
            if (!m_PendingLimbRig.Asset())
                m_PendingLimbRigHandle = {};
        }
        if (m_PendingLimbRig.Asset() && ui.Button("Cancel rig load"))
        {
            m_PendingLimbRig.Cancel();
            m_PendingLimbRigHandle = {};
            m_LimbAuthoringMessage = "Rig loading cancelled. Your draft is unchanged.";
            m_LimbAuthoringError = false;
        }
        if (m_AssigningLimbRig && ui.Button("Cancel rig assignment"))
        {
            m_Controller.CancelRiggingStudioLimbAssignment();
            m_AssigningLimbRig = {};
            m_LimbAuthoringMessage = "Assignment cancelled. No scene changes were made.";
            m_LimbAuthoringError = false;
        }
        if (m_AssigningLimbRig)
        {
            try
            {
                const auto progress = m_Controller.AssignRiggingStudioLimbRig(m_AssigningLimbRig);
                m_LimbAuthoringError = false;
                if (progress == LimbRigAssignmentProgress::Loading)
                    m_LimbAuthoringMessage = "Loading assignment dependencies... No scene changes have been made.";
                else
                {
                    m_AssigningLimbRig = {};
                    m_LimbAuthoringMessage =
                        progress == LimbRigAssignmentProgress::Complete
                            ? "Assigned the saved rig asset. Unsaved draft changes are not assigned."
                            : "Assignment cancelled because its target changed. No scene changes were made.";
                }
            }
            catch (const std::exception& error)
            {
                m_Controller.CancelRiggingStudioLimbAssignment();
                m_AssigningLimbRig = {};
                m_LimbAuthoringMessage = error.what();
                m_LimbAuthoringError = true;
            }
        }
        auto loadingDisabled =
            ui.BeginDisabled(static_cast<bool>(m_PendingLimbRig.Asset()) || static_cast<bool>(m_AssigningLimbRig));
        ui.TextColoredWrapped(theme.MutedText,
                              "Add selected chains to a draft, then save a separate .keirerig asset. Stable limb IDs "
                              "are used by scripts. Adding an existing ID replaces that draft limb. Imported rigs "
                              "are unchanged. Drafts are kept per skeleton until the editor closes.");
        (void)ui.InputText("Limb name", m_LimbName);
        (void)ui.DragInteger("Stable limb ID", m_LimbId, 1, 1, std::numeric_limits<std::uint32_t>::max());
        if (auto solver =
                ui.BeginCombo("Solver", m_LimbSettings.Solver == Keire::LimbSolver::TwoBone ? "Two bone" : "FABRIK");
            solver)
        {
            if (auto disabled = ui.BeginDisabled(chain.size() != 3); disabled)
                if (ui.Selectable("Two bone (exactly 3 bones)", m_LimbSettings.Solver == Keire::LimbSolver::TwoBone))
                    m_LimbSettings.Solver = Keire::LimbSolver::TwoBone;
            if (ui.Selectable("FABRIK (2 or more bones)", m_LimbSettings.Solver == Keire::LimbSolver::Fabrik))
                m_LimbSettings.Solver = Keire::LimbSolver::Fabrik;
        }
        if (chain.size() != 3 && m_LimbSettings.Solver == Keire::LimbSolver::TwoBone)
            ui.TextColoredWrapped(theme.Warning,
                                  "This chain does not have three bones. Choose FABRIK or change the endpoints.");
        (void)ui.SliderFloat("Contact radius (model units)", m_LimbSettings.ContactRadius, 0.0F, 1.0F);
        (void)ui.SliderFloat("Tolerance (model units)", m_LimbSettings.Tolerance, 0.000001F, 0.1F);
        std::int64_t iterations = m_LimbSettings.MaximumIterations;
        if (ui.DragInteger("Maximum solver iterations", iterations, 1, 1, 1024))
            m_LimbSettings.MaximumIterations = static_cast<std::uint32_t>(iterations);
        bool limits = m_LimbSettings.BendLimits.has_value();
        if (ui.Checkbox("Bend limits (Two bone only)", limits))
            m_LimbSettings.BendLimits = limits ? std::optional{Keire::LimbBendLimits{}} : std::nullopt;
        if (m_LimbSettings.BendLimits)
        {
            ui.TextColoredWrapped(theme.MutedText,
                                  "Degrees between segment directions: 0 is straight, 180 is folded back.");
            (void)ui.SliderFloat("Minimum bend degrees", m_LimbSettings.BendLimits->MinimumDegrees, 0, 180);
            (void)ui.SliderFloat("Maximum bend degrees", m_LimbSettings.BendLimits->MaximumDegrees, 0, 180);
        }
        bool direction = m_LimbSettings.PreferredBendDirection.has_value();
        if (ui.Checkbox("Preferred bend direction (Two bone only)", direction))
            m_LimbSettings.PreferredBendDirection = direction ? std::optional{Keire::Vector3{0, 0, 1}} : std::nullopt;
        if (m_LimbSettings.PreferredBendDirection)
        {
            ui.TextColoredWrapped(theme.MutedText, "Nonzero model-space direction relative to the chain root.");
            (void)ui.DragVector3("Bend direction", *m_LimbSettings.PreferredBendDirection, 0.01F);
        }
        if (m_LimbSettings.Solver == Keire::LimbSolver::Fabrik && (limits || direction))
            ui.TextColoredWrapped(
                theme.Warning, "FABRIK does not support these bend constraints. Disable them before adding the limb.");
        const auto selected = [&]
        {
            Keire::LimbDefinition limb = m_LimbSettings;
            limb.Id.Value = static_cast<std::uint32_t>(m_LimbId);
            limb.Name = m_LimbName;
            limb.Bones.clear();
            for (const auto bone : chain)
                limb.Bones.push_back(m_ChainSkeleton->Bones()[bone].Name);
            return limb;
        };
        const auto attempt = [&](const auto& action)
        {
            m_LimbAuthoringError = false;
            try
            {
                action();
            }
            catch (const std::exception& error)
            {
                if (m_AssigningLimbRig)
                {
                    m_Controller.CancelRiggingStudioLimbAssignment();
                    m_AssigningLimbRig = {};
                }
                m_LimbAuthoringError = true;
                m_LimbAuthoringMessage = error.what();
            }
        };
        if (ui.Button("Add / update selected limb"))
            attempt(
                [&]
                {
                    UpsertAuthoredLimb(m_ChainSkeleton, limbs, selected());
                    m_LimbAuthoringMessage = "Limb validated and added to the draft. Save the rig asset to keep it.";
                });
        if (auto mirror = ui.BeginTreeNode("Mirror using explicit bone-name tokens", false); mirror)
        {
            ui.TextColoredWrapped(
                theme.MutedText,
                "Every source bone must contain the exact token once. All destination bones and "
                "their chain must exist. The bend direction is cleared; no reflection plane is guessed.");
            (void)ui.InputText("Source token", m_MirrorFrom);
            (void)ui.InputText("Destination token", m_MirrorTo);
            (void)ui.InputText("Mirrored limb name", m_MirrorName);
            (void)ui.DragInteger("Mirrored limb ID", m_MirrorId, 1, 1, std::numeric_limits<std::uint32_t>::max());
            if (ui.Button("Validate and add mirrored limb"))
                attempt(
                    [&]
                    {
                        if (m_MirrorId == m_LimbId)
                            throw std::invalid_argument("Use a different stable ID for the mirrored limb.");
                        auto mirrored = MirrorAuthoredLimbNames(selected(), m_MirrorFrom, m_MirrorTo,
                                                                {static_cast<std::uint32_t>(m_MirrorId)}, m_MirrorName);
                        UpsertAuthoredLimb(m_ChainSkeleton, limbs, std::move(mirrored));
                        m_LimbAuthoringMessage = "Mirrored chain validated and added to the draft.";
                    });
        }
        if (auto load = ui.BeginCombo("Load saved limb rig", "Choose an existing .keirerig"); load)
        {
            const auto assets = m_Controller.RiggingStudioAssets();
            for (const auto& record : m_Controller.RiggingStudioRecords())
                if (record.Type == Keire::RigDefinitionAsset::StaticType() &&
                    ui.Selectable(record.RelativePath.generic_string()))
                    attempt(
                        [&]
                        {
                            m_PendingLimbRigHandle = assets->Load<Keire::RigDefinitionAsset>(record.Id);
                            m_PendingLimbRig.Begin(record.Id, m_ChainSkeleton);
                            m_LoadedLimbRig = {};
                            m_LimbAuthoringMessage = "Loading saved limb rig...";
                        });
        }
        auto pendingMutationDisabled = ui.BeginDisabled(static_cast<bool>(m_PendingLimbRig.Asset()));
        for (std::size_t index = 0; index < limbs.size(); ++index)
        {
            auto id = ui.PushId(std::to_string(limbs[index].Id.Value));
            ui.Text(std::to_string(limbs[index].Id.Value) + "  " + limbs[index].Name);
            ui.SameLine();
            if (ui.Button("Select"))
            {
                m_LimbId = limbs[index].Id.Value;
                m_LimbName = limbs[index].Name;
                m_LimbSettings = limbs[index];
                m_ChainRoot = limbs[index].Bones.front();
                m_ChainTip = limbs[index].Bones.back();
            }
            ui.SameLine();
            if (ui.Button("Remove"))
            {
                limbs.erase(limbs.begin() + static_cast<std::ptrdiff_t>(index));
                break;
            }
        }
        (void)ui.InputText("Rig asset name", m_LimbRigName);
        if (auto disabled = ui.BeginDisabled(!m_LoadedLimbRig); disabled)
            if (ui.Button("Assign loaded rig to selected Animator"))
                attempt(
                    [&]
                    {
                        m_AssigningLimbRig = m_LoadedLimbRig;
                        const auto progress = m_Controller.AssignRiggingStudioLimbRig(m_AssigningLimbRig);
                        if (progress != LimbRigAssignmentProgress::Loading)
                            m_AssigningLimbRig = {};
                        m_LimbAuthoringMessage =
                            progress == LimbRigAssignmentProgress::Complete
                                ? "Assigned the saved rig asset. Unsaved draft changes are not assigned."
                                : "Loading assignment dependencies... No scene changes have been made.";
                    });
        if (auto disabled = ui.BeginDisabled(limbs.empty() || static_cast<bool>(m_AssigningLimbRig)); disabled)
            if (ui.Button("Save as new limb rig asset"))
                attempt(
                    [&]
                    {
                        auto rig = Keire::InferRigDefinition(*m_ChainSkeleton, Keire::RigProfileType::Custom);
                        rig.SchemaVersion = 2;
                        rig.Limbs = limbs;
                        m_Controller.CreateRiggingStudioLimbRig(m_LimbRigName, Keire::RigDefinitionAsset::Encode(rig));
                        m_LimbAuthoringMessage = "Rig creation queued. See the workspace asset status for completion.";
                    });
        if (!m_LimbAuthoringMessage.empty())
            ui.TextColoredWrapped(m_LimbAuthoringError ? theme.Error : theme.MutedText, m_LimbAuthoringMessage);
    }
} // namespace KeireEditor

void EditorWorkspaceLayer::CreateRiggingStudioLimbRig(const std::string_view name, std::vector<std::byte> bytes)
{
    if (!m_AssetDatabase || !m_AssetOperations)
        throw std::runtime_error("Asset creation services are unavailable.");
    if (m_AssetOperations->Busy())
        throw std::runtime_error("Wait for the active asset operation before creating a limb rig.");
    if (const auto error = KeireEditor::RetargetOutputNameError(name); !error.empty())
        throw std::invalid_argument(std::string(error));
    const auto directory = m_AssetBrowserPanel ? m_AssetBrowserPanel->CurrentFolder() : std::filesystem::path{};
    auto destination = directory / (std::string(name) + ".keirerig");
    for (std::size_t copy = 2; m_AssetDatabase->Find(destination); ++copy)
        destination = directory / (std::string(name) + " " + std::to_string(copy) + ".keirerig");
    m_AssetOperations->QueueCreateAsset(
        destination, std::move(bytes), {},
        {.FollowUp = KeireEditor::AssetOperationFollowUp::Reveal, .UndoName = "Create Limb Rig Asset"});
    m_AssetStatus = "Creating " + destination.generic_string() + " in the isolated asset worker.";
}

void EditorWorkspaceLayer::CancelRiggingStudioLimbAssignment() noexcept
{
    m_LimbAssignment = {};
    m_AssignmentRig = {};
    m_AssignmentSkin = {};
    m_AssignmentSkeleton = {};
    m_AssignmentSkeletonId = {};
}

KeireEditor::LimbRigAssignmentProgress EditorWorkspaceLayer::AssignRiggingStudioLimbRig(const Keire::AssetId asset)
{
    using Progress = KeireEditor::LimbRigAssignmentProgress;
    const auto scene = m_SceneDocument ? m_SceneDocument->EditingScene() : Keire::Ref<Keire::Scene>{};
    const auto selection = m_SceneDocument ? Keire::EntityId(m_SceneDocument->Selection()) : Keire::EntityId{};
    const bool editing = m_SceneDocument && !m_SceneDocument->PlaySession();
    if (m_LimbAssignment.RequestedRig &&
        (m_LimbAssignment.RequestedRig != asset || !m_LimbAssignment.Matches(scene, selection, editing)))
    {
        CancelRiggingStudioLimbAssignment();
        return Progress::Cancelled;
    }
    if (!editing)
        throw std::runtime_error("Stop scene Play Mode before assigning a saved limb rig.");
    const auto entity = scene ? scene->FindEntity(selection) : Keire::Entity{};
    const auto animator =
        entity ? entity.GetComponent<Keire::AnimatorComponent>() : Keire::Ref<Keire::AnimatorComponent>{};
    if (!animator || !animator->SkinnedMesh())
        throw std::invalid_argument("Select a scene entity with an Animator and an assigned skinned mesh.");
    if (animator->PoseSource() != Keire::AnimatorPoseSource::AnimationGraph)
        throw std::invalid_argument("This assignment workflow requires Animator Pose Source = Animation Graph.");
    const auto assets = Owner().Assets();
    if (!assets || assets->TryGetType(asset) != Keire::RigDefinitionAsset::StaticType())
        throw std::invalid_argument("The saved rig asset is unavailable. Reload it from the Project panel.");
    if (!m_LimbAssignment.RequestedRig)
    {
        m_LimbAssignment = {scene,
                            selection,
                            animator,
                            asset,
                            animator->RigDefinition(),
                            animator->SkinnedMesh(),
                            animator->Skeleton()};
        m_AssignmentRig = assets->Load<Keire::RigDefinitionAsset>(asset);
        m_AssignmentSkin = assets->Load<Keire::SkinnedMeshAsset>(animator->SkinnedMesh());
    }
    const auto ready = [](const auto& handle, const std::string_view label)
    {
        if (handle.State() == Keire::AssetState::Failed)
            throw std::runtime_error(std::string(label) + " failed to load: " + handle.Diagnostic().Message);
        return handle.TryGetLoaded();
    };
    if (m_AssignmentRig.State() == Keire::AssetState::Cancelled ||
        m_AssignmentSkin.State() == Keire::AssetState::Cancelled)
    {
        CancelRiggingStudioLimbAssignment();
        return Progress::Cancelled;
    }
    const auto rig = ready(m_AssignmentRig, "Rig");
    const auto skin = ready(m_AssignmentSkin, "Skinned mesh");
    if (!rig || !skin)
        return Progress::Loading;
    if (m_AssignmentSkeletonId && m_AssignmentSkeletonId != skin->Skeleton())
    {
        CancelRiggingStudioLimbAssignment();
        return Progress::Cancelled;
    }
    if (!m_AssignmentSkeletonId)
    {
        m_AssignmentSkeletonId = skin->Skeleton();
        if (!m_AssignmentSkeletonId)
            throw std::invalid_argument("The selected skin does not reference a skeleton.");
        m_AssignmentSkeleton = assets->Load<Keire::SkeletonAsset>(m_AssignmentSkeletonId);
    }
    if (m_AssignmentSkeleton.State() == Keire::AssetState::Cancelled)
    {
        CancelRiggingStudioLimbAssignment();
        return Progress::Cancelled;
    }
    const auto skeleton = ready(m_AssignmentSkeleton, "Skeleton");
    if (!skeleton)
        return Progress::Loading;
    const auto progress = KeireEditor::CompleteLimbRigAssignment(
        m_LimbAssignment, scene, selection, editing, rig, skeleton, [this] { RecordSceneUndo("Assign Limb Rig"); });
    if (progress != Progress::Complete)
    {
        CancelRiggingStudioLimbAssignment();
        return progress;
    }
    m_SceneDocument->SetStatus("Assigned limb rig to " + entity.Name() + ". Save the scene to keep this assignment.");
    CancelRiggingStudioLimbAssignment();
    return Progress::Complete;
}
