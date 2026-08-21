#include "KeireInternal/Rendering/ShaderGraphIdentity.h"

#include <cstddef>
#include <cstdint>

namespace Keire::Detail
{
    AssetId StableMigratedShaderPinId(const AssetId node, const std::string_view name,
                                      const ShaderGraphPinDirection direction) noexcept
    {
        const auto hash = [name, direction](std::uint64_t value)
        {
            value ^= static_cast<std::uint8_t>(direction);
            value *= 1099511628211ULL;
            for (const char input : name)
            {
                const auto character = static_cast<unsigned char>(input);
                value ^= character;
                value *= 1099511628211ULL;
            }
            return value;
        };
        auto high = hash(node.High() ^ 0x4d4750494e484947ULL);
        auto low = hash(node.Low() ^ 0x4d4750494e4c4f57ULL);
        if ((high | low) == 0U)
            low = 1U;
        return {high, low};
    }

    AssetId DerivedShaderFunctionElementId(const AssetId call, const AssetId source,
                                           const std::string_view role) noexcept
    {
        std::uint64_t high = call.High() ^ 0x46554e4354494f4eULL;
        std::uint64_t low = call.Low() ^ 0x455850414e53494fULL;
        const auto mix = [&](const std::uint8_t value)
        {
            high = (high ^ value) * 1099511628211ULL;
            low ^= static_cast<std::uint64_t>(value) + 0x9e3779b97f4a7c15ULL + (low << 6U) + (low >> 2U);
        };
        const auto mixInteger = [&](const std::uint64_t value)
        {
            for (std::size_t shift = 0; shift < 64; shift += 8)
                mix(static_cast<std::uint8_t>(value >> shift));
        };
        mixInteger(source.High());
        mixInteger(source.Low());
        for (const char character : role)
            mix(static_cast<unsigned char>(character));
        high = (high & 0xffffffffffff0fffULL) | 0x0000000000005000ULL;
        low = (low & 0x3fffffffffffffffULL) | 0x8000000000000000ULL;
        if ((high | low) == 0U)
            low = 1U;
        return {high, low};
    }
} // namespace Keire::Detail
