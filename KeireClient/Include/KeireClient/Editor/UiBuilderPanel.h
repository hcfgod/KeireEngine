#pragma once

#include "Keire/Core.h"
#include "KeireClient/Editor/AssetPicker.h"
#include "KeireClient/Editor/UiBuilderDocument.h"
#include "KeireClient/Editor/UiBuilderStyleSheetDocument.h"
#include "KeireClient/Editor/UiStyleSourceEditor.h"
#include "KeireClient/Editor/UiStyleTokenRefactor.h"

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace KeireEditor
{
    enum class UiBuilderWorkspaceMode : std::uint8_t
    {
        Design,
        Styles,
        Debug
    };

    class IUiBuilderController
    {
      public:
        virtual ~IUiBuilderController() = default;
        [[nodiscard]] virtual UiBuilderDocument& UiBuilderState() noexcept = 0;
        [[nodiscard]] virtual const Keire::UiThemeDefinition& UiBuilderTheme() const noexcept = 0;
        [[nodiscard]] virtual Keire::Ref<Keire::AssetSystem> UiBuilderAssets() const noexcept = 0;
        [[nodiscard]] virtual std::span<const Keire::AssetSourceRecord> UiBuilderAssetRecords() const noexcept = 0;
        virtual void RevealUiBuilderAsset(Keire::AssetId asset) = 0;
        [[nodiscard]] virtual std::optional<Keire::UiSize> UiBuilderGameViewSize() const noexcept = 0;
        [[nodiscard]] virtual UiBuilderStyleSheetDocument& UiBuilderStyleSheetState() noexcept = 0;
        virtual void ActivateUiBuilderHistory() noexcept = 0;
        virtual void ActivateUiBuilderStyleSheetHistory() noexcept = 0;
        virtual void OpenUiBuilderStyleSheet(Keire::AssetId asset) = 0;
        virtual void SaveUiBuilderDocument() = 0;
        virtual void ReloadUiBuilderDocument() = 0;
        virtual void SaveUiBuilderStyleSheet() = 0;
        virtual void RequestSaveUiBuilderStyleSheetAs() = 0;
        virtual void ReloadUiBuilderStyleSheet() = 0;
        [[nodiscard]] virtual UiStyleTokenRefactorPreview
        PreviewUiBuilderTokenRefactor(std::string_view currentName, std::string_view replacementName) = 0;
        virtual void ApplyUiBuilderTokenRefactor(const UiStyleTokenRefactorPreview& preview) = 0;
        virtual void ReportUiBuilderError(std::string message) noexcept = 0;
        [[nodiscard]] virtual UiBuilderLiveDebugCapture CaptureUiBuilderLiveDebug(Keire::AssetId visualTree) = 0;
        virtual void SetUiBuilderLivePicking(Keire::AssetId visualTree, bool enabled) noexcept = 0;
        [[nodiscard]] virtual std::optional<Keire::AssetId>
        ConsumeUiBuilderLivePick(Keire::AssetId visualTree) noexcept = 0;
    };

    [[nodiscard]] std::string_view UiBuilderElementTypeName(Keire::UiVisualElementType type) noexcept;

    class UiBuilderPanel final
    {
      public:
        explicit UiBuilderPanel(IUiBuilderController& controller) noexcept : m_Controller(controller) {}
        void Attach(Keire::UiWorkspace& workspace)
        {
            m_Registration = workspace.RegisterPanel({"editor.ui-builder", "UI Builder", false});
        }
        void Draw(Keire::UiFrame& ui);
        void ResetTransientState() noexcept;
        void SetMessage(std::string message) { m_Message = std::move(message); }
        [[nodiscard]] Keire::UiPanelRegistration& Registration() noexcept { return m_Registration; }

      private:
        struct CanvasGestureState final
        {
            Keire::AssetId Element;
            Keire::RuntimeUiRect Initial;
            Keire::RuntimeUiRect Draft;
            Keire::RuntimeUiRect ParentBounds;
            Keire::UiPosition StartPointer;
            std::vector<Keire::RuntimeUiElementId> RuntimeElements;
            UiBuilderCanvasGesture Gesture = UiBuilderCanvasGesture::None;
            bool Changed = false;
        };

        void SynchronizeDraft();
        void DrawHierarchy(Keire::UiFrame& ui);
        void DrawLibrary(Keire::UiFrame& ui);
        void DrawViewport(Keire::UiFrame& ui);
        void DrawPreviewToolbar(Keire::UiFrame& ui);
        [[nodiscard]] Keire::UiTemplateResolver
        CreateTemplateResolver(const Keire::UiVisualTreeDefinition& definition,
                               std::vector<const Keire::UiVisualTreeAsset*>& identities) const;
        void DrawInspector(Keire::UiFrame& ui);
        void DrawStyleSheets(Keire::UiFrame& ui);
        void DrawStyleProperties(Keire::UiFrame& ui);
        void DrawStyleComputed(Keire::UiFrame& ui);
        void DrawStyleSource(Keire::UiFrame& ui);
        void DrawDebugger(Keire::UiFrame& ui);
        void DrawSource(Keire::UiFrame& ui);
        void RefreshPreviewSnapshot();
        void RefreshDebuggerSnapshot();
        [[nodiscard]] Keire::RuntimeUiRect CanvasParentBounds(Keire::AssetId parent) const noexcept;

        IUiBuilderController& m_Controller;
        Keire::UiPanelRegistration m_Registration;
        Keire::AssetId m_DraftElement;
        Keire::AssetId m_SourceAsset;
        Keire::AssetId m_DebugAsset;
        Keire::AssetId m_DebugSelection;
        std::uint64_t m_DebugGeneration = 0;
        std::optional<UiBuilderRuntimeDebugSnapshot> m_DebugSnapshot;
        std::vector<const Keire::UiStyleSheetAsset*> m_DebugStyleSheets;
        std::vector<const Keire::UiVisualTreeAsset*> m_DebugTemplates;
        UiBuilderLiveDebugStore m_LiveDebugStore;
        Keire::AssetId m_LiveDebugAsset;
        Keire::AssetId m_PreviewAsset;
        Keire::AssetId m_PreviewSelection;
        std::uint64_t m_PreviewGeneration = 0;
        std::optional<UiBuilderRetainedPreview> m_PreviewSnapshot;
        std::vector<const Keire::UiStyleSheetAsset*> m_PreviewStyleSheets;
        std::vector<const Keire::UiVisualTreeAsset*> m_PreviewTemplates;
        UiBuilderPreviewSettings m_PreviewSettings;
        std::optional<UiBuilderPreviewSettings> m_BuiltPreviewSettings;
        std::string m_PreviewDiagnostic;
        std::string m_NameDraft;
        std::string m_ClassesDraft;
        std::string m_TextDraft;
        std::string m_CustomTypeDraft;
        std::string m_InlineStyleDraft;
        AssetPicker m_StyleSheetPicker;
        Keire::AssetId m_StyleSheetDraft;
        AssetPicker m_StyleValueAssetPicker;
        Keire::AssetId m_StyleValueAssetDraft;
        Keire::AssetId m_StyleRuleAsset;
        std::uint64_t m_StyleRuleGeneration = 0;
        std::optional<std::size_t> m_StyleRuleSelection;
        std::string m_StyleSelectorDraft;
        std::string m_StyleSelectorTypeDraft;
        std::string m_StyleSelectorNameDraft;
        std::string m_StyleSelectorClassesDraft;
        Keire::UiStyleCombinator m_StyleSelectorCombinator = Keire::UiStyleCombinator::None;
        Keire::UiStylePseudoState m_StyleSelectorStates = Keire::UiStylePseudoState::None;
        std::string m_StyleDeclarationsDraft;
        std::string m_StyleRuleSearch;
        std::string m_StylePropertySearch;
        std::string m_StyleSourceDraft;
        UiStyleSourceEditor m_StyleSourceEditor;
        Keire::UiCodeEditorState m_StyleSourceEditorState;
        std::string m_StyleSourceFind;
        std::string m_StyleSourceReplace;
        std::vector<UiStyleSourceMatch> m_StyleSourceMatches;
        std::size_t m_StyleSourceMatch = 0;
        std::string m_StyleExternalComparison;
        std::string m_StyleTokenSearch;
        std::string m_StyleTokenSelection;
        std::string m_StyleTokenNameDraft;
        std::string m_StyleTokenValueDraft;
        std::optional<UiStyleTokenRefactorPreview> m_StyleTokenRefactorPreview;
        bool m_StyleTokenRefactorConfirmed = false;
        bool m_StyleSourceFindCaseSensitive = false;
        std::string m_NewStyleProperty;
        std::string m_NewStyleValue;
        std::uint64_t m_StyleSourceGeneration = 0;
        std::chrono::steady_clock::time_point m_StyleSourceEditTime{};
        bool m_StyleSourceParsePending = false;
        bool m_StyleEditInline = false;
        std::string m_TemplateDraft;
        std::string m_SlotDraft;
        std::string m_NewTemplateDraft;
        std::string m_NewSlotDraft = "content";
        std::string m_BindingPropertyDraft;
        std::string m_BindingPathDraft;
        std::string m_BindingModeDraft = "OneWay";
        std::string m_SourceDraft;
        std::string m_Message;
        UiBuilderClipboard m_Clipboard;
        CanvasGestureState m_CanvasGesture;
        UiBuilderWorkspaceMode m_WorkspaceMode = UiBuilderWorkspaceMode::Design;
        bool m_SourceEditing = false;
        bool m_LivePicking = false;
    };
} // namespace KeireEditor
