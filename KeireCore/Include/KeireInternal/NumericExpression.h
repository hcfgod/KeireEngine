#pragma once

#include <charconv>
#include <cmath>
#include <limits>
#include <optional>
#include <string_view>
#include <system_error>
#include <type_traits>

namespace Keire::Detail
{
    // A deliberately bounded arithmetic grammar; never executes scripts or accepts non-finite results.
    class NumericExpressionParser final
    {
      public:
        explicit NumericExpressionParser(const std::string_view text) : m_Text(text) {}
        [[nodiscard]] std::optional<double> Parse()
        {
            if (m_Text.size() > 256)
                return std::nullopt;
            const auto value = Sum(0);
            Space();
            return m_Valid && m_Position == m_Text.size() && std::isfinite(value) ? std::optional<double>(value)
                                                                                  : std::nullopt;
        }

      private:
        void Space()
        {
            while (m_Position < m_Text.size() && (m_Text[m_Position] == ' ' || m_Text[m_Position] == '\t'))
                ++m_Position;
        }
        bool Take(const char token)
        {
            Space();
            if (m_Position == m_Text.size() || m_Text[m_Position] != token)
                return false;
            ++m_Position;
            return true;
        }
        double Sum(const unsigned depth)
        {
            auto value = Product(depth);
            while (m_Valid)
            {
                if (Take('+'))
                    value += Product(depth);
                else if (Take('-'))
                    value -= Product(depth);
                else
                    break;
            }
            return value;
        }
        double Product(const unsigned depth)
        {
            auto value = Atom(depth);
            while (m_Valid)
            {
                if (Take('*'))
                    value *= Atom(depth);
                else if (Take('/'))
                {
                    const auto divisor = Atom(depth);
                    if (divisor == 0.0)
                    {
                        m_Valid = false;
                        return 0.0;
                    }
                    value /= divisor;
                }
                else
                    break;
            }
            return value;
        }
        double Atom(const unsigned depth)
        {
            if (depth > 32)
            {
                m_Valid = false;
                return 0.0;
            }
            if (Take('+'))
                return Atom(depth + 1);
            if (Take('-'))
                return -Atom(depth + 1);
            if (Take('('))
            {
                const auto value = Sum(depth + 1);
                if (!Take(')'))
                    m_Valid = false;
                return value;
            }
            Space();
            if (m_Position == m_Text.size())
            {
                m_Valid = false;
                return 0.0;
            }
            double value = 0.0;
            const auto begin = m_Text.data() + m_Position;
            const auto result = std::from_chars(begin, m_Text.data() + m_Text.size(), value);
            if (result.ec != std::errc{} || result.ptr == begin || !std::isfinite(value))
                m_Valid = false;
            m_Position = static_cast<std::size_t>(result.ptr - m_Text.data());
            return value;
        }
        std::string_view m_Text;
        std::size_t m_Position = 0;
        bool m_Valid = true;
    };

    [[nodiscard]] inline std::optional<double> EvaluateNumericExpression(const std::string_view expression)
    {
        return NumericExpressionParser(expression).Parse();
    }

    template <typename T> [[nodiscard]] std::optional<T> EvaluateNumericField(std::string_view expression)
    {
        if constexpr (std::is_integral_v<T>)
        {
            const auto first = expression.find_first_not_of(" \t");
            if (first == std::string_view::npos)
                return std::nullopt;
            expression = expression.substr(first, expression.find_last_not_of(" \t") - first + 1);
            if (expression.starts_with('+'))
                expression.remove_prefix(1);
            T exact{};
            const auto parsed = std::from_chars(expression.data(), expression.data() + expression.size(), exact);
            if (parsed.ec == std::errc{} && parsed.ptr == expression.data() + expression.size())
                return exact;
        }
        const auto result = EvaluateNumericExpression(expression);
        if (!result)
            return std::nullopt;
        const auto wide = static_cast<long double>(*result);
        if (wide < static_cast<long double>(std::numeric_limits<T>::lowest()) ||
            wide > static_cast<long double>(std::numeric_limits<T>::max()))
            return std::nullopt;
        if constexpr (std::is_integral_v<T>)
        {
            // Keep full-width integer literals exact; arithmetic outside double's exact integer range is rejected.
            if (std::trunc(*result) != *result || std::abs(*result) > 9007199254740991.0 ||
                *result >= std::ldexp(1.0, std::numeric_limits<T>::digits))
                return std::nullopt;
        }
        return static_cast<T>(*result);
    }
} // namespace Keire::Detail
