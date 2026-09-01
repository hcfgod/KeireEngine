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
        const auto prefix = Lower(m_Source.substr(begin, cursor - begin));
        const auto tagStart = m_Source.rfind('<', cursor);
        const auto tagEnd = m_Source.rfind('>', cursor);
        const bool insideTag = tagStart != std::string::npos && (tagEnd == std::string::npos || tagStart > tagEnd);
        const bool elementContext =
            insideTag && (begin <= tagStart + 1U || (tagStart + 1U < m_Source.size() &&
                                                     m_Source[tagStart + 1U] == '/' && begin <= tagStart + 2U));
        std::vector<UiMarkupSourceCompletion> result;
        const auto append = [&](const std::span<const std::string_view> values)
        {
            for (const auto value : values)
            {
                if (!prefix.empty() && !Lower(std::string(value)).starts_with(prefix))
                    continue;
                const auto insertion = elementContext ? std::string(value) : std::string(value) + "=\"\"";
                result.push_back({std::string(value), insertion, Documentation(value)});
                if (result.size() >= maximum)
                    break;
            }
        };
        if (elementContext)
            append({ElementNames.data(), ElementNames.size()});
        else
            append({AttributeNames.data(), AttributeNames.size()});
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
