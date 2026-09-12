#include "KeireClient/Editor/ShaderGraphSourceView.h"

#include <algorithm>

namespace KeireEditor
{
    namespace
    {
        [[nodiscard]] bool IdentifierCharacter(const char value) noexcept
        {
            return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') || (value >= '0' && value <= '9') ||
                   value == '_';
        }
    } // namespace

    std::vector<ShaderGraphSourceLine> ShaderGraphSourcePage(const std::string_view source, const std::size_t firstLine,
                                                             const std::size_t maximumLines)
    {
        std::vector<ShaderGraphSourceLine> result;
        if (source.empty() || maximumLines == 0)
            return result;
        const auto first = std::max(std::size_t{1}, firstLine);
        std::size_t offset = 0;
        std::size_t number = 1;
        while (offset < source.size() && result.size() < maximumLines)
        {
            const auto end = source.find('\n', offset);
            auto line = source.substr(offset, end == std::string_view::npos ? end : end - offset);
            if (line.ends_with('\r'))
                line.remove_suffix(1);
            if (number >= first)
                result.push_back({number, line});
            if (end == std::string_view::npos)
                break;
            offset = end + 1;
            ++number;
        }
        return result;
    }

    std::optional<std::size_t> FindShaderGraphSourceSymbol(const std::string_view source, const std::string_view symbol)
    {
        if (symbol.empty())
            return std::nullopt;
        std::size_t offset = 0;
        while ((offset = source.find(symbol, offset)) != std::string_view::npos)
        {
            const auto end = offset + symbol.size();
            if ((offset == 0 || !IdentifierCharacter(source[offset - 1])) &&
                (end == source.size() || !IdentifierCharacter(source[end])))
                return 1 + static_cast<std::size_t>(std::count(source.begin(), source.begin() + offset, '\n'));
            offset = end;
        }
        return std::nullopt;
    }
} // namespace KeireEditor
