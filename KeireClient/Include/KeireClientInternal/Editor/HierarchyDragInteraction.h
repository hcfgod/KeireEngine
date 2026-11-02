#pragma once

#include "Keire/Assets/Asset.h"

#include <algorithm>
#include <span>
#include <string_view>
#include <unordered_map>

namespace KeireEditor
{
    enum class HierarchyDropZone
    {
        Before,
        Child,
        After
    };

    [[nodiscard]] inline HierarchyDropZone ResolveHierarchyDropZone(const float relativeY, const bool filtered)
    {
        // Search results are not sibling order: only parenting is meaningful there.
        if (filtered)
        {
            return HierarchyDropZone::Child;
        }
        if (relativeY < 0.30F)
        {
            return HierarchyDropZone::Before;
        }
        return relativeY > 0.70F ? HierarchyDropZone::After : HierarchyDropZone::Child;
    }

    [[nodiscard]] inline std::string_view
    ValidateHierarchyDrop(const std::span<const Keire::AssetId> dragged, const Keire::AssetId parent,
                          const Keire::AssetId beforeSibling,
                          const std::unordered_map<Keire::AssetId, Keire::AssetId>& parents)
    {
        if (beforeSibling && std::ranges::find(dragged, beforeSibling) != dragged.end())
        {
            return "The insertion target is part of the selection.";
        }
        auto ancestor = parent;
        for (std::size_t depth = 0; ancestor && depth <= parents.size(); ++depth)
        {
            if (std::ranges::find(dragged, ancestor) != dragged.end())
            {
                return "Cannot parent an entity to itself or one of its descendants.";
            }
            const auto found = parents.find(ancestor);
            if (found == parents.end())
            {
                return "The destination is no longer in the scene.";
            }
            ancestor = found->second;
        }
        return ancestor ? "The destination hierarchy contains a cycle." : std::string_view{};
    }

    struct HierarchyDragHover
    {
        Keire::AssetId Target;
        double StartedAt = 0.0;

        [[nodiscard]] bool Update(const Keire::AssetId target, const double now)
        {
            if (target != Target)
            {
                Target = target;
                StartedAt = now;
            }
            constexpr double expandDelaySeconds = 0.65;
            return Target && now - StartedAt >= expandDelaySeconds;
        }
    };
} // namespace KeireEditor
