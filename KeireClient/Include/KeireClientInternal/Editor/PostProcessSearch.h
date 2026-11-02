#pragma once

#include <algorithm>
#include <cctype>
#include <string_view>

namespace KeireEditor::Detail
{
    [[nodiscard]] inline bool PostProcessSearchMatches(const std::string_view query, const std::string_view text)
    {
        return query.empty() || std::search(text.begin(), text.end(), query.begin(), query.end(),
                                            [](const unsigned char left, const unsigned char right)
                                            { return std::tolower(left) == std::tolower(right); }) != text.end();
    }

    [[nodiscard]] inline bool PostProcessParameterMatches(const std::string_view query, const std::string_view category,
                                                          const std::string_view label, const std::string_view unit)
    {
        return PostProcessSearchMatches(query, category) || PostProcessSearchMatches(query, label) ||
               PostProcessSearchMatches(query, unit);
    }
} // namespace KeireEditor::Detail
