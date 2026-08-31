#include "Keire/Ui/UiStyleProperties.h"

#include "KeireInternal/Ui/RuntimeUiStyleParsingInternal.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <ranges>
#include <stdexcept>
#include <string>
#include <vector>

namespace Keire
{
    namespace
    {
        using Category = UiStylePropertyCategory;
        using Kind = UiStyleValueKind;

        constexpr std::array Properties{
            UiStylePropertyDescriptor{"display", "Display", Category::Layout, Kind::Keyword, "flex", "flex|none"},
            UiStylePropertyDescriptor{"overflow", "Overflow", Category::Clipping, Kind::Keyword, "visible",
                                      "visible|hidden|clip"},
            UiStylePropertyDescriptor{"sorting-order", "Sorting Order", Category::Layout, Kind::Integer, "0"},
            UiStylePropertyDescriptor{"width", "Width", Category::Size, Kind::Length, "auto", {}, false, true},
            UiStylePropertyDescriptor{"height", "Height", Category::Size, Kind::Length, "auto", {}, false, true},
            UiStylePropertyDescriptor{"min-width", "Minimum Width", Category::Size, Kind::Length, "0px"},
            UiStylePropertyDescriptor{"min-height", "Minimum Height", Category::Size, Kind::Length, "0px"},
            UiStylePropertyDescriptor{"max-width", "Maximum Width", Category::Size, Kind::Length, "none"},
            UiStylePropertyDescriptor{"max-height", "Maximum Height", Category::Size, Kind::Length, "none"},
            UiStylePropertyDescriptor{"position", "Position", Category::Position, Kind::Keyword, "relative",
                                      "relative|flow|absolute"},
            UiStylePropertyDescriptor{"left", "Left", Category::Position, Kind::Length, "0px", {}, false, true},
            UiStylePropertyDescriptor{"top", "Top", Category::Position, Kind::Length, "0px", {}, false, true},
            UiStylePropertyDescriptor{"flex-direction", "Direction", Category::Flex, Kind::Keyword, "column",
                                      "column|column-reverse|row|row-reverse"},
            UiStylePropertyDescriptor{"flex-wrap", "Wrap", Category::Flex, Kind::Keyword, "nowrap",
                                      "nowrap|wrap|wrap-reverse"},
            UiStylePropertyDescriptor{"flex-grow", "Grow", Category::Flex, Kind::Number, "0"},
            UiStylePropertyDescriptor{"flex-shrink", "Shrink", Category::Flex, Kind::Number, "1"},
            UiStylePropertyDescriptor{"align-items", "Align Items", Category::Flex, Kind::Keyword, "start",
                                      "start|center|end|stretch"},
            UiStylePropertyDescriptor{"align-self", "Align Self", Category::Flex, Kind::Keyword, "auto",
                                      "auto|start|center|end|stretch"},
            UiStylePropertyDescriptor{"justify-content", "Justify Content", Category::Flex, Kind::Keyword, "start",
                                      "start|center|end|space-between|space-around|space-evenly"},
            UiStylePropertyDescriptor{"gap", "Gap", Category::Spacing, Kind::Length, "0px"},
            UiStylePropertyDescriptor{"margin", "Margin", Category::Spacing, Kind::Insets, "0px"},
            UiStylePropertyDescriptor{"margin-left", "Margin Left", Category::Spacing, Kind::Length, "0px"},
            UiStylePropertyDescriptor{"margin-top", "Margin Top", Category::Spacing, Kind::Length, "0px"},
            UiStylePropertyDescriptor{"margin-right", "Margin Right", Category::Spacing, Kind::Length, "0px"},
            UiStylePropertyDescriptor{"margin-bottom", "Margin Bottom", Category::Spacing, Kind::Length, "0px"},
            UiStylePropertyDescriptor{"padding", "Padding", Category::Spacing, Kind::Insets, "0px"},
            UiStylePropertyDescriptor{"padding-left", "Padding Left", Category::Spacing, Kind::Length, "0px"},
            UiStylePropertyDescriptor{"padding-top", "Padding Top", Category::Spacing, Kind::Length, "0px"},
            UiStylePropertyDescriptor{"padding-right", "Padding Right", Category::Spacing, Kind::Length, "0px"},
            UiStylePropertyDescriptor{"padding-bottom", "Padding Bottom", Category::Spacing, Kind::Length, "0px"},
            UiStylePropertyDescriptor{
                "color", "Text Color", Category::Typography, Kind::Color, "#ffffffff", {}, true, true},
            UiStylePropertyDescriptor{
                "font-size", "Font Size", Category::Typography, Kind::Length, "16px", {}, true, true},
            UiStylePropertyDescriptor{
                "font-family", "Font Family", Category::Typography, Kind::Asset, "none", {}, true, false, true, 2},
            UiStylePropertyDescriptor{
                "font-weight", "Font Weight", Category::Typography, Kind::Integer, "400", {}, true, false, true, 2},
            UiStylePropertyDescriptor{"font-style", "Font Style", Category::Typography, Kind::Keyword, "normal",
                                      "normal|italic|oblique", true, false, true, 2},
            UiStylePropertyDescriptor{
                "line-height", "Line Height", Category::Typography, Kind::Length, "normal", {}, true, true, true, 2},
            UiStylePropertyDescriptor{"letter-spacing",
                                      "Letter Spacing",
                                      Category::Typography,
                                      Kind::Length,
                                      "normal",
                                      {},
                                      true,
                                      true,
                                      true,
                                      2},
            UiStylePropertyDescriptor{
                "word-spacing", "Word Spacing", Category::Typography, Kind::Length, "normal", {}, true, true, true, 2},
            UiStylePropertyDescriptor{"white-space", "Wrapping", Category::Typography, Kind::Keyword, "normal",
                                      "normal|nowrap", true, false, true, 2},
            UiStylePropertyDescriptor{
                "max-lines", "Maximum Lines", Category::Typography, Kind::Integer, "0", {}, true, false, true, 2},
            UiStylePropertyDescriptor{"text-overflow", "Text Overflow", Category::Typography, Kind::Keyword, "clip",
                                      "clip|ellipsis", true, false, true, 2},
            UiStylePropertyDescriptor{"direction", "Text Direction", Category::Typography, Kind::Keyword, "auto",
                                      "auto|ltr|rtl", true, false, true, 2},
            UiStylePropertyDescriptor{
                "language", "Language", Category::Typography, Kind::String, "und", {}, true, false, true, 2},
            UiStylePropertyDescriptor{"text-align", "Horizontal Alignment", Category::Typography, Kind::Keyword,
                                      "start", "start|center|end|stretch", true},
            UiStylePropertyDescriptor{"vertical-align", "Vertical Alignment", Category::Typography, Kind::Keyword,
                                      "start", "start|center|end|stretch", true},
            UiStylePropertyDescriptor{"background-color",
                                      "Background Color",
                                      Category::Background,
                                      Kind::Color,
                                      "#00000000",
                                      {},
                                      false,
                                      true},
            UiStylePropertyDescriptor{"background", "Background Gradient", Category::Background, Kind::Gradient,
                                      "none"},
            UiStylePropertyDescriptor{"background-image",
                                      "Background Image",
                                      Category::Background,
                                      Kind::Asset,
                                      "none",
                                      {},
                                      false,
                                      false,
                                      true,
                                      2},
            UiStylePropertyDescriptor{"background-tint",
                                      "Image Tint",
                                      Category::Background,
                                      Kind::Color,
                                      "#ffffffff",
                                      {},
                                      false,
                                      true,
                                      true,
                                      2},
            UiStylePropertyDescriptor{"background-fit", "Image Fit", Category::Background, Kind::Keyword, "stretch",
                                      "stretch|contain|cover|none", false, false, true, 2},
            UiStylePropertyDescriptor{"background-position",
                                      "Image Position",
                                      Category::Background,
                                      Kind::String,
                                      "center center",
                                      {},
                                      false,
                                      false,
                                      true,
                                      2},
            UiStylePropertyDescriptor{"background-repeat", "Image Repeat", Category::Background, Kind::Keyword,
                                      "no-repeat", "no-repeat|repeat|repeat-x|repeat-y", false, false, true, 2},
            UiStylePropertyDescriptor{
                "background-slice", "Nine Slice", Category::Background, Kind::Insets, "0px", {}, false, false, true, 2},
            UiStylePropertyDescriptor{
                "border-color", "Border Color", Category::Border, Kind::Color, "#00000000", {}, false, true},
            UiStylePropertyDescriptor{"border-width", "Border Width", Category::Border, Kind::Length, "0px"},
            UiStylePropertyDescriptor{
                "border-radius", "Border Radius", Category::Border, Kind::Length, "0px", {}, false, true},
            UiStylePropertyDescriptor{"border-left-color",
                                      "Left Color",
                                      Category::Border,
                                      Kind::Color,
                                      "#00000000",
                                      {},
                                      false,
                                      true,
                                      true,
                                      2},
            UiStylePropertyDescriptor{
                "border-top-color", "Top Color", Category::Border, Kind::Color, "#00000000", {}, false, true, true, 2},
            UiStylePropertyDescriptor{"border-right-color",
                                      "Right Color",
                                      Category::Border,
                                      Kind::Color,
                                      "#00000000",
                                      {},
                                      false,
                                      true,
                                      true,
                                      2},
            UiStylePropertyDescriptor{"border-bottom-color",
                                      "Bottom Color",
                                      Category::Border,
                                      Kind::Color,
                                      "#00000000",
                                      {},
                                      false,
                                      true,
                                      true,
                                      2},
            UiStylePropertyDescriptor{
                "border-left-width", "Left Width", Category::Border, Kind::Length, "0px", {}, false, true, true, 2},
            UiStylePropertyDescriptor{
                "border-top-width", "Top Width", Category::Border, Kind::Length, "0px", {}, false, true, true, 2},
            UiStylePropertyDescriptor{
                "border-right-width", "Right Width", Category::Border, Kind::Length, "0px", {}, false, true, true, 2},
            UiStylePropertyDescriptor{
                "border-bottom-width", "Bottom Width", Category::Border, Kind::Length, "0px", {}, false, true, true, 2},
            UiStylePropertyDescriptor{"border-top-left-radius",
                                      "Top Left Radius",
                                      Category::Border,
                                      Kind::Length,
                                      "0px",
                                      {},
                                      false,
                                      true,
                                      true,
                                      2},
            UiStylePropertyDescriptor{"border-top-right-radius",
                                      "Top Right Radius",
                                      Category::Border,
                                      Kind::Length,
                                      "0px",
                                      {},
                                      false,
                                      true,
                                      true,
                                      2},
            UiStylePropertyDescriptor{"border-bottom-right-radius",
                                      "Bottom Right Radius",
                                      Category::Border,
                                      Kind::Length,
                                      "0px",
                                      {},
                                      false,
                                      true,
                                      true,
                                      2},
            UiStylePropertyDescriptor{"border-bottom-left-radius",
                                      "Bottom Left Radius",
                                      Category::Border,
                                      Kind::Length,
                                      "0px",
                                      {},
                                      false,
                                      true,
                                      true,
                                      2},
            UiStylePropertyDescriptor{"opacity", "Opacity", Category::Effects, Kind::Number, "1", {}, false, true},
            UiStylePropertyDescriptor{
                "box-shadow", "Box Shadows", Category::Effects, Kind::ShadowList, "none", {}, false, false, true, 2},
            UiStylePropertyDescriptor{
                "text-shadow", "Text Shadows", Category::Effects, Kind::ShadowList, "none", {}, true, false, true, 2},
            UiStylePropertyDescriptor{
                "translate", "Translate", Category::Transform, Kind::String, "0px 0px", {}, false, true, true, 2},
            UiStylePropertyDescriptor{
                "scale", "Scale", Category::Transform, Kind::String, "1 1", {}, false, true, true, 2},
            UiStylePropertyDescriptor{
                "rotate", "Rotation", Category::Transform, Kind::String, "0deg", {}, false, true, true, 2},
            UiStylePropertyDescriptor{"transform-origin",
                                      "Transform Origin",
                                      Category::Transform,
                                      Kind::String,
                                      "center center",
                                      {},
                                      false,
                                      false,
                                      true,
                                      2},
            UiStylePropertyDescriptor{
                "mask-image", "Alpha Mask", Category::Clipping, Kind::Asset, "none", {}, false, false, true, 2},
            UiStylePropertyDescriptor{"transition-property", "Transition Properties", Category::Transition,
                                      Kind::PropertyList, "none"},
            UiStylePropertyDescriptor{"transition-duration", "Transition Durations", Category::Transition,
                                      Kind::DurationList, "0ms"},
            UiStylePropertyDescriptor{"transition-delay",
                                      "Transition Delays",
                                      Category::Transition,
                                      Kind::DurationList,
                                      "0ms",
                                      {},
                                      false,
                                      false,
                                      true,
                                      2},
            UiStylePropertyDescriptor{"transition-timing-function",
                                      "Transition Easing",
                                      Category::Transition,
                                      Kind::TimingFunctionList,
                                      "ease",
                                      {},
                                      false,
                                      false,
                                      true,
                                      2},
        };

        [[nodiscard]] std::string_view Trim(const std::string_view value) noexcept
        {
            const auto begin = value.find_first_not_of(" \t\r\n");
            if (begin == std::string_view::npos)
                return {};
            const auto end = value.find_last_not_of(" \t\r\n");
            return value.substr(begin, end - begin + 1U);
        }

        [[nodiscard]] std::vector<std::string_view> Split(const std::string_view source, const char delimiter)
        {
            std::vector<std::string_view> result;
            std::size_t cursor = 0;
            while (cursor <= source.size())
            {
                const auto end = source.find(delimiter, cursor);
                const auto value = Trim(source.substr(cursor, end - cursor));
                if (value.empty())
                    throw std::invalid_argument("UI style list contains an empty value.");
                result.push_back(value);
                if (end == std::string_view::npos)
                    break;
                cursor = end + 1U;
            }
            return result;
        }

        [[nodiscard]] std::vector<std::string_view> Words(const std::string_view source)
        {
            std::vector<std::string_view> result;
            std::size_t cursor = 0;
            while (cursor < source.size())
            {
                const auto begin = source.find_first_not_of(" \t\r\n", cursor);
                if (begin == std::string_view::npos)
                    break;
                const auto end = source.find_first_of(" \t\r\n", begin);
                result.push_back(source.substr(begin, end - begin));
                cursor = end == std::string_view::npos ? source.size() : end;
            }
            return result;
        }

        [[nodiscard]] double Number(std::string_view value, const bool integer, const bool allowPercent,
                                    const bool allowPixels)
        {
            value = Trim(value);
            if (allowPixels && value.ends_with("px"))
                value.remove_suffix(2);
            else if (allowPercent && value.ends_with('%'))
                value.remove_suffix(1);
            double result = 0.0;
            const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), result);
            if (error != std::errc{} || end != value.data() + value.size() || !std::isfinite(result) ||
                (integer && std::trunc(result) != result))
                throw std::invalid_argument("UI style property contains an invalid numeric value.");
            return result;
        }

        void ValidateColor(const std::string_view value)
        {
            if (value == "transparent")
                return;
            if ((value.size() != 7U && value.size() != 9U) || value.front() != '#' ||
                !std::ranges::all_of(value.substr(1),
                                     [](const unsigned char character) { return std::isxdigit(character) != 0; }))
                throw std::invalid_argument("UI color requires #RRGGBB, #RRGGBBAA, or transparent.");
        }

        void ValidateAsset(const std::string_view value)
        {
            if (value == "none")
                return;
            if (!value.starts_with("asset(") || !value.ends_with(')'))
                throw std::invalid_argument("UI asset value requires asset(<stable-id>) or none.");
            (void)AssetId::Parse(Trim(value.substr(6, value.size() - 7U)));
        }

        void ValidateDuration(const std::string_view value)
        {
            auto number = Trim(value);
            double multiplier = 1.0;
            if (number.ends_with("ms"))
            {
                number.remove_suffix(2);
                multiplier = 0.001;
            }
            else if (number.ends_with('s'))
                number.remove_suffix(1);
            else
                throw std::invalid_argument("UI transition time requires milliseconds or seconds.");
            const auto seconds = Number(number, false, false, false) * multiplier;
            if (seconds < 0.0 || seconds > 60.0)
                throw std::invalid_argument("UI transition time must be between zero and 60 seconds.");
        }
    } // namespace

    std::span<const UiStylePropertyDescriptor> UiStylePropertyDescriptors() noexcept { return Properties; }

    const UiStylePropertyDescriptor* FindUiStylePropertyDescriptor(const std::string_view name) noexcept
    {
        const auto found = std::ranges::find(Properties, name, &UiStylePropertyDescriptor::Name);
        return found == Properties.end() ? nullptr : &*found;
    }

    void ValidateUiStylePropertyValue(const std::string_view name, std::string_view value,
                                      const std::uint32_t schemaVersion)
    {
        value = Trim(value);
        if (name.starts_with("--"))
        {
            if (value.empty())
                throw std::invalid_argument("UI design-token values cannot be empty.");
            return;
        }
        const auto* descriptor = FindUiStylePropertyDescriptor(name);
        if (!descriptor)
            throw std::invalid_argument("UI style contains an unsupported property: " + std::string(name));
        const bool legacyGradientAlias = name == "background-image" && (value.starts_with("linear-gradient(") ||
                                                                        value.starts_with("radial-gradient("));
        if (!legacyGradientAlias && schemaVersion < descriptor->MinimumSchemaVersion)
            throw std::invalid_argument("UI stylesheet property '" + std::string(name) + "' requires schema v" +
                                        std::to_string(descriptor->MinimumSchemaVersion) + '.');
        if (value.starts_with("var(") && value.ends_with(')'))
            return;
        if (legacyGradientAlias)
        {
            if (value.ends_with(')'))
                return;
            throw std::invalid_argument("UI background gradient is missing its closing parenthesis.");
        }

        RuntimeUiStyle parsed;
        if (!legacyGradientAlias && descriptor->MinimumSchemaVersion >= 2U &&
            Detail::TryApplyRuntimeUiStyleV2Property(parsed, name, value))
            return;

        switch (descriptor->ValueKind)
        {
        case UiStyleValueKind::Number:
            (void)Number(value, false, false, false);
            return;
        case UiStyleValueKind::Integer:
            (void)Number(value, true, false, false);
            return;
        case UiStyleValueKind::Length:
            if (value == "auto" || value == "none" || value == "normal")
                return;
            (void)Number(value, false, true, true);
            return;
        case UiStyleValueKind::Color:
            ValidateColor(value);
            return;
        case UiStyleValueKind::Keyword:
        {
            const auto keywords = Split(descriptor->Keywords, '|');
            if (std::ranges::find(keywords, value) == keywords.end())
                throw std::invalid_argument("UI style property contains an unsupported keyword.");
            return;
        }
        case UiStyleValueKind::Insets:
        {
            const auto values = Words(value);
            if (values.empty() || values.size() > 4U)
                throw std::invalid_argument("UI edge shorthand accepts one to four values.");
            for (const auto edge : values)
                (void)Number(edge, false, false, true);
            return;
        }
        case UiStyleValueKind::Asset:
            ValidateAsset(value);
            return;
        case UiStyleValueKind::DurationList:
        {
            const auto values = Split(value, ',');
            if (values.size() > 8U)
                throw std::invalid_argument("UI transition lists are limited to eight values.");
            for (const auto duration : values)
                ValidateDuration(duration);
            return;
        }
        case UiStyleValueKind::Gradient:
            if (value == "none" || ((value.starts_with("linear-gradient(") || value.starts_with("radial-gradient(")) &&
                                    value.ends_with(')')))
                return;
            throw std::invalid_argument("UI background requires none, linear-gradient(...), or radial-gradient(...).");
        case UiStyleValueKind::PropertyList:
        case UiStyleValueKind::TimingFunctionList:
        case UiStyleValueKind::AssetList:
        case UiStyleValueKind::ShadowList:
        case UiStyleValueKind::String:
            if (value.empty())
                throw std::invalid_argument("UI style values cannot be empty.");
            return;
        }
        throw std::invalid_argument("UI style property kind is invalid.");
    }
} // namespace Keire
