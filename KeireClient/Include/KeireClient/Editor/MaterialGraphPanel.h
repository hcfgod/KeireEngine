#pragma once

#include "Keire/Core.h"
#include "KeireClient/Editor/AssetPicker.h"
#include "KeireClient/Editor/AuthoringWidgets.h"
#include "KeireClient/Editor/MaterialGraphDocument.h"

#include <optional>
#include <span>
#include <string>
#include <utility>

namespace KeireEditor
{
    class IMaterialGraphPanelController
    {
      public:
        virtual ~IMaterialGraphPanelController() = default;
        [[nodiscard]] virtual MaterialGraphDocument& MaterialGraphState() noexcept = 0;
        [[nodiscard]] virtual const Keire::UiThemeDefinition& MaterialGraphTheme() const noexcept = 0;
        [[nodiscard]] virtual std::span<const Keire::AssetSourceRecord> MaterialGraphAssetRecords() const noexcept = 0;
        virtual void SaveMaterialGraphDocument() = 0;
        virtual void UndoMaterialGraphEdit() = 0;
        virtual void RedoMaterialGraphEdit() = 0;
        virtual void RevealMaterialGraphAsset(Keire::AssetId asset) = 0;
        virtual void ReportMaterialGraphError(std::string message) noexcept = 0;
    };

    class MaterialGraphPanel final
    {
      public:
        explicit MaterialGraphPanel(IMaterialGraphPanelController& controller) : m_Controller(controller) {}

        void Attach(Keire::UiWorkspace& workspace);
        void Draw(Keire::UiFrame& ui);
        void ResetTransientState() noexcept;
        void SetMessage(std::string message) { m_Message = std::move(message); }
        [[nodiscard]] Keire::UiPanelRegistration& Registration() noexcept { return m_Registration; }

      private:
        void DrawHeader(Keire::UiFrame& ui);
        void DrawCanvas(Keire::UiFrame& ui);
        void DrawInspector(Keire::UiFrame& ui);
        void DrawDiagnostics(Keire::UiFrame& ui);
        [[nodiscard]] bool DrawValueEditor(Keire::UiFrame& ui, std::string_view label,
                                           Keire::MaterialPropertyValue& value);
        void AddValueNode(const Keire::MaterialGraphPropertyBinding& property,
                          std::optional<Keire::Vector2> position = std::nullopt);
        void Report(std::string message) noexcept;

        IMaterialGraphPanelController& m_Controller;
        StableNodeGraphCanvas m_Canvas;
        AssetPicker m_ShaderGraphPicker;
        AssetPicker m_RawShaderPicker;
        AssetPicker m_TexturePicker;
        Keire::UiPanelRegistration m_Registration;
        std::optional<Keire::AssetId> m_SelectedNode;
        std::optional<Keire::AssetId> m_SelectedConnection;
        std::optional<Keire::Vector2> m_NodeCreationPosition;
        std::string m_Message;
    };
} // namespace KeireEditor
