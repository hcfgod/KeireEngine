#pragma once

#include "Keire/Api.h"

#include <array>
#include <compare>
#include <optional>

namespace Keire
{
    struct AssetBounds
    {
        std::array<float, 3> Minimum{};
        std::array<float, 3> Maximum{};

        auto operator<=>(const AssetBounds&) const noexcept = default;
    };

    struct AssetDerivedMetadata
    {
        std::optional<AssetBounds> LocalBounds;

        auto operator<=>(const AssetDerivedMetadata&) const noexcept = default;
    };
} // namespace Keire
