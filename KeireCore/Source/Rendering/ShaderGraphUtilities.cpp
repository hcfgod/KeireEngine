#include "Keire/Rendering/ShaderGraph.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <iomanip>
#include <ranges>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace Keire
{
    namespace
    {
        [[nodiscard]] bool ValidIdentifier(const std::string_view value)
        {
            if (value.empty() || value.size() > 128 ||
                !(std::isalpha(static_cast<unsigned char>(value.front())) || value.front() == '_'))
                return false;
            return std::ranges::all_of(value.substr(1), [](const unsigned char character)
                                       { return std::isalnum(character) || character == '_'; });
        }

        [[nodiscard]] std::string KeywordSuffix(const std::span<const std::string> keywords)
        {
            std::vector<std::string> canonical(keywords.begin(), keywords.end());
            std::ranges::sort(canonical);
            std::uint64_t hash = 1469598103934665603ULL;
            for (const auto& keyword : canonical)
            {
                for (const char input : keyword)
                {
                    hash ^= static_cast<unsigned char>(input);
                    hash *= 1099511628211ULL;
                }
                hash ^= 0xffU;
                hash *= 1099511628211ULL;
            }
            std::ostringstream result;
            result << std::hex << std::setfill('0') << std::setw(16) << hash;
            return result.str();
        }
    } // namespace

    std::string MakeShaderGraphVariantSubAssetKey(const std::string_view target,
                                                  const std::map<std::string, std::string, std::less<>>& keywords)
    {
        if (target.empty() || target.size() > 64 ||
            !std::ranges::all_of(target, [](const unsigned char character)
                                 { return std::isalnum(character) || character == '_' || character == '-'; }))
            throw std::invalid_argument("Shader Graph target ID is invalid.");
        std::vector<std::string> enabled;
        enabled.reserve(keywords.size());
        for (const auto& [name, option] : keywords)
        {
            if (!ValidIdentifier(name) || (option != "true" && option != "false" && !ValidIdentifier(option)))
                throw std::invalid_argument("Shader Graph keyword selection is invalid.");
            if (option == "true")
                enabled.push_back(name);
            else if (option != "false")
            {
                auto keyword = name;
                keyword += '_';
                keyword += option;
                enabled.push_back(std::move(keyword));
            }
        }
        return MakeShaderGraphVariantSubAssetKey(target, enabled);
    }

    std::string MakeShaderGraphVariantSubAssetKey(const std::string_view target,
                                                  const std::span<const std::string> canonicalKeywords)
    {
        if (target.empty() || target.size() > 64 ||
            !std::ranges::all_of(target, [](const unsigned char character)
                                 { return std::isalnum(character) || character == '_' || character == '-'; }))
            throw std::invalid_argument("Shader Graph target ID is invalid.");
        if (!std::ranges::all_of(canonicalKeywords, ValidIdentifier))
            throw std::invalid_argument("Shader Graph canonical keyword selection is invalid.");
        const auto suffix = KeywordSuffix(canonicalKeywords);
        return target == "default" ? "shader/" + suffix : "shader/" + std::string(target) + '/' + suffix;
    }
} // namespace Keire
