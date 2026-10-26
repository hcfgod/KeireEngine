#pragma once

#include "Keire/Assets/RenderingAssets.h"

#include <cmath>
#include <type_traits>
#include <variant>

namespace Keire::RenderBackend
{
    [[nodiscard]] inline bool ValidGlobalMaterialProperty(const MaterialPropertyValue& value) noexcept
    {
        return std::visit(
            [](const auto& property) noexcept
            {
                using Value = std::decay_t<decltype(property)>;
                if constexpr (std::is_same_v<Value, AssetId>)
                {
                    return true;
                }
                else if constexpr (std::is_same_v<Value, float>)
                {
                    return std::isfinite(property);
                }
                else
                {
                    return Math::IsFinite(property);
                }
            },
            value);
    }
} // namespace Keire::RenderBackend
