#pragma once

#include "Keire/Core.h"

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
        Keire::AssetImportSettings m_Draft;
        Keire::AssetId m_SourceClip;
        std::string m_RetargetName = "RetargetedClip";
        bool m_Dirty = false;
        std::string m_Message;
    };
} // namespace KeireEditor
