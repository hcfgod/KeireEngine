#pragma once

#include "KeireInternal/Spatial/FrameRegistry.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <span>
#include <stdexcept>
#include <vector>

namespace Keire::Detail
{
    struct SpatialBodyState
    {
        SpatialMotion Motion;
        FrameRotation Orientation;
        std::array<double, 3> AngularVelocity{};
    };

    // Rotation and angular velocity use the same live route as positions, without constructing an absolute position.
    inline void ConvertFrameAxes(const FrameRegistry& registry, const LiveFrame& source, const LiveFrame& target,
                                 const std::span<const BoundFrameSnapshot> snapshots, const double time,
                                 FrameRotation& orientation, std::array<double, 3>& angularVelocity)
    {
        (void)registry.Identity(source);
        (void)registry.Identity(target);
        if (!std::isfinite(time))
        {
            throw std::invalid_argument("Frame evaluation time must be finite.");
        }
        for (const auto value : angularVelocity)
        {
            if (!std::isfinite(value))
            {
                throw std::invalid_argument("Body angular velocity must be finite.");
            }
        }
        std::map<std::uint64_t, const FrameSnapshot*> evaluations;
        for (const auto& snapshot : snapshots)
        {
            if (!registry.IsCurrent(snapshot, time) ||
                !evaluations.emplace(snapshot.Evaluation().ChildIdentity().Frame, &snapshot.Evaluation()).second)
            {
                throw std::invalid_argument("Frame axes contain stale or duplicate evaluations.");
            }
        }
        std::vector<std::uint64_t> sourcePath;
        auto current = source;
        while (true)
        {
            sourcePath.push_back(registry.Identity(current).Frame);
            const auto parent = registry.Parent(current);
            if (!parent)
            {
                break;
            }
            current = *parent;
        }
        std::vector<std::uint64_t> targetPath;
        current = target;
        while (std::find(sourcePath.begin(), sourcePath.end(), registry.Identity(current).Frame) == sourcePath.end())
        {
            targetPath.push_back(registry.Identity(current).Frame);
            const auto parent = registry.Parent(current);
            if (!parent)
            {
                throw std::invalid_argument("Frame axes have no common ancestor.");
            }
            current = *parent;
        }
        sourcePath.erase(std::find(sourcePath.begin(), sourcePath.end(), registry.Identity(current).Frame),
                         sourcePath.end());
        auto convertedOrientation = orientation;
        auto convertedAngularVelocity = angularVelocity;
        const auto apply = [&](const std::uint64_t key, const bool inverse)
        {
            const auto found = evaluations.find(key);
            if (found == evaluations.end())
            {
                throw std::invalid_argument("Missing frame axes evaluation.");
            }
            const auto& edge = *found->second;
            if (inverse)
            {
                for (std::size_t axis = 0; axis < 3; ++axis)
                {
                    convertedAngularVelocity[axis] -= edge.AngularVelocity()[axis];
                }
                convertedAngularVelocity = edge.Rotation().Inverse().Apply(convertedAngularVelocity);
                convertedOrientation = edge.Rotation().Inverse().Compose(convertedOrientation);
            }
            else
            {
                convertedAngularVelocity = edge.Rotation().Apply(convertedAngularVelocity);
                for (std::size_t axis = 0; axis < 3; ++axis)
                {
                    convertedAngularVelocity[axis] += edge.AngularVelocity()[axis];
                }
                convertedOrientation = edge.Rotation().Compose(convertedOrientation);
            }
            for (const auto value : convertedAngularVelocity)
            {
                if (!std::isfinite(value))
                {
                    throw std::out_of_range("Converted angular velocity exceeds double range.");
                }
            }
        };
        for (const auto key : sourcePath)
        {
            apply(key, false);
        }
        for (auto edge = targetPath.rbegin(); edge != targetPath.rend(); ++edge)
        {
            apply(*edge, true);
        }
        orientation = convertedOrientation;
        angularVelocity = convertedAngularVelocity;
    }

    [[nodiscard]] inline FrameRotation ConvertFrameRotation(const FrameRegistry& registry, const LiveFrame& source,
                                                            const LiveFrame& target,
                                                            const std::span<const BoundFrameSnapshot> snapshots,
                                                            const double time)
    {
        FrameRotation rotation;
        std::array<double, 3> angularVelocity{};
        ConvertFrameAxes(registry, source, target, snapshots, time, rotation, angularVelocity);
        return rotation;
    }

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
    [[nodiscard]] inline SpatialBodyState ConvertFrameBodyState(const FrameRegistry& registry, const LiveFrame& source,
                                                                const LiveFrame& target, const SpatialBodyState& state,
                                                                const std::span<const BoundFrameSnapshot> snapshots,
                                                                const double time)
    {
        auto result = state;
        result.Motion = ConvertFrameMotion(registry, source, target, state.Motion, snapshots, time);
        ConvertFrameAxes(registry, source, target, snapshots, time, result.Orientation, result.AngularVelocity);
        return result;
    }
} // namespace Keire::Detail
