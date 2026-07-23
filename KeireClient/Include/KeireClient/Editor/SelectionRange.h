#pragma once

#include "Keire/Assets/Asset.h"

#include <algorithm>
#include <span>
#include <vector>

namespace KeireEditor
{
    [[nodiscard]] inline std::vector<Keire::AssetId>
    BuildRangeSelection(const std::span<const Keire::AssetId> order, const Keire::AssetId anchor,
                        const Keire::AssetId target, const std::span<const Keire::AssetId> existing = {},
                        const bool additive = false)
    {
        std::vector<Keire::AssetId> result;
        if (additive)
            result.assign(existing.begin(), existing.end());

        const auto anchorIterator = std::ranges::find(order, anchor);
        const auto targetIterator = std::ranges::find(order, target);
        if (anchorIterator == order.end() || targetIterator == order.end())
        {
            if (std::ranges::find(result, target) == result.end())
                result.push_back(target);
            return result;
        }

        const auto anchorIndex = static_cast<std::size_t>(anchorIterator - order.begin());
        const auto targetIndex = static_cast<std::size_t>(targetIterator - order.begin());
        const auto first = std::min(anchorIndex, targetIndex);
        const auto last = std::max(anchorIndex, targetIndex);
        for (std::size_t index = first; index <= last; ++index)
            if (order[index] != target && std::ranges::find(result, order[index]) == result.end())
                result.push_back(order[index]);
        if (std::ranges::find(result, target) == result.end())
            result.push_back(target);
        return result;
    }
} // namespace KeireEditor
