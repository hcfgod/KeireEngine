#include "KeireClient/Editor/UiMarkupSourceEditor.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <ranges>
#include <utility>

namespace KeireEditor
{
    namespace
    {
        constexpr std::array ElementNames{
            std::string_view("VisualElement"), std::string_view("TemplateContainer"),
            std::string_view("Label"),         std::string_view("Image"),
            std::string_view("Button"),        std::string_view("TextField"),
            std::string_view("Toggle"),        std::string_view("Slider"),
            std::string_view("ProgressBar"),   std::string_view("ScrollView"),
            std::string_view("ListView"),      std::string_view("TreeView"),
            std::string_view("DropdownField"), std::string_view("Foldout"),
            std::string_view("TabView"),       std::string_view("Toolbar"),
            std::string_view("Spacer"),        std::string_view("Slot"),
            std::string_view("style"),         std::string_view("ui"),
        };
        constexpr std::array AttributeNames{
            std::string_view("id"),        std::string_view("name"),       std::string_view("class"),
            std::string_view("style"),     std::string_view("text"),       std::string_view("slot"),
            std::string_view("template"),  std::string_view("src"),        std::string_view("schemaVersion"),
            std::string_view("value"),     std::string_view("minimum"),    std::string_view("maximum"),
            std::string_view("checked"),   std::string_view("enabled"),    std::string_view("accessibility-label"),
            std::string_view("bind:text"), std::string_view("bind:value"), std::string_view("bind-two-way:value"),
        };

        [[nodiscard]] bool NameCharacter(const char value) noexcept
        {
            return std::isalnum(static_cast<unsigned char>(value)) != 0 || value == '-' || value == '_' ||
                   value == ':' || value == '.';
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

        [[nodiscard]] std::string Documentation(const std::string_view value)
        {
            if (std::ranges::find(ElementNames, value) != ElementNames.end())
                return "Kéire retained-UI element <" + std::string(value) + ">.";
            if (value == "id")
                return "Optional stable element ID. Omit it to generate one automatically when source is applied; "
                       "keep an explicit ID only when identity must be pinned for bindings or debugging.";
            if (value == "class")
                return "Space-separated style classes matched by .class selectors.";
            if (value == "style")
                return "Inline declarations. Prefer classes for reusable presentation.";
            if (value.starts_with("bind"))
                return "Data binding from this target property to a managed source path.";
            return "Kéire UI markup attribute `" + std::string(value) + "`.";
        }
    } // namespace

    void UiMarkupSourceEditor::SetSource(std::string source)
    {
        m_Source = std::move(source);
        m_Cursor = std::min(m_Cursor, m_Source.size());
        Analyze();
    }

    void UiMarkupSourceEditor::SetCursor(const std::size_t offset) noexcept
    {
        m_Cursor = std::min(offset, m_Source.size());
    }

    UiMarkupSourceLocation UiMarkupSourceEditor::CursorLocation() const noexcept { return Location(m_Cursor); }

    std::optional<std::string> UiMarkupSourceEditor::HoverDocumentation(const std::size_t offset) const
    {
        const auto found =
            std::ranges::find_if(m_Tokens, [offset](const auto& token)
                                 { return offset >= token.Offset && offset < token.Offset + token.Length; });
        if (found == m_Tokens.end() ||
            (found->Kind != UiMarkupSourceTokenKind::Element && found->Kind != UiMarkupSourceTokenKind::Attribute))
        {
            return std::nullopt;
        }
        return Documentation(std::string_view(m_Source).substr(found->Offset, found->Length));
    }

    std::vector<UiMarkupSourceCompletion> UiMarkupSourceEditor::Completions(const std::size_t offset,
                                                                            const std::size_t maximum) const
    {
        if (maximum == 0U)
            return {};
        const auto cursor = std::min(offset, m_Source.size());
        auto begin = cursor;
        while (begin > 0U && NameCharacter(m_Source[begin - 1U]))
            --begin;
        const auto prefix = std::string_view(m_Source).substr(begin, cursor - begin);
        const auto tagStart = m_Source.rfind('<', cursor);
        const auto tagEnd = m_Source.rfind('>', cursor);
        const bool insideTag = tagStart != std::string::npos && (tagEnd == std::string::npos || tagStart > tagEnd);
        if (!insideTag)
            return {};

        char quote = 0;
        for (auto index = tagStart + 1U; index < cursor; ++index)
        {
            if (m_Source[index] != '"' && m_Source[index] != '\'')
                continue;
            if (quote == 0)
                quote = m_Source[index];
            else if (quote == m_Source[index])
                quote = 0;
        }
        if (quote != 0)
        {
            const auto equals = m_Source.rfind('=', cursor);
            if (equals == std::string::npos || equals < tagStart)
                return {};
            auto nameBegin = equals;
            while (nameBegin > tagStart + 1U && NameCharacter(m_Source[nameBegin - 1U]))
                --nameBegin;
            const auto name = std::string_view(m_Source).substr(nameBegin, equals - nameBegin);
            std::span<const std::string_view> values;
            constexpr std::array booleanValues{std::string_view("true"), std::string_view("false")};
            constexpr std::array schemaValues{std::string_view("1")};
            if (name == "checked" || name == "enabled")
                values = booleanValues;
            else if (name == "schemaVersion")
                values = schemaValues;
            else
                return {};
            std::vector<std::pair<int, std::string_view>> ranked;
            for (const auto value : values)
                if (const auto score = MatchScore(value, prefix); score >= 0)
                    ranked.emplace_back(score, value);
            std::ranges::sort(ranked, {}, &std::pair<int, std::string_view>::first);
            std::vector<UiMarkupSourceCompletion> result;
            for (const auto& [score, value] : ranked | std::views::take(maximum))
            {
                (void)score;
                result.push_back(
                    {std::string(value), std::string(value), "Valid value for `" + std::string(name) + "`."});
            }
            return result;
        }

        const bool elementContext =
            insideTag && (begin <= tagStart + 1U || (tagStart + 1U < m_Source.size() &&
                                                     m_Source[tagStart + 1U] == '/' && begin <= tagStart + 2U));
        std::vector<std::pair<int, std::string_view>> ranked;
        const auto append = [&](const std::span<const std::string_view> values, const int priority = 0)
        {
            for (const auto value : values)
            {
                if (!elementContext &&
                    std::string_view(m_Source).substr(tagStart, cursor - tagStart).find(std::string(value) + "=") !=
                        std::string_view::npos)
                    continue;
                if (const auto score = MatchScore(value, prefix); score >= 0)
                    ranked.emplace_back(priority + score, value);
            }
        };
        if (elementContext)
        {
            append({ElementNames.data(), ElementNames.size()});
        }
        else
        {
            const auto nameStart = tagStart + 1U + (m_Source[tagStart + 1U] == '/' ? 1U : 0U);
            const auto nameEnd = m_Source.find_first_of(" \t\r\n/>", nameStart);
            const auto elementName = std::string_view(m_Source).substr(nameStart, nameEnd - nameStart);
            constexpr std::array textAttributes{std::string_view("text")};
            constexpr std::array imageAttributes{std::string_view("src")};
            constexpr std::array valueAttributes{std::string_view("value"), std::string_view("minimum"),
                                                 std::string_view("maximum")};
            if (elementName == "Label" || elementName == "Button" || elementName == "TextField")
                append(textAttributes, -50);
            if (elementName == "Image")
                append(imageAttributes, -50);
            if (elementName == "Slider" || elementName == "ProgressBar")
                append(valueAttributes, -50);
            append({AttributeNames.data(), AttributeNames.size()});
        }
        std::ranges::sort(
            ranked, [](const auto& left, const auto& right)
            { return left.first != right.first ? left.first < right.first : left.second < right.second; });
        std::vector<UiMarkupSourceCompletion> result;
        for (const auto& [score, value] : ranked)
        {
            (void)score;
            if (std::ranges::find(result, value, &UiMarkupSourceCompletion::Label) != result.end())
                continue;
            const auto insertion = elementContext ? std::string(value) : std::string(value) + "=\"\"";
            result.push_back({std::string(value), insertion, Documentation(value)});
            if (result.size() >= maximum)
                break;
        }
        return result;
    }

    bool UiMarkupSourceEditor::ApplyCompletion(const std::size_t offset, const UiMarkupSourceCompletion& completion)
    {
        if (completion.Insertion.empty())
            return false;
        const auto cursor = std::min(offset, m_Source.size());
        auto begin = cursor;
        while (begin > 0U && NameCharacter(m_Source[begin - 1U]))
            --begin;
        m_Source.replace(begin, cursor - begin, completion.Insertion);
        m_Cursor = begin + completion.Insertion.size();
        if (completion.Insertion.ends_with("=\"\""))
            --m_Cursor;
        Analyze();
        return true;
    }

    void UiMarkupSourceEditor::Analyze()
    {
        m_Tokens.clear();
        m_LineOffsets.assign(1U, 0U);
        for (std::size_t index = 0; index < m_Source.size(); ++index)
            if (m_Source[index] == '\n')
                m_LineOffsets.push_back(index + 1U);

        bool insideTag = false;
        bool expectingElement = false;
        for (std::size_t index = 0; index < m_Source.size();)
        {
            if (m_Source.compare(index, 4U, "<!--") == 0)
            {
                const auto end = m_Source.find("-->", index + 4U);
                const auto length = end == std::string::npos ? m_Source.size() - index : end + 3U - index;
                const auto location = Location(index);
                m_Tokens.push_back({UiMarkupSourceTokenKind::Comment, index, length, location.Line, location.Column});
                index += length;
                continue;
            }
            if (m_Source.compare(index, 2U, "<?") == 0)
            {
                const auto end = m_Source.find("?>", index + 2U);
                const auto length = end == std::string::npos ? m_Source.size() - index : end + 2U - index;
                const auto location = Location(index);
                m_Tokens.push_back(
                    {UiMarkupSourceTokenKind::Declaration, index, length, location.Line, location.Column});
                index += length;
                continue;
            }
            if (m_Source[index] == '<')
            {
                const auto location = Location(index);
                m_Tokens.push_back({UiMarkupSourceTokenKind::Punctuation, index, 1U, location.Line, location.Column});
                insideTag = true;
                expectingElement = true;
                ++index;
                if (index < m_Source.size() && m_Source[index] == '/')
                {
                    const auto slash = Location(index);
                    m_Tokens.push_back({UiMarkupSourceTokenKind::Punctuation, index, 1U, slash.Line, slash.Column});
                    ++index;
                }
                continue;
            }
            if (insideTag && m_Source[index] == '>')
            {
                const auto location = Location(index);
                m_Tokens.push_back({UiMarkupSourceTokenKind::Punctuation, index, 1U, location.Line, location.Column});
                insideTag = false;
                expectingElement = false;
                ++index;
                continue;
            }
            if (insideTag && (m_Source[index] == '"' || m_Source[index] == '\''))
            {
                const auto quote = m_Source[index];
                auto end = index + 1U;
                while (end < m_Source.size() && m_Source[end] != quote)
                    ++end;
                end = std::min(m_Source.size(), end + 1U);
                const auto location = Location(index);
                m_Tokens.push_back(
                    {UiMarkupSourceTokenKind::Value, index, end - index, location.Line, location.Column});
                index = end;
                continue;
            }
            if (insideTag && NameCharacter(m_Source[index]))
            {
                auto end = index + 1U;
                while (end < m_Source.size() && NameCharacter(m_Source[end]))
                    ++end;
                const auto location = Location(index);
                m_Tokens.push_back(
                    {expectingElement ? UiMarkupSourceTokenKind::Element : UiMarkupSourceTokenKind::Attribute, index,
                     end - index, location.Line, location.Column});
                expectingElement = false;
                index = end;
                continue;
            }
            ++index;
        }
    }

    UiMarkupSourceLocation UiMarkupSourceEditor::Location(const std::size_t offset) const noexcept
    {
        const auto clamped = std::min(offset, m_Source.size());
        const auto line = std::upper_bound(m_LineOffsets.begin(), m_LineOffsets.end(), clamped);
        const auto lineIndex =
            line == m_LineOffsets.begin() ? 0U : static_cast<std::size_t>(line - m_LineOffsets.begin() - 1);
        return {.Offset = clamped, .Line = lineIndex + 1U, .Column = clamped - m_LineOffsets[lineIndex] + 1U};
    }
} // namespace KeireEditor
