#pragma once

#include "Keire/ECS/Component.h"

#include <array>
#include <string>

namespace Keire
{
    enum class CanvasScaleMode : std::uint8_t
    {
        ConstantPixels,
        ScaleWithViewport,
        ConstantPhysicalSize
    };

    enum class UiTextAlignment : std::uint8_t
    {
        UpperLeft,
        UpperCenter,
        UpperRight,
        MiddleLeft,
        MiddleCenter,
        MiddleRight,
        LowerLeft,
        LowerCenter,
        LowerRight
    };

    enum class UiImageType : std::uint8_t
    {
        Simple,
        Sliced,
        Tiled,
        Filled
    };

    enum class UiButtonTransition : std::uint8_t
    {
        None,
        ColorTint,
        SpriteSwap,
        Animation
    };

    enum class UiLayoutDirection : std::uint8_t
    {
        Horizontal,
        Vertical,
        Grid
    };

    enum class UiSliderDirection : std::uint8_t
    {
        LeftToRight,
        RightToLeft,
        BottomToTop,
        TopToBottom
    };

    enum class UiInputContentType : std::uint8_t
    {
        Standard,
        Integer,
        Decimal,
        Password
    };

    enum class UiAccessibilityRole : std::uint8_t
    {
        Automatic,
        Button,
        Slider,
        Toggle,
        TextBox,
        ScrollView,
        Text,
        Image
    };

    class KEIRE_API CanvasComponent final : public Component
    {
      public:
        CanvasComponent();

        [[nodiscard]] static constexpr ComponentTypeId StaticType() noexcept
        {
            return ComponentTypeId(AssetId(0x4b45495245554943ULL, 0x414e564153000001ULL));
        }

        [[nodiscard]] Vector2 ReferenceResolution() const noexcept { return m_ReferenceResolution; }
        [[nodiscard]] CanvasScaleMode ScaleMode() const noexcept { return m_ScaleMode; }
        [[nodiscard]] float MatchWidthOrHeight() const noexcept { return m_MatchWidthOrHeight; }
        [[nodiscard]] float AccessibilityScale() const noexcept { return m_AccessibilityScale; }
        [[nodiscard]] std::int32_t SortingOrder() const noexcept { return m_SortingOrder; }
        [[nodiscard]] bool RespectSafeArea() const noexcept { return m_RespectSafeArea; }
        [[nodiscard]] bool PixelPerfect() const noexcept { return m_PixelPerfect; }

        void SetReferenceResolution(Vector2 value);
        void SetScaleMode(CanvasScaleMode value);
        void SetMatchWidthOrHeight(float value);
        void SetAccessibilityScale(float value);
        void SetSortingOrder(std::int32_t value);
        void SetRespectSafeArea(bool value);
        void SetPixelPerfect(bool value);

      private:
        friend ComponentRegistration CreateCanvasComponentRegistration();
        Vector2 m_ReferenceResolution{1920.0F, 1080.0F};
        CanvasScaleMode m_ScaleMode = CanvasScaleMode::ScaleWithViewport;
        float m_MatchWidthOrHeight = 0.5F;
        float m_AccessibilityScale = 1.0F;
        std::int32_t m_SortingOrder = 0;
        bool m_RespectSafeArea = true;
        bool m_PixelPerfect = false;
    };

    class KEIRE_API RectTransformComponent final : public Component
    {
      public:
        RectTransformComponent();

        [[nodiscard]] static constexpr ComponentTypeId StaticType() noexcept
        {
            return ComponentTypeId(AssetId(0x4b45495245554952ULL, 0x4543545452410001ULL));
        }

        [[nodiscard]] Vector2 AnchorMinimum() const noexcept { return m_AnchorMinimum; }
        [[nodiscard]] Vector2 AnchorMaximum() const noexcept { return m_AnchorMaximum; }
        [[nodiscard]] Vector2 Pivot() const noexcept { return m_Pivot; }
        [[nodiscard]] Vector2 AnchoredPosition() const noexcept { return m_AnchoredPosition; }
        [[nodiscard]] Vector2 SizeDelta() const noexcept { return m_SizeDelta; }
        [[nodiscard]] Vector2 Scale() const noexcept { return m_Scale; }
        [[nodiscard]] float RotationDegrees() const noexcept { return m_RotationDegrees; }

        void SetAnchorMinimum(Vector2 value);
        void SetAnchorMaximum(Vector2 value);
        void SetPivot(Vector2 value);
        void SetAnchoredPosition(Vector2 value);
        void SetSizeDelta(Vector2 value);
        void SetScale(Vector2 value);
        void SetRotationDegrees(float value);

      private:
        friend ComponentRegistration CreateRectTransformComponentRegistration();
        Vector2 m_AnchorMinimum{0.5F, 0.5F};
        Vector2 m_AnchorMaximum{0.5F, 0.5F};
        Vector2 m_Pivot{0.5F, 0.5F};
        Vector2 m_AnchoredPosition;
        Vector2 m_SizeDelta{320.0F, 96.0F};
        Vector2 m_Scale{1.0F, 1.0F};
        float m_RotationDegrees = 0.0F;
    };

    class KEIRE_API UiTextComponent final : public Component
    {
      public:
        UiTextComponent();

        [[nodiscard]] static constexpr ComponentTypeId StaticType() noexcept
        {
            return ComponentTypeId(AssetId(0x4b45495245554954ULL, 0x4558540000000001ULL));
        }

        [[nodiscard]] const std::string& Text() const noexcept { return m_Text; }
        [[nodiscard]] AssetId Font() const noexcept { return m_Font; }
        [[nodiscard]] Color TextColor() const noexcept { return m_Color; }
        [[nodiscard]] float FontSize() const noexcept { return m_FontSize; }
        [[nodiscard]] UiTextAlignment Alignment() const noexcept { return m_Alignment; }
        [[nodiscard]] bool Wrap() const noexcept { return m_Wrap; }
        [[nodiscard]] bool RichText() const noexcept { return m_RichText; }
        [[nodiscard]] bool RaycastTarget() const noexcept { return m_RaycastTarget; }

        void SetText(std::string value);
        void SetFont(AssetId value);
        void SetTextColor(Color value);
        void SetFontSize(float value);
        void SetAlignment(UiTextAlignment value);
        void SetWrap(bool value);
        void SetRichText(bool value);
        void SetRaycastTarget(bool value);

      private:
        friend ComponentRegistration CreateUiTextComponentRegistration();
        std::string m_Text = "Text";
        AssetId m_Font;
        Color m_Color{0.94F, 0.97F, 1.0F, 1.0F};
        float m_FontSize = 24.0F;
        UiTextAlignment m_Alignment = UiTextAlignment::MiddleCenter;
        bool m_Wrap = true;
        bool m_RichText = true;
        bool m_RaycastTarget = false;
    };

    class KEIRE_API UiImageComponent final : public Component
    {
      public:
        UiImageComponent();

        [[nodiscard]] static constexpr ComponentTypeId StaticType() noexcept
        {
            return ComponentTypeId(AssetId(0x4b45495245554949ULL, 0x4d41474500000001ULL));
        }

        [[nodiscard]] AssetId Sprite() const noexcept { return m_Sprite; }
        [[nodiscard]] Color Tint() const noexcept { return m_Tint; }
        [[nodiscard]] UiImageType ImageType() const noexcept { return m_ImageType; }
        [[nodiscard]] float FillAmount() const noexcept { return m_FillAmount; }
        [[nodiscard]] float PixelsPerUnit() const noexcept { return m_PixelsPerUnit; }
        [[nodiscard]] bool PreserveAspect() const noexcept { return m_PreserveAspect; }
        [[nodiscard]] bool RaycastTarget() const noexcept { return m_RaycastTarget; }

        void SetSprite(AssetId value);
        void SetTint(Color value);
        void SetImageType(UiImageType value);
        void SetFillAmount(float value);
        void SetPixelsPerUnit(float value);
        void SetPreserveAspect(bool value);
        void SetRaycastTarget(bool value);

      private:
        friend ComponentRegistration CreateUiImageComponentRegistration();
        AssetId m_Sprite;
        Color m_Tint{0.08F, 0.12F, 0.18F, 0.96F};
        UiImageType m_ImageType = UiImageType::Sliced;
        float m_FillAmount = 1.0F;
        float m_PixelsPerUnit = 100.0F;
        bool m_PreserveAspect = false;
        bool m_RaycastTarget = true;
    };

    class KEIRE_API UiButtonComponent final : public Component
    {
      public:
        UiButtonComponent();

        [[nodiscard]] static constexpr ComponentTypeId StaticType() noexcept
        {
            return ComponentTypeId(AssetId(0x4b45495245554942ULL, 0x5554544f4e000001ULL));
        }

        [[nodiscard]] bool Interactable() const noexcept { return m_Interactable; }
        [[nodiscard]] UiButtonTransition Transition() const noexcept { return m_Transition; }
        [[nodiscard]] Color NormalColor() const noexcept { return m_NormalColor; }
        [[nodiscard]] Color HoverColor() const noexcept { return m_HoverColor; }
        [[nodiscard]] Color PressedColor() const noexcept { return m_PressedColor; }
        [[nodiscard]] Color DisabledColor() const noexcept { return m_DisabledColor; }
        [[nodiscard]] float TransitionDuration() const noexcept { return m_TransitionDuration; }
        [[nodiscard]] const std::string& Action() const noexcept { return m_Action; }

        void SetInteractable(bool value);
        void SetTransition(UiButtonTransition value);
        void SetNormalColor(Color value);
        void SetHoverColor(Color value);
        void SetPressedColor(Color value);
        void SetDisabledColor(Color value);
        void SetTransitionDuration(float value);
        void SetAction(std::string value);

      private:
        friend ComponentRegistration CreateUiButtonComponentRegistration();
        bool m_Interactable = true;
        UiButtonTransition m_Transition = UiButtonTransition::ColorTint;
        Color m_NormalColor{0.08F, 0.45F, 0.72F, 1.0F};
        Color m_HoverColor{0.12F, 0.60F, 0.92F, 1.0F};
        Color m_PressedColor{0.05F, 0.32F, 0.56F, 1.0F};
        Color m_DisabledColor{0.20F, 0.23F, 0.28F, 0.55F};
        float m_TransitionDuration = 0.12F;
        std::string m_Action;
    };

    class KEIRE_API UiLayoutComponent final : public Component
    {
      public:
        UiLayoutComponent();

        [[nodiscard]] static constexpr ComponentTypeId StaticType() noexcept
        {
            return ComponentTypeId(AssetId(0x4b4549524555494cULL, 0x41594f5554000001ULL));
        }

        [[nodiscard]] UiLayoutDirection Direction() const noexcept { return m_Direction; }
        [[nodiscard]] Vector4 Padding() const noexcept { return m_Padding; }
        [[nodiscard]] Vector2 CellSize() const noexcept { return m_CellSize; }
        [[nodiscard]] float Spacing() const noexcept { return m_Spacing; }
        [[nodiscard]] std::int32_t Alignment() const noexcept { return m_Alignment; }
        [[nodiscard]] bool ControlChildWidth() const noexcept { return m_ControlChildWidth; }
        [[nodiscard]] bool ControlChildHeight() const noexcept { return m_ControlChildHeight; }
        [[nodiscard]] bool ForceExpandWidth() const noexcept { return m_ForceExpandWidth; }
        [[nodiscard]] bool ForceExpandHeight() const noexcept { return m_ForceExpandHeight; }

        void SetDirection(UiLayoutDirection value);
        void SetPadding(Vector4 value);
        void SetCellSize(Vector2 value);
        void SetSpacing(float value);
        void SetAlignment(std::int32_t value);
        void SetControlChildWidth(bool value);
        void SetControlChildHeight(bool value);
        void SetForceExpandWidth(bool value);
        void SetForceExpandHeight(bool value);

      private:
        friend ComponentRegistration CreateUiLayoutComponentRegistration();
        UiLayoutDirection m_Direction = UiLayoutDirection::Vertical;
        Vector4 m_Padding{24.0F, 24.0F, 24.0F, 24.0F};
        Vector2 m_CellSize{160.0F, 48.0F};
        float m_Spacing = 12.0F;
        std::int32_t m_Alignment = 4;
        bool m_ControlChildWidth = true;
        bool m_ControlChildHeight = false;
        bool m_ForceExpandWidth = true;
        bool m_ForceExpandHeight = false;
    };

    class KEIRE_API UiSliderComponent final : public Component
    {
      public:
        UiSliderComponent();

        [[nodiscard]] static constexpr ComponentTypeId StaticType() noexcept
        {
            return ComponentTypeId(AssetId(0x4b45495245554953ULL, 0x4c49444552000001ULL));
        }

        [[nodiscard]] float Minimum() const noexcept { return m_Minimum; }
        [[nodiscard]] float Maximum() const noexcept { return m_Maximum; }
        [[nodiscard]] float Value() const noexcept { return m_Value; }
        [[nodiscard]] float Step() const noexcept { return m_Step; }
        [[nodiscard]] UiSliderDirection Direction() const noexcept { return m_Direction; }
        [[nodiscard]] bool WholeNumbers() const noexcept { return m_WholeNumbers; }
        [[nodiscard]] bool Interactable() const noexcept { return m_Interactable; }

        void SetRange(float minimum, float maximum);
        void SetValue(float value);
        void SetStep(float value);
        void SetDirection(UiSliderDirection value);
        void SetWholeNumbers(bool value);
        void SetInteractable(bool value);

      private:
        friend ComponentRegistration CreateUiSliderComponentRegistration();
        float m_Minimum = 0.0F;
        float m_Maximum = 1.0F;
        float m_Value = 0.5F;
        float m_Step = 0.0F;
        UiSliderDirection m_Direction = UiSliderDirection::LeftToRight;
        bool m_WholeNumbers = false;
        bool m_Interactable = true;
    };

    class KEIRE_API UiToggleComponent final : public Component
    {
      public:
        UiToggleComponent();

        [[nodiscard]] static constexpr ComponentTypeId StaticType() noexcept
        {
            return ComponentTypeId(AssetId(0x4b45495245554954ULL, 0x4f47474c45000001ULL));
        }

        [[nodiscard]] bool IsOn() const noexcept { return m_IsOn; }
        [[nodiscard]] bool Interactable() const noexcept { return m_Interactable; }
        [[nodiscard]] Color OnColor() const noexcept { return m_OnColor; }
        [[nodiscard]] Color OffColor() const noexcept { return m_OffColor; }

        void SetIsOn(bool value);
        void SetInteractable(bool value);
        void SetOnColor(Color value);
        void SetOffColor(Color value);

      private:
        friend ComponentRegistration CreateUiToggleComponentRegistration();
        bool m_IsOn = false;
        bool m_Interactable = true;
        Color m_OnColor{0.08F, 0.72F, 0.55F, 1.0F};
        Color m_OffColor{0.16F, 0.19F, 0.24F, 1.0F};
    };

    class KEIRE_API UiInputFieldComponent final : public Component
    {
      public:
        UiInputFieldComponent();

        [[nodiscard]] static constexpr ComponentTypeId StaticType() noexcept
        {
            return ComponentTypeId(AssetId(0x4b45495245554949ULL, 0x4e505554464c4401ULL));
        }

        [[nodiscard]] const std::string& Text() const noexcept { return m_Text; }
        [[nodiscard]] const std::string& Placeholder() const noexcept { return m_Placeholder; }
        [[nodiscard]] std::uint32_t CharacterLimit() const noexcept { return m_CharacterLimit; }
        [[nodiscard]] UiInputContentType ContentType() const noexcept { return m_ContentType; }
        [[nodiscard]] bool Multiline() const noexcept { return m_Multiline; }
        [[nodiscard]] bool Interactable() const noexcept { return m_Interactable; }

        void SetText(std::string value);
        void SetPlaceholder(std::string value);
        void SetCharacterLimit(std::uint32_t value);
        void SetContentType(UiInputContentType value);
        void SetMultiline(bool value);
        void SetInteractable(bool value);

      private:
        friend ComponentRegistration CreateUiInputFieldComponentRegistration();
        std::string m_Text;
        std::string m_Placeholder = "Enter text";
        std::uint32_t m_CharacterLimit = 256;
        UiInputContentType m_ContentType = UiInputContentType::Standard;
        bool m_Multiline = false;
        bool m_Interactable = true;
    };

    class KEIRE_API UiScrollViewComponent final : public Component
    {
      public:
        UiScrollViewComponent();

        [[nodiscard]] static constexpr ComponentTypeId StaticType() noexcept
        {
            return ComponentTypeId(AssetId(0x4b45495245554953ULL, 0x43524f4c4c000001ULL));
        }

        [[nodiscard]] Vector2 ContentSize() const noexcept { return m_ContentSize; }
        [[nodiscard]] Vector2 Offset() const noexcept { return m_Offset; }
        [[nodiscard]] float Sensitivity() const noexcept { return m_Sensitivity; }
        [[nodiscard]] bool Horizontal() const noexcept { return m_Horizontal; }
        [[nodiscard]] bool Vertical() const noexcept { return m_Vertical; }
        [[nodiscard]] bool Interactable() const noexcept { return m_Interactable; }

        void SetContentSize(Vector2 value);
        void SetOffset(Vector2 value);
        void SetSensitivity(float value);
        void SetHorizontal(bool value);
        void SetVertical(bool value);
        void SetInteractable(bool value);

      private:
        friend ComponentRegistration CreateUiScrollViewComponentRegistration();
        Vector2 m_ContentSize{1920.0F, 1080.0F};
        Vector2 m_Offset;
        float m_Sensitivity = 48.0F;
        bool m_Horizontal = false;
        bool m_Vertical = true;
        bool m_Interactable = true;
    };

    class KEIRE_API UiAccessibilityComponent final : public Component
    {
      public:
        UiAccessibilityComponent();

        [[nodiscard]] static constexpr ComponentTypeId StaticType() noexcept
        {
            return ComponentTypeId(AssetId(0x4b45495245554941ULL, 0x4343455353000001ULL));
        }

        [[nodiscard]] const std::string& Label() const noexcept { return m_Label; }
        [[nodiscard]] const std::string& Hint() const noexcept { return m_Hint; }
        [[nodiscard]] UiAccessibilityRole Role() const noexcept { return m_Role; }
        [[nodiscard]] std::int32_t NavigationOrder() const noexcept { return m_NavigationOrder; }

        void SetLabel(std::string value);
        void SetHint(std::string value);
        void SetRole(UiAccessibilityRole value);
        void SetNavigationOrder(std::int32_t value);

      private:
        friend ComponentRegistration CreateUiAccessibilityComponentRegistration();
        std::string m_Label;
        std::string m_Hint;
        UiAccessibilityRole m_Role = UiAccessibilityRole::Automatic;
        std::int32_t m_NavigationOrder = 0;
    };

    [[nodiscard]] KEIRE_API ComponentRegistration CreateCanvasComponentRegistration();
    [[nodiscard]] KEIRE_API ComponentRegistration CreateRectTransformComponentRegistration();
    [[nodiscard]] KEIRE_API ComponentRegistration CreateUiTextComponentRegistration();
    [[nodiscard]] KEIRE_API ComponentRegistration CreateUiImageComponentRegistration();
    [[nodiscard]] KEIRE_API ComponentRegistration CreateUiButtonComponentRegistration();
    [[nodiscard]] KEIRE_API ComponentRegistration CreateUiLayoutComponentRegistration();
    [[nodiscard]] KEIRE_API ComponentRegistration CreateUiSliderComponentRegistration();
    [[nodiscard]] KEIRE_API ComponentRegistration CreateUiToggleComponentRegistration();
    [[nodiscard]] KEIRE_API ComponentRegistration CreateUiInputFieldComponentRegistration();
    [[nodiscard]] KEIRE_API ComponentRegistration CreateUiScrollViewComponentRegistration();
    [[nodiscard]] KEIRE_API ComponentRegistration CreateUiAccessibilityComponentRegistration();
} // namespace Keire
