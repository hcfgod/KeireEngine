#pragma once

#include "Keire/Core.h"
#include "KeireClient/Editor/RiggingStudioValidation.h"
#include "KeireInternal/Assets/AssetImportDrafts.h"

#include <optional>
#include <span>
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
        [[nodiscard]] Keire::UiPanelRegistration& Registration() noexcept { return m_Registration; }

      private:
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
