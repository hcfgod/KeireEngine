#pragma once

#include "Keire/Core.h"
#include "KeireClient/Editor/AssetPicker.h"
#include "KeireClient/Editor/AuthoringWidgets.h"

#include <cstddef>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace KeireEditor
{
    class VfxEffectDocument;

    struct VfxEffectPreviewStatus
    {
        bool Active = false;
        bool Paused = false;
        bool AutoRestart = true;
        Keire::VfxBackend Backend = Keire::VfxBackend::Cpu;
        float Speed = 1.0F;
        std::uint32_t ActiveParticles = 0;
        std::uint64_t DroppedParticles = 0;
    };

    class IVfxEffectPanelController
    {
      public:
        virtual ~IVfxEffectPanelController() = default;
        [[nodiscard]] virtual VfxEffectDocument& VfxEffectState() noexcept = 0;
        [[nodiscard]] virtual const Keire::UiThemeDefinition& VfxEffectTheme() const noexcept = 0;
        [[nodiscard]] virtual Keire::Ref<Keire::AssetDatabase> VfxEffectDatabase() const noexcept = 0;
        [[nodiscard]] virtual std::span<const Keire::AssetSourceRecord> VfxEffectAssetRecords() const noexcept = 0;
        [[nodiscard]] virtual std::string_view VfxEffectPreviewDiagnostic() const noexcept = 0;
        [[nodiscard]] virtual VfxEffectPreviewStatus VfxEffectPreviewState() const noexcept = 0;
        virtual void ActivateVfxEffectHistory() noexcept = 0;
        virtual void SaveVfxEffectDocument() = 0;
        virtual void DiscardVfxEffectDocument() = 0;
        virtual void ReloadVfxEffectDocument(Keire::AssetId asset) = 0;
        virtual void UndoVfxEffectEdit() = 0;
        virtual void RedoVfxEffectEdit() = 0;
        virtual void RevealVfxEffectAsset(Keire::AssetId asset) = 0;
        virtual void RestartVfxEffectPreview() = 0;
        virtual void SetVfxEffectPreviewPaused(bool paused) noexcept = 0;
        virtual void SetVfxEffectPreviewAutoRestart(bool enabled) noexcept = 0;
        virtual void SetVfxEffectPreviewBackend(Keire::VfxBackend backend) = 0;
        virtual void SetVfxEffectPreviewSpeed(float speed) = 0;
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
        void DrawHeader(Keire::UiFrame& ui);
        void DrawPreviewToolbar(Keire::UiFrame& ui);
        void DrawEffectSettings(Keire::UiFrame& ui);
        void DrawGraphEditor(Keire::UiFrame& ui);
        void DrawGraphSystems(Keire::UiFrame& ui);
        void DrawGraphCanvas(Keire::UiFrame& ui);
        void DrawGraphInspector(Keire::UiFrame& ui);
        [[nodiscard]] bool DrawGraphValueEditor(Keire::UiFrame& ui, std::string_view label, Keire::VfxValueType type,
                                                Keire::VfxParameterValue& value);
        [[nodiscard]] bool DrawGraphPropertyEditor(Keire::UiFrame& ui, Keire::VfxGraphProperty& property);
        [[nodiscard]] bool DrawNodePaletteEntries(Keire::UiFrame& ui, Keire::AssetId system, Keire::Vector2 position,
                                                  std::string_view filter, Keire::AssetId blockContext = {});
        void DrawBlackboard(Keire::UiFrame& ui);
        void DrawModules(Keire::UiFrame& ui);
        void DrawSelectedModule(Keire::UiFrame& ui);

        IVfxEffectPanelController& m_Controller;
        AssetPicker m_AssetPicker;
        StableNodeGraphCanvas m_GraphCanvas;
        Keire::UiPanelRegistration m_Registration;
        Keire::AssetId m_SelectedModule;
        Keire::AssetId m_SelectedSystem;
        Keire::AssetId m_SelectedNode;
        Keire::AssetId m_SelectedBlock;
        Keire::AssetId m_SelectedConnection;
        Keire::AssetId m_SelectedParameter;
        Keire::AssetId m_ContextNode;
        Keire::AssetId m_ContextBlock;
        Keire::AssetId m_ContextPin;
        Keire::AssetId m_ContextConnection;
        Keire::Vector2 m_NodePalettePosition;
        std::string m_NodePaletteSearch;
        NodeMenuSelection m_NodeMenuSelection;
        std::string m_Message;
        bool m_WasVisible = false;
        bool m_NodePaletteMenuOpen = false;
    };
} // namespace KeireEditor
