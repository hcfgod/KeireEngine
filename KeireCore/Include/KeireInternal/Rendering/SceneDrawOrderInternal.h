#pragma once

#include "Keire/Assets/RenderingAssets.h"
#include "KeireInternal/Rendering/TransparencyInternal.h"

#include <cstdint>
#include <tuple>

namespace Keire::Detail
{
    struct SceneDrawOrderKey final
    {
        MaterialAlphaMode AlphaMode = MaterialAlphaMode::Opaque;
        AssetId Material;
        AssetId Mesh;
        std::uint32_t SubmeshIndex = 0;
        bool ReceiveShadows = true;
        bool CastShadows = true;
        float Depth = 0.0F;
        std::uint32_t ContributionOrder = 0;
        AssetId Entity;

        bool operator==(const SceneDrawOrderKey&) const = default;
    };

    [[nodiscard]] inline bool SceneDrawLess(const SceneDrawOrderKey& left, const SceneDrawOrderKey& right,
                                            const bool backToFront) noexcept
    {
        // The queue owns the ordering policy: opaque-state decals also enter depth-ordered queues.
        if (backToFront && left.Depth != right.Depth)
            return TransparentBackToFront(left.Depth, right.Depth);
        if (!backToFront)
        {
            const auto leftKey = std::tie(left.AlphaMode, left.Material, left.Mesh, left.SubmeshIndex,
                                          left.ReceiveShadows, left.CastShadows, left.Depth);
            const auto rightKey = std::tie(right.AlphaMode, right.Material, right.Mesh, right.SubmeshIndex,
                                           right.ReceiveShadows, right.CastShadows, right.Depth);
            if (leftKey != rightKey)
                return leftKey < rightKey;
        }
        return std::tie(left.ContributionOrder, left.Entity, left.SubmeshIndex) <
               std::tie(right.ContributionOrder, right.Entity, right.SubmeshIndex);
    }
} // namespace Keire::Detail
