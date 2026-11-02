#pragma once

#include <algorithm>
#include <cstddef>
#include <string_view>

namespace KeireEditor::Detail
{
    [[nodiscard]] inline std::string_view PostProcessProfileNameError(const std::string_view name) noexcept
    {
        if (name.empty())
        {
            return "Enter a profile name.";
        }
        // Leave room for the source extension under portable single-component filesystem limits.
        constexpr std::size_t maximumNameBytes = 230;
        if (name.size() > maximumNameBytes)
        {
            return "Use a shorter profile name (230 UTF-8 bytes maximum).";
        }
        if (name.front() == ' ' || name.back() == ' ' || name.back() == '.')
        {
            return "A profile name cannot begin or end with spaces, or end with a period.";
        }
        if (name.find_first_of("<>:\"/\\|?*") != std::string_view::npos ||
            std::ranges::any_of(name, [](const unsigned char value) { return value < 32 || value == 127; }))
        {
            return "The profile name contains a character that cannot be used in a portable file name.";
        }
        auto stem = name.substr(0, name.find('.'));
        const auto last = stem.find_last_not_of(' ');
        stem = last == std::string_view::npos ? std::string_view{} : stem.substr(0, last + 1);
        const auto same = [](const std::string_view value, const std::string_view candidate)
        {
            return value.size() == candidate.size() &&
                   std::equal(value.begin(), value.end(), candidate.begin(), [](const char a, const char b)
                              { return (a >= 'a' && a <= 'z' ? a - 'a' + 'A' : a) == b; });
        };
        if (same(stem, "CON") || same(stem, "PRN") || same(stem, "AUX") || same(stem, "NUL") ||
            (stem.size() == 4 && stem[3] >= '1' && stem[3] <= '9' &&
             (same(stem.substr(0, 3), "COM") || same(stem.substr(0, 3), "LPT"))))
        {
            return "Choose a profile name that is not a reserved device name.";
        }
        return {};
    }
} // namespace KeireEditor::Detail
