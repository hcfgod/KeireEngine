#pragma once

#include "Keire/Scenes/ScenePresentationRuntime.h"
#include "Keire/Ui.h"
#include "Keire/Ui/UiToolkit.h"
#include "Keire/Undo.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace KeireEditor
{
    enum class UiBuilderCanvasGesture : std::uint8_t
    {
        None,
        Move,
        ResizeTop,
        ResizeRight,
        ResizeBottom,
        ResizeLeft,
        ResizeTopLeft,
        ResizeTopRight,
        ResizeBottomRight,
        ResizeBottomLeft
    };

    [[nodiscard]] Keire::RuntimeUiRect ResolveUiBuilderCanvasGesture(Keire::RuntimeUiRect initial,
                                                                     Keire::RuntimeUiRect parentBounds,
                                                                     Keire::Vector2 delta,
                                                                     UiBuilderCanvasGesture gesture) noexcept;
    [[nodiscard]] Keire::RuntimeUiRect ResolveUiBuilderCanvasPlacement(Keire::RuntimeUiRect parentBounds,
                                                                       Keire::UiSize desiredSize,
                                                                       Keire::UiPosition center) noexcept;
    [[nodiscard]] Keire::UiSize UiBuilderCanvasControlDefaultSize(Keire::UiVisualElementType type) noexcept;
    void PersistUiBuilderCanvasGeometry(Keire::UiVisualElementDefinition& element, Keire::RuntimeUiRect parentBounds,
                                        Keire::RuntimeUiRect geometry);
    [[nodiscard]] Keire::RuntimeUiRect TransformUiBuilderCanvasPreviewRect(Keire::RuntimeUiRect rectangle,
                                                                           Keire::RuntimeUiRect initial,
                                                                           Keire::RuntimeUiRect draft) noexcept;

    enum class UiBuilderResolutionPreset : std::uint8_t
    {
        Hd,
        FullHd,
        Qhd,
        UltraHd,
        Custom
    };

    enum class UiBuilderOrientation : std::uint8_t
    {
        Landscape,
        Portrait
    };

    struct UiBuilderPreviewSettings final
    {
        std::uint32_t Width = 1920;
        std::uint32_t Height = 1080;
        std::uint32_t ReferenceWidth = 1920;
        std::uint32_t ReferenceHeight = 1080;
        float Dpi = 96.0F;
        float UserScale = 1.0F;
        Keire::RuntimeUiScaleMode ScaleMode = Keire::RuntimeUiScaleMode::ScaleWithViewport;
        float MatchWidthOrHeight = 0.5F;
        Keire::RuntimeUiInsets SafeArea;
        Keire::UiStylePseudoState PseudoStates = Keire::UiStylePseudoState::None;
        Keire::Vector2 Pan;
        float Zoom = 1.0F;
        float VerticalGuide = -1.0F;
        float HorizontalGuide = -1.0F;
        UiBuilderResolutionPreset Preset = UiBuilderResolutionPreset::FullHd;
        bool ShowSafeArea = true;
        bool ShowRulers = true;
        bool ShowGuides = true;

        void ApplyPreset(UiBuilderResolutionPreset preset) noexcept;
        void ApplyOrientation(UiBuilderOrientation orientation) noexcept;
        [[nodiscard]] bool MatchGameView(std::uint32_t width, std::uint32_t height) noexcept;
        void Normalize() noexcept;
        void SetPseudoState(Keire::UiStylePseudoState state, bool enabled) noexcept;
        [[nodiscard]] bool HasPseudoState(Keire::UiStylePseudoState state) const noexcept;
        void PanBy(Keire::Vector2 delta) noexcept;
        void ZoomBy(float factor) noexcept;
        void ResetView() noexcept;
        [[nodiscard]] Keire::RuntimeUiCanvasSettings CanvasSettings() const noexcept;
        [[nodiscard]] bool operator==(const UiBuilderPreviewSettings&) const = default;
    };

    struct UiBuilderPreviewElement final
    {
        Keire::AssetId StableId;
        Keire::RuntimeUiElementId RuntimeId;
        Keire::RuntimeUiElementState State;
    };

    struct UiBuilderRetainedPreview final
    {
        Keire::RuntimeUiStatistics Statistics;
        std::optional<Keire::RuntimeUiElementState> SelectedState;
        std::vector<Keire::RuntimeUiDrawCommand> DrawCommands;
        std::vector<UiBuilderPreviewElement> Elements;
        std::size_t LinkedStyleSheets = 0;
        std::size_t ResolvedStyleSheets = 0;
        std::size_t InlineStyleProperties = 0;
    };

    [[nodiscard]] UiBuilderRetainedPreview
    BuildUiBuilderRetainedPreview(const Keire::UiVisualTreeDefinition& definition, Keire::AssetId selected,
                                  const UiBuilderPreviewSettings& settings,
                                  std::span<const Keire::Ref<const Keire::UiStyleSheetAsset>> styleSheets = {},
                                  Keire::UiTemplateResolver templateResolver = {});

    struct UiBuilderRuntimeDebugSnapshot final
    {
        Keire::RuntimeUiStatistics Statistics;
        std::optional<Keire::RuntimeUiElementState> SelectedState;
        std::size_t LinkedStyleSheets = 0;
        std::size_t ResolvedStyleSheets = 0;
        std::size_t InlineStyleProperties = 0;
    };

    [[nodiscard]] UiBuilderRuntimeDebugSnapshot
    BuildUiBuilderRuntimeDebugSnapshot(const Keire::UiVisualTreeDefinition& definition, Keire::AssetId selected,
                                       std::span<const Keire::Ref<const Keire::UiStyleSheetAsset>> styleSheets = {},
                                       float viewportWidth = 1920.0F, float viewportHeight = 1080.0F,
                                       Keire::UiTemplateResolver templateResolver = {});

    enum class UiBuilderLiveDebugStatus : std::uint8_t
    {
        Unavailable,
        Live,
        Stale
    };

    struct UiBuilderLiveDebugElement final
    {
        Keire::AssetId StableId;
        Keire::ScenePresentationUiDocumentDebugState State;

        [[nodiscard]] bool operator==(const UiBuilderLiveDebugElement&) const = default;
    };

    struct UiBuilderLiveDebugEvent final
    {
        enum class Phase : std::uint8_t
        {
            Capture,
            Target,
            Bubble
        };

        Keire::RuntimeUiEventType Type = Keire::RuntimeUiEventType::Click;
        Phase PropagationPhase = Phase::Target;
        std::uint64_t Sequence = 0;
        std::optional<Keire::AssetId> Target;
        std::optional<Keire::AssetId> CurrentTarget;
        float PointerX = 0.0F;
        float PointerY = 0.0F;
        Keire::RuntimeUiPointerButton Button = Keire::RuntimeUiPointerButton::Primary;

        [[nodiscard]] bool operator==(const UiBuilderLiveDebugEvent&) const = default;
    };

    struct UiBuilderLiveDebugDocument final
    {
        Keire::EntityId Entity;
        std::uint64_t DocumentGeneration = 0;
        Keire::RuntimeUiStatistics PresentationStatistics;
        std::optional<Keire::AssetId> FocusedElement;
        std::array<bool, 3> PresentationPointerCaptures{};
        std::array<std::optional<Keire::AssetId>, 3> CapturedElements;
        std::vector<UiBuilderLiveDebugElement> Elements;
        std::vector<UiBuilderLiveDebugEvent> PendingTargetEvents;
        std::vector<UiBuilderLiveDebugEvent> EventTrace;
    };

    struct UiBuilderLiveDebugSnapshot final
    {
        Keire::AssetId VisualTree;
        std::uint64_t Sequence = 0;
        std::vector<UiBuilderLiveDebugDocument> Documents;
        std::optional<std::size_t> VertexCount;
        std::optional<std::size_t> AtlasTextureCount;
        std::optional<std::size_t> AtlasBytes;
        std::optional<float> StyleMilliseconds;
        std::optional<float> LayoutMilliseconds;
        std::optional<float> RepaintMilliseconds;
        std::vector<std::string> DirtyReasons;
        std::vector<std::string> SelectorPrecedence;
        bool EventPropagationTraceAvailable = false;
        bool ElementPointerCaptureAvailable = false;
        bool DirtyReasonsAvailable = false;
        bool SelectorPrecedenceAvailable = false;
    };

    struct UiBuilderLiveDebugCapture final
    {
        std::shared_ptr<const UiBuilderLiveDebugSnapshot> Snapshot;
        std::string Diagnostic;
    };

    class UiBuilderLiveDebugStore final
    {
      public:
        void Refresh(Keire::AssetId expectedVisualTree, UiBuilderLiveDebugCapture capture) noexcept;
        void Close() noexcept;

        [[nodiscard]] UiBuilderLiveDebugStatus Status() const noexcept { return m_Status; }
        [[nodiscard]] const std::shared_ptr<const UiBuilderLiveDebugSnapshot>& Current() const noexcept
        {
            return m_Current;
        }
        [[nodiscard]] const std::string& Diagnostic() const noexcept { return m_Diagnostic; }

      private:
        Keire::AssetId m_VisualTree;
        std::shared_ptr<const UiBuilderLiveDebugSnapshot> m_Current;
        std::string m_Diagnostic;
        UiBuilderLiveDebugStatus m_Status = UiBuilderLiveDebugStatus::Unavailable;
    };

    struct UiBuilderClipboard final
    {
        std::vector<Keire::UiVisualElementDefinition> Elements;

        [[nodiscard]] bool Empty() const noexcept { return Elements.empty(); }
    };

    class UiBuilderDocument final
    {
      public:
        void Open(Keire::AssetId asset, Keire::UiVisualTreeDefinition definition, std::uint64_t revision,
                  std::filesystem::path source, Keire::Ref<Keire::UndoContext> undo = {});
        void Close() noexcept;

        [[nodiscard]] Keire::AssetId Asset() const noexcept { return m_Asset; }
        [[nodiscard]] std::uint64_t Revision() const noexcept { return m_Revision; }
        [[nodiscard]] std::uint64_t Generation() const noexcept { return m_Generation; }
        [[nodiscard]] bool Dirty() const noexcept { return m_Dirty; }
        [[nodiscard]] const Keire::UiVisualTreeDefinition& Definition() const noexcept { return m_Definition; }
        [[nodiscard]] Keire::AssetId Selection() const noexcept { return m_Selection; }
        [[nodiscard]] std::span<const Keire::AssetId> Selections() const noexcept { return m_Selections; }
        [[nodiscard]] bool IsSelected(Keire::AssetId element) const noexcept;
        [[nodiscard]] Keire::Ref<Keire::UndoContext> UndoContext() const noexcept { return m_Undo; }
        [[nodiscard]] const std::filesystem::path& SourcePath() const noexcept { return m_Source; }

        void Select(Keire::AssetId element) noexcept;
        void SetSelection(std::span<const Keire::AssetId> elements, Keire::AssetId primary = {}) noexcept;
        void ToggleSelection(Keire::AssetId element) noexcept;
        [[nodiscard]] const Keire::UiVisualElementDefinition* Find(Keire::AssetId element) const noexcept;
        [[nodiscard]] Keire::UiVisualElementDefinition* Find(Keire::AssetId element) noexcept;
        [[nodiscard]] Keire::AssetId ParentOf(Keire::AssetId element) const noexcept;
        [[nodiscard]] bool Edit(std::string_view name, Keire::UiVisualTreeDefinition candidate);
        [[nodiscard]] Keire::AssetId AddElement(Keire::AssetId parent, Keire::UiVisualElementType type);
        [[nodiscard]] Keire::AssetId AddCustomElement(Keire::AssetId parent, std::string customType);
        [[nodiscard]] Keire::AssetId AddCanvasElement(Keire::AssetId parent, Keire::UiVisualElementType type,
                                                      Keire::RuntimeUiRect parentBounds, Keire::UiPosition center);
        [[nodiscard]] Keire::AssetId AddCanvasCustomElement(Keire::AssetId parent, std::string customType,
                                                            Keire::RuntimeUiRect parentBounds,
                                                            Keire::UiPosition center);
        [[nodiscard]] bool RemoveElement(Keire::AssetId element);
        [[nodiscard]] bool RemoveSelection();
        [[nodiscard]] bool ReparentElement(Keire::AssetId element, Keire::AssetId parent, std::size_t index);
        [[nodiscard]] bool ReparentElements(std::span<const Keire::AssetId> elements, Keire::AssetId parent,
                                            std::size_t index);
        [[nodiscard]] UiBuilderClipboard CopySelection() const;
        [[nodiscard]] std::vector<Keire::AssetId> PasteElements(Keire::AssetId parent,
                                                                const UiBuilderClipboard& clipboard);
        [[nodiscard]] Keire::AssetId AddTemplate(Keire::AssetId parent, Keire::AssetId visualTree);
        [[nodiscard]] Keire::AssetId AddSlot(Keire::AssetId parent, std::string slot);
        [[nodiscard]] bool SetClasses(std::span<const Keire::AssetId> elements, std::vector<std::string> classes);
        [[nodiscard]] bool SetTemplate(Keire::AssetId element, Keire::AssetId visualTree);
        [[nodiscard]] bool SetSlot(Keire::AssetId element, std::string slot);
        [[nodiscard]] bool SetBindings(Keire::AssetId element, std::vector<Keire::UiBindingDefinition> bindings);
        [[nodiscard]] bool ApplySource(std::span<const std::byte> source, std::string& diagnostic);
        [[nodiscard]] std::string SourcePreview() const;
        void Save();
        void ReloadFromSource(bool discardLocalChanges = false);
        [[nodiscard]] bool Undo();
        [[nodiscard]] bool Redo();

      private:
        void AdvanceGeneration() noexcept;
        void NormalizeSelection() noexcept;
        void RefreshDirtyState();
        void RecordApplied(std::string_view name, Keire::UiVisualTreeDefinition before);

        Keire::AssetId m_Asset;
        Keire::AssetId m_Selection;
        std::vector<Keire::AssetId> m_Selections;
        Keire::UiVisualTreeDefinition m_Definition;
        Keire::UiVisualTreeDefinition m_Baseline;
        std::uint64_t m_Revision = 0;
        std::uint64_t m_Generation = 0;
        std::filesystem::path m_Source;
        Keire::Ref<Keire::UndoContext> m_Undo;
        bool m_Dirty = false;
    };
} // namespace KeireEditor
