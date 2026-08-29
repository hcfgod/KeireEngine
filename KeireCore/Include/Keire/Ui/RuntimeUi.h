#pragma once

#include "Keire/Api.h"
#include "Keire/Assets/Asset.h"
#include "Keire/Math/Math.h"
#include "Keire/Ref.h"

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
    };

    struct RuntimeUiInsets
    {
        float Left = 0.0F;
        float Top = 0.0F;
        float Right = 0.0F;
        float Bottom = 0.0F;
    };

    struct RuntimeUiCanvasSettings
    {
        float ReferenceWidth = 1920.0F;
        float ReferenceHeight = 1080.0F;
        RuntimeUiScaleMode ScaleMode = RuntimeUiScaleMode::ScaleWithViewport;
        float MatchWidthOrHeight = 0.5F;
        float AccessibilityScale = 1.0F;
        bool RespectSafeArea = true;
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
        float MinimumWidth = 0.0F;
        float MinimumHeight = 0.0F;
        float MaximumWidth = 100000.0F;
        float MaximumHeight = 100000.0F;
        float FlexGrow = 0.0F;
        float Gap = 0.0F;
        RuntimeUiInsets Margin;
        RuntimeUiInsets Padding;
        Color Background{0.0F, 0.0F, 0.0F, 0.0F};
        Color HoverBackground{0.0F, 0.0F, 0.0F, 0.0F};
        Color PressedBackground{0.0F, 0.0F, 0.0F, 0.0F};
        Color DisabledBackground{0.0F, 0.0F, 0.0F, 0.0F};
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
    };

    struct RuntimeUiContent
    {
        std::string Text;
        AssetId Image;
        AssetId Font;
        std::string AccessibilityLabel;
        std::string AccessibilityHint;
        std::uint8_t AccessibilityRole = 0;
    };

    struct RuntimeUiControlState
    {
        float Minimum = 0.0F;
        float Maximum = 1.0F;
        float Value = 0.0F;
        Vector2 ContentSize;
        bool Checked = false;
        bool Vertical = false;
        bool Reversed = false;
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
        AssetId Asset;
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
        float Scale = 1.0F;
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
        [[nodiscard]] std::vector<RuntimeUiElementId> Children(RuntimeUiElementId element) const;

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
        [[nodiscard]] bool PollEvent(RuntimeUiEvent& event);
        [[nodiscard]] std::vector<RuntimeUiEvent> PendingEvents() const;
        void ReplacePendingEvents(std::span<const RuntimeUiEvent> events);

        [[nodiscard]] RuntimeUiStatistics Statistics() const noexcept;

      private:
        class Impl;
        std::unique_ptr<Impl> m_Impl;
    };
} // namespace Keire
