#pragma once

#include "Keire/Core.h"
#include "KeireClient/Editor/LimbRigAssignment.h"
#include "KeireClient/Editor/LimbRigAuthoring.h"
#include "KeireClient/Editor/RiggingStudioValidation.h"
#include "KeireInternal/Assets/AssetImportDrafts.h"

#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace KeireEditor
{
    class IRiggingStudioController
    {
      public:
        virtual ~IRiggingStudioController() = default;
        [[nodiscard]] virtual const Keire::UiThemeDefinition& RiggingStudioTheme() const noexcept = 0;
        [[nodiscard]] virtual Keire::Ref<Keire::AssetDatabase> RiggingStudioDatabase() const noexcept = 0;
        [[nodiscard]] virtual Keire::Ref<Keire::AssetSystem> RiggingStudioAssets() const noexcept = 0;
        [[nodiscard]] virtual std::span<const Keire::AssetSourceRecord> RiggingStudioRecords() const noexcept = 0;
        [[nodiscard]] virtual Keire::AssetId RiggingStudioSelectedAsset() const noexcept = 0;
        [[nodiscard]] virtual std::string_view RiggingStudioStatus() const noexcept = 0;
        virtual void ApplyRiggingStudioSettings(Keire::AssetId asset, const Keire::AssetImportSettings& settings) = 0;
        virtual void CreateRiggingStudioRetarget(std::string_view name, std::vector<std::byte> bytes) = 0;
        virtual void CreateRiggingStudioLimbRig(std::string_view, std::vector<std::byte>)
        {
            throw std::runtime_error("Rig asset creation is unavailable in this workspace.");
        }
        virtual LimbRigAssignmentProgress AssignRiggingStudioLimbRig(Keire::AssetId)
        {
            throw std::runtime_error("Rig assignment is unavailable in this workspace.");
        }
        virtual void CancelRiggingStudioLimbAssignment() noexcept {}
        virtual void RevealRiggingStudioAsset(Keire::AssetId asset) = 0;
        virtual void PreviewRiggingStudioClip(Keire::AssetId asset) = 0;
        virtual void ReportRiggingStudioError(std::string message) noexcept = 0;
    };

    class RiggingStudioPanel final
    {
      public:
        explicit RiggingStudioPanel(IRiggingStudioController& controller) noexcept : m_Controller(controller) {}

        void Attach(Keire::UiWorkspace& workspace);
        void Draw(Keire::UiFrame& ui);
        void DrawChainOverlay(Keire::UiFrame& ui, const Keire::Ref<Keire::Scene>& scene, Keire::EntityId selected,
                              const Keire::RenderCamera& camera, Keire::UiItemRect viewport);
        [[nodiscard]] Keire::UiPanelRegistration& Registration() noexcept { return m_Registration; }

      private:
        void DrawChainInspector(Keire::UiFrame& ui, const Keire::AssetSourceRecord& model);
        void DrawLimbAuthoring(Keire::UiFrame& ui, std::span<const std::size_t> chain);
        std::map<Keire::AssetId, std::vector<Keire::LimbDefinition>> m_LimbDrafts;
        std::string m_LimbName = "Front limb";
        std::int64_t m_LimbId = 1;
        std::string m_LimbRigName = "Custom Limb Rig";
        std::string m_MirrorFrom = "Left";
        std::string m_MirrorTo = "Right";
        std::string m_MirrorName = "Opposite limb";
        std::int64_t m_MirrorId = 2;
        std::string m_LimbAuthoringMessage;
        bool m_LimbAuthoringError = false;
        Keire::LimbDefinition m_LimbSettings;
        Keire::AssetId m_LoadedLimbRig;
        Keire::AssetId m_AssigningLimbRig;
        bool m_LimbAuthoringWasDrawn = false;
        LimbRigDraftLoad m_PendingLimbRig;
        Keire::AssetHandle<Keire::RigDefinitionAsset> m_PendingLimbRigHandle;
        Keire::Ref<const Keire::SkeletonAsset> m_ChainSkeleton;
        Keire::AssetId m_ChainSkeletonId;
        bool m_ShowChainOverlay = false;
        std::string m_ChainOverlayDiagnostic;
        std::string m_ChainRoot;
        std::string m_ChainTip;
        std::string m_ChainFilter;
        IRiggingStudioController& m_Controller;
        Keire::UiPanelRegistration m_Registration;
        Keire::AssetId m_DraftAsset;
        Keire::AssetId m_LockedAsset;
        Keire::Internal::AssetImportDrafts m_Drafts;
        Keire::AssetId m_SourceClip;
        Keire::Ref<const Keire::AnimationClipAsset> m_DiagnosticSourceClip;
        Keire::Ref<const Keire::SkeletonAsset> m_DiagnosticSourceSkeleton;
        Keire::Ref<const Keire::RigDefinitionAsset> m_DiagnosticSourceRig;
        Keire::Ref<const Keire::SkeletonAsset> m_DiagnosticTargetSkeleton;
        Keire::Ref<const Keire::RigDefinitionAsset> m_DiagnosticTargetRig;
        std::optional<Keire::AnimationRetargetDiagnostics> m_RetargetDiagnostics;
        RetargetMappingDraft m_MappingDraft;
        std::string m_MappingMessage;
        bool m_MappingMessageError = false;
        std::string m_RetargetName = "RetargetedClip";
        std::string m_Message;
        bool m_MessageError = false;
        bool m_ReviewedPartialMapping = false;
    };
} // namespace KeireEditor
