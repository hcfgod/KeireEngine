#pragma once

#include "KeireInternal/Scenes/AnimationIkPasses.h"

#include <algorithm>
#include <cmath>
#include <optional>

namespace Keire::Detail
{
    struct SettledFootSupportState final
    {
        std::optional<Matrix4> Previous;
        float StationarySeconds = 0.0F;
    };

    [[nodiscard]] inline bool ObserveSettledFootSupport(const Matrix4& world, const float deltaSeconds,
                                                        SettledFootSupportState& state) noexcept
    {
        if (!Math::IsFinite(world) || !std::isfinite(deltaSeconds) || deltaSeconds <= 0.0F)
            return false;
        if (!state.Previous || *state.Previous != world)
            state.StationarySeconds = 0.0F;
        else
            state.StationarySeconds = std::min(state.StationarySeconds + deltaSeconds, 1.0F);
        state.Previous = world;
        return state.StationarySeconds >= 0.5F;
    }

    [[nodiscard]] inline bool IsSettledFootPlant(const bool grounded, const Vector3 velocity,
                                                 const float standingBalance, const bool staticSupport) noexcept
    {
        return grounded && staticSupport && std::isfinite(standingBalance) && standingBalance > 0.99F &&
               std::isfinite(velocity.X) && std::isfinite(velocity.Y) && std::isfinite(velocity.Z) &&
               IkVectorLength(velocity) < 0.01F;
    }

    [[nodiscard]] inline bool ReconcileSettledFootPlant(const bool settled, const Vector3 candidate,
                                                        const Vector3 normal, const Vector3 upperPosition,
                                                        const float maximumReach, const float deltaSeconds,
                                                        bool& awaitingAnimationPlant,
                                                        AutomaticFootPlantState& state) noexcept
    {
        if (!settled || !std::isfinite(deltaSeconds) || deltaSeconds <= 0.0F || !std::isfinite(maximumReach) ||
            maximumReach <= 0.0F || !std::isfinite(candidate.X) || !std::isfinite(candidate.Y) ||
            !std::isfinite(candidate.Z) || !std::isfinite(upperPosition.X) || !std::isfinite(upperPosition.Y) ||
            !std::isfinite(upperPosition.Z) || IkVectorLength(IkSubtract(candidate, upperPosition)) > maximumReach)
            return false;
        const auto direction = IkNormalize(normal);
        if (!std::isfinite(direction.X) || !std::isfinite(direction.Y) || !std::isfinite(direction.Z) ||
            IkVectorLength(direction) <= 0.000001F)
            return false;
        // A settled stance must escape an interrupted swing's lift reference. Existing gait
        // anchors converge gradually; new contacts still use the grounding acquisition blend.
        const auto blend = 1.0F - std::exp(-deltaSeconds / 0.2F);
        const auto position = state.Locked ? Vector3{state.Position.X + (candidate.X - state.Position.X) * blend,
                                                     state.Position.Y + (candidate.Y - state.Position.Y) * blend,
                                                     state.Position.Z + (candidate.Z - state.Position.Z) * blend}
                                           : candidate;
        if (!ForceAutomaticFootPlant(position, direction, state))
            return false;
        awaitingAnimationPlant = false;
        return true;
    }
} // namespace Keire::Detail
