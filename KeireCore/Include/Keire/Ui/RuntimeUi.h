#pragma once

#include "Keire/Api.h"
#include "Keire/Assets/Asset.h"
#include "Keire/Math/Math.h"
#include "Keire/Ref.h"

#include <array>
#include <compare>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace Keire
{
    class KEIRE_API RuntimeUiElementId final
    {
      public:
        constexpr RuntimeUiElementId() noexcept = default;
        [[nodiscard]] constexpr explicit operator bool() const noexcept { return m_Value != 0; }
        [[nodiscard]] constexpr auto operator<=>(const RuntimeUiElementId&) const noexcept = default;
        [[nodiscard]] constexpr std::uint64_t Value() const noexcept { return m_Value; }

      private:
        friend class RuntimeUiTree;
        explicit constexpr RuntimeUiElementId(const std::uint64_t value) noexcept : m_Value(value) {}
        std::uint64_t m_Value = 0;
    };

    enum class RuntimeUiElementType : std::uint8_t
    {
        Canvas,
        Panel,
        Text,
        Image,
        Button,
        HorizontalLayout,
        VerticalLayout,
        Spacer,
        GridLayout,
        Slider,
        Toggle,
        InputField,
        ScrollView
    };

    enum class RuntimeUiDirtyReason : std::uint16_t
    {
        None = 0,
        Hierarchy = 1U << 0U,
        Style = 1U << 1U,
        Content = 1U << 2U,
        Control = 1U << 3U,
        Visibility = 1U << 4U,
        Interaction = 1U << 5U,
        Transition = 1U << 6U,
        LayoutSettings = 1U << 7U,
        Descendant = 1U << 8U
    };

    [[nodiscard]] constexpr RuntimeUiDirtyReason operator|(const RuntimeUiDirtyReason left,
                                                           const RuntimeUiDirtyReason right) noexcept
    {
        return static_cast<RuntimeUiDirtyReason>(static_cast<std::uint16_t>(left) | static_cast<std::uint16_t>(right));
    }

    enum class RuntimeUiPositionMode : std::uint8_t
    {
        Flow,
        Absolute
    };

    enum class RuntimeUiAlignment : std::uint8_t
    {
        Start,
        Center,
        End,
        Stretch
    };

    enum class RuntimeUiJustification : std::uint8_t
    {
        Start,
        Center,
        End,
        SpaceBetween,
        SpaceAround,
        SpaceEvenly
    };

    enum class RuntimeUiWrapMode : std::uint8_t
    {
        NoWrap,
        Wrap,
        WrapReverse
    };

    enum class RuntimeUiScaleMode : std::uint8_t
    {
        ConstantPixels,
        ScaleWithViewport,
        ConstantPhysicalSize
    };

    enum class RuntimeUiPointerButton : std::uint8_t
    {
        Primary,
        Secondary,
        Middle
    };

    enum class RuntimeUiNavigation : std::uint8_t
    {
        Previous,
        Next,
        Left,
        Right,
        Up,
        Down,
        Accept,
        Cancel
    };

    enum class RuntimeUiKey : std::uint8_t
    {
        Backspace,
        Delete,
        Enter,
        Escape
    };

    struct RuntimeUiRect
    {
        float X = 0.0F;
        float Y = 0.0F;
        float Width = 0.0F;
        float Height = 0.0F;

        [[nodiscard]] bool Contains(float x, float y) const noexcept;
        [[nodiscard]] RuntimeUiRect Intersect(RuntimeUiRect other) const noexcept;
        [[nodiscard]] bool Empty() const noexcept { return Width <= 0.0F || Height <= 0.0F; }
        [[nodiscard]] bool operator==(const RuntimeUiRect&) const = default;
    };

    struct RuntimeUiInsets
    {
        float Left = 0.0F;
        float Top = 0.0F;
        float Right = 0.0F;
        float Bottom = 0.0F;

        [[nodiscard]] bool operator==(const RuntimeUiInsets&) const = default;
    };

    struct RuntimeUiCanvasSettings
    {
        float ReferenceWidth = 1920.0F;
        float ReferenceHeight = 1080.0F;
        RuntimeUiScaleMode ScaleMode = RuntimeUiScaleMode::ScaleWithViewport;
        float MatchWidthOrHeight = 0.5F;
        float AccessibilityScale = 1.0F;
        bool RespectSafeArea = true;

        [[nodiscard]] bool operator==(const RuntimeUiCanvasSettings&) const = default;
    };

    enum class RuntimeUiGradientKind : std::uint8_t
    {
        None,
        Linear,
        Radial
    };

    struct RuntimeUiGradientStop
    {
        float Offset = 0.0F;
        Color ColorValue;

        [[nodiscard]] bool operator==(const RuntimeUiGradientStop&) const = default;
    };

    struct RuntimeUiGradient
    {
        RuntimeUiGradientKind Kind = RuntimeUiGradientKind::None;
        float LinearAngleDegrees = 180.0F;
        Vector2 RadialCenter{0.5F, 0.5F};
        float RadialRadius = 0.5F;
        std::array<RuntimeUiGradientStop, 8> Stops{};
        std::uint8_t StopCount = 0;

        [[nodiscard]] bool operator==(const RuntimeUiGradient&) const = default;
    };

    enum class RuntimeUiTransitionProperty : std::uint8_t
    {
        All,
        BackgroundColor,
        ForegroundColor,
        BorderColor,
        Opacity,
        Left,
        Top,
        Width,
        Height,
        BorderRadius
    };

    struct RuntimeUiStyle
    {
        RuntimeUiPositionMode Position = RuntimeUiPositionMode::Flow;
        RuntimeUiAlignment HorizontalAlignment = RuntimeUiAlignment::Stretch;
        RuntimeUiAlignment VerticalAlignment = RuntimeUiAlignment::Stretch;
        RuntimeUiAlignment ChildHorizontalAlignment = RuntimeUiAlignment::Start;
        RuntimeUiAlignment ChildVerticalAlignment = RuntimeUiAlignment::Start;
        bool UseAnchors = false;
        Vector2 AnchorMinimum{0.5F, 0.5F};
        Vector2 AnchorMaximum{0.5F, 0.5F};
        Vector2 Pivot{0.5F, 0.5F};
        Vector2 AnchoredPosition;
        Vector2 SizeDelta;
        Vector2 LocalScale{1.0F, 1.0F};
        float X = 0.0F;
        float Y = 0.0F;
        float Width = 0.0F;
        float Height = 0.0F;
        /// Fractions of the containing block; a negative value means an authored pixel value or automatic sizing.
        float XPercent = -1.0F;
        float YPercent = -1.0F;
        float WidthPercent = -1.0F;
        float HeightPercent = -1.0F;
        float MinimumWidth = 0.0F;
        float MinimumHeight = 0.0F;
        float MaximumWidth = 100000.0F;
        float MaximumHeight = 100000.0F;
        float MinimumWidthPercent = -1.0F;
        float MinimumHeightPercent = -1.0F;
        float MaximumWidthPercent = -1.0F;
        float MaximumHeightPercent = -1.0F;
        float FlexGrow = 0.0F;
        float FlexShrink = 1.0F;
        float Gap = 0.0F;
        RuntimeUiAlignment AlignSelf = RuntimeUiAlignment::Stretch;
        bool HasAlignSelf = false;
        RuntimeUiJustification JustifyContent = RuntimeUiJustification::Start;
        RuntimeUiWrapMode Wrap = RuntimeUiWrapMode::NoWrap;
        bool ReverseChildren = false;
        RuntimeUiInsets Margin;
        RuntimeUiInsets Padding;
        Color Background{0.0F, 0.0F, 0.0F, 0.0F};
        Color HoverBackground{0.0F, 0.0F, 0.0F, 0.0F};
        Color PressedBackground{0.0F, 0.0F, 0.0F, 0.0F};
        Color DisabledBackground{0.0F, 0.0F, 0.0F, 0.0F};
        RuntimeUiGradient BackgroundGradient;
        Color Foreground{1.0F, 1.0F, 1.0F, 1.0F};
        Color Border{0.0F, 0.0F, 0.0F, 0.0F};
        float BorderWidth = 0.0F;
        float CornerRadius = 0.0F;
        float Opacity = 1.0F;
        float FontSize = 16.0F;
        Vector2 GridCellSize{160.0F, 48.0F};
        Vector2 ContentOffset;
        bool ControlChildWidth = true;
        bool ControlChildHeight = false;
        bool ForceExpandWidth = true;
        bool ForceExpandHeight = false;
        bool ClipChildren = false;
        std::int32_t SortingOrder = 0;
        std::int32_t NavigationOrder = 0;
        std::array<RuntimeUiTransitionProperty, 8> TransitionProperties{};
        std::array<float, 8> TransitionDurations{};
        std::uint8_t TransitionPropertyCount = 0;
        std::uint8_t TransitionDurationCount = 0;

        [[nodiscard]] bool operator==(const RuntimeUiStyle&) const = default;
    };

    struct RuntimeUiContent
    {
        std::string Text;
        AssetId Image;
        AssetId RenderTexture;
        AssetId Font;
        std::string AccessibilityLabel;
        std::string AccessibilityHint;
        std::uint8_t AccessibilityRole = 0;

        [[nodiscard]] bool operator==(const RuntimeUiContent&) const = default;
    };

    struct RuntimeUiControlState
    {
        float Minimum = 0.0F;
        float Maximum = 1.0F;
        float Value = 0.0F;
        Vector2 ContentSize;
        float ScrollSensitivity = 40.0F;
        bool Checked = false;
        bool Vertical = false;
        bool Reversed = false;

        [[nodiscard]] bool operator==(const RuntimeUiControlState&) const = default;
    };

    struct RuntimeUiElementState
    {
        RuntimeUiElementType Type = RuntimeUiElementType::Panel;
        RuntimeUiElementId Parent;
        RuntimeUiStyle Style;
        RuntimeUiContent Content;
        RuntimeUiControlState Control;
        RuntimeUiRect Rect;
        RuntimeUiRect ClipRect;
        float LayoutScale = 1.0F;
        bool Visible = true;
        bool Enabled = true;
        bool Interactable = false;
        bool Focused = false;
        bool Hovered = false;
        bool Pressed = false;
    };

    enum class RuntimeUiEventType : std::uint8_t
    {
        PointerEnter,
        PointerExit,
        PointerDown,
        PointerUp,
        Click,
        Focus,
        Blur,
        Submit,
        Cancel,
        ValueChanged,
        TextChanged
    };

    struct RuntimeUiEvent
    {
        RuntimeUiEventType Type = RuntimeUiEventType::Click;
        RuntimeUiElementId Target;
        float PointerX = 0.0F;
        float PointerY = 0.0F;
        RuntimeUiPointerButton Button = RuntimeUiPointerButton::Primary;
    };

    enum class RuntimeUiEventPhase : std::uint8_t
    {
        TrickleDown,
        Target,
        BubbleUp
    };

    struct RuntimeUiEventRouteEntry
    {
        std::uint64_t Sequence = 0;
        RuntimeUiEventType Type = RuntimeUiEventType::Click;
        RuntimeUiEventPhase Phase = RuntimeUiEventPhase::Target;
        RuntimeUiElementId Target;
        RuntimeUiElementId CurrentTarget;
        float PointerX = 0.0F;
        float PointerY = 0.0F;
        RuntimeUiPointerButton Button = RuntimeUiPointerButton::Primary;
    };

    enum class RuntimeUiDrawType : std::uint8_t
    {
        Quad,
        Image,
        Text,
        PushClip,
        PopClip
    };

    struct RuntimeUiDrawCommand
    {
        RuntimeUiDrawType Type = RuntimeUiDrawType::Quad;
        RuntimeUiElementId Element;
        RuntimeUiRect Rect;
        RuntimeUiRect ClipRect;
        Color ColorValue{1.0F, 1.0F, 1.0F, 1.0F};
        Color BorderColor{0.0F, 0.0F, 0.0F, 0.0F};
        RuntimeUiGradient BackgroundGradient;
        AssetId Asset;
        AssetId RenderTexture;
        std::string Text;
        float FontSize = 16.0F;
        float CornerRadius = 0.0F;
        float BorderWidth = 0.0F;
        RuntimeUiAlignment HorizontalAlignment = RuntimeUiAlignment::Start;
        RuntimeUiAlignment VerticalAlignment = RuntimeUiAlignment::Start;
    };

    struct RuntimeUiStatistics
    {
        std::uint64_t Generation = 1;
        std::size_t Elements = 0;
        std::size_t VisibleElements = 0;
        std::size_t InteractableElements = 0;
        std::size_t DrawCommands = 0;
        std::size_t DrawBatches = 0;
        std::size_t ClippedElements = 0;
        std::size_t PendingEvents = 0;
        std::size_t DroppedEvents = 0;
        std::size_t DirtyElements = 0;
        std::uint64_t LayoutPasses = 0;
        std::uint64_t ReusedLayoutPasses = 0;
        std::uint64_t StylePasses = 0;
        std::uint64_t RepaintPasses = 0;
        float StyleMilliseconds = 0.0F;
        float LayoutMilliseconds = 0.0F;
        float RepaintMilliseconds = 0.0F;
        float Scale = 1.0F;
    };

    struct RuntimeUiRendererStatistics
    {
        std::size_t RenderedVertices = 0;
        std::size_t DrawBatches = 0;
        std::size_t GlyphAtlasEntries = 0;
        std::size_t GlyphAtlasBytes = 0;
        std::size_t ImageAtlasEntries = 0;
        std::size_t ImageAtlasBytes = 0;
        float RepaintCpuMilliseconds = 0.0F;
    };

    class KEIRE_API RuntimeUiTree final : public RefCounted
    {
      public:
        explicit RuntimeUiTree(std::size_t maximumElements = 16'384, std::size_t maximumEvents = 4'096);
        ~RuntimeUiTree() override;

        RuntimeUiTree(const RuntimeUiTree&) = delete;
        RuntimeUiTree& operator=(const RuntimeUiTree&) = delete;

        [[nodiscard]] RuntimeUiElementId Create(RuntimeUiElementType type,
                                                RuntimeUiElementId parent = RuntimeUiElementId{});
        [[nodiscard]] bool Destroy(RuntimeUiElementId element);
        void Clear();

        [[nodiscard]] bool Exists(RuntimeUiElementId element) const noexcept;
        [[nodiscard]] std::optional<RuntimeUiElementState> State(RuntimeUiElementId element) const;
        [[nodiscard]] bool SetType(RuntimeUiElementId element, RuntimeUiElementType type);
        [[nodiscard]] bool SetStyle(RuntimeUiElementId element, RuntimeUiStyle style);
        [[nodiscard]] bool SetContent(RuntimeUiElementId element, RuntimeUiContent content);
        [[nodiscard]] bool SetControl(RuntimeUiElementId element, RuntimeUiControlState control);
        [[nodiscard]] bool SetVisible(RuntimeUiElementId element, bool visible);
        [[nodiscard]] bool SetEnabled(RuntimeUiElementId element, bool enabled);
        [[nodiscard]] bool SetInteractable(RuntimeUiElementId element, bool interactable);
        [[nodiscard]] bool SetParent(RuntimeUiElementId element, RuntimeUiElementId parent);
        [[nodiscard]] bool SetRootCanvasSettings(RuntimeUiElementId root,
                                                 std::optional<RuntimeUiCanvasSettings> settings);
        [[nodiscard]] std::vector<RuntimeUiElementId> Children(RuntimeUiElementId element) const;
        [[nodiscard]] bool AdvanceTransitions(float deltaSeconds);

        void Layout(float viewportWidth, float viewportHeight, RuntimeUiInsets safeArea = {},
                    RuntimeUiCanvasSettings settings = {});
        [[nodiscard]] std::span<const RuntimeUiDrawCommand> DrawCommands() const noexcept;
        [[nodiscard]] std::optional<RuntimeUiElementId> HitTest(float x, float y) const noexcept;
        [[nodiscard]] std::optional<RuntimeUiElementId> HitTestWithin(RuntimeUiElementId root, float x,
                                                                      float y) const noexcept;

        void PointerMove(float x, float y);
        void PointerMoveTo(RuntimeUiElementId target, float x, float y);
        void PointerLeave();
        bool PointerButton(float x, float y, RuntimeUiPointerButton button, bool pressed);
        bool PointerButtonTo(RuntimeUiElementId target, float x, float y, RuntimeUiPointerButton button, bool pressed);
        bool CancelPointerButton(RuntimeUiPointerButton button) noexcept;
        void Navigate(RuntimeUiNavigation navigation);
        [[nodiscard]] bool SetFocus(RuntimeUiElementId element);
        [[nodiscard]] RuntimeUiElementId Focus() const noexcept;
        [[nodiscard]] bool DispatchEvent(RuntimeUiEvent event);
        [[nodiscard]] bool PollEvent(RuntimeUiEvent& event);
        [[nodiscard]] std::vector<RuntimeUiEvent> PendingEvents() const;
        void ReplacePendingEvents(std::span<const RuntimeUiEvent> events);
        [[nodiscard]] RuntimeUiDirtyReason DirtyReasons(RuntimeUiElementId element) const noexcept;
        [[nodiscard]] std::vector<RuntimeUiEventRouteEntry> EventRouteHistory() const;
        void ReportStylePass(float milliseconds);
        void ReportRepaintPass(float milliseconds);

        [[nodiscard]] RuntimeUiStatistics Statistics() const noexcept;

      private:
        class Impl;
        std::unique_ptr<Impl> m_Impl;
    };
} // namespace Keire
