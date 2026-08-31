#pragma once

#include "Keire/Ui/UiStyleProperties.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace KeireEditor
{
    enum class UiStyleSourceTokenKind : std::uint8_t
    {
        Header,
        Selector,
        Property,
        Value,
        Variable,
        Number,
        String,
        Comment,
        Punctuation,
        Invalid
    };

    struct UiStyleSourceToken final
    {
        UiStyleSourceTokenKind Kind = UiStyleSourceTokenKind::Invalid;
        std::size_t Offset = 0;
        std::size_t Length = 0;
        std::size_t Line = 1;
        std::size_t Column = 1;

        [[nodiscard]] bool operator==(const UiStyleSourceToken&) const = default;
    };

    struct UiStyleSourceCompletion final
    {
        std::string Label;
        std::string Insertion;
        std::string Documentation;

        [[nodiscard]] bool operator==(const UiStyleSourceCompletion&) const = default;
    };

    struct UiStyleSourceMatch final
    {
        std::size_t Offset = 0;
        std::size_t Length = 0;
        std::size_t Line = 1;
        std::size_t Column = 1;

        [[nodiscard]] bool operator==(const UiStyleSourceMatch&) const = default;
    };

    struct UiStyleSourceRuleLocation final
    {
        std::string Selector;
        std::size_t Offset = 0;
        std::size_t Line = 1;
        std::size_t Column = 1;

        [[nodiscard]] bool operator==(const UiStyleSourceRuleLocation&) const = default;
    };

    class UiStyleSourceEditor final
    {
      public:
        void SetSource(std::string source);
        [[nodiscard]] const std::string& Source() const noexcept { return m_Source; }
        [[nodiscard]] std::span<const UiStyleSourceToken> Tokens() const noexcept { return m_Tokens; }
        [[nodiscard]] std::span<const UiStyleSourceRuleLocation> Rules() const noexcept { return m_Rules; }
        [[nodiscard]] std::size_t LineCount() const noexcept { return m_LineOffsets.size(); }

        void SetCursor(std::size_t offset) noexcept;
        [[nodiscard]] std::size_t Cursor() const noexcept { return m_Cursor; }
        [[nodiscard]] UiStyleSourceMatch CursorLocation() const noexcept;
        [[nodiscard]] std::optional<std::size_t> MatchingBrace(std::size_t offset) const noexcept;
        [[nodiscard]] std::optional<std::string> HoverDocumentation(std::size_t offset) const;
        [[nodiscard]] std::vector<UiStyleSourceCompletion> Completions(std::size_t offset,
                                                                       std::size_t maximum = 64U) const;
        [[nodiscard]] bool ApplyCompletion(std::size_t offset, const UiStyleSourceCompletion& completion);
        [[nodiscard]] std::vector<UiStyleSourceMatch> Find(std::string_view value, bool caseSensitive = false,
                                                           std::size_t maximum = 4096U) const;
        [[nodiscard]] std::size_t ReplaceAll(std::string_view value, std::string_view replacement,
                                             bool caseSensitive = false);
        [[nodiscard]] bool GoToRule(std::string_view selector) noexcept;
        [[nodiscard]] bool Format();

      private:
        void Analyze();
        [[nodiscard]] UiStyleSourceMatch Location(std::size_t offset, std::size_t length = 0U) const noexcept;

        std::string m_Source;
        std::vector<UiStyleSourceToken> m_Tokens;
        std::vector<UiStyleSourceRuleLocation> m_Rules;
        std::vector<std::size_t> m_LineOffsets{0U};
        std::size_t m_Cursor = 0;
    };
} // namespace KeireEditor
