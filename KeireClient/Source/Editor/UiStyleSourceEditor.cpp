#include "KeireClient/Editor/UiStyleSourceEditor.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <ranges>
#include <stdexcept>
#include <utility>

namespace KeireEditor
{
    namespace
    {
        [[nodiscard]] bool IdentifierCharacter(const char value) noexcept
        {
            return std::isalnum(static_cast<unsigned char>(value)) != 0 || value == '-' || value == '_';
        }

        [[nodiscard]] std::string Lower(std::string value)
        {
            std::ranges::transform(value, value.begin(), [](const unsigned char character)
                                   { return static_cast<char>(std::tolower(character)); });
            return value;
        }

        [[nodiscard]] int MatchScore(const std::string_view candidate, const std::string_view prefix)
        {
            if (prefix.empty())
                return 0;
            const auto value = Lower(std::string(candidate));
            const auto query = Lower(std::string(prefix));
            if (value.starts_with(query))
                return static_cast<int>(value.size() - query.size());
            if (const auto offset = value.find(query); offset != std::string::npos)
                return 100 + static_cast<int>(offset);
            std::size_t cursor = 0;
            int gaps = 0;
            for (const auto character : query)
            {
                const auto match = value.find(character, cursor);
                if (match == std::string::npos)
                    return -1;
                gaps += static_cast<int>(match - cursor);
                cursor = match + 1U;
            }
            return 200 + gaps;
        }

        [[nodiscard]] std::string DescriptorDocumentation(const Keire::UiStylePropertyDescriptor& descriptor)
        {
            std::string result(descriptor.DisplayName);
            result += " (`" + std::string(descriptor.Name) + "`)\nDefault: " + std::string(descriptor.DefaultValue);
            if (!descriptor.Keywords.empty())
                result += "\nValues: " + std::string(descriptor.Keywords);
            result += descriptor.Inherited ? "\nInherited." : "\nNot inherited.";
            result += descriptor.Animatable ? " Animatable." : " Not animatable.";
            if (descriptor.MinimumSchemaVersion > 1U)
                result += " Requires style schema v" + std::to_string(descriptor.MinimumSchemaVersion) + '.';
            return result;
        }

        [[nodiscard]] std::string_view Trim(const std::string_view value) noexcept
        {
            const auto begin = value.find_first_not_of(" \t\r\n");
            if (begin == std::string_view::npos)
                return {};
            const auto end = value.find_last_not_of(" \t\r\n");
            return value.substr(begin, end - begin + 1U);
        }

        [[nodiscard]] std::vector<std::string_view> Keywords(const std::string_view values)
        {
            std::vector<std::string_view> result;
            std::size_t cursor = 0;
            while (cursor < values.size())
            {
                const auto end = values.find('|', cursor);
                result.push_back(values.substr(cursor, end - cursor));
                if (end == std::string_view::npos)
                    break;
                cursor = end + 1U;
            }
            return result;
        }
    } // namespace

    void UiStyleSourceEditor::SetSource(std::string source)
    {
        m_Source = std::move(source);
        m_Cursor = std::min(m_Cursor, m_Source.size());
        Analyze();
    }

    void UiStyleSourceEditor::SetCursor(const std::size_t offset) noexcept
    {
        m_Cursor = std::min(offset, m_Source.size());
    }

    UiStyleSourceMatch UiStyleSourceEditor::CursorLocation() const noexcept { return Location(m_Cursor); }

    std::optional<std::size_t> UiStyleSourceEditor::MatchingBrace(const std::size_t offset) const noexcept
    {
        if (m_Source.empty())
            return std::nullopt;
        const auto cursor = std::min(offset, m_Source.size() - 1U);
        const auto value = m_Source[cursor];
        if (value != '{' && value != '(' && value != '[' && value != '}' && value != ')' && value != ']')
            return std::nullopt;
        const char open = value == '{' || value == '}' ? '{' : value == '(' || value == ')' ? '(' : '[';
        const char close = open == '{' ? '}' : open == '(' ? ')' : ']';
        std::vector<std::size_t> openings;
        char quote = 0;
        bool escaped = false;
        bool comment = false;
        for (std::size_t index = 0; index < m_Source.size(); ++index)
        {
            if (comment)
            {
                if (index + 1U < m_Source.size() && m_Source[index] == '*' && m_Source[index + 1U] == '/')
                {
                    comment = false;
                    ++index;
                }
                continue;
            }
            if (quote != 0)
            {
                if (escaped)
                    escaped = false;
                else if (m_Source[index] == '\\')
                    escaped = true;
                else if (m_Source[index] == quote)
                    quote = 0;
                continue;
            }
            if (index + 1U < m_Source.size() && m_Source[index] == '/' && m_Source[index + 1U] == '*')
            {
                comment = true;
                ++index;
                continue;
            }
            if (m_Source[index] == '"' || m_Source[index] == '\'')
            {
                quote = m_Source[index];
                continue;
            }
            if (m_Source[index] == open)
                openings.push_back(index);
            else if (m_Source[index] == close && !openings.empty())
            {
                const auto matching = openings.back();
                openings.pop_back();
                if (matching == cursor)
                    return index;
                if (index == cursor)
                    return matching;
            }
        }
        return std::nullopt;
    }

    std::optional<std::string> UiStyleSourceEditor::HoverDocumentation(const std::size_t offset) const
    {
        const auto found =
            std::ranges::find_if(m_Tokens, [offset](const auto& token)
                                 { return offset >= token.Offset && offset < token.Offset + token.Length; });
        if (found == m_Tokens.end())
            return std::nullopt;
        const auto text = std::string_view(m_Source).substr(found->Offset, found->Length);
        if (const auto* descriptor = Keire::FindUiStylePropertyDescriptor(text))
            return DescriptorDocumentation(*descriptor);
        if (text.starts_with("--"))
            return "Design token `" + std::string(text) + "`. Use var(" + std::string(text) + ") to reference it.";
        if (text.starts_with(':'))
            return "Pseudo-state selector " + std::string(text) + ".";
        return std::nullopt;
    }

    std::vector<UiStyleSourceCompletion> UiStyleSourceEditor::Completions(const std::size_t offset,
                                                                          const std::size_t maximum) const
    {
        if (maximum == 0U)
            return {};
        const auto cursor = std::min(offset, m_Source.size());
        auto begin = cursor;
        while (begin > 0U && IdentifierCharacter(m_Source[begin - 1U]))
            --begin;

        auto prefix = std::string_view(m_Source).substr(begin, cursor - begin);
        std::size_t blockDepth = 0;
        for (const auto& token : m_Tokens)
        {
            if (token.Offset >= cursor || token.Kind != UiStyleSourceTokenKind::Punctuation)
                continue;
            if (m_Source[token.Offset] == '{')
                ++blockDepth;
            else if (m_Source[token.Offset] == '}' && blockDepth > 0U)
                --blockDepth;
        }

        std::vector<std::pair<int, UiStyleSourceCompletion>> ranked;
        const auto append =
            [&](std::string_view label, std::string insertion, std::string documentation, const int priority = 0)
        {
            if (const auto score = MatchScore(label, prefix); score >= 0)
                ranked.push_back(
                    {priority + score, {std::string(label), std::move(insertion), std::move(documentation)}});
        };

        if (blockDepth == 0U)
        {
            constexpr std::array selectors{
                std::string_view("VisualElement"), std::string_view("Label"),       std::string_view("Image"),
                std::string_view("Button"),        std::string_view("TextField"),   std::string_view("Toggle"),
                std::string_view("Slider"),        std::string_view("ProgressBar"), std::string_view("ScrollView"),
                std::string_view("ListView"),      std::string_view("TreeView"),    std::string_view("DropdownField"),
                std::string_view("Foldout"),       std::string_view("TabView"),     std::string_view("Toolbar"),
            };
            for (const auto selector : selectors)
                append(selector, std::string(selector), "Kéire retained-UI element selector.");
            for (const std::string_view selector : {":hover", ":active", ":focus", ":disabled", ":checked", ":root"})
                append(selector, std::string(selector), "UI selector pseudo-state.");
        }
        else
        {
            auto declarationStart = std::string::npos;
            auto separator = std::string::npos;
            for (const auto& token : m_Tokens)
            {
                if (token.Offset >= cursor)
                    continue;
                const auto character = m_Source[token.Offset];
                if (token.Kind == UiStyleSourceTokenKind::Punctuation && (character == '{' || character == ';'))
                {
                    declarationStart = token.Offset;
                    separator = std::string::npos;
                }
                else if (character == ':' && declarationStart != std::string::npos && separator == std::string::npos)
                    separator = token.Offset;
            }
            const bool valueContext = separator != std::string::npos && separator < cursor;
            if (valueContext)
            {
                const auto property =
                    Trim(std::string_view(m_Source).substr(declarationStart + 1U, separator - declarationStart - 1U));
                const auto* descriptor = Keire::FindUiStylePropertyDescriptor(property);
                if (descriptor && descriptor->ValueKind == Keire::UiStyleValueKind::Keyword)
                {
                    for (const auto keyword : Keywords(descriptor->Keywords))
                        append(keyword, std::string(keyword),
                               "Valid value for `" + std::string(descriptor->Name) + "`.", -50);
                }
            }
            if (!valueContext)
            {
                for (const auto& descriptor : Keire::UiStylePropertyDescriptors())
                    append(descriptor.Name, std::string(descriptor.Name) + ": ", DescriptorDocumentation(descriptor));
            }
            else
            {
                for (const auto& token : m_Tokens)
                {
                    if (token.Kind != UiStyleSourceTokenKind::Variable)
                        continue;
                    const auto variable = std::string_view(m_Source).substr(token.Offset, token.Length);
                    const bool insideVar = begin >= 4U && std::string_view(m_Source).substr(begin - 4U, 4U) == "var(";
                    append(variable, insideVar ? std::string(variable) : "var(" + std::string(variable) + ")",
                           "Design token resolved from this style sheet.", -25);
                }
            }
        }

        std::ranges::sort(
            ranked, [](const auto& left, const auto& right)
            { return left.first != right.first ? left.first < right.first : left.second.Label < right.second.Label; });
        std::vector<UiStyleSourceCompletion> result;
        for (auto& [score, completion] : ranked)
        {
            (void)score;
            if (std::ranges::find(result, completion.Label, &UiStyleSourceCompletion::Label) != result.end())
                continue;
            result.push_back(std::move(completion));
            if (result.size() >= maximum)
                break;
        }
        return result;
    }

    bool UiStyleSourceEditor::ApplyCompletion(const std::size_t offset, const UiStyleSourceCompletion& completion)
    {
        const auto cursor = std::min(offset, m_Source.size());
        auto begin = cursor;
        while (begin > 0U && IdentifierCharacter(m_Source[begin - 1U]))
            --begin;
        if (completion.Insertion.starts_with(':') && begin > 0U && m_Source[begin - 1U] == ':')
            --begin;
        if (completion.Insertion.empty())
            return false;
        m_Source.replace(begin, cursor - begin, completion.Insertion);
        m_Cursor = begin + completion.Insertion.size();
        Analyze();
        return true;
    }

    std::vector<UiStyleSourceMatch> UiStyleSourceEditor::Find(const std::string_view value, const bool caseSensitive,
                                                              const std::size_t maximum) const
    {
        if (value.empty() || maximum == 0U)
            return {};
        const auto source = caseSensitive ? m_Source : Lower(m_Source);
        const auto needle = caseSensitive ? std::string(value) : Lower(std::string(value));
        std::vector<UiStyleSourceMatch> result;
        std::size_t cursor = 0;
        while (result.size() < maximum && (cursor = source.find(needle, cursor)) != std::string::npos)
        {
            result.push_back(Location(cursor, needle.size()));
            cursor += std::max<std::size_t>(1U, needle.size());
        }
        return result;
    }

    std::size_t UiStyleSourceEditor::ReplaceAll(const std::string_view value, const std::string_view replacement,
                                                const bool caseSensitive)
    {
        const auto matches = Find(value, caseSensitive);
        if (matches.empty())
            return 0U;
        for (auto match = matches.rbegin(); match != matches.rend(); ++match)
            m_Source.replace(match->Offset, match->Length, replacement);
        m_Cursor = std::min(m_Cursor, m_Source.size());
        Analyze();
        return matches.size();
    }

    bool UiStyleSourceEditor::GoToRule(const std::string_view selector) noexcept
    {
        const auto found = std::ranges::find(m_Rules, selector, &UiStyleSourceRuleLocation::Selector);
        if (found == m_Rules.end())
            return false;
        m_Cursor = found->Offset;
        return true;
    }

    bool UiStyleSourceEditor::Format()
    {
        std::string result;
        std::size_t begin = 0;
        std::size_t indentation = 0;
        while (begin <= m_Source.size())
        {
            const auto end = m_Source.find('\n', begin);
            const auto lineEnd = end == std::string::npos ? m_Source.size() : end;
            const auto line = Trim(std::string_view(m_Source).substr(
                begin, end == std::string::npos ? m_Source.size() - begin : end - begin));
            std::size_t opens = 0U;
            std::size_t closes = 0U;
            for (const auto& token : m_Tokens)
            {
                if (token.Offset < begin || token.Offset >= lineEnd ||
                    token.Kind != UiStyleSourceTokenKind::Punctuation)
                    continue;
                if (m_Source[token.Offset] == '{')
                    ++opens;
                else if (m_Source[token.Offset] == '}')
                    ++closes;
            }
            const bool leadingClose = !line.empty() && line.front() == '}';
            const auto lineIndentation = leadingClose && indentation > 0U ? indentation - 1U : indentation;
            if (!line.empty())
                result.append(lineIndentation * 2U, ' ');
            result += line;
            if (end == std::string::npos)
                break;
            result += '\n';
            indentation += opens;
            indentation = closes >= indentation ? 0U : indentation - closes;
            begin = end + 1U;
        }
        if (result == m_Source)
            return false;
        m_Source = std::move(result);
        m_Cursor = std::min(m_Cursor, m_Source.size());
        Analyze();
        return true;
    }

    void UiStyleSourceEditor::Analyze()
    {
        m_Tokens.clear();
        m_Rules.clear();
        m_LineOffsets.assign(1U, 0U);
        for (std::size_t index = 0; index < m_Source.size(); ++index)
            if (m_Source[index] == '\n')
                m_LineOffsets.push_back(index + 1U);

        std::size_t blockDepth = 0;
        for (std::size_t index = 0; index < m_Source.size();)
        {
            if (std::isspace(static_cast<unsigned char>(m_Source[index])) != 0)
            {
                ++index;
                continue;
            }
            const auto location = Location(index);
            if (index + 1U < m_Source.size() && m_Source[index] == '/' && m_Source[index + 1U] == '*')
            {
                const auto end = m_Source.find("*/", index + 2U);
                const auto length = end == std::string::npos ? m_Source.size() - index : end + 2U - index;
                m_Tokens.push_back({UiStyleSourceTokenKind::Comment, index, length, location.Line, location.Column});
                index += length;
                continue;
            }
            if (m_Source[index] == '"' || m_Source[index] == '\'')
            {
                const auto quote = m_Source[index];
                auto end = index + 1U;
                while (end < m_Source.size() && m_Source[end] != quote)
                    end += m_Source[end] == '\\' && end + 1U < m_Source.size() ? 2U : 1U;
                end = std::min(m_Source.size(), end + 1U);
                m_Tokens.push_back(
                    {UiStyleSourceTokenKind::String, index, end - index, location.Line, location.Column});
                index = end;
                continue;
            }
            if (std::isdigit(static_cast<unsigned char>(m_Source[index])) != 0)
            {
                auto end = index + 1U;
                while (end < m_Source.size() &&
                       (std::isdigit(static_cast<unsigned char>(m_Source[end])) != 0 || m_Source[end] == '.'))
                    ++end;
                m_Tokens.push_back(
                    {UiStyleSourceTokenKind::Number, index, end - index, location.Line, location.Column});
                index = end;
                continue;
            }
            if (IdentifierCharacter(m_Source[index]) || m_Source[index] == ':' || m_Source[index] == '.' ||
                m_Source[index] == '#' || m_Source[index] == '@')
            {
                const bool declarationCandidate = blockDepth > 0U && IdentifierCharacter(m_Source[index]) &&
                                                  m_Source[index] != ':' && m_Source[index] != '.' &&
                                                  m_Source[index] != '#' && m_Source[index] != '@';
                auto end = index + 1U;
                while (end < m_Source.size() &&
                       (IdentifierCharacter(m_Source[end]) ||
                        (!declarationCandidate && (m_Source[end] == ':' || m_Source[end] == '.' ||
                                                   m_Source[end] == '#' || m_Source[end] == '*'))))
                    ++end;
                const auto value = std::string_view(m_Source).substr(index, end - index);
                const auto next = m_Source.find_first_not_of(" \t\r\n", end);
                UiStyleSourceTokenKind kind = UiStyleSourceTokenKind::Value;
                if (value.starts_with('@'))
                    kind = UiStyleSourceTokenKind::Header;
                else if (value.starts_with("--"))
                    kind = UiStyleSourceTokenKind::Variable;
                else if (blockDepth > 0U && next != std::string::npos && m_Source[next] == ':')
                    kind = UiStyleSourceTokenKind::Property;
                else if (blockDepth == 0U || (next != std::string::npos && m_Source[next] == '{'))
                    kind = UiStyleSourceTokenKind::Selector;
                m_Tokens.push_back({kind, index, end - index, location.Line, location.Column});
                if (kind == UiStyleSourceTokenKind::Selector && next != std::string::npos && m_Source[next] == '{')
                    m_Rules.push_back({std::string(Trim(value)), index, location.Line, location.Column});
                index = end;
                continue;
            }
            if (m_Source[index] == '{')
                ++blockDepth;
            else if (m_Source[index] == '}' && blockDepth > 0U)
                --blockDepth;
            m_Tokens.push_back({UiStyleSourceTokenKind::Punctuation, index, 1U, location.Line, location.Column});
            ++index;
        }
    }

    UiStyleSourceMatch UiStyleSourceEditor::Location(const std::size_t offset, const std::size_t length) const noexcept
    {
        const auto clamped = std::min(offset, m_Source.size());
        const auto line = std::upper_bound(m_LineOffsets.begin(), m_LineOffsets.end(), clamped);
        const auto lineIndex =
            line == m_LineOffsets.begin() ? 0U : static_cast<std::size_t>(line - m_LineOffsets.begin() - 1);
        return {.Offset = clamped,
                .Length = length,
                .Line = lineIndex + 1U,
                .Column = clamped - m_LineOffsets[lineIndex] + 1U};
    }
} // namespace KeireEditor
