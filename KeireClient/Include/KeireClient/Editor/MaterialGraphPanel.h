#pragma once

#include "Keire/Core.h"
#include "KeireClient/Editor/AssetPicker.h"
#include "KeireClient/Editor/AuthoringWidgets.h"
#include "KeireClient/Editor/MaterialGraphDocument.h"

#include <atomic>
#include <cstdint>
#include <future>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace KeireEditor
{
    class IMaterialGraphPanelController
    {
      public:
        virtual ~IMaterialGraphPanelController() = default;
        [[nodiscard]] virtual MaterialGraphDocument& MaterialGraphState() noexcept = 0;
        [[nodiscard]] virtual const Keire::UiThemeDefinition& MaterialGraphTheme() const noexcept = 0;
        virtual void SaveMaterialGraphDocument() = 0;
        virtual void UndoMaterialGraphEdit() = 0;
        virtual void RedoMaterialGraphEdit() = 0;
        [[nodiscard]] virtual std::span<const Keire::AssetSourceRecord> MaterialGraphAssetRecords() const noexcept = 0;
        [[nodiscard]] virtual Keire::Ref<const Keire::MeshAsset>
        ResolveMaterialGraphPreviewMesh(Keire::AssetId asset) = 0;
        virtual void RevealMaterialGraphAsset(Keire::AssetId asset) = 0;
        virtual void ReportMaterialGraphError(std::string message) noexcept = 0;
    };

    class MaterialGraphPanel final
    {
      public:
        explicit MaterialGraphPanel(IMaterialGraphPanelController& controller) : m_Controller(controller) {}
        ~MaterialGraphPanel() noexcept;

        void Attach(Keire::UiWorkspace& workspace);
        void Draw(Keire::UiFrame& ui);
        void UpdatePreview(const Keire::MaterialGraphCompilation& compilation,
                           const MaterialGraphPreviewSettings& settings);
        void ClearPreview() noexcept;
        void SetMessage(std::string message) { m_Message = std::move(message); }
        [[nodiscard]] Keire::UiPanelRegistration& Registration() noexcept { return m_Registration; }

      private:
        struct PreviewRenderResult
        {
            std::uint64_t Generation = 0;
            std::uint32_t Width = 0;
            std::uint32_t Height = 0;
            std::vector<std::byte> Pixels;
            std::string Error;
            bool FinalQuality = false;
        };

        void DrawHeader(Keire::UiFrame& ui);
        void DrawPreview(Keire::UiFrame& ui);
        void DrawCanvas(Keire::UiFrame& ui);
        void DrawInspector(Keire::UiFrame& ui);
        void DrawDiagnostics(Keire::UiFrame& ui);
        [[nodiscard]] bool AddNode(Keire::MaterialGraphNodeKind kind,
                                   Keire::MaterialGraphValueType type = Keire::MaterialGraphValueType::Scalar);
        void Report(std::string message) noexcept;

        IMaterialGraphPanelController& m_Controller;
        StableNodeGraphCanvas m_Canvas;
        AssetPicker m_AssetPicker;
        AssetPicker m_NodeAssetPicker;
        Keire::UiPanelRegistration m_Registration;
        Keire::Ref<Keire::UiImage> m_PreviewImage;
        std::vector<Keire::ShaderPropertyDefinition> m_PreviewProperties;
        MaterialGraphPreviewSettings m_PreviewSettings;
        std::optional<Keire::AssetId> m_SelectedNode;
        std::optional<Keire::AssetId> m_SelectedConnection;
        std::optional<Keire::AssetId> m_InspectorNode;
        std::string m_InspectorName;
        std::string m_InspectorSymbol;
        std::string m_InspectorInclude;
        std::string m_InspectorFunction;
        std::string m_InspectorDescription;
        std::string m_InspectorCategory;
        double m_InspectorSortPriority = 0.0;
        double m_InspectorMinimum = 0.0;
        double m_InspectorMaximum = 1.0;
        double m_InspectorStep = 0.01;
        bool m_InspectorHasMinimum = false;
        bool m_InspectorHasMaximum = false;
        bool m_InspectorHasStep = false;
        std::string m_NodeSearch;
        std::string m_Message;
        std::uint32_t m_PreviewWidth = 320;
        std::uint32_t m_PreviewHeight = 220;
        std::future<PreviewRenderResult> m_PreviewRender;
        std::shared_ptr<std::atomic<std::uint64_t>> m_PreviewCancellation =
            std::make_shared<std::atomic<std::uint64_t>>(1);
        std::uint64_t m_PreviewGeneration = 1;
        bool m_PreviewRefinement = false;
        bool m_PreviewDirty = false;
    };
} // namespace KeireEditor
