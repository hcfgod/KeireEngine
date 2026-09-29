#include "Keire/Ui/UiToolkit.h"

#include "Keire/Ui/UiStyleProperties.h"

#include "Keire/Assets/AssetPipeline.h"
#include "KeireInternal/Assets/AssetInternal.h"
#include "KeireInternal/Assets/UiStyleSource.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstring>
#include <functional>
#include <limits>
#include <ranges>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace Keire
{
    namespace
    {
        using Json = nlohmann::json;
        using namespace Detail::UiStyleSource;

        struct XmlTag
        {
            std::string Name;
            std::vector<UiNamedValue> Attributes;
            bool Closing = false;
            bool SelfClosing = false;
        };

        [[nodiscard]] std::string_view Text(const std::span<const std::byte> bytes)
        {
            auto result = std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size());
            if (result.starts_with("\xEF\xBB\xBF"))
                result.remove_prefix(3U);
            return result;
        }

        [[nodiscard]] std::vector<std::byte> Bytes(const std::string_view text)
        {
            std::vector<std::byte> result(text.size());
            std::memcpy(result.data(), text.data(), text.size());
            return result;
        }

        [[nodiscard]] const std::string* NamedValue(const std::vector<UiNamedValue>& values,
                                                    const std::string_view name) noexcept
        {
            const auto found = std::ranges::find(values, name, &UiNamedValue::Name);
            return found == values.end() ? nullptr : &found->Value;
        }

        void AppendUtf8(std::string& output, const std::uint32_t codePoint)
        {
            if (codePoint <= 0x7FU)
            {
                output += static_cast<char>(codePoint);
                return;
            }
            if (codePoint <= 0x7FFU)
            {
                output += static_cast<char>(0xC0U | codePoint >> 6U);
                output += static_cast<char>(0x80U | codePoint & 0x3FU);
                return;
            }
            if (codePoint <= 0xFFFFU)
            {
                output += static_cast<char>(0xE0U | codePoint >> 12U);
                output += static_cast<char>(0x80U | codePoint >> 6U & 0x3FU);
                output += static_cast<char>(0x80U | codePoint & 0x3FU);
                return;
            }
            output += static_cast<char>(0xF0U | codePoint >> 18U);
            output += static_cast<char>(0x80U | codePoint >> 12U & 0x3FU);
            output += static_cast<char>(0x80U | codePoint >> 6U & 0x3FU);
            output += static_cast<char>(0x80U | codePoint & 0x3FU);
        }

        [[nodiscard]] std::string DecodeXml(const std::string_view value)
        {
            constexpr std::array<std::pair<std::string_view, std::string_view>, 5> namedEntities{{
                {"quot", "\""},
                {"apos", "'"},
                {"lt", "<"},
                {"gt", ">"},
                {"amp", "&"},
            }};
            std::string result;
            result.reserve(value.size());
            for (std::size_t cursor = 0; cursor < value.size();)
            {
                if (value[cursor] != '&')
                {
                    result += value[cursor++];
                    continue;
                }
                const auto semicolon = value.find(';', cursor + 1U);
                if (semicolon == std::string_view::npos)
                    throw std::runtime_error("UI document contains an unterminated XML entity.");
                const auto entity = value.substr(cursor + 1U, semicolon - cursor - 1U);
                const auto named =
                    std::ranges::find_if(namedEntities, [entity](const auto& value) { return value.first == entity; });
                if (named != namedEntities.end())
                {
                    result += named->second;
                }
                else if (entity.starts_with('#'))
                {
                    const bool hexadecimal = entity.size() > 2U && (entity[1] == 'x' || entity[1] == 'X');
                    const auto digits = entity.substr(hexadecimal ? 2U : 1U);
                    std::uint32_t codePoint = 0;
                    const auto [end, error] =
                        std::from_chars(digits.data(), digits.data() + digits.size(), codePoint, hexadecimal ? 16 : 10);
                    const bool validXmlCharacter = codePoint == 0x9U || codePoint == 0xAU || codePoint == 0xDU ||
                                                   (codePoint >= 0x20U && codePoint <= 0xD7FFU) ||
                                                   (codePoint >= 0xE000U && codePoint <= 0xFFFDU) ||
                                                   (codePoint >= 0x10000U && codePoint <= 0x10FFFFU);
                    if (digits.empty() || error != std::errc{} || end != digits.data() + digits.size() ||
                        !validXmlCharacter)
                    {
                        throw std::runtime_error("UI document contains an invalid numeric XML entity.");
                    }
                    AppendUtf8(result, codePoint);
                }
                else
                {
                    throw std::runtime_error("UI document contains an unsupported XML entity.");
                }
                cursor = semicolon + 1U;
            }
            return result;
        }

        [[nodiscard]] std::size_t FindXmlTagEnd(const std::string_view source, const std::size_t begin) noexcept
        {
            char quote = 0;
            for (auto cursor = begin; cursor < source.size(); ++cursor)
            {
                if (quote != 0)
                {
                    if (source[cursor] == quote)
                        quote = 0;
                    continue;
                }
                if (source[cursor] == '\'' || source[cursor] == '"')
                    quote = source[cursor];
                else if (source[cursor] == '>')
                    return cursor;
            }
            return std::string_view::npos;
        }

        [[nodiscard]] bool ValidXmlName(const std::string_view value) noexcept
        {
            if (value.empty())
                return false;
            const auto first = static_cast<unsigned char>(value.front());
            if (std::isalpha(first) == 0 && value.front() != '_' && value.front() != ':')
                return false;
            return std::ranges::all_of(value.substr(1U),
                                       [](const unsigned char character)
                                       {
                                           return std::isalnum(character) != 0 || character == '_' ||
                                                  character == ':' || character == '-' || character == '.';
                                       });
        }

        [[nodiscard]] std::vector<XmlTag> ParseXmlTags(const std::string_view source)
        {
            std::vector<XmlTag> result;
            std::size_t cursor = 0;
            while (cursor < source.size())
            {
                const auto open = source.find('<', cursor);
                if (open == std::string_view::npos)
                {
                    if (!Trim(source.substr(cursor)).empty())
                        throw std::runtime_error("UI document text must be stored in element attributes.");
                    break;
                }
                if (!Trim(source.substr(cursor, open - cursor)).empty())
                    throw std::runtime_error("UI document text must be stored in element attributes.");
                if (source.substr(open, 4) == "<!--")
                {
                    const auto close = source.find("-->", open + 4);
                    if (close == std::string_view::npos)
                        throw std::runtime_error("UI document contains an unterminated comment.");
                    if (source.substr(open + 4U, close - open - 4U).find("--") != std::string_view::npos)
                        throw std::runtime_error("UI document comments cannot contain '--'.");
                    cursor = close + 3;
                    continue;
                }
                if (source.substr(open, 2) == "<?")
                {
                    const auto close = source.find("?>", open + 2);
                    if (close == std::string_view::npos)
                        throw std::runtime_error("UI document contains an unterminated declaration.");
                    if (!result.empty() || !Trim(source.substr(0, open)).empty() ||
                        source.substr(open, 5U) != "<?xml" || open + 5U >= source.size() ||
                        std::isspace(static_cast<unsigned char>(source[open + 5U])) == 0)
                        throw std::runtime_error("UI document contains an unsupported processing instruction.");
                    cursor = close + 2;
                    continue;
                }
                if (source.substr(open, 2) == "<!")
                    throw std::runtime_error("UI documents do not support DTD or CDATA declarations.");
                const auto close = FindXmlTagEnd(source, open + 1U);
                if (close == std::string_view::npos)
                    throw std::runtime_error("UI document contains an unterminated tag.");
                auto body = Trim(source.substr(open + 1, close - open - 1));
                if (body.empty())
                    throw std::runtime_error("UI document contains an empty tag.");
                XmlTag tag;
                if (body.front() == '/')
                {
                    tag.Closing = true;
                    body = Trim(std::string_view(body).substr(1));
                }
                if (!tag.Closing && body.back() == '/')
                {
                    tag.SelfClosing = true;
                    body = Trim(std::string_view(body).substr(0, body.size() - 1));
                }
                const auto nameEnd = body.find_first_of(" \t\r\n");
                tag.Name = body.substr(0, nameEnd);
                if (!ValidXmlName(tag.Name))
                    throw std::runtime_error("UI document contains an invalid tag name.");
                if (tag.Closing)
                {
                    if (nameEnd != std::string::npos)
                        throw std::runtime_error("UI document closing tags cannot contain attributes.");
                }
                else
                {
                    std::size_t attributeCursor = nameEnd == std::string::npos ? body.size() : nameEnd;
                    while (attributeCursor < body.size())
                    {
                        attributeCursor = body.find_first_not_of(" \t\r\n", attributeCursor);
                        if (attributeCursor == std::string::npos)
                            break;
                        const auto equals = body.find('=', attributeCursor);
                        if (equals == std::string::npos)
                            throw std::runtime_error("UI document attribute is missing '='.");
                        const auto name =
                            Trim(std::string_view(body).substr(attributeCursor, equals - attributeCursor));
                        if (!ValidXmlName(name))
                            throw std::runtime_error("UI document contains an invalid attribute name.");
                        const auto quotePosition = body.find_first_not_of(" \t\r\n", equals + 1);
                        if (quotePosition == std::string::npos ||
                            (body[quotePosition] != '\'' && body[quotePosition] != '\"'))
                            throw std::runtime_error("UI document attribute values must be quoted.");
                        const auto quote = body[quotePosition];
                        const auto valueEnd = body.find(quote, quotePosition + 1);
                        if (valueEnd == std::string::npos)
                            throw std::runtime_error("UI document contains an unterminated attribute.");
                        if (body.substr(quotePosition + 1U, valueEnd - quotePosition - 1U).find('<') !=
                            std::string_view::npos)
                            throw std::runtime_error("UI document attribute values must escape '<' as '&lt;'.");
                        if (std::ranges::find(tag.Attributes, name, &UiNamedValue::Name) != tag.Attributes.end())
                            throw std::runtime_error("UI document contains a duplicate attribute.");
                        tag.Attributes.push_back(
                            {name, DecodeXml(body.substr(quotePosition + 1, valueEnd - quotePosition - 1))});
                        attributeCursor = valueEnd + 1;
                    }
                }
                result.push_back(std::move(tag));
                cursor = close + 1;
            }
            return result;
        }

        [[nodiscard]] const std::string* Attribute(const XmlTag& tag, const std::string_view name) noexcept
        {
            const auto found = std::ranges::find(tag.Attributes, name, &UiNamedValue::Name);
            return found == tag.Attributes.end() ? nullptr : &found->Value;
        }

        [[nodiscard]] UiVisualElementType ParseElementType(const std::string_view name)
        {
            const std::pair<std::string_view, UiVisualElementType> types[] = {
                {"VisualElement", UiVisualElementType::VisualElement},
                {"TemplateContainer", UiVisualElementType::TemplateContainer},
                {"Slot", UiVisualElementType::Slot},
                {"Label", UiVisualElementType::Label},
                {"Image", UiVisualElementType::Image},
                {"Button", UiVisualElementType::Button},
                {"TextField", UiVisualElementType::TextField},
                {"Toggle", UiVisualElementType::Toggle},
                {"Slider", UiVisualElementType::Slider},
                {"ProgressBar", UiVisualElementType::ProgressBar},
                {"ScrollView", UiVisualElementType::ScrollView},
                {"ListView", UiVisualElementType::ListView},
                {"TreeView", UiVisualElementType::TreeView},
                {"DropdownField", UiVisualElementType::DropdownField},
                {"Foldout", UiVisualElementType::Foldout},
                {"TabView", UiVisualElementType::TabView},
                {"Toolbar", UiVisualElementType::Toolbar},
                {"Spacer", UiVisualElementType::Spacer},
            };
            const auto found = std::ranges::find_if(types, [&](const auto& value) { return value.first == name; });
            return found == std::end(types) ? UiVisualElementType::Custom : found->second;
        }

        [[nodiscard]] std::string EncodeXml(std::string_view value)
        {
            std::string result;
            result.reserve(value.size());
            for (const auto character : value)
            {
                switch (character)
                {
                case '&':
                    result += "&amp;";
                    break;
                case '\"':
                    result += "&quot;";
                    break;
                case '<':
                    result += "&lt;";
                    break;
                case '>':
                    result += "&gt;";
                    break;
                default:
                    result += character;
                    break;
                }
            }
            return result;
        }

        [[nodiscard]] const char* ToString(const UiVisualElementType value) noexcept
        {
            switch (value)
            {
            case UiVisualElementType::VisualElement:
                return "VisualElement";
            case UiVisualElementType::TemplateContainer:
                return "TemplateContainer";
            case UiVisualElementType::Slot:
                return "Slot";
            case UiVisualElementType::Label:
                return "Label";
            case UiVisualElementType::Image:
                return "Image";
            case UiVisualElementType::Button:
                return "Button";
            case UiVisualElementType::TextField:
                return "TextField";
            case UiVisualElementType::Toggle:
                return "Toggle";
            case UiVisualElementType::Slider:
                return "Slider";
            case UiVisualElementType::ProgressBar:
                return "ProgressBar";
            case UiVisualElementType::ScrollView:
                return "ScrollView";
            case UiVisualElementType::ListView:
                return "ListView";
            case UiVisualElementType::TreeView:
                return "TreeView";
            case UiVisualElementType::DropdownField:
                return "DropdownField";
            case UiVisualElementType::Foldout:
                return "Foldout";
            case UiVisualElementType::TabView:
                return "TabView";
            case UiVisualElementType::Toolbar:
                return "Toolbar";
            case UiVisualElementType::Spacer:
                return "Spacer";
            case UiVisualElementType::Custom:
                return "Custom";
            }
            return "VisualElement";
        }

        [[nodiscard]] UiVisualElementType ParseEncodedElementType(const std::string_view name)
        {
            if (name == "Custom")
                return UiVisualElementType::Custom;
            const auto type = ParseElementType(name);
            if (type == UiVisualElementType::Custom)
                throw std::runtime_error("UI visual tree contains an unsupported element type.");
            return type;
        }

        [[nodiscard]] std::vector<std::string> SplitClasses(const std::string_view value)
        {
            std::vector<std::string> result;
            std::size_t cursor = 0;
            while (cursor < value.size())
            {
                const auto begin = value.find_first_not_of(" \t\r\n", cursor);
                if (begin == std::string_view::npos)
                    break;
                const auto end = value.find_first_of(" \t\r\n", begin);
                result.emplace_back(value.substr(begin, end - begin));
                cursor = end == std::string_view::npos ? value.size() : end;
            }
            return result;
        }

        [[nodiscard]] std::string RemoveStyleComments(const std::string_view source)
        {
            std::string result(source);
            char quote = 0;
            bool escaped = false;
            for (std::size_t cursor = 0; cursor < source.size(); ++cursor)
            {
                const auto character = source[cursor];
                if (quote != 0)
                {
                    if (escaped)
                        escaped = false;
                    else if (character == '\\')
                        escaped = true;
                    else if (character == quote)
                        quote = 0;
                    continue;
                }
                if (character == '"' || character == '\'')
                {
                    quote = character;
                    continue;
                }
                if (character != '/' || cursor + 1U >= source.size() || source[cursor + 1U] != '*')
                    continue;
                const auto close = source.find("*/", cursor + 2U);
                if (close == std::string_view::npos)
                    throw std::runtime_error(
                        StyleSourceMessage(source, cursor, "UI style source contains an unterminated comment."));
                for (auto index = cursor; index < close + 2U; ++index)
                    if (result[index] != '\r' && result[index] != '\n')
                        result[index] = ' ';
                cursor = close + 1U;
            }
            return result;
        }

        [[nodiscard]] std::vector<UiNamedValue> ParseDeclarations(const std::string_view source)
        {
            const auto value = RemoveStyleComments(source);
            std::vector<UiNamedValue> result;
            std::size_t begin = 0;
            std::size_t colon = std::string_view::npos;
            std::size_t parenthesisDepth = 0;
            char quote = 0;
            bool escaped = false;
            const auto append = [&](const std::size_t end)
            {
                const auto declaration = Trim(std::string_view(value).substr(begin, end - begin));
                if (!declaration.empty())
                {
                    if (colon == std::string_view::npos || colon >= end)
                        throw std::runtime_error("UI style declaration is missing ':'.");
                    auto name = Trim(std::string_view(value).substr(begin, colon - begin));
                    auto propertyValue = Trim(std::string_view(value).substr(colon + 1U, end - colon - 1U));
                    if (name.empty() || propertyValue.empty() ||
                        std::ranges::find(result, name, &UiNamedValue::Name) != result.end())
                        throw std::runtime_error("UI style declaration is empty or duplicated.");
                    result.push_back({std::move(name), std::move(propertyValue)});
                }
            };
            for (std::size_t cursor = 0; cursor < value.size(); ++cursor)
            {
                const auto character = value[cursor];
                if (quote != 0)
                {
                    if (escaped)
                        escaped = false;
                    else if (character == '\\')
                        escaped = true;
                    else if (character == quote)
                        quote = 0;
                    continue;
                }
                if (character == '"' || character == '\'')
                    quote = character;
                else if (character == '(')
                    ++parenthesisDepth;
                else if (character == ')')
                {
                    if (parenthesisDepth == 0U)
                        throw std::runtime_error("UI style declaration contains an unmatched ')'.");
                    --parenthesisDepth;
                }
                else if (character == ':' && parenthesisDepth == 0U && colon == std::string_view::npos)
                    colon = cursor;
                else if (character == ';' && parenthesisDepth == 0U)
                {
                    append(cursor);
                    begin = cursor + 1U;
                    colon = std::string_view::npos;
                }
            }
            if (quote != 0)
                throw std::runtime_error("UI style declaration contains an unterminated string.");
            if (parenthesisDepth != 0U)
                throw std::runtime_error("UI style declaration contains an unterminated function.");
            append(value.size());
            return result;
        }

        [[nodiscard]] AssetId DeriveAutomaticElementId(const Detail::Sha256Digest& documentDigest,
                                                       const std::size_t elementOrdinal) noexcept
        {
            std::array<std::byte, 40> identity{};
            std::ranges::copy(documentDigest, identity.begin());
            const auto ordinal = static_cast<std::uint64_t>(elementOrdinal);
            for (std::size_t index = 0; index < sizeof(ordinal); ++index)
            {
                const auto shift = static_cast<unsigned int>((sizeof(ordinal) - index - 1U) * 8U);
                identity[documentDigest.size() + index] = static_cast<std::byte>((ordinal >> shift) & 0xffU);
            }

            const auto digest = Detail::Sha256(identity);
            std::uint64_t high = 0;
            std::uint64_t low = 0;
            for (std::size_t index = 0; index < 8U; ++index)
            {
                high = (high << 8U) | std::to_integer<std::uint8_t>(digest[index]);
                low = (low << 8U) | std::to_integer<std::uint8_t>(digest[index + 8U]);
            }
            high = (high & 0xffffffffffff0fffULL) | 0x0000000000005000ULL;
            low = (low & 0x3fffffffffffffffULL) | 0x8000000000000000ULL;
            return {high, low};
        }

        [[nodiscard]] UiVisualElementDefinition ParseElement(const std::vector<XmlTag>& tags, std::size_t& cursor,
                                                             const std::size_t depth,
                                                             const Detail::Sha256Digest& documentDigest,
                                                             std::size_t& elementOrdinal)
        {
            if (depth > MaximumUiTreeDepth || cursor >= tags.size() || tags[cursor].Closing)
                throw std::runtime_error("UI document element nesting is invalid or exceeds the safety limit.");
            const auto& tag = tags[cursor++];
            UiVisualElementDefinition result;
            result.StableId = DeriveAutomaticElementId(documentDigest, elementOrdinal++);
            result.Type = ParseElementType(tag.Name);
            if (result.Type == UiVisualElementType::Custom)
                result.CustomType = tag.Name;
            if (const auto* id = Attribute(tag, "id"))
            {
                try
                {
                    result.StableId = AssetId::Parse(*id);
                }
                catch (const std::invalid_argument& error)
                {
                    throw std::runtime_error("UI element <" + tag.Name + "> has an invalid id: " + error.what());
                }
            }
            if (const auto* name = Attribute(tag, "name"))
                result.Name = *name;
            if (const auto* classes = Attribute(tag, "class"))
                result.Classes = SplitClasses(*classes);
            if (const auto* style = Attribute(tag, "style"))
                result.InlineStyles = ParseDeclarations(*style);
            if (const auto* value = Attribute(tag, "template"))
                result.Template = AssetId::Parse(*value);
            if (const auto* value = Attribute(tag, "slot"))
                result.Slot = *value;
            for (const auto& attribute : tag.Attributes)
            {
                if (attribute.Name == "id" || attribute.Name == "name" || attribute.Name == "class" ||
                    attribute.Name == "style" || attribute.Name == "template" || attribute.Name == "slot")
                    continue;
                constexpr std::string_view BindingPrefix = "bind:";
                constexpr std::string_view TwoWayBindingPrefix = "bind-two-way:";
                constexpr std::string_view OneTimeBindingPrefix = "bind-one-time:";
                if (attribute.Name.starts_with(TwoWayBindingPrefix))
                    result.Bindings.push_back(
                        {attribute.Name.substr(TwoWayBindingPrefix.size()), attribute.Value, "TwoWay"});
                else if (attribute.Name.starts_with(OneTimeBindingPrefix))
                    result.Bindings.push_back(
                        {attribute.Name.substr(OneTimeBindingPrefix.size()), attribute.Value, "OneTime"});
                else if (attribute.Name.starts_with(BindingPrefix))
                    result.Bindings.push_back({attribute.Name.substr(BindingPrefix.size()), attribute.Value, "OneWay"});
                else
                    result.Attributes.push_back(attribute);
            }
            if (tag.SelfClosing)
                return result;
            while (cursor < tags.size() && !tags[cursor].Closing)
                result.Children.push_back(ParseElement(tags, cursor, depth + 1, documentDigest, elementOrdinal));
            if (cursor >= tags.size() || tags[cursor].Name != tag.Name)
                throw std::runtime_error("UI document element tags are not balanced.");
            ++cursor;
            return result;
        }

        [[nodiscard]] Json EncodeNamedValues(const std::vector<UiNamedValue>& values)
        {
            Json result = Json::array();
            for (const auto& value : values)
                result.push_back({{"name", value.Name}, {"value", value.Value}});
            return result;
        }

        [[nodiscard]] std::vector<UiNamedValue> DecodeNamedValues(const Json& values)
        {
            std::vector<UiNamedValue> result;
            if (!values.is_array())
                throw std::runtime_error("UI named-value collection must be an array.");
            for (const auto& value : values)
                result.push_back({value.at("name").get<std::string>(), value.at("value").get<std::string>()});
            return result;
        }

        [[nodiscard]] Json EncodeElement(const UiVisualElementDefinition& element)
        {
            Json children = Json::array();
            for (const auto& child : element.Children)
                children.push_back(EncodeElement(child));
            Json bindings = Json::array();
            for (const auto& binding : element.Bindings)
                bindings.push_back({{"property", binding.Property}, {"path", binding.Path}, {"mode", binding.Mode}});
            return {{"id", element.StableId.ToString()},
                    {"type", ToString(element.Type)},
                    {"customType", element.CustomType},
                    {"name", element.Name},
                    {"classes", element.Classes},
                    {"attributes", EncodeNamedValues(element.Attributes)},
                    {"inlineStyles", EncodeNamedValues(element.InlineStyles)},
                    {"bindings", std::move(bindings)},
                    {"template", element.Template.ToString()},
                    {"slot", element.Slot},
                    {"children", std::move(children)}};
        }

        void EncodeSourceElement(const UiVisualElementDefinition& element, const std::size_t depth, std::string& output)
        {
            output.append(depth * 2, ' ');
            const auto type = element.Type == UiVisualElementType::Custom ? element.CustomType : ToString(element.Type);
            output += '<';
            output += type;
            output += " id=\"" + element.StableId.ToString() + "\"";
            if (!element.Name.empty())
                output += " name=\"" + EncodeXml(element.Name) + "\"";
            if (!element.Classes.empty())
            {
                output += " class=\"";
                for (std::size_t index = 0; index < element.Classes.size(); ++index)
                {
                    if (index != 0)
                        output += ' ';
                    output += EncodeXml(element.Classes[index]);
                }
                output += '\"';
            }
            if (!element.InlineStyles.empty())
            {
                output += " style=\"";
                for (const auto& property : element.InlineStyles)
                    output += EncodeXml(property.Name) + ": " + EncodeXml(property.Value) + "; ";
                output += '\"';
            }
            if (element.Template)
                output += " template=\"" + element.Template.ToString() + "\"";
            if (!element.Slot.empty())
                output += " slot=\"" + EncodeXml(element.Slot) + "\"";
            for (const auto& binding : element.Bindings)
            {
                const auto prefix = binding.Mode == "TwoWay"    ? " bind-two-way:"
                                    : binding.Mode == "OneTime" ? " bind-one-time:"
                                                                : " bind:";
                output += prefix + binding.Property + "=\"" + EncodeXml(binding.Path) + "\"";
            }
            for (const auto& attribute : element.Attributes)
                output += ' ' + attribute.Name + "=\"" + EncodeXml(attribute.Value) + "\"";
            if (element.Children.empty())
            {
                output += "/>\n";
                return;
            }
            output += ">\n";
            for (const auto& child : element.Children)
                EncodeSourceElement(child, depth + 1, output);
            output.append(depth * 2, ' ');
            output += "</" + std::string(type) + ">\n";
        }

        [[nodiscard]] UiVisualElementDefinition DecodeElement(const Json& source)
        {
            UiVisualElementDefinition result;
            result.StableId = AssetId::Parse(source.at("id").get<std::string>());
            result.Type = ParseEncodedElementType(source.at("type").get<std::string>());
            result.CustomType = source.value("customType", std::string{});
            result.Name = source.value("name", std::string{});
            result.Classes = source.value("classes", std::vector<std::string>{});
            result.Attributes = DecodeNamedValues(source.value("attributes", Json::array()));
            result.InlineStyles = DecodeNamedValues(source.value("inlineStyles", Json::array()));
            for (const auto& binding : source.value("bindings", Json::array()))
                result.Bindings.push_back({binding.at("property").get<std::string>(),
                                           binding.at("path").get<std::string>(),
                                           binding.value("mode", std::string("OneWay"))});
            result.Template =
                AssetId::Parse(source.value("template", std::string("00000000-0000-0000-0000-000000000000")));
            result.Slot = source.value("slot", std::string{});
            for (const auto& child : source.at("children"))
                result.Children.push_back(DecodeElement(child));
            return result;
        }

        template <typename Callback>
        void VisitElements(const UiVisualElementDefinition& element, const std::size_t depth, Callback&& callback)
        {
            callback(element, depth);
            for (const auto& child : element.Children)
                VisitElements(child, depth + 1, callback);
        }

        [[nodiscard]] std::vector<AssetId> UiStyleAssetDependencies(const UiStyleSheetDefinition& definition)
        {
            std::vector<AssetId> result;
            for (const auto& rule : definition.Rules)
            {
                for (const auto& property : rule.Properties)
                {
                    char quote = 0;
                    bool escaped = false;
                    for (std::size_t cursor = 0; cursor < property.Value.size(); ++cursor)
                    {
                        const auto character = property.Value[cursor];
                        if (quote != 0)
                        {
                            if (escaped)
                                escaped = false;
                            else if (character == '\\')
                                escaped = true;
                            else if (character == quote)
                                quote = 0;
                            continue;
                        }
                        if (character == '"' || character == '\'')
                        {
                            quote = character;
                            continue;
                        }
                        if (!std::string_view(property.Value).substr(cursor).starts_with("asset("))
                            continue;
                        const auto close = property.Value.find(')', cursor + 6);
                        if (close == std::string::npos)
                            throw std::runtime_error("UI stylesheet asset reference is missing ')'.");
                        const auto identifier =
                            Trim(std::string_view(property.Value).substr(cursor + 6, close - (cursor + 6)));
                        const auto asset = AssetId::Parse(identifier);
                        if (!asset)
                            throw std::runtime_error("UI stylesheet asset reference requires a non-zero asset ID.");
                        result.push_back(asset);
                        cursor = close;
                    }
                }
            }
            std::ranges::sort(result);
            const auto unique = std::ranges::unique(result);
            result.erase(unique.begin(), unique.end());
            return result;
        }

        [[nodiscard]] std::size_t VisualTreeResidentBytes(const UiVisualTreeDefinition& definition)
        {
            std::size_t result = sizeof(UiVisualTreeDefinition) + definition.Name.size() +
                                 definition.StyleSheets.size() * sizeof(AssetId);
            VisitElements(definition.Root, 1,
                          [&](const UiVisualElementDefinition& element, std::size_t)
                          {
                              result += sizeof(UiVisualElementDefinition) + element.CustomType.size() +
                                        element.Name.size() + element.Slot.size();
                              for (const auto& value : element.Classes)
                                  result += value.size();
                              for (const auto& value : element.Attributes)
                                  result += value.Name.size() + value.Value.size();
                              for (const auto& value : element.InlineStyles)
                                  result += value.Name.size() + value.Value.size();
                              for (const auto& value : element.Bindings)
                                  result += value.Property.size() + value.Path.size() + value.Mode.size();
                          });
            return result;
        }

        [[nodiscard]] Json ParseJson(const std::span<const std::byte> bytes, const std::string_view kind)
        {
            if (bytes.size() > MaximumUiDocumentBytes)
                throw std::runtime_error(std::string(kind) + " exceeds the 16 MiB safety limit.");
            try
            {
                return Json::parse(reinterpret_cast<const char*>(bytes.data()),
                                   reinterpret_cast<const char*>(bytes.data() + bytes.size()));
            }
            catch (const Json::exception& error)
            {
                throw std::runtime_error(std::string(kind) + " JSON is malformed: " + error.what());
            }
        }
    } // namespace

    UiVisualTreeAsset::UiVisualTreeAsset(UiVisualTreeDefinition definition) : m_Definition(std::move(definition))
    {
        if (!m_Definition.Root.StableId && m_Definition.Name.empty())
            return;
        Validate(m_Definition);
        m_ResidentBytes = VisualTreeResidentBytes(m_Definition);
    }

    std::size_t UiVisualTreeAsset::ResidentBytes() const noexcept { return m_ResidentBytes; }

    const UiVisualElementDefinition* UiVisualTreeAsset::Find(const AssetId stableId) const noexcept
    {
        const UiVisualElementDefinition* result = nullptr;
        VisitElements(m_Definition.Root, 1,
                      [&](const UiVisualElementDefinition& element, std::size_t)
                      {
                          if (element.StableId == stableId)
                              result = &element;
                      });
        return result;
    }

    const UiVisualElementDefinition* UiVisualTreeAsset::Find(const std::string_view name) const noexcept
    {
        const UiVisualElementDefinition* result = nullptr;
        VisitElements(m_Definition.Root, 1,
                      [&](const UiVisualElementDefinition& element, std::size_t)
                      {
                          if (element.Name == name)
                              result = &element;
                      });
        return result;
    }

    UiVisualTreeDefinition UiVisualTreeAsset::ParseSource(const std::span<const std::byte> bytes)
    {
        if (bytes.size() > MaximumUiDocumentBytes)
            throw std::runtime_error("UI document exceeds the 16 MiB safety limit.");
        const auto source = Text(bytes);
        const auto tags = ParseXmlTags(source);
        if (tags.size() < 3 || tags.front().Name != "ui" || tags.front().Closing || tags.front().SelfClosing)
            throw std::runtime_error("UI document must have a non-empty <ui> root.");
        const auto* schema = Attribute(tags.front(), "schemaVersion");
        const auto* name = Attribute(tags.front(), "name");
        if (schema == nullptr || *schema != "1" || name == nullptr)
            throw std::runtime_error("UI document has an unsupported schema or no name.");
        UiVisualTreeDefinition result;
        result.Name = *name;
        std::size_t cursor = 1;
        while (cursor < tags.size() && !tags[cursor].Closing && tags[cursor].Name == "style")
        {
            const auto* styleSource = Attribute(tags[cursor], "src");
            if (!tags[cursor].SelfClosing || styleSource == nullptr)
                throw std::runtime_error("UI document style references must be self-closing and have a source.");
            result.StyleSheets.push_back(AssetId::Parse(*styleSource));
            ++cursor;
        }
        const auto documentDigest = Detail::Sha256(std::as_bytes(std::span(source)));
        std::size_t elementOrdinal = 0;
        result.Root = ParseElement(tags, cursor, 1, documentDigest, elementOrdinal);
        if (cursor >= tags.size() || !tags[cursor].Closing || tags[cursor].Name != "ui" || cursor + 1 != tags.size())
            throw std::runtime_error("UI document must contain exactly one visual root.");
        Validate(result);
        return result;
    }

    Ref<UiVisualTreeAsset> UiVisualTreeAsset::Decode(const std::span<const std::byte> bytes)
    {
        const auto document = ParseJson(bytes, "UI visual tree asset");
        if (!document.is_object() || document.value("schemaVersion", 0) != 1)
            throw std::runtime_error("UI visual tree asset has an unsupported schema.");
        UiVisualTreeDefinition definition;
        definition.Name = document.at("name").get<std::string>();
        for (const auto& style : document.at("styleSheets"))
            definition.StyleSheets.push_back(AssetId::Parse(style.get<std::string>()));
        definition.Root = DecodeElement(document.at("root"));
        return CreateRef<UiVisualTreeAsset>(std::move(definition));
    }

    std::vector<std::byte> UiVisualTreeAsset::Encode(const UiVisualTreeDefinition& definition)
    {
        Validate(definition);
        Json styles = Json::array();
        for (const auto style : definition.StyleSheets)
            styles.push_back(style.ToString());
        const Json document{{"schemaVersion", 1},
                            {"name", definition.Name},
                            {"styleSheets", std::move(styles)},
                            {"root", EncodeElement(definition.Root)}};
        return Bytes(document.dump(2) + '\n');
    }

    std::vector<std::byte> UiVisualTreeAsset::EncodeSource(const UiVisualTreeDefinition& definition)
    {
        Validate(definition);
        std::string result = "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<ui schemaVersion=\"1\" name=\"" +
                             EncodeXml(definition.Name) + "\">\n";
        for (const auto style : definition.StyleSheets)
            result += "  <style src=\"" + style.ToString() + "\"/>\n";
        EncodeSourceElement(definition.Root, 1, result);
        result += "</ui>\n";
        return Bytes(result);
    }

    void UiVisualTreeAsset::Validate(const UiVisualTreeDefinition& definition)
    {
        if (definition.SchemaVersion != 1 || definition.Name.empty() || definition.Name.size() > 256 ||
            definition.StyleSheets.size() > 64)
            throw std::invalid_argument("UI document name, schema, or stylesheet count is invalid.");
        std::unordered_set<AssetId> identities;
        std::unordered_set<std::string> names;
        std::size_t count = 0;
        std::size_t stringBytes = definition.Name.size();
        for (const auto style : definition.StyleSheets)
        {
            if (!style)
                throw std::invalid_argument("UI document contains an empty stylesheet reference.");
        }
        VisitElements(
            definition.Root, 1,
            [&](const UiVisualElementDefinition& element, const std::size_t depth)
            {
                if (++count > MaximumUiElements || depth > MaximumUiTreeDepth || !element.StableId ||
                    !identities.insert(element.StableId).second || element.Name.size() > 256 ||
                    (!element.Name.empty() && !names.insert(element.Name).second) || element.Classes.size() > 64 ||
                    element.Attributes.size() > 128 || element.InlineStyles.size() > 128 ||
                    element.Bindings.size() > 128 || element.Children.size() > 4'096 ||
                    element.CustomType.size() > 256 || element.Slot.size() > 256 ||
                    (element.Type == UiVisualElementType::Custom && element.CustomType.empty()) ||
                    (element.Type != UiVisualElementType::Custom && !element.CustomType.empty()))
                    throw std::invalid_argument("UI document contains an invalid or duplicated element.");
                if (element.Type == UiVisualElementType::TemplateContainer && !element.Template)
                    throw std::invalid_argument("UI template containers require a template asset.");
                if (element.Type == UiVisualElementType::Slot && (element.Template || !element.Slot.empty()))
                    throw std::invalid_argument("UI slot declarations cannot reference a template or slot.");
                std::unordered_set<std::string> classNames;
                for (const auto& value : element.Classes)
                {
                    if (value.empty() || value.size() > 128 || !classNames.insert(value).second)
                        throw std::invalid_argument("UI document element class is empty or duplicated.");
                    stringBytes += value.size();
                }
                const auto validateValues = [&](const std::vector<UiNamedValue>& values)
                {
                    std::unordered_set<std::string> keys;
                    for (const auto& value : values)
                    {
                        if (value.Name.empty() || value.Name.size() > 128 || value.Value.size() > 65'536 ||
                            !keys.insert(value.Name).second)
                            throw std::invalid_argument("UI document value is empty, duplicated, or too large.");
                        stringBytes += value.Name.size() + value.Value.size();
                    }
                };
                validateValues(element.Attributes);
                validateValues(element.InlineStyles);
                const auto* image = NamedValue(element.Attributes, "image");
                const auto* renderTexture = NamedValue(element.Attributes, "render-texture");
                const auto* font = NamedValue(element.Attributes, "font");
                if (image != nullptr && renderTexture != nullptr)
                    throw std::invalid_argument(
                        "UI elements cannot reference both an asset image and logical render texture.");
                if (renderTexture != nullptr && (element.Type != UiVisualElementType::Image || renderTexture->empty()))
                    throw std::invalid_argument(
                        "UI render-texture references require a non-empty logical ID on an Image element.");
                const auto validateAssetReference = [](const std::string* value, const std::string_view name)
                {
                    if (value == nullptr || value->empty())
                        return;
                    if (!AssetId::Parse(*value))
                        throw std::invalid_argument("UI " + std::string(name) + " reference cannot be the empty ID.");
                };
                validateAssetReference(image, "image");
                validateAssetReference(font, "font");
                validateAssetReference(renderTexture, "render-texture");
                std::unordered_set<std::string> bindingProperties;
                for (const auto& binding : element.Bindings)
                {
                    if (binding.Property.empty() || binding.Path.empty() || binding.Property.size() > 128 ||
                        binding.Path.size() > 1'024 ||
                        (binding.Mode != "OneWay" && binding.Mode != "TwoWay" && binding.Mode != "OneTime") ||
                        !bindingProperties.insert(binding.Property).second)
                        throw std::invalid_argument("UI document binding is invalid or duplicated.");
                    stringBytes += binding.Property.size() + binding.Path.size() + binding.Mode.size();
                }
                stringBytes += element.Name.size() + element.CustomType.size() + element.Slot.size();
            });
        if (stringBytes > MaximumUiDocumentBytes)
            throw std::invalid_argument("UI document string data exceeds the 16 MiB safety limit.");
    }

    UiStyleSheetAsset::UiStyleSheetAsset(UiStyleSheetDefinition definition) : m_Definition(std::move(definition))
    {
        if (m_Definition.Rules.empty())
            return;
        Validate(m_Definition);
        m_ResidentBytes = sizeof(*this);
        for (const auto& rule : m_Definition.Rules)
        {
            m_ResidentBytes += sizeof(rule) + rule.Selector.size();
            for (const auto& part : rule.Parts)
            {
                m_ResidentBytes += sizeof(part) + part.Type.size() + part.Name.size();
                for (const auto& value : part.Classes)
                    m_ResidentBytes += value.size();
            }
            for (const auto& property : rule.Properties)
                m_ResidentBytes += property.Name.size() + property.Value.size();
        }
    }

    std::size_t UiStyleSheetAsset::ResidentBytes() const noexcept { return m_ResidentBytes; }

    UiStyleSheetDefinition UiStyleSheetAsset::ParseSource(const std::span<const std::byte> bytes)
    {
        if (bytes.size() > MaximumUiDocumentBytes)
            throw std::runtime_error("UI stylesheet exceeds the 16 MiB safety limit.");
        auto source = RemoveStyleComments(Text(bytes));
        const auto first = source.find_first_not_of(" \t\r\n");
        UiStyleSheetDefinition result;
        constexpr std::string_view HeaderPrefix = "@keire-style ";
        if (first == std::string::npos || !std::string_view(source).substr(first).starts_with(HeaderPrefix))
            throw std::runtime_error("UI stylesheet must begin with '@keire-style 1;' or '@keire-style 2;'.");
        const auto versionStart = first + HeaderPrefix.size();
        const auto headerEnd = source.find(';', versionStart);
        if (headerEnd == std::string::npos)
            throw std::runtime_error("UI stylesheet version header is unterminated.");
        const auto version = Trim(std::string_view(source).substr(versionStart, headerEnd - versionStart));
        if (version == "1")
            result.SchemaVersion = 1;
        else if (version == "2")
            result.SchemaVersion = 2;
        else
            throw std::runtime_error("UI stylesheet source has an unsupported schema version.");

        const auto matchingBrace = [&source](const std::size_t open, const std::size_t end)
        {
            std::size_t depth = 0;
            char quote = 0;
            bool escaped = false;
            for (std::size_t cursor = open; cursor < end; ++cursor)
            {
                const auto character = source[cursor];
                if (quote != 0)
                {
                    if (escaped)
                        escaped = false;
                    else if (character == '\\')
                        escaped = true;
                    else if (character == quote)
                        quote = 0;
                    continue;
                }
                if (character == '"' || character == '\'')
                {
                    quote = character;
                    continue;
                }
                if (character == '{')
                    ++depth;
                else if (character == '}' && depth > 0U && --depth == 0U)
                    return cursor;
            }
            return std::string::npos;
        };
        const auto containsOpeningBrace = [&source](const std::size_t begin, const std::size_t end)
        {
            char quote = 0;
            bool escaped = false;
            for (auto cursor = begin; cursor < end; ++cursor)
            {
                const auto character = source[cursor];
                if (quote != 0)
                {
                    if (escaped)
                        escaped = false;
                    else if (character == '\\')
                        escaped = true;
                    else if (character == quote)
                        quote = 0;
                    continue;
                }
                if (character == '"' || character == '\'')
                    quote = character;
                else if (character == '{')
                    return true;
            }
            return false;
        };
        std::function<void(std::size_t, std::size_t, std::optional<UiStyleMediaCondition>)> parseBlock;
        parseBlock =
            [&](std::size_t cursor, const std::size_t end, const std::optional<UiStyleMediaCondition> inheritedMedia)
        {
            while (cursor < end)
            {
                cursor = source.find_first_not_of(" \t\r\n", cursor);
                if (cursor == std::string::npos || cursor >= end)
                    break;
                const auto open = source.find('{', cursor);
                if (open == std::string::npos || open >= end)
                {
                    if (!Trim(std::string_view(source).substr(cursor, end - cursor)).empty())
                        ThrowStyleSourceError(source, cursor, "UI stylesheet contains text outside a rule.");
                    break;
                }
                const auto close = matchingBrace(open, end);
                if (close == std::string::npos)
                    ThrowStyleSourceError(source, open, "UI stylesheet contains an unterminated rule or media block.");
                const auto heading = Trim(std::string_view(source).substr(cursor, open - cursor));
                if (std::string_view(heading).starts_with("@media"))
                {
                    if (result.SchemaVersion < 2)
                        ThrowStyleSourceError(source, cursor, "Responsive @media rules require '@keire-style 2;'.");
                    if (inheritedMedia)
                        ThrowStyleSourceError(source, cursor, "UI stylesheet media blocks cannot be nested.");
                    const auto conditionText =
                        Trim(std::string_view(heading).substr(std::string_view("@media").size()));
                    try
                    {
                        parseBlock(open + 1, close, ParseMediaCondition(conditionText));
                    }
                    catch (const std::runtime_error& error)
                    {
                        if (std::string_view(error.what()).starts_with("UI stylesheet line "))
                            throw;
                        ThrowStyleSourceError(source, cursor, error.what());
                    }
                }
                else
                {
                    if (containsOpeningBrace(open + 1U, close))
                        ThrowStyleSourceError(source, open + 1U, "UI stylesheet rules cannot contain nested blocks.");
                    try
                    {
                        auto rule = ParseSelector(heading);
                        rule.Properties =
                            ParseDeclarations(std::string_view(source).substr(open + 1, close - open - 1));
                        if (rule.Properties.empty())
                            throw std::runtime_error("UI stylesheet rules require at least one declaration.");
                        for (const auto& property : rule.Properties)
                            ValidateUiStylePropertyValue(property.Name, property.Value, result.SchemaVersion);
                        rule.Media = inheritedMedia;
                        result.Rules.push_back(std::move(rule));
                    }
                    catch (const std::runtime_error& error)
                    {
                        ThrowStyleSourceError(source, cursor, error.what());
                    }
                    catch (const std::invalid_argument& error)
                    {
                        throw std::invalid_argument(
                            Detail::UiStyleSource::StyleSourceMessage(source, cursor, error.what()));
                    }
                }
                cursor = close + 1;
            }
        };
        parseBlock(headerEnd + 1, source.size(), std::nullopt);
        Validate(result);
        return result;
    }

    Ref<UiStyleSheetAsset> UiStyleSheetAsset::Decode(const std::span<const std::byte> bytes)
    {
        const auto document = ParseJson(bytes, "UI stylesheet asset");
        if (!document.is_object() || document.value("schemaVersion", 0) < 1 || document.value("schemaVersion", 0) > 2)
            throw std::runtime_error("UI stylesheet asset has an unsupported schema.");
        UiStyleSheetDefinition definition;
        definition.SchemaVersion = document.at("schemaVersion").get<std::uint32_t>();
        for (const auto& sourceRule : document.at("rules"))
        {
            UiStyleRuleDefinition rule;
            rule.Selector = sourceRule.at("selector").get<std::string>();
            rule.Specificity = sourceRule.at("specificity").get<std::uint32_t>();
            for (const auto& sourcePart : sourceRule.at("parts"))
                rule.Parts.push_back(DecodeSelectorPart(sourcePart));
            rule.Properties = DecodeNamedValues(sourceRule.at("properties"));
            if (const auto media = sourceRule.find("media"); media != sourceRule.end())
                rule.Media = DecodeMediaConditionJson(*media);
            definition.Rules.push_back(std::move(rule));
        }
        return CreateRef<UiStyleSheetAsset>(std::move(definition));
    }

    std::vector<std::byte> UiStyleSheetAsset::Encode(const UiStyleSheetDefinition& definition)
    {
        Validate(definition);
        Json rules = Json::array();
        for (const auto& rule : definition.Rules)
        {
            Json parts = Json::array();
            for (const auto& part : rule.Parts)
                parts.push_back(EncodeSelectorPart(part));
            rules.push_back({{"selector", rule.Selector},
                             {"specificity", rule.Specificity},
                             {"parts", std::move(parts)},
                             {"properties", EncodeNamedValues(rule.Properties)}});
            if (rule.Media)
                rules.back()["media"] = EncodeMediaConditionJson(*rule.Media);
        }
        return Bytes(Json{{"schemaVersion", definition.SchemaVersion}, {"rules", std::move(rules)}}.dump(2) + '\n');
    }

    std::vector<std::byte> UiStyleSheetAsset::EncodeSource(const UiStyleSheetDefinition& definition)
    {
        Validate(definition);
        std::string result = "@keire-style " + std::to_string(definition.SchemaVersion) + ";\n\n";
        for (const auto& rule : definition.Rules)
        {
            const bool media = rule.Media.has_value();
            if (media)
                result += "@media " + EncodeMediaCondition(*rule.Media) + "\n{\n  ";
            result += rule.Selector + "\n" + (media ? "  " : "") + "{\n";
            for (const auto& property : rule.Properties)
                result += std::string(media ? "    " : "  ") + property.Name + ": " + property.Value + ";\n";
            result += std::string(media ? "  }\n}\n\n" : "}\n\n");
        }
        return Bytes(result);
    }

    std::string EncodeUiStyleMediaCondition(const UiStyleMediaCondition& condition)
    {
        if (condition.Empty())
            throw std::invalid_argument("A responsive UI style rule requires at least one condition.");
        return EncodeMediaCondition(condition);
    }

    void UiStyleSheetAsset::Validate(const UiStyleSheetDefinition& definition)
    {
        if ((definition.SchemaVersion != 1 && definition.SchemaVersion != 2) ||
            definition.Rules.size() > MaximumUiStyleRules)
            throw std::invalid_argument("UI stylesheet schema or rule count is invalid.");
        std::size_t properties = 0;
        std::size_t stringBytes = 0;
        for (const auto& rule : definition.Rules)
        {
            if (rule.Selector.empty() || rule.Selector.size() > 1'024 || rule.Parts.empty() || rule.Parts.size() > 32 ||
                rule.Specificity > 100'000 || rule.Properties.empty() || rule.Properties.size() > 256)
                throw std::invalid_argument("UI stylesheet rule is empty or exceeds a safety limit.");
            if (rule.Media)
            {
                if (definition.SchemaVersion < 2 || rule.Media->Empty())
                    throw std::invalid_argument("UI stylesheet media rules require schema v2 and a condition.");
                const auto finite = [](const std::optional<float> value)
                { return !value || (std::isfinite(*value) && *value >= 0.0F); };
                if (!finite(rule.Media->MinimumWidth) || !finite(rule.Media->MaximumWidth) ||
                    !finite(rule.Media->MinimumHeight) || !finite(rule.Media->MaximumHeight) ||
                    !finite(rule.Media->MinimumAspectRatio) || !finite(rule.Media->MaximumAspectRatio) ||
                    !finite(rule.Media->MinimumDpi) || !finite(rule.Media->MaximumDpi) ||
                    static_cast<std::uint8_t>(rule.Media->Orientation) >
                        static_cast<std::uint8_t>(UiStyleOrientation::Portrait) ||
                    static_cast<std::uint8_t>(rule.Media->Pointer) >
                        static_cast<std::uint8_t>(UiStylePointerPrecision::None) ||
                    static_cast<std::uint8_t>(rule.Media->Navigation) >
                        static_cast<std::uint8_t>(UiStyleNavigationMode::Gamepad))
                    throw std::invalid_argument("UI stylesheet media condition is invalid.");
            }
            std::unordered_set<std::string> propertyNames;
            for (std::size_t index = 0; index < rule.Parts.size(); ++index)
            {
                const auto& part = rule.Parts[index];
                if ((index == 0 && part.Combinator != UiStyleCombinator::None) ||
                    (index != 0 && part.Combinator == UiStyleCombinator::None) || part.Type.size() > 256 ||
                    part.Name.size() > 256 || part.Classes.size() > 64 ||
                    static_cast<std::uint16_t>(part.States) >
                        static_cast<std::uint16_t>(UiStylePseudoState::Hover | UiStylePseudoState::Active |
                                                   UiStylePseudoState::Focus | UiStylePseudoState::Disabled |
                                                   UiStylePseudoState::Checked | UiStylePseudoState::Root))
                    throw std::invalid_argument("UI stylesheet selector part is invalid.");
                stringBytes += part.Type.size() + part.Name.size();
                for (const auto& className : part.Classes)
                {
                    if (className.empty() || className.size() > 128)
                        throw std::invalid_argument("UI stylesheet selector class is invalid.");
                    stringBytes += className.size();
                }
            }
            for (const auto& property : rule.Properties)
            {
                if (property.Name.empty() || property.Name.size() > 128 || property.Value.empty() ||
                    property.Value.size() > 65'536 || !propertyNames.insert(property.Name).second)
                    throw std::invalid_argument("UI stylesheet property is empty, duplicated, or too large.");
                ValidateUiStylePropertyValue(property.Name, property.Value, definition.SchemaVersion);
                stringBytes += property.Name.size() + property.Value.size();
            }
            properties += rule.Properties.size();
            stringBytes += rule.Selector.size();
        }
        if (properties > MaximumUiStyleProperties || stringBytes > MaximumUiDocumentBytes)
            throw std::invalid_argument("UI stylesheet exceeds the property or string-data safety limit.");
    }

    AssetImporterRegistration CreateUiVisualTreeAssetImporter()
    {
        AssetImporterRegistration result{"Keire.UiVisualTree", 1, UiVisualTreeAsset::StaticType(), {".keireui"}};
        result.ContextualImport = [](const AssetImportContext&, const std::span<const std::byte> bytes)
        {
            auto definition = UiVisualTreeAsset::ParseSource(bytes);
            AssetImportOutput output;
            output.Bytes = UiVisualTreeAsset::Encode(definition);
            output.AssetDependencies = definition.StyleSheets;
            VisitElements(definition.Root, 1,
                          [&](const UiVisualElementDefinition& element, std::size_t)
                          {
                              if (element.Template)
                                  output.AssetDependencies.push_back(element.Template);
                              for (const auto name : {std::string_view("image"), std::string_view("font")})
                              {
                                  const auto* value = NamedValue(element.Attributes, name);
                                  if (value != nullptr && !value->empty())
                                      output.AssetDependencies.push_back(AssetId::Parse(*value));
                              }
                          });
            std::ranges::sort(output.AssetDependencies);
            const auto unique = std::ranges::unique(output.AssetDependencies);
            output.AssetDependencies.erase(unique.begin(), unique.end());
            return output;
        };
        return result;
    }

    AssetImporterRegistration CreateUiStyleSheetAssetImporter()
    {
        AssetImporterRegistration result{"Keire.UiStyleSheet", 2, UiStyleSheetAsset::StaticType(), {".keirestyle"}};
        result.ContextualImport = [](const AssetImportContext&, const std::span<const std::byte> bytes)
        {
            const auto definition = UiStyleSheetAsset::ParseSource(bytes);
            AssetImportOutput output;
            output.Bytes = UiStyleSheetAsset::Encode(definition);
            output.AssetDependencies = UiStyleAssetDependencies(definition);
            return output;
        };
        return result;
    }

    AssetDecoderRegistration CreateUiVisualTreeAssetDecoder()
    {
        return {UiVisualTreeAsset::StaticType(), CreateRef<UiVisualTreeAsset>(),
                [](const std::span<const std::byte> bytes) -> Ref<Asset> { return UiVisualTreeAsset::Decode(bytes); }};
    }

    AssetDecoderRegistration CreateUiStyleSheetAssetDecoder()
    {
        return {UiStyleSheetAsset::StaticType(), CreateRef<UiStyleSheetAsset>(),
                [](const std::span<const std::byte> bytes) -> Ref<Asset> { return UiStyleSheetAsset::Decode(bytes); }};
    }

} // namespace Keire
