#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace KeireEditor
{
    enum class UiMarkupSourceTokenKind : std::uint8_t
    {
        Declaration,
        Element,
        Attribute,
        Value,
        Comment,
        Punctuation,
        Invalid
    };

    struct UiMarkupSourceToken final
    {
        UiMarkupSourceTokenKind Kind = UiMarkupSourceTokenKind::Invalid;
        std::size_t Offset = 0;
        std::size_t Length = 0;
        std::size_t Line = 1;
        std::size_t Column = 1;

        [[nodiscard]] bool operator==(const UiMarkupSourceToken&) const = default;
    };

    struct UiMarkupSourceCompletion final
    {
        std::string Label;
        std::string Insertion;
        std::string Documentation;

        [[nodiscard]] bool operator==(const UiMarkupSourceCompletion&) const = default;
    };

    struct UiMarkupSourceLocation final
    {
        std::size_t Offset = 0;
        std::size_t Line = 1;
        std::size_t Column = 1;

        [[nodiscard]] bool operator==(const UiMarkupSourceLocation&) const = default;
    };

    class UiMarkupSourceEditor final
    {
      public:
        void SetSource(std::string source);
        [[nodiscard]] const std::string& Source() const noexcept { return m_Source; }
        [[nodiscard]] std::span<const UiMarkupSourceToken> Tokens() const noexcept { return m_Tokens; }
        [[nodiscard]] std::size_t LineCount() const noexcept { return m_LineOffsets.size(); }

        void SetCursor(std::size_t offset) noexcept;
        [[nodiscard]] std::size_t Cursor() const noexcept { return m_Cursor; }
        [[nodiscard]] UiMarkupSourceLocation CursorLocation() const noexcept;
        [[nodiscard]] std::optional<std::string> HoverDocumentation(std::size_t offset) const;
        [[nodiscard]] std::vector<UiMarkupSourceCompletion> Completions(std::size_t offset,
                                                                        std::size_t maximum = 32U) const;
        [[nodiscard]] bool ApplyCompletion(std::size_t offset, const UiMarkupSourceCompletion& completion);

      private:
        void Analyze();
        [[nodiscard]] UiMarkupSourceLocation Location(std::size_t offset) const noexcept;

        std::string m_Source;
        std::vector<UiMarkupSourceToken> m_Tokens;
        std::vector<std::size_t> m_LineOffsets{0U};
        std::size_t m_Cursor = 0;
    };
} // namespace KeireEditor
