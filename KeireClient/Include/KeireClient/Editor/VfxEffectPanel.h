#pragma once

#include "Keire/Core.h"
#include "KeireClient/Editor/AssetPicker.h"

#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace KeireEditor
{
    class VfxEffectDocument;

    class IVfxEffectPanelController
    {
      public:
        virtual ~IVfxEffectPanelController() = default;
        [[nodiscard]] virtual VfxEffectDocument& VfxEffectState() noexcept = 0;
        [[nodiscard]] virtual const Keire::UiThemeDefinition& VfxEffectTheme() const noexcept = 0;
        [[nodiscard]] virtual Keire::Ref<Keire::AssetDatabase> VfxEffectDatabase() const noexcept = 0;
        [[nodiscard]] virtual std::span<const Keire::AssetSourceRecord> VfxEffectAssetRecords() const noexcept = 0;
        [[nodiscard]] virtual std::string_view VfxEffectPreviewDiagnostic() const noexcept = 0;
        virtual void ActivateVfxEffectHistory() noexcept = 0;
        virtual void SaveVfxEffectDocument() = 0;
        virtual void DiscardVfxEffectDocument() = 0;
        virtual void ReloadVfxEffectDocument(Keire::AssetId asset) = 0;
        virtual void UndoVfxEffectEdit() = 0;
        virtual void RedoVfxEffectEdit() = 0;
        virtual void RevealVfxEffectAsset(Keire::AssetId asset) = 0;
        virtual void StopVfxEffectPreview() noexcept = 0;
        virtual void ReportVfxEffectError(std::string message) noexcept = 0;
    };

    class VfxEffectPanel final
    {
      public:
        explicit VfxEffectPanel(IVfxEffectPanelController& controller) noexcept : m_Controller(controller) {}

        void Attach(Keire::UiWorkspace& workspace);
        void Draw(Keire::UiFrame& ui);
        void SetMessage(std::string message) { m_Message = std::move(message); }
        void ResetTransientState() noexcept;
        void StopTransientPreview() noexcept;
        [[nodiscard]] Keire::UiPanelRegistration& Registration() noexcept { return m_Registration; }

      private:
        [[nodiscard]] bool ApplyEdit(std::string_view name,
                                     const std::function<void(Keire::VfxEffectDefinition&)>& operation);
        [[nodiscard]] bool ApplyAction(std::string_view name, const std::function<bool()>& operation);
        void DrawEffectSettings(Keire::UiFrame& ui);
        void DrawGraphSummary(Keire::UiFrame& ui);
        void DrawModules(Keire::UiFrame& ui);
        void DrawSelectedModule(Keire::UiFrame& ui);

        IVfxEffectPanelController& m_Controller;
        AssetPicker m_AssetPicker;
        Keire::UiPanelRegistration m_Registration;
        Keire::AssetId m_SelectedModule;
        std::string m_Message;
        bool m_WasVisible = false;
    };
} // namespace KeireEditor
