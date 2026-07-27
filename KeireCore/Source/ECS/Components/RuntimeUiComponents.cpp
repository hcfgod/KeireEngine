#include "Keire/ECS/Components/RuntimeUiComponents.h"

#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace Keire
{
    namespace
    {
        template <typename T>
        [[nodiscard]] T ReadUiProperty(const ComponentPropertyBag& values, const std::string_view key,
                                       const T& fallback)
        {
            const auto found = values.find(key);
            if (found == values.end())
                return fallback;
            if (const auto* value = std::get_if<T>(&found->second))
                return *value;
            throw std::invalid_argument("Runtime UI component property has an incompatible type.");
        }

        [[nodiscard]] bool Finite(const Vector2 value) noexcept
        {
            return std::isfinite(value.X) && std::isfinite(value.Y);
        }

        [[nodiscard]] bool Finite(const Vector4 value) noexcept
        {
            return std::isfinite(value.X) && std::isfinite(value.Y) && std::isfinite(value.Z) && std::isfinite(value.W);
        }

        [[nodiscard]] bool Finite(const Color value) noexcept
        {
            return std::isfinite(value.Red) && std::isfinite(value.Green) && std::isfinite(value.Blue) &&
                   std::isfinite(value.Alpha);
        }

        template <typename Enum>
        [[nodiscard]] Enum ReadEnum(const ComponentPropertyBag& values, const std::string_view key, const Enum fallback,
                                    const std::int64_t maximum)
        {
            const auto raw = ReadUiProperty(values, key, static_cast<std::int64_t>(fallback));
            if (raw < 0 || raw > maximum)
                throw std::invalid_argument("Runtime UI enum property is outside the supported range.");
            return static_cast<Enum>(raw);
        }
    } // namespace

    CanvasComponent::CanvasComponent() : Component(StaticType()) {}

    void CanvasComponent::SetReferenceResolution(const Vector2 value)
    {
        if (!Finite(value) || value.X <= 0.0F || value.Y <= 0.0F)
            throw std::invalid_argument("Canvas reference resolution must be finite and positive.");
        m_ReferenceResolution = value;
        NotifyChanged();
    }

    void CanvasComponent::SetScaleMode(const CanvasScaleMode value)
    {
        m_ScaleMode = value;
        NotifyChanged();
    }

    void CanvasComponent::SetMatchWidthOrHeight(const float value)
    {
        if (!std::isfinite(value) || value < 0.0F || value > 1.0F)
            throw std::invalid_argument("Canvas match value must be between zero and one.");
        m_MatchWidthOrHeight = value;
        NotifyChanged();
    }

    void CanvasComponent::SetAccessibilityScale(const float value)
    {
        if (!std::isfinite(value) || value < 0.5F || value > 3.0F)
            throw std::invalid_argument("Canvas accessibility scale must be between 0.5 and 3.");
        m_AccessibilityScale = value;
        NotifyChanged();
    }

    void CanvasComponent::SetSortingOrder(const std::int32_t value)
    {
        m_SortingOrder = value;
        NotifyChanged();
    }

    void CanvasComponent::SetRespectSafeArea(const bool value)
    {
        m_RespectSafeArea = value;
        NotifyChanged();
    }

    void CanvasComponent::SetPixelPerfect(const bool value)
    {
        m_PixelPerfect = value;
        NotifyChanged();
    }

    RectTransformComponent::RectTransformComponent() : Component(StaticType()) {}

    void RectTransformComponent::SetAnchorMinimum(const Vector2 value)
    {
        if (!Finite(value) || value.X < 0.0F || value.X > 1.0F || value.Y < 0.0F || value.Y > 1.0F)
            throw std::invalid_argument("Rect Transform minimum anchor must be normalized.");
        m_AnchorMinimum = value;
        NotifyChanged();
    }

    void RectTransformComponent::SetAnchorMaximum(const Vector2 value)
    {
        if (!Finite(value) || value.X < 0.0F || value.X > 1.0F || value.Y < 0.0F || value.Y > 1.0F)
            throw std::invalid_argument("Rect Transform maximum anchor must be normalized.");
        m_AnchorMaximum = value;
        NotifyChanged();
    }

    void RectTransformComponent::SetPivot(const Vector2 value)
    {
        if (!Finite(value) || value.X < 0.0F || value.X > 1.0F || value.Y < 0.0F || value.Y > 1.0F)
            throw std::invalid_argument("Rect Transform pivot must be normalized.");
        m_Pivot = value;
        NotifyChanged();
    }

    void RectTransformComponent::SetAnchoredPosition(const Vector2 value)
    {
        if (!Finite(value))
            throw std::invalid_argument("Rect Transform anchored position must be finite.");
        m_AnchoredPosition = value;
        NotifyChanged();
    }

    void RectTransformComponent::SetSizeDelta(const Vector2 value)
    {
        if (!Finite(value))
            throw std::invalid_argument("Rect Transform size delta must be finite.");
        m_SizeDelta = value;
        NotifyChanged();
    }

    void RectTransformComponent::SetScale(const Vector2 value)
    {
        if (!Finite(value) || value.X <= 0.0F || value.Y <= 0.0F)
            throw std::invalid_argument("Rect Transform scale must be finite and positive.");
        m_Scale = value;
        NotifyChanged();
    }

    void RectTransformComponent::SetRotationDegrees(const float value)
    {
        if (!std::isfinite(value))
            throw std::invalid_argument("Rect Transform rotation must be finite.");
        m_RotationDegrees = value;
        NotifyChanged();
    }

    UiTextComponent::UiTextComponent() : Component(StaticType()) {}

    void UiTextComponent::SetText(std::string value)
    {
        if (value.size() > 1'048'576)
            throw std::length_error("UI text exceeds the one MiB component limit.");
        m_Text = std::move(value);
        NotifyChanged();
    }

    void UiTextComponent::SetFont(const AssetId value)
    {
        m_Font = value;
        NotifyChanged();
    }

    void UiTextComponent::SetTextColor(const Color value)
    {
        if (!Finite(value))
            throw std::invalid_argument("UI text color must be finite.");
        m_Color = value;
        NotifyChanged();
    }

    void UiTextComponent::SetFontSize(const float value)
    {
        if (!std::isfinite(value) || value < 1.0F || value > 512.0F)
            throw std::invalid_argument("UI font size must be between 1 and 512.");
        m_FontSize = value;
        NotifyChanged();
    }

    void UiTextComponent::SetAlignment(const UiTextAlignment value)
    {
        m_Alignment = value;
        NotifyChanged();
    }

    void UiTextComponent::SetWrap(const bool value)
    {
        m_Wrap = value;
        NotifyChanged();
    }

    void UiTextComponent::SetRichText(const bool value)
    {
        m_RichText = value;
        NotifyChanged();
    }

    void UiTextComponent::SetRaycastTarget(const bool value)
    {
        m_RaycastTarget = value;
        NotifyChanged();
    }

    UiImageComponent::UiImageComponent() : Component(StaticType()) {}

    void UiImageComponent::SetSprite(const AssetId value)
    {
        m_Sprite = value;
        NotifyChanged();
    }

    void UiImageComponent::SetTint(const Color value)
    {
        if (!Finite(value))
            throw std::invalid_argument("UI image tint must be finite.");
        m_Tint = value;
        NotifyChanged();
    }

    void UiImageComponent::SetImageType(const UiImageType value)
    {
        m_ImageType = value;
        NotifyChanged();
    }

    void UiImageComponent::SetFillAmount(const float value)
    {
        if (!std::isfinite(value) || value < 0.0F || value > 1.0F)
            throw std::invalid_argument("UI image fill amount must be between zero and one.");
        m_FillAmount = value;
        NotifyChanged();
    }

    void UiImageComponent::SetPixelsPerUnit(const float value)
    {
        if (!std::isfinite(value) || value <= 0.0F || value > 100'000.0F)
            throw std::invalid_argument("UI image pixels per unit must be finite and positive.");
        m_PixelsPerUnit = value;
        NotifyChanged();
    }

    void UiImageComponent::SetPreserveAspect(const bool value)
    {
        m_PreserveAspect = value;
        NotifyChanged();
    }

    void UiImageComponent::SetRaycastTarget(const bool value)
    {
        m_RaycastTarget = value;
        NotifyChanged();
    }

    UiButtonComponent::UiButtonComponent() : Component(StaticType()) {}

    void UiButtonComponent::SetInteractable(const bool value)
    {
        m_Interactable = value;
        NotifyChanged();
    }

    void UiButtonComponent::SetTransition(const UiButtonTransition value)
    {
        m_Transition = value;
        NotifyChanged();
    }

    void UiButtonComponent::SetNormalColor(const Color value)
    {
        if (!Finite(value))
            throw std::invalid_argument("UI button normal color must be finite.");
        m_NormalColor = value;
        NotifyChanged();
    }

    void UiButtonComponent::SetHoverColor(const Color value)
    {
        if (!Finite(value))
            throw std::invalid_argument("UI button hover color must be finite.");
        m_HoverColor = value;
        NotifyChanged();
    }

    void UiButtonComponent::SetPressedColor(const Color value)
    {
        if (!Finite(value))
            throw std::invalid_argument("UI button pressed color must be finite.");
        m_PressedColor = value;
        NotifyChanged();
    }

    void UiButtonComponent::SetDisabledColor(const Color value)
    {
        if (!Finite(value))
            throw std::invalid_argument("UI button disabled color must be finite.");
        m_DisabledColor = value;
        NotifyChanged();
    }

    void UiButtonComponent::SetTransitionDuration(const float value)
    {
        if (!std::isfinite(value) || value < 0.0F || value > 10.0F)
            throw std::invalid_argument("UI button transition duration must be between zero and ten seconds.");
        m_TransitionDuration = value;
        NotifyChanged();
    }

    void UiButtonComponent::SetAction(std::string value)
    {
        if (value.size() > 256)
            throw std::length_error("UI button action name exceeds 256 bytes.");
        m_Action = std::move(value);
        NotifyChanged();
    }

    UiLayoutComponent::UiLayoutComponent() : Component(StaticType()) {}

    void UiLayoutComponent::SetDirection(const UiLayoutDirection value)
    {
        m_Direction = value;
        NotifyChanged();
    }

    void UiLayoutComponent::SetPadding(const Vector4 value)
    {
        if (!Finite(value) || value.X < 0.0F || value.Y < 0.0F || value.Z < 0.0F || value.W < 0.0F)
            throw std::invalid_argument("UI layout padding must be finite and non-negative.");
        m_Padding = value;
        NotifyChanged();
    }

    void UiLayoutComponent::SetCellSize(const Vector2 value)
    {
        if (!Finite(value) || value.X < 0.0F || value.Y < 0.0F)
            throw std::invalid_argument("UI layout cell size must be finite and non-negative.");
        m_CellSize = value;
        NotifyChanged();
    }

    void UiLayoutComponent::SetSpacing(const float value)
    {
        if (!std::isfinite(value) || value < 0.0F || value > 100'000.0F)
            throw std::invalid_argument("UI layout spacing must be finite and non-negative.");
        m_Spacing = value;
        NotifyChanged();
    }

    void UiLayoutComponent::SetAlignment(const std::int32_t value)
    {
        if (value < 0 || value > 8)
            throw std::invalid_argument("UI layout alignment is outside the supported range.");
        m_Alignment = value;
        NotifyChanged();
    }

    void UiLayoutComponent::SetControlChildWidth(const bool value)
    {
        m_ControlChildWidth = value;
        NotifyChanged();
    }

    void UiLayoutComponent::SetControlChildHeight(const bool value)
    {
        m_ControlChildHeight = value;
        NotifyChanged();
    }

    void UiLayoutComponent::SetForceExpandWidth(const bool value)
    {
        m_ForceExpandWidth = value;
        NotifyChanged();
    }

    void UiLayoutComponent::SetForceExpandHeight(const bool value)
    {
        m_ForceExpandHeight = value;
        NotifyChanged();
    }

    ComponentRegistration CreateCanvasComponentRegistration()
    {
        ComponentRegistration result;
        result.Type = CanvasComponent::StaticType();
        result.Name = "Canvas";
        result.Category = "UI";
        result.Properties = {
            {"referenceResolution", "Reference Resolution", "Canvas", ComponentPropertyKind::Vector2},
            {"scaleMode", "Scale Mode", "Canvas", ComponentPropertyKind::Integer, false, 0.0, 2.0, 1.0},
            {"match", "Match Width Or Height", "Canvas", ComponentPropertyKind::Scalar, false, 0.0, 1.0, 0.01},
            {"accessibilityScale", "Accessibility Scale", "Accessibility", ComponentPropertyKind::Scalar, false, 0.5,
             3.0, 0.05},
            {"sortingOrder", "Sorting Order", "Canvas", ComponentPropertyKind::Integer},
            {"respectSafeArea", "Respect Safe Area", "Canvas", ComponentPropertyKind::Boolean},
            {"pixelPerfect", "Pixel Perfect", "Canvas", ComponentPropertyKind::Boolean},
        };
        result.Factory = [] { return Ref<Component>(CreateRef<CanvasComponent>()); };
        result.Serialize = [](const Component& component)
        {
            const auto& canvas = dynamic_cast<const CanvasComponent&>(component);
            return ComponentPropertyBag{
                {"referenceResolution", canvas.m_ReferenceResolution},
                {"scaleMode", static_cast<std::int64_t>(canvas.m_ScaleMode)},
                {"match", static_cast<double>(canvas.m_MatchWidthOrHeight)},
                {"accessibilityScale", static_cast<double>(canvas.m_AccessibilityScale)},
                {"sortingOrder", static_cast<std::int64_t>(canvas.m_SortingOrder)},
                {"respectSafeArea", canvas.m_RespectSafeArea},
                {"pixelPerfect", canvas.m_PixelPerfect},
            };
        };
        result.Deserialize = [](Component& component, const ComponentPropertyBag& values, const std::uint32_t version)
        {
            if (version != 1)
                throw std::invalid_argument("Unsupported Canvas component schema version.");
            auto& canvas = dynamic_cast<CanvasComponent&>(component);
            canvas.SetReferenceResolution(ReadUiProperty(values, "referenceResolution", Vector2{1920.0F, 1080.0F}));
            canvas.SetScaleMode(ReadEnum(values, "scaleMode", CanvasScaleMode::ScaleWithViewport, 2));
            canvas.SetMatchWidthOrHeight(static_cast<float>(ReadUiProperty(values, "match", 0.5)));
            canvas.SetAccessibilityScale(static_cast<float>(ReadUiProperty(values, "accessibilityScale", 1.0)));
            const auto sorting = ReadUiProperty(values, "sortingOrder", std::int64_t{0});
            if (sorting < std::numeric_limits<std::int32_t>::min() ||
                sorting > std::numeric_limits<std::int32_t>::max())
                throw std::invalid_argument("Canvas sorting order is outside the supported range.");
            canvas.SetSortingOrder(static_cast<std::int32_t>(sorting));
            canvas.SetRespectSafeArea(ReadUiProperty(values, "respectSafeArea", true));
            canvas.SetPixelPerfect(ReadUiProperty(values, "pixelPerfect", false));
        };
        return result;
    }

    ComponentRegistration CreateRectTransformComponentRegistration()
    {
        ComponentRegistration result;
        result.Type = RectTransformComponent::StaticType();
        result.Name = "Rect Transform";
        result.Category = "UI";
        result.Properties = {
            {"anchorMinimum", "Anchor Minimum", "Anchors", ComponentPropertyKind::Vector2},
            {"anchorMaximum", "Anchor Maximum", "Anchors", ComponentPropertyKind::Vector2},
            {"pivot", "Pivot", "Anchors", ComponentPropertyKind::Vector2},
            {"anchoredPosition", "Position", "Rect", ComponentPropertyKind::Vector2},
            {"sizeDelta", "Size Delta", "Rect", ComponentPropertyKind::Vector2},
            {"rotation", "Rotation", "Rect", ComponentPropertyKind::Scalar, false, -360.0, 360.0, 0.1},
            {"scale", "Scale", "Rect", ComponentPropertyKind::Vector2},
        };
        result.Factory = [] { return Ref<Component>(CreateRef<RectTransformComponent>()); };
        result.Serialize = [](const Component& component)
        {
            const auto& rect = dynamic_cast<const RectTransformComponent&>(component);
            return ComponentPropertyBag{
                {"anchorMinimum", rect.m_AnchorMinimum},
                {"anchorMaximum", rect.m_AnchorMaximum},
                {"pivot", rect.m_Pivot},
                {"anchoredPosition", rect.m_AnchoredPosition},
                {"sizeDelta", rect.m_SizeDelta},
                {"rotation", static_cast<double>(rect.m_RotationDegrees)},
                {"scale", rect.m_Scale},
            };
        };
        result.Deserialize = [](Component& component, const ComponentPropertyBag& values, const std::uint32_t version)
        {
            if (version != 1)
                throw std::invalid_argument("Unsupported Rect Transform component schema version.");
            auto& rect = dynamic_cast<RectTransformComponent&>(component);
            rect.SetAnchorMinimum(ReadUiProperty(values, "anchorMinimum", Vector2{0.5F, 0.5F}));
            rect.SetAnchorMaximum(ReadUiProperty(values, "anchorMaximum", Vector2{0.5F, 0.5F}));
            rect.SetPivot(ReadUiProperty(values, "pivot", Vector2{0.5F, 0.5F}));
            rect.SetAnchoredPosition(ReadUiProperty(values, "anchoredPosition", Vector2{}));
            rect.SetSizeDelta(ReadUiProperty(values, "sizeDelta", Vector2{320.0F, 96.0F}));
            rect.SetRotationDegrees(static_cast<float>(ReadUiProperty(values, "rotation", 0.0)));
            rect.SetScale(ReadUiProperty(values, "scale", Vector2{1.0F, 1.0F}));
        };
        return result;
    }

    ComponentRegistration CreateUiTextComponentRegistration()
    {
        ComponentRegistration result;
        result.Type = UiTextComponent::StaticType();
        result.Name = "UI Text";
        result.Category = "UI";
        result.RequiredComponents = {RectTransformComponent::StaticType()};
        result.Properties = {
            {"text", "Text", "Content", ComponentPropertyKind::Text},
            {"font", "Font", "Typography", ComponentPropertyKind::Asset},
            {"color", "Color", "Typography", ComponentPropertyKind::Color},
            {"fontSize", "Font Size", "Typography", ComponentPropertyKind::Scalar, false, 1.0, 512.0, 1.0},
            {"alignment", "Alignment", "Typography", ComponentPropertyKind::Integer, false, 0.0, 8.0, 1.0},
            {"wrap", "Wrap", "Typography", ComponentPropertyKind::Boolean},
            {"richText", "Rich Text", "Typography", ComponentPropertyKind::Boolean},
            {"raycastTarget", "Raycast Target", "Interaction", ComponentPropertyKind::Boolean},
        };
        result.Factory = [] { return Ref<Component>(CreateRef<UiTextComponent>()); };
        result.Serialize = [](const Component& component)
        {
            const auto& text = dynamic_cast<const UiTextComponent&>(component);
            return ComponentPropertyBag{
                {"text", text.m_Text},
                {"font", text.m_Font},
                {"color", text.m_Color},
                {"fontSize", static_cast<double>(text.m_FontSize)},
                {"alignment", static_cast<std::int64_t>(text.m_Alignment)},
                {"wrap", text.m_Wrap},
                {"richText", text.m_RichText},
                {"raycastTarget", text.m_RaycastTarget},
            };
        };
        result.Deserialize = [](Component& component, const ComponentPropertyBag& values, const std::uint32_t version)
        {
            if (version != 1)
                throw std::invalid_argument("Unsupported UI Text component schema version.");
            auto& text = dynamic_cast<UiTextComponent&>(component);
            text.SetText(ReadUiProperty(values, "text", std::string{"Text"}));
            text.SetFont(ReadUiProperty(values, "font", AssetId{}));
            text.SetTextColor(ReadUiProperty(values, "color", Color{0.94F, 0.97F, 1.0F, 1.0F}));
            text.SetFontSize(static_cast<float>(ReadUiProperty(values, "fontSize", 24.0)));
            text.SetAlignment(ReadEnum(values, "alignment", UiTextAlignment::MiddleCenter, 8));
            text.SetWrap(ReadUiProperty(values, "wrap", true));
            text.SetRichText(ReadUiProperty(values, "richText", true));
            text.SetRaycastTarget(ReadUiProperty(values, "raycastTarget", false));
        };
        return result;
    }

    ComponentRegistration CreateUiImageComponentRegistration()
    {
        ComponentRegistration result;
        result.Type = UiImageComponent::StaticType();
        result.Name = "UI Image";
        result.Category = "UI";
        result.RequiredComponents = {RectTransformComponent::StaticType()};
        result.Properties = {
            {"sprite", "Sprite", "Image", ComponentPropertyKind::Asset},
            {"tint", "Tint", "Image", ComponentPropertyKind::Color},
            {"imageType", "Image Type", "Image", ComponentPropertyKind::Integer, false, 0.0, 3.0, 1.0},
            {"fillAmount", "Fill Amount", "Image", ComponentPropertyKind::Scalar, false, 0.0, 1.0, 0.01},
            {"pixelsPerUnit", "Pixels Per Unit", "Image", ComponentPropertyKind::Scalar, false, 0.01, 100000.0, 1.0},
            {"preserveAspect", "Preserve Aspect", "Image", ComponentPropertyKind::Boolean},
            {"raycastTarget", "Raycast Target", "Interaction", ComponentPropertyKind::Boolean},
        };
        result.Factory = [] { return Ref<Component>(CreateRef<UiImageComponent>()); };
        result.Serialize = [](const Component& component)
        {
            const auto& image = dynamic_cast<const UiImageComponent&>(component);
            return ComponentPropertyBag{
                {"sprite", image.m_Sprite},
                {"tint", image.m_Tint},
                {"imageType", static_cast<std::int64_t>(image.m_ImageType)},
                {"fillAmount", static_cast<double>(image.m_FillAmount)},
                {"pixelsPerUnit", static_cast<double>(image.m_PixelsPerUnit)},
                {"preserveAspect", image.m_PreserveAspect},
                {"raycastTarget", image.m_RaycastTarget},
            };
        };
        result.Deserialize = [](Component& component, const ComponentPropertyBag& values, const std::uint32_t version)
        {
            if (version != 1)
                throw std::invalid_argument("Unsupported UI Image component schema version.");
            auto& image = dynamic_cast<UiImageComponent&>(component);
            image.SetSprite(ReadUiProperty(values, "sprite", AssetId{}));
            image.SetTint(ReadUiProperty(values, "tint", Color{0.08F, 0.12F, 0.18F, 0.96F}));
            image.SetImageType(ReadEnum(values, "imageType", UiImageType::Sliced, 3));
            image.SetFillAmount(static_cast<float>(ReadUiProperty(values, "fillAmount", 1.0)));
            image.SetPixelsPerUnit(static_cast<float>(ReadUiProperty(values, "pixelsPerUnit", 100.0)));
            image.SetPreserveAspect(ReadUiProperty(values, "preserveAspect", false));
            image.SetRaycastTarget(ReadUiProperty(values, "raycastTarget", true));
        };
        return result;
    }

    ComponentRegistration CreateUiButtonComponentRegistration()
    {
        ComponentRegistration result;
        result.Type = UiButtonComponent::StaticType();
        result.Name = "UI Button";
        result.Category = "UI";
        result.RequiredComponents = {RectTransformComponent::StaticType()};
        result.Properties = {
            {"interactable", "Interactable", "Interaction", ComponentPropertyKind::Boolean},
            {"transition", "Transition", "Interaction", ComponentPropertyKind::Integer, false, 0.0, 3.0, 1.0},
            {"normalColor", "Normal", "Colors", ComponentPropertyKind::Color},
            {"hoverColor", "Hover", "Colors", ComponentPropertyKind::Color},
            {"pressedColor", "Pressed", "Colors", ComponentPropertyKind::Color},
            {"disabledColor", "Disabled", "Colors", ComponentPropertyKind::Color},
            {"transitionDuration", "Duration", "Colors", ComponentPropertyKind::Scalar, false, 0.0, 10.0, 0.01},
            {"action", "Action", "Events", ComponentPropertyKind::Text},
        };
        result.Factory = [] { return Ref<Component>(CreateRef<UiButtonComponent>()); };
        result.Serialize = [](const Component& component)
        {
            const auto& button = dynamic_cast<const UiButtonComponent&>(component);
            return ComponentPropertyBag{
                {"interactable", button.m_Interactable},
                {"transition", static_cast<std::int64_t>(button.m_Transition)},
                {"normalColor", button.m_NormalColor},
                {"hoverColor", button.m_HoverColor},
                {"pressedColor", button.m_PressedColor},
                {"disabledColor", button.m_DisabledColor},
                {"transitionDuration", static_cast<double>(button.m_TransitionDuration)},
                {"action", button.m_Action},
            };
        };
        result.Deserialize = [](Component& component, const ComponentPropertyBag& values, const std::uint32_t version)
        {
            if (version != 1)
                throw std::invalid_argument("Unsupported UI Button component schema version.");
            auto& button = dynamic_cast<UiButtonComponent&>(component);
            button.SetInteractable(ReadUiProperty(values, "interactable", true));
            button.SetTransition(ReadEnum(values, "transition", UiButtonTransition::ColorTint, 3));
            button.SetNormalColor(ReadUiProperty(values, "normalColor", Color{0.08F, 0.45F, 0.72F, 1.0F}));
            button.SetHoverColor(ReadUiProperty(values, "hoverColor", Color{0.12F, 0.60F, 0.92F, 1.0F}));
            button.SetPressedColor(ReadUiProperty(values, "pressedColor", Color{0.05F, 0.32F, 0.56F, 1.0F}));
            button.SetDisabledColor(ReadUiProperty(values, "disabledColor", Color{0.20F, 0.23F, 0.28F, 0.55F}));
            button.SetTransitionDuration(static_cast<float>(ReadUiProperty(values, "transitionDuration", 0.12)));
            button.SetAction(ReadUiProperty(values, "action", std::string{}));
        };
        return result;
    }

    ComponentRegistration CreateUiLayoutComponentRegistration()
    {
        ComponentRegistration result;
        result.Type = UiLayoutComponent::StaticType();
        result.Name = "UI Layout";
        result.Category = "UI";
        result.RequiredComponents = {RectTransformComponent::StaticType()};
        result.Properties = {
            {"direction", "Direction", "Layout", ComponentPropertyKind::Integer, false, 0.0, 2.0, 1.0},
            {"padding", "Padding L/T/R/B", "Layout", ComponentPropertyKind::Vector4},
            {"cellSize", "Cell Size", "Grid", ComponentPropertyKind::Vector2},
            {"spacing", "Spacing", "Layout", ComponentPropertyKind::Scalar, false, 0.0, 100000.0, 1.0},
            {"alignment", "Alignment", "Layout", ComponentPropertyKind::Integer, false, 0.0, 8.0, 1.0},
            {"controlChildWidth", "Control Child Width", "Children", ComponentPropertyKind::Boolean},
            {"controlChildHeight", "Control Child Height", "Children", ComponentPropertyKind::Boolean},
            {"forceExpandWidth", "Force Expand Width", "Children", ComponentPropertyKind::Boolean},
            {"forceExpandHeight", "Force Expand Height", "Children", ComponentPropertyKind::Boolean},
        };
        result.Factory = [] { return Ref<Component>(CreateRef<UiLayoutComponent>()); };
        result.Serialize = [](const Component& component)
        {
            const auto& layout = dynamic_cast<const UiLayoutComponent&>(component);
            return ComponentPropertyBag{
                {"direction", static_cast<std::int64_t>(layout.m_Direction)},
                {"padding", layout.m_Padding},
                {"cellSize", layout.m_CellSize},
                {"spacing", static_cast<double>(layout.m_Spacing)},
                {"alignment", static_cast<std::int64_t>(layout.m_Alignment)},
                {"controlChildWidth", layout.m_ControlChildWidth},
                {"controlChildHeight", layout.m_ControlChildHeight},
                {"forceExpandWidth", layout.m_ForceExpandWidth},
                {"forceExpandHeight", layout.m_ForceExpandHeight},
            };
        };
        result.Deserialize = [](Component& component, const ComponentPropertyBag& values, const std::uint32_t version)
        {
            if (version != 1)
                throw std::invalid_argument("Unsupported UI Layout component schema version.");
            auto& layout = dynamic_cast<UiLayoutComponent&>(component);
            layout.SetDirection(ReadEnum(values, "direction", UiLayoutDirection::Vertical, 2));
            layout.SetPadding(ReadUiProperty(values, "padding", Vector4{24.0F, 24.0F, 24.0F, 24.0F}));
            layout.SetCellSize(ReadUiProperty(values, "cellSize", Vector2{160.0F, 48.0F}));
            layout.SetSpacing(static_cast<float>(ReadUiProperty(values, "spacing", 12.0)));
            const auto alignment = ReadUiProperty(values, "alignment", std::int64_t{4});
            if (alignment < 0 || alignment > 8)
                throw std::invalid_argument("UI layout alignment is outside the supported range.");
            layout.SetAlignment(static_cast<std::int32_t>(alignment));
            layout.SetControlChildWidth(ReadUiProperty(values, "controlChildWidth", true));
            layout.SetControlChildHeight(ReadUiProperty(values, "controlChildHeight", false));
            layout.SetForceExpandWidth(ReadUiProperty(values, "forceExpandWidth", true));
            layout.SetForceExpandHeight(ReadUiProperty(values, "forceExpandHeight", false));
        };
        return result;
    }
} // namespace Keire
