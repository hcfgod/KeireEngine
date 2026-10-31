#pragma once

#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <system_error>
#include <type_traits>

namespace Keire::Detail
{
    inline constexpr std::size_t MaximumScalarDecimalCharacters = 768;

    template <typename T> [[nodiscard]] std::string ScalarEditText(const T value)
    {
        static_assert(std::is_floating_point_v<T>);
        if (!std::isfinite(value))
        {
            throw std::invalid_argument("Numeric UI values must be finite.");
        }
        if (value == T{0})
        {
            return std::signbit(value) ? "-0" : "0";
        }
        std::array<char, MaximumScalarDecimalCharacters> buffer{};
        const auto result =
            std::to_chars(buffer.data(), buffer.data() + buffer.size(), value, std::chars_format::fixed);
        if (result.ec != std::errc{})
        {
            throw std::runtime_error("Could not format a numeric UI value.");
        }
        return std::string(buffer.data(), result.ptr);
    }

    [[nodiscard]] inline std::string ExpandScalarExponent(std::string text)
    {
        const auto exponentMarker = text.find_first_of("eE");
        if (exponentMarker == std::string::npos)
        {
            return text;
        }
        int exponent = 0;
        auto* exponentBegin = text.data() + exponentMarker + 1;
        if (*exponentBegin == '+')
        {
            ++exponentBegin;
        }
        const auto parsed = std::from_chars(exponentBegin, text.data() + text.size(), exponent);
        if (parsed.ec != std::errc{})
        {
            throw std::runtime_error("Could not expand a numeric UI exponent.");
        }
        const bool negative = text.front() == '-';
        const auto firstDigit = negative ? 1U : 0U;
        auto digits = text.substr(firstDigit, exponentMarker - firstDigit);
        const auto point = digits.find('.');
        const auto integerDigits = point == std::string::npos ? digits.size() : point;
        if (point != std::string::npos)
        {
            digits.erase(point, 1);
        }
        const auto decimalPosition = static_cast<int>(integerDigits) + exponent;
        std::string expanded = negative ? "-" : "";
        if (decimalPosition <= 0)
        {
            expanded += "0.";
            expanded.append(static_cast<std::size_t>(-decimalPosition), '0');
            expanded += digits;
        }
        else if (decimalPosition >= static_cast<int>(digits.size()))
        {
            expanded += digits;
            expanded.append(static_cast<std::size_t>(decimalPosition) - digits.size(), '0');
        }
        else
        {
            digits.insert(static_cast<std::size_t>(decimalPosition), 1, '.');
            expanded += digits;
        }
        return expanded;
    }

    template <typename T> [[nodiscard]] std::string ScalarDisplayText(const T value)
    {
        static_assert(std::is_floating_point_v<T>);
        if (!std::isfinite(value))
        {
            return ScalarEditText(value);
        }
        if (value == T{0})
        {
            return "0";
        }
        // Six significant digits keep promoted float settings readable. Editing uses the full round-trip value.
        constexpr int significantDigits = 6;
        std::array<char, MaximumScalarDecimalCharacters> buffer{};
        const auto result = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value,
                                          std::chars_format::general, significantDigits);
        if (result.ec != std::errc{})
        {
            throw std::runtime_error("Could not format a numeric UI value.");
        }
        return ExpandScalarExponent(std::string(buffer.data(), result.ptr));
    }
} // namespace Keire::Detail
