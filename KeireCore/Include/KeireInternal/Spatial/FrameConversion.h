#pragma once

#include "KeireInternal/Spatial/FrameRegistry.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <span>
#include <stdexcept>
#include <vector>

namespace Keire::Detail
{
    // Owner-serialized operation. Input snapshots are borrowed only for this synchronous call.
    [[nodiscard]] inline SpatialMotion ConvertFrameMotion(const FrameRegistry& registry, const LiveFrame& source,
                                                          const LiveFrame& target, const SpatialMotion& motion,
                                                          const std::span<const BoundFrameSnapshot> snapshots,
                                                          const double time)
    {
        const auto sourceIdentity = registry.Identity(source);
        const auto targetIdentity = registry.Identity(target);
        if (motion.Position.Identity() != sourceIdentity || !std::isfinite(time))
        {
            throw std::invalid_argument("Frame conversion source or evaluation time is invalid.");
        }
        for (std::size_t axis = 0; axis < 3; ++axis)
        {
            if (!std::isfinite(motion.Velocity[axis]) || !std::isfinite(motion.Acceleration[axis]))
            {
                throw std::invalid_argument("Frame conversion motion must be finite.");
            }
        }
        std::map<std::uint64_t, const FrameSnapshot*> evaluations;
        for (const auto& snapshot : snapshots)
        {
            if (!registry.IsCurrent(snapshot, time) ||
                !evaluations.emplace(snapshot.Evaluation().ChildIdentity().Frame, &snapshot.Evaluation()).second)
            {
                throw std::invalid_argument("Frame conversion contains stale or duplicate evaluations.");
            }
        }
        std::vector<std::uint64_t> sourcePath;
        std::map<std::uint64_t, std::size_t> sourceAncestors;
        auto current = source;
        while (true)
        {
            const auto key = registry.Identity(current).Frame;
            sourceAncestors.emplace(key, sourcePath.size());
            sourcePath.push_back(key);
            const auto parent = registry.Parent(current);
            if (!parent)
            {
                break;
            }
            current = *parent;
        }
        std::vector<std::uint64_t> targetPath;
        current = target;
        while (!sourceAncestors.contains(registry.Identity(current).Frame))
        {
            targetPath.push_back(registry.Identity(current).Frame);
            const auto parent = registry.Parent(current);
            if (!parent)
            {
                throw std::invalid_argument("Frame conversion has no common ancestor.");
            }
            current = *parent;
        }
        sourcePath.resize(sourceAncestors.at(registry.Identity(current).Frame));
        // Validate the entire route before evaluation; no incomplete result is published on failure.
        for (const auto key : sourcePath)
        {
            if (!evaluations.contains(key))
            {
                throw std::invalid_argument("Missing source-path frame evaluation.");
            }
        }
        for (const auto key : targetPath)
        {
            if (!evaluations.contains(key))
            {
                throw std::invalid_argument("Missing target-path frame evaluation.");
            }
        }
        auto result = motion;
        for (const auto key : sourcePath)
        {
            result = evaluations.at(key)->ToParent(result);
        }
        for (auto edge = targetPath.rbegin(); edge != targetPath.rend(); ++edge)
        {
            result = evaluations.at(*edge)->ToChild(result);
        }
        if (result.Position.Identity() != targetIdentity)
        {
            throw std::logic_error("Frame conversion did not reach its target.");
        }
        return result;
    }
} // namespace Keire::Detail
