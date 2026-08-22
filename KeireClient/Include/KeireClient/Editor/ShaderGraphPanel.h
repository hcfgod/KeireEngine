#pragma once

#include "Keire/Core.h"
#include "KeireClient/Editor/AssetPicker.h"
#include "KeireClient/Editor/AuthoringWidgets.h"
#include "KeireClient/Editor/GraphClipboard.h"
#include "KeireClient/Editor/GraphComments.h"
#include "KeireClient/Editor/GraphLayout.h"
#include "KeireClient/Editor/GraphNavigation.h"
#include "KeireClient/Editor/ShaderGraphDocument.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace KeireEditor
{
    class IShaderGraphPanelController
    {
      public:
        virtual ~IShaderGraphPanelController() = default;
        [[nodiscard]] virtual ShaderGraphDocument& ShaderGraphState() noexcept = 0;
        [[nodiscard]] virtual const Keire::UiThemeDefinition& ShaderGraphTheme() const noexcept = 0;
        virtual void SaveShaderGraphDocument() = 0;
        virtual void UndoShaderGraphEdit() = 0;
        virtual void RedoShaderGraphEdit() = 0;
        [[nodiscard]] virtual std::span<const Keire::AssetSourceRecord> ShaderGraphAssetRecords() const noexcept = 0;
        [[nodiscard]] virtual Keire::Ref<const Keire::MeshAsset>
        ResolveShaderGraphPreviewMesh(Keire::AssetId asset) = 0;
        [[nodiscard]] virtual std::optional<Keire::ShaderGraphDefinition>
        ResolveShaderGraphFunction(Keire::AssetId asset) const = 0;
        virtual void RevealShaderGraphAsset(Keire::AssetId asset) = 0;
        virtual void SetGraphClipboard(std::string_view text) = 0;
        [[nodiscard]] virtual std::string GraphClipboard() const = 0;
        [[nodiscard]] virtual bool ExtractShaderGraphSelectionToFunction(std::span<const Keire::AssetId> selection,
                                                                         std::string_view name) = 0;
        virtual void ReportShaderGraphError(std::string message) noexcept = 0;
    };

    class ShaderGraphPanel final
    {
      public:
        explicit ShaderGraphPanel(IShaderGraphPanelController& controller) : m_Controller(controller) {}
        ~ShaderGraphPanel() noexcept;

        void Attach(Keire::UiWorkspace& workspace);
        void SetJobSystem(Keire::Ref<Keire::JobSystem> jobs);
        void Draw(Keire::UiFrame& ui);
        void ResetTransientState() noexcept;
        void UpdatePreview(const Keire::ShaderGraphCompilation& compilation,
                           const ShaderGraphPreviewSettings& settings);
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

        struct PreviewRenderState
        {
            std::mutex Mutex;
            std::optional<PreviewRenderResult> Result;
        };

        void DrawHeader(Keire::UiFrame& ui);
        void DrawPreview(Keire::UiFrame& ui);
        void DrawCanvas(Keire::UiFrame& ui);
        void DrawComments(Keire::UiFrame& ui, ShaderGraphDocument& document, const ShaderGraphCanvasModel& model,
                          const NodeGraphCommentModel& comments, const NodeGraphCanvasResult& canvas);
        void CreateComment(Keire::UiFrame& ui, ShaderGraphDocument& document, const ShaderGraphCanvasModel& model,
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
        void DrawDiagnostics(Keire::UiFrame& ui);
        void EnsureJobScope();
        [[nodiscard]] bool DrawNodeCreationMenu(Keire::UiFrame& ui, std::optional<Keire::Vector2> graphPosition,
                                                const Keire::ShaderGraphNode* compatibleNode = nullptr,
                                                const Keire::ShaderGraphPin* compatiblePin = nullptr);
        [[nodiscard]] bool AddNode(Keire::ShaderGraphNodeKind kind,
                                   Keire::ShaderGraphValueType type = Keire::ShaderGraphValueType::Scalar,
                                   std::optional<Keire::Vector2> graphPosition = std::nullopt);
        [[nodiscard]] bool AddFunctionNode(Keire::AssetId asset, std::string_view name,
                                           std::optional<Keire::Vector2> graphPosition);
        [[nodiscard]] bool CanExtractSelection(const Keire::ShaderGraphDefinition& definition) const;
        [[nodiscard]] bool DrawFunctionExtractionPopup(Keire::UiFrame& ui);
        void Report(std::string message) noexcept;

        IShaderGraphPanelController& m_Controller;
        StableNodeGraphCanvas m_Canvas;
        GraphCommentEditorState m_CommentEditor;
        AssetPicker m_AssetPicker;
        AssetPicker m_NodeAssetPicker;
        Keire::UiPanelRegistration m_Registration;
        Keire::Ref<Keire::UiImage> m_PreviewImage;
        std::vector<Keire::ShaderPropertyDefinition> m_PreviewProperties;
        ShaderGraphPreviewSettings m_PreviewSettings;
        std::optional<Keire::AssetId> m_SelectedNode;
        std::vector<Keire::AssetId> m_SelectedNodes;
        std::optional<Keire::AssetId> m_SelectedConnection;
        std::optional<Keire::AssetId> m_FrameNode;
        std::optional<Keire::AssetId> m_InspectorNode;
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
        std::string m_NodeSearch;
        std::string m_ExtractionName;
        NodeMenuSelection m_NodeMenuSelection;
        GraphBookmarkSet m_Bookmarks;
        std::optional<Keire::Vector2> m_NodeCreationPosition;
        std::optional<NodeGraphContextRequest> m_GraphContext;
        std::string m_Message;
        std::uint32_t m_PreviewWidth = 320;
        std::uint32_t m_PreviewHeight = 220;
        Keire::Ref<Keire::JobSystem> m_JobSystem;
        Keire::Ref<Keire::JobScope> m_JobScope;
        Keire::JobHandle m_PreviewRender;
        std::shared_ptr<PreviewRenderState> m_PreviewRenderState;
        std::shared_ptr<std::atomic<std::uint64_t>> m_PreviewCancellation =
            std::make_shared<std::atomic<std::uint64_t>>(1);
        std::uint64_t m_PreviewGeneration = 1;
        bool m_PreviewRefinement = false;
        bool m_PreviewDirty = false;
        bool m_OwnJobSystem = false;
        bool m_NodeMenuOpen = false;
        bool m_OpenFunctionExtractionPopup = false;
        bool m_ShowPreview = true;
    };
} // namespace KeireEditor
