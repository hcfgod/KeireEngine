#pragma once

#include "Keire/Physics/PhysicsSystem.h"
#include "KeireInternal/Physics/CapsuleRotationBounds.h"
#include "KeireInternal/Physics/PhysicsFrameAccess.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <stdexcept>

namespace Keire::Detail
{
    struct PlanetCharacterPoseInterval
    {
        PhysicsCapsuleOverlapQuery Midpoint;
        double AngularExcursion = 0.0;
        double CenterExcursion = 0.0;
    };

    // Bounds a continuous pose interval about its midpoint. The caller must bound ALL center motion,
    // including a moving support's pivot arc, and ALL orientation deviation over that interval.
    // A clear envelope proves clearance; an occupied envelope needs subdivision or rejection.
    [[nodiscard]] inline PhysicsCapsuleOverlapQuery
    PlanetCharacterPoseEnvelope(const PhysicsCapsuleOverlapQuery& midpoint, const double angularExcursion,
                                const double centerExcursion)
    {
        if (!Math::IsFinite(midpoint.Center) || !Math::IsFinite(midpoint.Rotation) || !std::isfinite(midpoint.Radius) ||
            midpoint.Radius <= 0.0F || !std::isfinite(midpoint.Height) ||
            static_cast<double>(midpoint.Height) < 2.0 * midpoint.Radius)
        {
            throw std::invalid_argument("Character pose clearance requires a finite valid capsule.");
        }
        const auto rotation = Math::Normalize(midpoint.Rotation);
        const double cylinderLength = static_cast<double>(midpoint.Height) - 2.0 * midpoint.Radius;
        const double inflation = CapsuleRotationInflation(cylinderLength * 0.5, angularExcursion, centerExcursion);
        auto envelope = midpoint;
        envelope.Rotation = rotation;
        envelope.Radius = RoundCapsuleBoundOutward(static_cast<double>(midpoint.Radius) + inflation);
        // Reconstruct height from the rounded radius so outward radius rounding cannot shorten the segment.
        envelope.Height = RoundCapsuleBoundOutward(cylinderLength + 2.0 * envelope.Radius);
        return envelope;
    }

    [[nodiscard]] inline bool PlanetCharacterPoseIntervalClear(const PhysicsWorld& world,
                                                               const PhysicsCapsuleOverlapQuery& midpoint,
                                                               const double angularExcursion,
                                                               const double centerExcursion)
    {
        return !PhysicsFrameAccess::HasCapsuleContact(
            world, PlanetCharacterPoseEnvelope(midpoint, angularExcursion, centerExcursion));
    }

    // The interval provider must conservatively bound the actual path over [begin, end], not merely its
    // endpoints. Query budget exhaustion is unresolved clearance and therefore rejects the whole move.
    // No accepted prefix is applied: callers commit pose/support state only after this returns true.
    template <typename IntervalProvider, typename ClearanceQuery>
    [[nodiscard]] bool PlanetCharacterPathClear(IntervalProvider&& intervalAt, ClearanceQuery&& isClear,
                                                const std::size_t queryBudget = 255,
                                                const std::size_t maximumDepth = 12)
    {
        constexpr std::size_t depthLimit = 20;
        constexpr std::size_t queryLimit = 4095;
        if (queryBudget == 0 || queryBudget > queryLimit || maximumDepth > depthLimit)
        {
            throw std::invalid_argument("Character path clearance budget is invalid.");
        }
        struct PendingInterval
        {
            double Begin = 0.0;
            double End = 1.0;
            std::size_t Depth = 0;
        };
        std::array<PendingInterval, depthLimit + 1> pending;
        std::size_t pendingCount = 1;
        std::size_t queries = 0;
        while (pendingCount != 0)
        {
            if (queries == queryBudget)
            {
                return false;
            }
            const auto interval = pending[--pendingCount];
            const auto bounds = intervalAt(interval.Begin, interval.End);
            const auto envelope =
                PlanetCharacterPoseEnvelope(bounds.Midpoint, bounds.AngularExcursion, bounds.CenterExcursion);
            ++queries;
            if (isClear(envelope))
            {
                continue;
            }
            if (interval.Depth == maximumDepth)
            {
                return false;
            }
            const double midpoint = interval.Begin + (interval.End - interval.Begin) * 0.5;
            pending[pendingCount++] = {midpoint, interval.End, interval.Depth + 1};
            pending[pendingCount++] = {interval.Begin, midpoint, interval.Depth + 1};
        }
        return true;
    }
} // namespace Keire::Detail
