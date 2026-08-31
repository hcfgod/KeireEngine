#include "KeireInternal/Ui/RuntimeUiStyleParsingInternal.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <iterator>
#include <ranges>
#include <stdexcept>
#include <string>
#include <vector>

namespace Keire::Detail
{
    namespace
    {
        [[nodiscard]] std::string_view Trim(const std::string_view value) noexcept
        {
            const auto begin = value.find_first_not_of(" \t\r\n");
            if (begin == std::string_view::npos)
                return {};
            const auto end = value.find_last_not_of(" \t\r\n");
            return value.substr(begin, end - begin + 1);
        }

        [[nodiscard]] float Scalar(std::string_view value)
        {
            value = Trim(value);
            if (value.ends_with("px"))
                value.remove_suffix(2);
            float result = 0.0F;
            const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), result);
            if (error != std::errc{} || end != value.data() + value.size() || !std::isfinite(result))
                throw std::runtime_error("UI style property contains an invalid scalar value.");
            return result;
        }

        [[nodiscard]] std::vector<std::string_view> Words(const std::string_view value)
        {
            std::vector<std::string_view> result;
            std::size_t cursor = 0;
            while (cursor < value.size())
            {
                const auto begin = value.find_first_not_of(" \t\r\n", cursor);
                if (begin == std::string_view::npos)
                    break;
                const auto end = value.find_first_of(" \t\r\n", begin);
                result.push_back(value.substr(begin, end - begin));
                cursor = end == std::string_view::npos ? value.size() : end;
            }
            return result;
        }

        [[nodiscard]] std::vector<std::string_view> CommaList(const std::string_view value)
        {
            std::vector<std::string_view> result;
            std::size_t cursor = 0;
            int parentheses = 0;
            for (std::size_t index = 0; index <= value.size(); ++index)
            {
                if (index < value.size() && value[index] == '(')
                    ++parentheses;
                else if (index < value.size() && value[index] == ')')
                    --parentheses;
                if (index != value.size() && (value[index] != ',' || parentheses != 0))
                    continue;
                const auto item = Trim(value.substr(cursor, index - cursor));
                if (item.empty())
                    throw std::runtime_error("UI style list contains an empty value.");
                result.push_back(item);
                cursor = index + 1;
            }
            return result;
        }

        [[nodiscard]] float Byte(const std::string_view value)
        {
            unsigned int result = 0;
            const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), result, 16);
            if (error != std::errc{} || end != value.data() + value.size() || result > 255)
                throw std::runtime_error("UI color contains an invalid hexadecimal component.");
            return static_cast<float>(result) / 255.0F;
        }

        [[nodiscard]] Color ParseColor(const std::string_view value)
        {
            if (value == "transparent")
                return {};
            if ((value.size() != 7 && value.size() != 9) || value.front() != '#')
                throw std::runtime_error("UI colors must use #RRGGBB or #RRGGBBAA syntax.");
            return {Byte(value.substr(1, 2)), Byte(value.substr(3, 2)), Byte(value.substr(5, 2)),
                    value.size() == 9 ? Byte(value.substr(7, 2)) : 1.0F};
        }

        [[nodiscard]] AssetId ParseAsset(const std::string_view value)
        {
            if (value == "none")
                return {};
            if (!value.starts_with("asset(") || !value.ends_with(')'))
                throw std::runtime_error("UI asset properties require asset(<stable-id>) or none.");
            return AssetId::Parse(Trim(value.substr(6, value.size() - 7)));
        }

        [[nodiscard]] Vector2 Pair(const std::string_view value, const bool normalized)
        {
            const auto words = Words(value);
            if (words.empty() || words.size() > 2)
                throw std::runtime_error("UI vector properties require one or two values.");
            const auto parse = [normalized](std::string_view word)
            {
                if (normalized)
                {
                    if (word == "left" || word == "top")
                        return 0.0F;
                    if (word == "center")
                        return 0.5F;
                    if (word == "right" || word == "bottom")
                        return 1.0F;
                    if (!word.ends_with('%'))
                        throw std::runtime_error("UI normalized positions require percentages or named positions.");
                    word.remove_suffix(1);
                    const auto value = Scalar(word) / 100.0F;
                    if (value < 0.0F || value > 1.0F)
                        throw std::runtime_error("UI normalized positions must be between 0% and 100%.");
                    return value;
                }
                return Scalar(word);
            };
            const auto first = parse(words[0]);
            return {first, words.size() == 2 ? parse(words[1]) : first};
        }

        void SetInsets(RuntimeUiInsets& destination, const std::string_view source)
        {
            const auto words = Words(source);
            if (words.empty() || words.size() > 4)
                throw std::runtime_error("UI edge shorthand requires one to four values.");
            std::vector<float> values;
            values.reserve(words.size());
            std::ranges::transform(words, std::back_inserter(values), Scalar);
            if (values.size() == 1)
                destination = {values[0], values[0], values[0], values[0]};
            else if (values.size() == 2)
                destination = {values[1], values[0], values[1], values[0]};
            else if (values.size() == 3)
                destination = {values[1], values[0], values[1], values[2]};
            else
                destination = {values[3], values[0], values[1], values[2]};
        }

        void SetColors(RuntimeUiBorderColors& destination, const std::string_view source)
        {
            const auto words = Words(source);
            if (words.empty() || words.size() > 4)
                throw std::runtime_error("UI border-color shorthand requires one to four colors.");
            std::vector<Color> values;
            values.reserve(words.size());
            std::ranges::transform(words, std::back_inserter(values), ParseColor);
            if (values.size() == 1)
                destination = {values[0], values[0], values[0], values[0]};
            else if (values.size() == 2)
                destination = {values[1], values[0], values[1], values[0]};
            else if (values.size() == 3)
                destination = {values[1], values[0], values[1], values[2]};
            else
                destination = {values[3], values[0], values[1], values[2]};
        }

        void SetRadii(RuntimeUiCornerRadii& destination, const std::string_view source)
        {
            RuntimeUiInsets values;
            SetInsets(values, source);
            destination = {values.Top, values.Right, values.Bottom, values.Left};
        }

        [[nodiscard]] RuntimeUiShadow ParseShadow(const std::string_view source, const bool allowInset)
        {
            auto words = Words(source);
            RuntimeUiShadow result;
            if (!words.empty() && words.front() == "inset")
            {
                if (!allowInset)
                    throw std::runtime_error("UI text-shadow does not support inset shadows.");
                result.Inset = true;
                words.erase(words.begin());
            }
            if (words.size() < 3 || words.size() > 5)
                throw std::runtime_error("UI shadows require x y [blur] [spread] color.");
            result.Offset = {Scalar(words[0]), Scalar(words[1])};
            result.ColorValue = ParseColor(words.back());
            if (words.size() >= 4)
                result.BlurRadius = Scalar(words[2]);
            if (words.size() == 5)
                result.SpreadRadius = Scalar(words[3]);
            return result;
        }

        template <std::size_t Capacity>
        void SetShadows(std::array<RuntimeUiShadow, Capacity>& destination, std::uint8_t& count,
                        const std::string_view source, const bool allowInset)
        {
            count = 0;
            if (source == "none")
                return;
            const auto values = CommaList(source);
            if (values.size() > Capacity)
                throw std::runtime_error("UI shadow list exceeds its bounded capacity.");
            for (const auto value : values)
                destination[count++] = ParseShadow(value, allowInset);
        }

        [[nodiscard]] float Duration(std::string_view value)
        {
            value = Trim(value);
            float multiplier = 1.0F;
            if (value.ends_with("ms"))
            {
                value.remove_suffix(2);
                multiplier = 0.001F;
            }
            else if (value.ends_with('s'))
                value.remove_suffix(1);
            else
                throw std::runtime_error("UI transition time requires seconds or milliseconds.");
            const auto result = Scalar(value) * multiplier;
            if (result < 0.0F || result > 60.0F)
                throw std::runtime_error("UI transition time must be between zero and 60 seconds.");
            return result;
        }

        template <std::size_t Capacity>
        void SetDurations(std::array<float, Capacity>& destination, std::uint8_t& count, const std::string_view source)
        {
            const auto values = CommaList(source);
            if (values.size() > Capacity)
                throw std::runtime_error("UI transition list exceeds the limit of eight values.");
            count = static_cast<std::uint8_t>(values.size());
            for (std::size_t index = 0; index < values.size(); ++index)
                destination[index] = Duration(values[index]);
        }

        [[nodiscard]] RuntimeUiTransitionEasing Easing(const std::string_view value)
        {
            if (value == "linear")
                return RuntimeUiTransitionEasing::Linear;
            if (value == "ease")
                return RuntimeUiTransitionEasing::Ease;
            if (value == "ease-in")
                return RuntimeUiTransitionEasing::EaseIn;
            if (value == "ease-out")
                return RuntimeUiTransitionEasing::EaseOut;
            if (value == "ease-in-out")
                return RuntimeUiTransitionEasing::EaseInOut;
            throw std::runtime_error("UI transition timing supports linear, ease, ease-in, ease-out, or ease-in-out.");
        }
    } // namespace

    bool TryApplyRuntimeUiStyleV2Property(RuntimeUiStyle& style, const std::string_view name,
                                          const std::string_view value)
    {
        if (name == "border-left-width")
            style.BorderWidths.Left = Scalar(value);
        else if (name == "border-top-width")
            style.BorderWidths.Top = Scalar(value);
        else if (name == "border-right-width")
            style.BorderWidths.Right = Scalar(value);
        else if (name == "border-bottom-width")
            style.BorderWidths.Bottom = Scalar(value);
        else if (name == "border-left-color")
            style.BorderColors.Left = ParseColor(value);
        else if (name == "border-top-color")
            style.BorderColors.Top = ParseColor(value);
        else if (name == "border-right-color")
            style.BorderColors.Right = ParseColor(value);
        else if (name == "border-bottom-color")
            style.BorderColors.Bottom = ParseColor(value);
        else if (name == "border-top-left-radius")
            style.CornerRadii.TopLeft = Scalar(value);
        else if (name == "border-top-right-radius")
            style.CornerRadii.TopRight = Scalar(value);
        else if (name == "border-bottom-right-radius")
            style.CornerRadii.BottomRight = Scalar(value);
        else if (name == "border-bottom-left-radius")
            style.CornerRadii.BottomLeft = Scalar(value);
        else if (name == "box-shadow")
            SetShadows(style.BoxShadows, style.BoxShadowCount, value, true);
        else if (name == "text-shadow")
            SetShadows(style.TextShadows, style.TextShadowCount, value, false);
        else if (name == "background-image")
        {
            if (value.starts_with("linear-gradient(") || value.starts_with("radial-gradient("))
                return false;
            style.BackgroundImage = ParseAsset(value);
        }
        else if (name == "background-tint")
            style.BackgroundTint = ParseColor(value);
        else if (name == "background-fit")
        {
            if (value == "stretch")
                style.BackgroundFit = RuntimeUiBackgroundFit::Stretch;
            else if (value == "contain")
                style.BackgroundFit = RuntimeUiBackgroundFit::Contain;
            else if (value == "cover")
                style.BackgroundFit = RuntimeUiBackgroundFit::Cover;
            else if (value == "none")
                style.BackgroundFit = RuntimeUiBackgroundFit::None;
            else
                throw std::runtime_error("UI background-fit is invalid.");
        }
        else if (name == "background-repeat")
        {
            if (value == "no-repeat")
                style.BackgroundRepeat = RuntimeUiBackgroundRepeat::NoRepeat;
            else if (value == "repeat")
                style.BackgroundRepeat = RuntimeUiBackgroundRepeat::Repeat;
            else if (value == "repeat-x")
                style.BackgroundRepeat = RuntimeUiBackgroundRepeat::RepeatX;
            else if (value == "repeat-y")
                style.BackgroundRepeat = RuntimeUiBackgroundRepeat::RepeatY;
            else
                throw std::runtime_error("UI background-repeat is invalid.");
        }
        else if (name == "background-position")
            style.BackgroundPosition = Pair(value, true);
        else if (name == "background-slice")
            SetInsets(style.BackgroundSlice, value);
        else if (name == "mask-image")
            style.AlphaMask = ParseAsset(value);
        else if (name == "translate")
            style.Translation = Pair(value, false);
        else if (name == "scale")
            style.TransformScale = Pair(value, false);
        else if (name == "rotate")
        {
            auto degrees = Trim(value);
            if (!degrees.ends_with("deg"))
                throw std::runtime_error("UI rotate requires degrees.");
            degrees.remove_suffix(3);
            style.RotationDegrees = Scalar(degrees);
        }
        else if (name == "transform-origin")
            style.TransformOrigin = Pair(value, true);
        else if (name == "font-family")
            style.FontFamily = ParseAsset(value);
        else if (name == "font-weight")
        {
            const auto weight = Scalar(value);
            if (weight < 1.0F || weight > 1000.0F)
                throw std::runtime_error("UI font-weight must be between 1 and 1000.");
            style.FontWeight = static_cast<std::uint16_t>(weight);
        }
        else if (name == "font-style")
        {
            if (value == "normal")
                style.FontSlant = RuntimeUiFontSlant::Normal;
            else if (value == "italic")
                style.FontSlant = RuntimeUiFontSlant::Italic;
            else if (value == "oblique")
                style.FontSlant = RuntimeUiFontSlant::Oblique;
            else
                throw std::runtime_error("UI font-style is invalid.");
        }
        else if (name == "line-height")
            style.LineHeight = value == "normal" ? 0.0F : Scalar(value);
        else if (name == "letter-spacing")
            style.LetterSpacing = value == "normal" ? 0.0F : Scalar(value);
        else if (name == "word-spacing")
            style.WordSpacing = value == "normal" ? 0.0F : Scalar(value);
        else if (name == "white-space")
        {
            if (value == "normal")
                style.TextWrap = RuntimeUiTextWrap::Normal;
            else if (value == "nowrap")
                style.TextWrap = RuntimeUiTextWrap::NoWrap;
            else
                throw std::runtime_error("UI white-space supports normal or nowrap.");
        }
        else if (name == "max-lines")
        {
            const auto lines = Scalar(value);
            if (lines < 0.0F || lines > 16'384.0F)
                throw std::runtime_error("UI max-lines must be between 0 and 16,384.");
            style.MaximumLines = static_cast<std::uint16_t>(lines);
        }
        else if (name == "text-overflow")
        {
            if (value == "clip")
                style.TextOverflow = RuntimeUiTextOverflow::Clip;
            else if (value == "ellipsis")
                style.TextOverflow = RuntimeUiTextOverflow::Ellipsis;
            else
                throw std::runtime_error("UI text-overflow supports clip or ellipsis.");
        }
        else if (name == "direction")
        {
            if (value == "auto")
                style.TextDirection = RuntimeUiTextDirection::Automatic;
            else if (value == "ltr")
                style.TextDirection = RuntimeUiTextDirection::LeftToRight;
            else if (value == "rtl")
                style.TextDirection = RuntimeUiTextDirection::RightToLeft;
            else
                throw std::runtime_error("UI direction supports auto, ltr, or rtl.");
        }
        else if (name == "language")
        {
            const auto language = Trim(value);
            if (language.empty() || language.size() > 64U ||
                !std::ranges::all_of(language, [](const unsigned char character)
                                     { return std::isalnum(character) || character == '-' || character == '_'; }))
            {
                throw std::runtime_error("UI language must be a bounded BCP-47-style identifier.");
            }
            style.Language = std::string(language);
        }
        else if (name == "transition-delay")
            SetDurations(style.TransitionDelays, style.TransitionDelayCount, value);
        else if (name == "transition-timing-function")
        {
            const auto values = CommaList(value);
            if (values.size() > style.TransitionEasings.size())
                throw std::runtime_error("UI transition timing list exceeds the limit of eight values.");
            style.TransitionEasingCount = static_cast<std::uint8_t>(values.size());
            for (std::size_t index = 0; index < values.size(); ++index)
                style.TransitionEasings[index] = Easing(values[index]);
        }
        else
            return false;
        return true;
    }
} // namespace Keire::Detail
