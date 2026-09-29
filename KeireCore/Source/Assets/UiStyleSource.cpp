#include "KeireInternal/Assets/UiStyleSource.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <unordered_set>
#include <utility>
#include <vector>

namespace Keire::Detail::UiStyleSource
{
    using Json = nlohmann::json;

    [[nodiscard]] std::string Trim(std::string_view value)
    {
        const auto first = value.find_first_not_of(" \t\r\n");
        if (first == std::string_view::npos)
            return {};
        const auto last = value.find_last_not_of(" \t\r\n");
        return std::string(value.substr(first, last - first + 1));
    }

    [[nodiscard]] UiStylePseudoState ParsePseudoState(const std::string_view value)
    {
        if (value == "hover")
            return UiStylePseudoState::Hover;
        if (value == "active")
            return UiStylePseudoState::Active;
        if (value == "focus")
            return UiStylePseudoState::Focus;
        if (value == "disabled")
            return UiStylePseudoState::Disabled;
        if (value == "checked")
            return UiStylePseudoState::Checked;
        if (value == "root")
            return UiStylePseudoState::Root;
        throw std::runtime_error("UI stylesheet contains an unsupported pseudo-state.");
    }

    [[nodiscard]] bool IdentifierCharacter(const char value) noexcept
    {
        return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') || (value >= '0' && value <= '9') ||
               value == '-' || value == '_';
    }

    [[nodiscard]] UiStyleRuleDefinition ParseSelector(std::string selector)
    {
        UiStyleRuleDefinition result;
        result.Selector = Trim(selector);
        if (result.Selector.empty() || result.Selector.find(',') != std::string::npos)
            throw std::runtime_error("UI stylesheet selector is empty or uses unsupported selector groups.");
        std::size_t cursor = 0;
        UiStyleCombinator nextCombinator = UiStyleCombinator::None;
        while (cursor < result.Selector.size())
        {
            bool consumedWhitespace = false;
            while (cursor < result.Selector.size() && std::isspace(static_cast<unsigned char>(result.Selector[cursor])))
            {
                consumedWhitespace = true;
                ++cursor;
            }
            if (cursor >= result.Selector.size())
                break;
            if (result.Selector[cursor] == '>')
            {
                if (result.Parts.empty() || nextCombinator == UiStyleCombinator::Child)
                    throw std::runtime_error("UI stylesheet contains an invalid child combinator.");
                nextCombinator = UiStyleCombinator::Child;
                ++cursor;
                continue;
            }
            if (consumedWhitespace && !result.Parts.empty() && nextCombinator == UiStyleCombinator::None)
                nextCombinator = UiStyleCombinator::Descendant;
            UiStyleSelectorPart part;
            part.Combinator = result.Parts.empty() ? UiStyleCombinator::None : nextCombinator;
            nextCombinator = UiStyleCombinator::None;
            bool hasTerm = false;
            while (cursor < result.Selector.size() && result.Selector[cursor] != '>' &&
                   !std::isspace(static_cast<unsigned char>(result.Selector[cursor])))
            {
                const auto prefix = result.Selector[cursor];
                if (prefix == '*' && !hasTerm)
                {
                    hasTerm = true;
                    ++cursor;
                    continue;
                }
                if (prefix != '#' && prefix != '.' && prefix != ':' && hasTerm && !part.Type.empty())
                    throw std::runtime_error("UI stylesheet selector term is malformed.");
                if (prefix == '#' || prefix == '.' || prefix == ':')
                    ++cursor;
                const auto begin = cursor;
                while (cursor < result.Selector.size() && IdentifierCharacter(result.Selector[cursor]))
                    ++cursor;
                if (begin == cursor)
                    throw std::runtime_error("UI stylesheet selector contains an empty term.");
                const auto term = result.Selector.substr(begin, cursor - begin);
                hasTerm = true;
                if (prefix == '#')
                {
                    if (!part.Name.empty())
                        throw std::runtime_error("UI stylesheet selector contains multiple names.");
                    part.Name = term;
                    result.Specificity += 100;
                }
                else if (prefix == '.')
                {
                    part.Classes.push_back(term);
                    result.Specificity += 10;
                }
                else if (prefix == ':')
                {
                    part.States = part.States | ParsePseudoState(term);
                    result.Specificity += 10;
                }
                else
                {
                    part.Type = term;
                    result.Specificity += 1;
                }
            }
            if (!hasTerm)
                throw std::runtime_error("UI stylesheet selector contains an empty compound selector.");
            result.Parts.push_back(std::move(part));
        }
        if (result.Parts.empty() || nextCombinator != UiStyleCombinator::None)
            throw std::runtime_error("UI stylesheet selector is incomplete.");
        return result;
    }

    [[nodiscard]] std::string StyleSourceMessage(const std::string_view source, const std::size_t offset,
                                                 const std::string_view message)
    {
        std::size_t line = 1U;
        std::size_t column = 1U;
        for (std::size_t cursor = 0; cursor < std::min(offset, source.size()); ++cursor)
        {
            if (source[cursor] == '\n')
            {
                ++line;
                column = 1U;
            }
            else
                ++column;
        }
        return "UI stylesheet line " + std::to_string(line) + ", column " + std::to_string(column) + ": " +
               std::string(message);
    }

    [[noreturn]] void ThrowStyleSourceError(const std::string_view source, const std::size_t offset,
                                            const std::string_view message)
    {
        throw std::runtime_error(StyleSourceMessage(source, offset, message));
    }

    [[nodiscard]] Json EncodeSelectorPart(const UiStyleSelectorPart& part)
    {
        return {{"combinator", static_cast<std::uint8_t>(part.Combinator)},
                {"type", part.Type},
                {"name", part.Name},
                {"classes", part.Classes},
                {"states", static_cast<std::uint16_t>(part.States)}};
    }

    [[nodiscard]] UiStyleSelectorPart DecodeSelectorPart(const Json& source)
    {
        UiStyleSelectorPart result;
        result.Combinator = static_cast<UiStyleCombinator>(source.at("combinator").get<std::uint8_t>());
        result.Type = source.value("type", std::string{});
        result.Name = source.value("name", std::string{});
        result.Classes = source.value("classes", std::vector<std::string>{});
        result.States = static_cast<UiStylePseudoState>(source.value("states", std::uint16_t{}));
        return result;
    }

    [[nodiscard]] const char* ToString(const UiStyleOrientation value) noexcept
    {
        switch (value)
        {
        case UiStyleOrientation::Any:
            return "any";
        case UiStyleOrientation::Landscape:
            return "landscape";
        case UiStyleOrientation::Portrait:
            return "portrait";
        }
        return "any";
    }

    [[nodiscard]] const char* ToString(const UiStylePointerPrecision value) noexcept
    {
        switch (value)
        {
        case UiStylePointerPrecision::Any:
            return "any";
        case UiStylePointerPrecision::Fine:
            return "fine";
        case UiStylePointerPrecision::Coarse:
            return "coarse";
        case UiStylePointerPrecision::None:
            return "none";
        }
        return "any";
    }

    [[nodiscard]] const char* ToString(const UiStyleNavigationMode value) noexcept
    {
        switch (value)
        {
        case UiStyleNavigationMode::Any:
            return "any";
        case UiStyleNavigationMode::Pointer:
            return "pointer";
        case UiStyleNavigationMode::Keyboard:
            return "keyboard";
        case UiStyleNavigationMode::Gamepad:
            return "gamepad";
        }
        return "any";
    }

    [[nodiscard]] float ParseMediaScalar(std::string value, const std::string_view suffix,
                                         const std::string_view property)
    {
        value = Trim(value);
        if (!suffix.empty() && value.ends_with(suffix))
            value.resize(value.size() - suffix.size());
        float result = 0.0F;
        const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), result);
        if (error != std::errc{} || end != value.data() + value.size() || !std::isfinite(result) || result < 0.0F)
            throw std::runtime_error("UI stylesheet media condition '" + std::string(property) +
                                     "' requires a non-negative finite value.");
        return result;
    }

    [[nodiscard]] float ParseMediaRatio(const std::string_view value, const std::string_view property)
    {
        const auto separator = value.find('/');
        if (separator == std::string_view::npos)
            return ParseMediaScalar(std::string(value), {}, property);
        const auto numerator = ParseMediaScalar(std::string(value.substr(0, separator)), {}, property);
        const auto denominator = ParseMediaScalar(std::string(value.substr(separator + 1)), {}, property);
        if (denominator <= 0.0F)
            throw std::runtime_error("UI stylesheet media aspect-ratio denominator must be positive.");
        return numerator / denominator;
    }

    [[nodiscard]] UiStyleMediaCondition ParseMediaCondition(const std::string_view source)
    {
        UiStyleMediaCondition result;
        std::unordered_set<std::string> names;
        std::size_t cursor = 0;
        while (cursor < source.size())
        {
            cursor = source.find_first_not_of(" \t\r\n", cursor);
            if (cursor == std::string_view::npos)
                break;
            if (source[cursor] != '(')
                throw std::runtime_error("UI stylesheet media conditions must be parenthesized and joined by 'and'.");
            const auto close = source.find(')', cursor + 1);
            if (close == std::string_view::npos)
                throw std::runtime_error("UI stylesheet contains an unterminated media condition.");
            const auto condition = Trim(source.substr(cursor + 1, close - cursor - 1));
            const auto colon = condition.find(':');
            if (colon == std::string::npos)
                throw std::runtime_error("UI stylesheet media condition is missing ':'.");
            const auto name = Trim(std::string_view(condition).substr(0, colon));
            const auto value = Trim(std::string_view(condition).substr(colon + 1));
            if (name.empty() || value.empty() || !names.insert(name).second)
                throw std::runtime_error("UI stylesheet media condition is empty or duplicated.");

            if (name == "min-width")
                result.MinimumWidth = ParseMediaScalar(value, "px", name);
            else if (name == "max-width")
                result.MaximumWidth = ParseMediaScalar(value, "px", name);
            else if (name == "min-height")
                result.MinimumHeight = ParseMediaScalar(value, "px", name);
            else if (name == "max-height")
                result.MaximumHeight = ParseMediaScalar(value, "px", name);
            else if (name == "min-aspect-ratio")
                result.MinimumAspectRatio = ParseMediaRatio(value, name);
            else if (name == "max-aspect-ratio")
                result.MaximumAspectRatio = ParseMediaRatio(value, name);
            else if (name == "min-dpi")
                result.MinimumDpi = ParseMediaScalar(value, "dpi", name);
            else if (name == "max-dpi")
                result.MaximumDpi = ParseMediaScalar(value, "dpi", name);
            else if (name == "orientation")
            {
                if (value == "landscape")
                    result.Orientation = UiStyleOrientation::Landscape;
                else if (value == "portrait")
                    result.Orientation = UiStyleOrientation::Portrait;
                else
                    throw std::runtime_error("UI stylesheet media orientation must be landscape or portrait.");
            }
            else if (name == "pointer")
            {
                if (value == "fine")
                    result.Pointer = UiStylePointerPrecision::Fine;
                else if (value == "coarse")
                    result.Pointer = UiStylePointerPrecision::Coarse;
                else if (value == "none")
                    result.Pointer = UiStylePointerPrecision::None;
                else
                    throw std::runtime_error("UI stylesheet media pointer must be fine, coarse, or none.");
            }
            else if (name == "navigation")
            {
                if (value == "pointer")
                    result.Navigation = UiStyleNavigationMode::Pointer;
                else if (value == "keyboard")
                    result.Navigation = UiStyleNavigationMode::Keyboard;
                else if (value == "gamepad")
                    result.Navigation = UiStyleNavigationMode::Gamepad;
                else
                    throw std::runtime_error("UI stylesheet media navigation must be pointer, keyboard, or gamepad.");
            }
            else if (name == "prefers-reduced-motion")
            {
                if (value == "reduce")
                    result.ReducedMotion = true;
                else if (value == "no-preference")
                    result.ReducedMotion = false;
                else
                    throw std::runtime_error(
                        "UI stylesheet reduced-motion preference must be reduce or no-preference.");
            }
            else
                throw std::runtime_error("UI stylesheet contains an unsupported media condition: " + name);

            cursor = source.find_first_not_of(" \t\r\n", close + 1);
            if (cursor == std::string_view::npos)
                break;
            if (source.substr(cursor, 3) != "and" ||
                (cursor + 3 < source.size() && !std::isspace(static_cast<unsigned char>(source[cursor + 3])) &&
                 source[cursor + 3] != '('))
                throw std::runtime_error("UI stylesheet media conditions must be joined by 'and'.");
            cursor += 3;
        }
        if (result.Empty())
            throw std::runtime_error("UI stylesheet media block requires at least one condition.");
        return result;
    }

    [[nodiscard]] std::string EncodeMediaCondition(const UiStyleMediaCondition& condition)
    {
        std::vector<std::string> values;
        const auto scalar =
            [&values](const std::string_view name, const std::optional<float> value, const std::string_view suffix)
        {
            if (value)
            {
                std::array<char, 32> storage{};
                const auto [end, error] =
                    std::to_chars(storage.data(), storage.data() + storage.size(), *value, std::chars_format::general);
                if (error != std::errc{})
                    throw std::runtime_error("UI stylesheet media value could not be encoded.");
                values.push_back("(" + std::string(name) + ": " + std::string(storage.data(), end) +
                                 std::string(suffix) + ")");
            }
        };
        scalar("min-width", condition.MinimumWidth, "px");
        scalar("max-width", condition.MaximumWidth, "px");
        scalar("min-height", condition.MinimumHeight, "px");
        scalar("max-height", condition.MaximumHeight, "px");
        scalar("min-aspect-ratio", condition.MinimumAspectRatio, {});
        scalar("max-aspect-ratio", condition.MaximumAspectRatio, {});
        scalar("min-dpi", condition.MinimumDpi, "dpi");
        scalar("max-dpi", condition.MaximumDpi, "dpi");
        if (condition.Orientation != UiStyleOrientation::Any)
            values.push_back("(orientation: " + std::string(ToString(condition.Orientation)) + ")");
        if (condition.Pointer != UiStylePointerPrecision::Any)
            values.push_back("(pointer: " + std::string(ToString(condition.Pointer)) + ")");
        if (condition.Navigation != UiStyleNavigationMode::Any)
            values.push_back("(navigation: " + std::string(ToString(condition.Navigation)) + ")");
        if (condition.ReducedMotion)
            values.push_back(std::string("(prefers-reduced-motion: ") +
                             (*condition.ReducedMotion ? "reduce)" : "no-preference)"));
        std::string result;
        for (const auto& value : values)
        {
            if (!result.empty())
                result += " and ";
            result += value;
        }
        return result;
    }

    [[nodiscard]] Json EncodeMediaConditionJson(const UiStyleMediaCondition& condition)
    {
        Json result = Json::object();
        const auto optional = [&result](const char* name, const std::optional<float> value)
        {
            if (value)
                result[name] = *value;
        };
        optional("minimumWidth", condition.MinimumWidth);
        optional("maximumWidth", condition.MaximumWidth);
        optional("minimumHeight", condition.MinimumHeight);
        optional("maximumHeight", condition.MaximumHeight);
        optional("minimumAspectRatio", condition.MinimumAspectRatio);
        optional("maximumAspectRatio", condition.MaximumAspectRatio);
        optional("minimumDpi", condition.MinimumDpi);
        optional("maximumDpi", condition.MaximumDpi);
        result["orientation"] = static_cast<std::uint8_t>(condition.Orientation);
        result["pointer"] = static_cast<std::uint8_t>(condition.Pointer);
        result["navigation"] = static_cast<std::uint8_t>(condition.Navigation);
        if (condition.ReducedMotion)
            result["reducedMotion"] = *condition.ReducedMotion;
        return result;
    }

    [[nodiscard]] UiStyleMediaCondition DecodeMediaConditionJson(const Json& source)
    {
        UiStyleMediaCondition result;
        const auto optional = [&source](const char* name) -> std::optional<float>
        {
            const auto found = source.find(name);
            return found == source.end() ? std::nullopt : std::optional(found->get<float>());
        };
        result.MinimumWidth = optional("minimumWidth");
        result.MaximumWidth = optional("maximumWidth");
        result.MinimumHeight = optional("minimumHeight");
        result.MaximumHeight = optional("maximumHeight");
        result.MinimumAspectRatio = optional("minimumAspectRatio");
        result.MaximumAspectRatio = optional("maximumAspectRatio");
        result.MinimumDpi = optional("minimumDpi");
        result.MaximumDpi = optional("maximumDpi");
        result.Orientation = static_cast<UiStyleOrientation>(source.value("orientation", std::uint8_t{}));
        result.Pointer = static_cast<UiStylePointerPrecision>(source.value("pointer", std::uint8_t{}));
        result.Navigation = static_cast<UiStyleNavigationMode>(source.value("navigation", std::uint8_t{}));
        if (const auto found = source.find("reducedMotion"); found != source.end())
            result.ReducedMotion = found->get<bool>();
        return result;
    }

} // namespace Keire::Detail::UiStyleSource
