#include "Keire/ECS/Components/RuntimeUiComponents.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace Keire
{
    namespace
    {
        template <typename T>
        [[nodiscard]] T ReadProperty(const ComponentPropertyBag& values, const std::string_view key, const T& fallback)
        {
            const auto found = values.find(key);
            if (found == values.end())
                return fallback;
            if (const auto* value = std::get_if<T>(&found->second))
                return *value;
            throw std::invalid_argument("Runtime UI control property has an incompatible type.");
        }

        template <typename Enum>
        [[nodiscard]] Enum ReadEnum(const ComponentPropertyBag& values, const std::string_view key, const Enum fallback,
                                    const std::int64_t maximum)
        {
            const auto raw = ReadProperty(values, key, static_cast<std::int64_t>(fallback));
            if (raw < 0 || raw > maximum)
                throw std::invalid_argument("Runtime UI control enum is outside the supported range.");
            return static_cast<Enum>(raw);
        }

        [[nodiscard]] bool Finite(const Color value) noexcept
        {
            return std::isfinite(value.Red) && std::isfinite(value.Green) && std::isfinite(value.Blue) &&
                   std::isfinite(value.Alpha);
        }

        [[nodiscard]] bool Finite(const Vector2 value) noexcept
        {
            return std::isfinite(value.X) && std::isfinite(value.Y);
        }

        [[nodiscard]] bool ValidInputText(const std::string_view value, const UiInputContentType type)
        {
            if (type == UiInputContentType::Standard || type == UiInputContentType::Password || value.empty())
                return true;
            bool decimal = false;
            for (std::size_t index = 0; index < value.size(); ++index)
            {
                const char character = value[index];
                if (character == '-' && index == 0)
                    continue;
                if (character == '.' && type == UiInputContentType::Decimal && !decimal)
                {
                    decimal = true;
                    continue;
                }
                if (character < '0' || character > '9')
                    return false;
            }
            return true;
        }
    } // namespace

    UiSliderComponent::UiSliderComponent() : Component(StaticType()) {}

    void UiSliderComponent::SetRange(const float minimum, const float maximum)
    {
        if (!std::isfinite(minimum) || !std::isfinite(maximum) || minimum >= maximum || m_Step > maximum - minimum)
            throw std::invalid_argument("UI Slider range must be finite and increasing.");
        m_Minimum = minimum;
        m_Maximum = maximum;
        m_Value = std::clamp(m_Value, minimum, maximum);
        NotifyChanged();
    }

    void UiSliderComponent::SetValue(const float value)
    {
        if (!std::isfinite(value))
            throw std::invalid_argument("UI Slider value must be finite.");
        auto candidate = std::clamp(value, m_Minimum, m_Maximum);
        if (m_WholeNumbers)
            candidate = std::round(candidate);
        if (m_Step > 0.0F)
            candidate = m_Minimum + std::round((candidate - m_Minimum) / m_Step) * m_Step;
        m_Value = std::clamp(candidate, m_Minimum, m_Maximum);
        NotifyChanged();
    }

    void UiSliderComponent::SetStep(const float value)
    {
        if (!std::isfinite(value) || value < 0.0F || value > m_Maximum - m_Minimum)
            throw std::invalid_argument("UI Slider step must be finite and fit inside its range.");
        m_Step = value;
        SetValue(m_Value);
    }

    void UiSliderComponent::SetDirection(const UiSliderDirection value)
    {
        if (value > UiSliderDirection::TopToBottom)
            throw std::invalid_argument("UI Slider direction is unsupported.");
        m_Direction = value;
        NotifyChanged();
    }

    void UiSliderComponent::SetWholeNumbers(const bool value)
    {
        m_WholeNumbers = value;
        SetValue(m_Value);
    }

    void UiSliderComponent::SetInteractable(const bool value)
    {
        m_Interactable = value;
        NotifyChanged();
    }

    UiToggleComponent::UiToggleComponent() : Component(StaticType()) {}

    void UiToggleComponent::SetIsOn(const bool value)
    {
        m_IsOn = value;
        NotifyChanged();
    }

    void UiToggleComponent::SetInteractable(const bool value)
    {
        m_Interactable = value;
        NotifyChanged();
    }

    void UiToggleComponent::SetOnColor(const Color value)
    {
        if (!Finite(value))
            throw std::invalid_argument("UI Toggle on color must be finite.");
        m_OnColor = value;
        NotifyChanged();
    }

    void UiToggleComponent::SetOffColor(const Color value)
    {
        if (!Finite(value))
            throw std::invalid_argument("UI Toggle off color must be finite.");
        m_OffColor = value;
        NotifyChanged();
    }

    UiInputFieldComponent::UiInputFieldComponent() : Component(StaticType()) {}

    void UiInputFieldComponent::SetText(std::string value)
    {
        if (value.size() > m_CharacterLimit)
            throw std::length_error("UI Input Field text exceeds its UTF-8 byte limit.");
        if (!ValidInputText(value, m_ContentType))
            throw std::invalid_argument("UI Input Field text is incompatible with its content type.");
        m_Text = std::move(value);
        NotifyChanged();
    }

    void UiInputFieldComponent::SetPlaceholder(std::string value)
    {
        if (value.size() > 16'384)
            throw std::length_error("UI Input Field placeholder exceeds 16 KiB.");
        m_Placeholder = std::move(value);
        NotifyChanged();
    }

    void UiInputFieldComponent::SetCharacterLimit(const std::uint32_t value)
    {
        if (value == 0 || value > 1'048'576 || m_Text.size() > value)
            throw std::invalid_argument("UI Input Field limit must contain the current text and be at most one MiB.");
        m_CharacterLimit = value;
        NotifyChanged();
    }

    void UiInputFieldComponent::SetContentType(const UiInputContentType value)
    {
        if (value > UiInputContentType::Password)
            throw std::invalid_argument("UI Input Field content type is unsupported.");
        if (!ValidInputText(m_Text, value))
            throw std::invalid_argument("UI Input Field content type is incompatible with its current text.");
        m_ContentType = value;
        NotifyChanged();
    }

    void UiInputFieldComponent::SetMultiline(const bool value)
    {
        m_Multiline = value;
        NotifyChanged();
    }

    void UiInputFieldComponent::SetInteractable(const bool value)
    {
        m_Interactable = value;
        NotifyChanged();
    }

    UiScrollViewComponent::UiScrollViewComponent() : Component(StaticType()) {}

    void UiScrollViewComponent::SetContentSize(const Vector2 value)
    {
        if (!Finite(value) || value.X < 0.0F || value.Y < 0.0F)
            throw std::invalid_argument("UI Scroll View content size must be finite and non-negative.");
        m_ContentSize = value;
        NotifyChanged();
    }

    void UiScrollViewComponent::SetOffset(const Vector2 value)
    {
        if (!Finite(value) || value.X < 0.0F || value.Y < 0.0F)
            throw std::invalid_argument("UI Scroll View offset must be finite and non-negative.");
        m_Offset = value;
        NotifyChanged();
    }

    void UiScrollViewComponent::SetSensitivity(const float value)
    {
        if (!std::isfinite(value) || value <= 0.0F || value > 10'000.0F)
            throw std::invalid_argument("UI Scroll View sensitivity must be finite and positive.");
        m_Sensitivity = value;
        NotifyChanged();
    }

    void UiScrollViewComponent::SetHorizontal(const bool value)
    {
        m_Horizontal = value;
        NotifyChanged();
    }

    void UiScrollViewComponent::SetVertical(const bool value)
    {
        m_Vertical = value;
        NotifyChanged();
    }

    void UiScrollViewComponent::SetInteractable(const bool value)
    {
        m_Interactable = value;
        NotifyChanged();
    }

    UiAccessibilityComponent::UiAccessibilityComponent() : Component(StaticType()) {}

    void UiAccessibilityComponent::SetLabel(std::string value)
    {
        if (value.size() > 16'384)
            throw std::length_error("UI accessibility labels cannot exceed 16 KiB.");
        m_Label = std::move(value);
        NotifyChanged();
    }

    void UiAccessibilityComponent::SetHint(std::string value)
    {
        if (value.size() > 16'384)
            throw std::length_error("UI accessibility hints cannot exceed 16 KiB.");
        m_Hint = std::move(value);
        NotifyChanged();
    }

    void UiAccessibilityComponent::SetRole(const UiAccessibilityRole value)
    {
        if (value > UiAccessibilityRole::Image)
            throw std::invalid_argument("UI accessibility role is unsupported.");
        m_Role = value;
        NotifyChanged();
    }

    void UiAccessibilityComponent::SetNavigationOrder(const std::int32_t value)
    {
        if (value < 0 || value > 1'000'000)
            throw std::invalid_argument("UI accessibility navigation order must be in the range 0..1000000.");
        m_NavigationOrder = value;
        NotifyChanged();
    }

    ComponentRegistration CreateUiSliderComponentRegistration()
    {
        ComponentRegistration result;
        result.Type = UiSliderComponent::StaticType();
        result.Name = "UI Slider";
        result.Category = "UI";
        result.RequiredComponents = {RectTransformComponent::StaticType()};
        result.Properties = {
            {"minimum", "Minimum", "Value", ComponentPropertyKind::Scalar},
            {"maximum", "Maximum", "Value", ComponentPropertyKind::Scalar},
            {"value", "Value", "Value", ComponentPropertyKind::Scalar},
            {"step", "Step", "Value", ComponentPropertyKind::Scalar, false, 0.0},
            {"wholeNumbers", "Whole Numbers", "Value", ComponentPropertyKind::Boolean},
            {"direction", "Direction", "Interaction", ComponentPropertyKind::Integer, false, 0.0, 3.0, 1.0},
            {"interactable", "Interactable", "Interaction", ComponentPropertyKind::Boolean},
        };
        result.Factory = [] { return Ref<Component>(CreateRef<UiSliderComponent>()); };
        result.Serialize = [](const Component& component)
        {
            const auto& slider = dynamic_cast<const UiSliderComponent&>(component);
            return ComponentPropertyBag{{"minimum", static_cast<double>(slider.m_Minimum)},
                                        {"maximum", static_cast<double>(slider.m_Maximum)},
                                        {"value", static_cast<double>(slider.m_Value)},
                                        {"step", static_cast<double>(slider.m_Step)},
                                        {"wholeNumbers", slider.m_WholeNumbers},
                                        {"direction", static_cast<std::int64_t>(slider.m_Direction)},
                                        {"interactable", slider.m_Interactable}};
        };
        result.Deserialize = [](Component& component, const ComponentPropertyBag& values, const std::uint32_t version)
        {
            if (version != 1)
                throw std::invalid_argument("Unsupported UI Slider component schema version.");
            auto& slider = dynamic_cast<UiSliderComponent&>(component);
            slider.SetRange(static_cast<float>(ReadProperty(values, "minimum", 0.0)),
                            static_cast<float>(ReadProperty(values, "maximum", 1.0)));
            slider.SetStep(static_cast<float>(ReadProperty(values, "step", 0.0)));
            slider.SetWholeNumbers(ReadProperty(values, "wholeNumbers", false));
            slider.SetValue(static_cast<float>(ReadProperty(values, "value", 0.5)));
            slider.SetDirection(ReadEnum(values, "direction", UiSliderDirection::LeftToRight, 3));
            slider.SetInteractable(ReadProperty(values, "interactable", true));
        };
        return result;
    }

    ComponentRegistration CreateUiToggleComponentRegistration()
    {
        ComponentRegistration result;
        result.Type = UiToggleComponent::StaticType();
        result.Name = "UI Toggle";
        result.Category = "UI";
        result.RequiredComponents = {RectTransformComponent::StaticType()};
        result.Properties = {{"isOn", "Is On", "Value", ComponentPropertyKind::Boolean},
                             {"interactable", "Interactable", "Interaction", ComponentPropertyKind::Boolean},
                             {"onColor", "On", "Colors", ComponentPropertyKind::Color},
                             {"offColor", "Off", "Colors", ComponentPropertyKind::Color}};
        result.Factory = [] { return Ref<Component>(CreateRef<UiToggleComponent>()); };
        result.Serialize = [](const Component& component)
        {
            const auto& toggle = dynamic_cast<const UiToggleComponent&>(component);
            return ComponentPropertyBag{{"isOn", toggle.m_IsOn},
                                        {"interactable", toggle.m_Interactable},
                                        {"onColor", toggle.m_OnColor},
                                        {"offColor", toggle.m_OffColor}};
        };
        result.Deserialize = [](Component& component, const ComponentPropertyBag& values, const std::uint32_t version)
        {
            if (version != 1)
                throw std::invalid_argument("Unsupported UI Toggle component schema version.");
            auto& toggle = dynamic_cast<UiToggleComponent&>(component);
            toggle.SetIsOn(ReadProperty(values, "isOn", false));
            toggle.SetInteractable(ReadProperty(values, "interactable", true));
            toggle.SetOnColor(ReadProperty(values, "onColor", Color{0.08F, 0.72F, 0.55F, 1.0F}));
            toggle.SetOffColor(ReadProperty(values, "offColor", Color{0.16F, 0.19F, 0.24F, 1.0F}));
        };
        return result;
    }

    ComponentRegistration CreateUiInputFieldComponentRegistration()
    {
        ComponentRegistration result;
        result.Type = UiInputFieldComponent::StaticType();
        result.Name = "UI Input Field";
        result.Category = "UI";
        result.RequiredComponents = {RectTransformComponent::StaticType()};
        result.Properties = {
            {"text", "Text", "Content", ComponentPropertyKind::Text},
            {"placeholder", "Placeholder", "Content", ComponentPropertyKind::Text},
            {"characterLimit", "Character Limit", "Content", ComponentPropertyKind::Integer, false, 1.0, 1048576.0,
             1.0},
            {"contentType", "Content Type", "Content", ComponentPropertyKind::Integer, false, 0.0, 3.0, 1.0},
            {"multiline", "Multiline", "Content", ComponentPropertyKind::Boolean},
            {"interactable", "Interactable", "Interaction", ComponentPropertyKind::Boolean},
        };
        result.Factory = [] { return Ref<Component>(CreateRef<UiInputFieldComponent>()); };
        result.Serialize = [](const Component& component)
        {
            const auto& input = dynamic_cast<const UiInputFieldComponent&>(component);
            return ComponentPropertyBag{{"text", input.m_Text},
                                        {"placeholder", input.m_Placeholder},
                                        {"characterLimit", static_cast<std::int64_t>(input.m_CharacterLimit)},
                                        {"contentType", static_cast<std::int64_t>(input.m_ContentType)},
                                        {"multiline", input.m_Multiline},
                                        {"interactable", input.m_Interactable}};
        };
        result.Deserialize = [](Component& component, const ComponentPropertyBag& values, const std::uint32_t version)
        {
            if (version != 1)
                throw std::invalid_argument("Unsupported UI Input Field component schema version.");
            auto& input = dynamic_cast<UiInputFieldComponent&>(component);
            input.SetCharacterLimit(
                static_cast<std::uint32_t>(ReadProperty(values, "characterLimit", std::int64_t{256})));
            input.SetContentType(ReadEnum(values, "contentType", UiInputContentType::Standard, 3));
            input.SetText(ReadProperty(values, "text", std::string{}));
            input.SetPlaceholder(ReadProperty(values, "placeholder", std::string("Enter text")));
            input.SetMultiline(ReadProperty(values, "multiline", false));
            input.SetInteractable(ReadProperty(values, "interactable", true));
        };
        return result;
    }

    ComponentRegistration CreateUiScrollViewComponentRegistration()
    {
        ComponentRegistration result;
        result.Type = UiScrollViewComponent::StaticType();
        result.Name = "UI Scroll View";
        result.Category = "UI";
        result.RequiredComponents = {RectTransformComponent::StaticType()};
        result.Properties = {
            {"contentSize", "Content Size", "Scrolling", ComponentPropertyKind::Vector2},
            {"offset", "Offset", "Scrolling", ComponentPropertyKind::Vector2},
            {"sensitivity", "Sensitivity", "Scrolling", ComponentPropertyKind::Scalar, false, 0.01, 10000.0, 1.0},
            {"horizontal", "Horizontal", "Scrolling", ComponentPropertyKind::Boolean},
            {"vertical", "Vertical", "Scrolling", ComponentPropertyKind::Boolean},
            {"interactable", "Interactable", "Interaction", ComponentPropertyKind::Boolean},
        };
        result.Factory = [] { return Ref<Component>(CreateRef<UiScrollViewComponent>()); };
        result.Serialize = [](const Component& component)
        {
            const auto& scroll = dynamic_cast<const UiScrollViewComponent&>(component);
            return ComponentPropertyBag{{"contentSize", scroll.m_ContentSize},
                                        {"offset", scroll.m_Offset},
                                        {"sensitivity", static_cast<double>(scroll.m_Sensitivity)},
                                        {"horizontal", scroll.m_Horizontal},
                                        {"vertical", scroll.m_Vertical},
                                        {"interactable", scroll.m_Interactable}};
        };
        result.Deserialize = [](Component& component, const ComponentPropertyBag& values, const std::uint32_t version)
        {
            if (version != 1)
                throw std::invalid_argument("Unsupported UI Scroll View component schema version.");
            auto& scroll = dynamic_cast<UiScrollViewComponent&>(component);
            scroll.SetContentSize(ReadProperty(values, "contentSize", Vector2{1920.0F, 1080.0F}));
            scroll.SetOffset(ReadProperty(values, "offset", Vector2{}));
            scroll.SetSensitivity(static_cast<float>(ReadProperty(values, "sensitivity", 48.0)));
            scroll.SetHorizontal(ReadProperty(values, "horizontal", false));
            scroll.SetVertical(ReadProperty(values, "vertical", true));
            scroll.SetInteractable(ReadProperty(values, "interactable", true));
        };
        return result;
    }

    ComponentRegistration CreateUiAccessibilityComponentRegistration()
    {
        ComponentRegistration result;
        result.Type = UiAccessibilityComponent::StaticType();
        result.Name = "UI Accessibility";
        result.Category = "UI";
        result.RequiredComponents = {RectTransformComponent::StaticType()};
        result.Properties = {
            {"label", "Label", "Accessibility", ComponentPropertyKind::Text},
            {"hint", "Hint", "Accessibility", ComponentPropertyKind::Text},
            {"role", "Role", "Accessibility", ComponentPropertyKind::Integer, false, 0.0, 7.0, 1.0},
            {"navigationOrder", "Navigation Order", "Accessibility", ComponentPropertyKind::Integer, false, 0.0,
             1000000.0, 1.0},
        };
        result.Factory = [] { return Ref<Component>(CreateRef<UiAccessibilityComponent>()); };
        result.Serialize = [](const Component& component)
        {
            const auto& accessibility = dynamic_cast<const UiAccessibilityComponent&>(component);
            return ComponentPropertyBag{
                {"label", accessibility.m_Label},
                {"hint", accessibility.m_Hint},
                {"role", static_cast<std::int64_t>(accessibility.m_Role)},
                {"navigationOrder", static_cast<std::int64_t>(accessibility.m_NavigationOrder)}};
        };
        result.Deserialize = [](Component& component, const ComponentPropertyBag& values, const std::uint32_t version)
        {
            if (version != 1)
                throw std::invalid_argument("Unsupported UI Accessibility component schema version.");
            auto& accessibility = dynamic_cast<UiAccessibilityComponent&>(component);
            accessibility.SetLabel(ReadProperty(values, "label", std::string{}));
            accessibility.SetHint(ReadProperty(values, "hint", std::string{}));
            accessibility.SetRole(ReadEnum(values, "role", UiAccessibilityRole::Automatic, 7));
            accessibility.SetNavigationOrder(
                static_cast<std::int32_t>(ReadProperty(values, "navigationOrder", std::int64_t{0})));
        };
        return result;
    }
} // namespace Keire
