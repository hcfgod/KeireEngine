#pragma once

#include "Keire/Core.h"
#include "KeireClient/Editor/AssetPicker.h"
#include "KeireClient/Editor/AuthoringWidgets.h"
#include "KeireClient/Editor/GraphClipboard.h"
#include "KeireClient/Editor/GraphComments.h"
#include "KeireClient/Editor/GraphLayout.h"
#include "KeireClient/Editor/GraphNavigation.h"
#include "KeireClient/Editor/MaterialGraphDocument.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
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
        [[nodiscard]] virtual std::span<const Keire::AssetSourceRecord> MaterialGraphAssetRecords() const noexcept = 0;
        [[nodiscard]] virtual std::optional<Keire::ShaderGraphDefinition>
        ResolveMaterialGraphFunction(Keire::AssetId asset) const = 0;
        [[nodiscard]] virtual std::optional<Keire::ShaderGraphDefinition>
        ResolveMaterialGraphTemplate(const Keire::MaterialShaderReference& shader) const = 0;
        [[nodiscard]] virtual Keire::Ref<const Keire::Texture2DAsset>
        ResolveMaterialGraphTexture(Keire::AssetId asset) const = 0;
        virtual void SaveMaterialGraphDocument() = 0;
        virtual void UndoMaterialGraphEdit() = 0;
        virtual void RedoMaterialGraphEdit() = 0;
        virtual void RevealMaterialGraphAsset(Keire::AssetId asset) = 0;
        virtual void SetGraphClipboard(std::string_view text) = 0;
        [[nodiscard]] virtual std::string GraphClipboard() const = 0;
        [[nodiscard]] virtual bool ExtractMaterialGraphSelectionToFunction(std::span<const Keire::AssetId> selection,
                                                                           std::string_view name) = 0;
        virtual void ReportMaterialGraphError(std::string message) noexcept = 0;
    };

    class MaterialGraphPanel final
    {
      public:
        explicit MaterialGraphPanel(IMaterialGraphPanelController& controller) : m_Controller(controller) {}
        ~MaterialGraphPanel() noexcept;

        void Attach(Keire::UiWorkspace& workspace);
        void SetJobSystem(Keire::Ref<Keire::JobSystem> jobs);
        void Draw(Keire::UiFrame& ui);
        void ResetTransientState() noexcept;
        void SetMessage(std::string message) { m_Message = std::move(message); }
        [[nodiscard]] Keire::UiPanelRegistration& Registration() noexcept { return m_Registration; }

      private:
        struct PreviewRenderResult
        {
            std::uint64_t Generation = 0;
            std::vector<std::byte> Pixels;
            std::string Error;
        };

        struct PreviewRenderState
        {
            std::mutex Mutex;
            std::optional<PreviewRenderResult> Result;
        };

        void DrawHeader(Keire::UiFrame& ui);
        void DrawPreview(Keire::UiFrame& ui);
        void DrawCanvas(Keire::UiFrame& ui);
        void DrawComments(Keire::UiFrame& ui, MaterialGraphDocument& document, const MaterialGraphCanvasModel& model,
                          const NodeGraphCommentModel& comments, const NodeGraphCanvasResult& canvas);
        void CreateComment(Keire::UiFrame& ui, MaterialGraphDocument& document, const MaterialGraphCanvasModel& model,
                           Keire::Vector2 position, bool selection);
        void DuplicateSelection(std::span<const StableNodeId> selection,
                                std::span<const std::pair<StableNodeId, Keire::AssetId>> identities);
        [[nodiscard]] bool HandleClipboard(const NodeGraphCanvasResult& result,
                                           std::span<const std::pair<StableNodeId, Keire::AssetId>> identities);
        [[nodiscard]] bool DrawClipboardContextMenu(Keire::UiFrame& ui,
                                                    std::span<const std::pair<StableNodeId, Keire::AssetId>> identities,
                                                    bool includeCopy, bool copyEnabled = true);
        [[nodiscard]] bool
        DrawArrangeMenu(Keire::UiFrame& ui, std::span<const NodeGraphNode> nodes,
                        std::span<const NodeGraphConnection> connections,
                        std::span<const std::pair<StableNodeId, Keire::AssetId>> nodeIdentities,
                        std::span<const std::pair<StableNodeId, Keire::AssetId>> connectionIdentities);
        void DrawInspector(Keire::UiFrame& ui);
        [[nodiscard]] bool DrawMultiSelectionInspector(Keire::UiFrame& ui);
        [[nodiscard]] bool DrawNodeCommentInspector(Keire::UiFrame& ui);
        void DrawDiagnostics(Keire::UiFrame& ui);
        void EnsureJobScope();
        [[nodiscard]] bool DrawExpressionCreationMenu(Keire::UiFrame& ui, std::optional<Keire::Vector2> position,
                                                      const Keire::ShaderGraphNode* compatibleNode = nullptr,
                                                      const Keire::ShaderGraphPin* compatiblePin = nullptr);
        [[nodiscard]] bool DrawValueEditor(Keire::UiFrame& ui, std::string_view label,
                                           Keire::MaterialPropertyValue& value);
        [[nodiscard]] bool DrawExpressionValueEditor(Keire::UiFrame& ui, std::string_view label,
                                                     Keire::ShaderGraphValue& value);
        void AddValueNode(const Keire::MaterialGraphPropertyBinding& property,
                          std::optional<Keire::Vector2> position = std::nullopt);
        [[nodiscard]] bool AddExpressionNode(Keire::ShaderGraphNodeKind kind, Keire::ShaderGraphValueType type,
                                             std::optional<Keire::Vector2> position = std::nullopt);
        [[nodiscard]] bool AddFunctionNode(Keire::AssetId asset, std::string_view name,
                                           std::optional<Keire::Vector2> position);
        [[nodiscard]] bool DrawFunctionExtractionPopup(Keire::UiFrame& ui);
        void DrawFunctionExtractionContextMenu(Keire::UiFrame& ui, const Keire::MaterialGraphDefinition& definition);
        void Report(std::string message) noexcept;

        IMaterialGraphPanelController& m_Controller;
        StableNodeGraphCanvas m_Canvas;
        GraphCommentEditorState m_CommentEditor;
        AssetPicker m_ShaderGraphPicker;
        AssetPicker m_RawShaderPicker;
        AssetPicker m_TexturePicker;
        Keire::UiPanelRegistration m_Registration;
        std::optional<Keire::AssetId> m_SelectedNode;
        std::vector<Keire::AssetId> m_SelectedNodes;
        std::optional<Keire::AssetId> m_SelectedConnection;
        std::optional<Keire::AssetId> m_FrameNode;
        std::optional<Keire::AssetId> m_InspectorNode;
        std::optional<Keire::AssetId> m_InspectorCommentNode;
        std::optional<Keire::Vector2> m_NodeCreationPosition;
        std::optional<NodeGraphContextRequest> m_GraphContext;
        std::string m_InspectorName;
        std::string m_InspectorSymbol;
        std::string m_InspectorInclude;
        std::string m_InspectorFunction;
        std::string m_InspectorDescription;
        std::string m_InspectorCategory;
        std::string m_InspectorComment;
        double m_InspectorSortPriority = 0.0;
        double m_InspectorMinimum = 0.0;
        double m_InspectorMaximum = 1.0;
        double m_InspectorStep = 0.01;
        bool m_InspectorHasMinimum = false;
        bool m_InspectorHasMaximum = false;
        bool m_InspectorHasStep = false;
        bool m_InspectorCommentPinned = false;
        bool m_ShowTemplateParameters = false;
        std::string m_NodeSearch;
        std::string m_ExtractionName;
        GraphBookmarkSet m_Bookmarks;
        std::string m_Message;
        Keire::Ref<Keire::UiImage> m_PreviewImage;
        std::optional<Keire::MaterialGraphDefinition> m_PreviewDefinition;
        std::string m_PreviewError;
        Keire::Ref<Keire::JobSystem> m_JobSystem;
        Keire::Ref<Keire::JobScope> m_JobScope;
        Keire::JobHandle m_PreviewRender;
        std::shared_ptr<PreviewRenderState> m_PreviewRenderState;
        std::shared_ptr<std::atomic<std::uint64_t>> m_PreviewCancellation =
            std::make_shared<std::atomic<std::uint64_t>>(1);
        std::uint64_t m_PreviewGeneration = 1;
        bool m_PreviewDirty = false;
        bool m_OwnJobSystem = false;
        bool m_OpenFunctionExtractionPopup = false;
        bool m_ShowPreview = true;
    };
} // namespace KeireEditor
