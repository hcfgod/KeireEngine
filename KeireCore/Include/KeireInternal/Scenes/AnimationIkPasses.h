#pragma once

#include "Keire/Math/Math.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace Keire::Detail
{
    struct AutomaticLimbIkState final
    {
        Vector3 ForwardDirection;
        Vector3 BendDirection;
        bool HasForwardDirection = false;
        bool HasBendDirection = false;
    };

    struct AutomaticFootPlantState final
    {
        Vector3 Position;
        Vector3 Normal{0.0F, 1.0F, 0.0F};
        bool Locked = false;
    };

    struct AutomaticFootGroundingSmoothingState final
    {
        Vector3 Position;
        Vector3 Normal{0.0F, 1.0F, 0.0F};
        float Blend = 0.0F;
        bool Initialized = false;
    };

    struct AutomaticFootGroundingTarget final
    {
        Vector3 Position;
        Vector3 Normal{0.0F, 1.0F, 0.0F};
        float Blend = 0.0F;
    };

    [[nodiscard]] inline float IkVectorLength(const Vector3 value) noexcept
    {
        return std::sqrt(value.X * value.X + value.Y * value.Y + value.Z * value.Z);
    }

    [[nodiscard]] inline Vector3 IkSubtract(const Vector3 left, const Vector3 right) noexcept
    {
        return {left.X - right.X, left.Y - right.Y, left.Z - right.Z};
    }

    [[nodiscard]] inline Vector3 IkProjectOntoPlane(const Vector3 value, const Vector3 normal) noexcept
    {
        const auto projection = value.X * normal.X + value.Y * normal.Y + value.Z * normal.Z;
        return {value.X - normal.X * projection, value.Y - normal.Y * projection, value.Z - normal.Z * projection};
    }

    [[nodiscard]] inline Vector3 IkNormalize(const Vector3 value) noexcept
    {
        const auto length = IkVectorLength(value);
        return length > 0.000001F ? Vector3{value.X / length, value.Y / length, value.Z / length} : Vector3{};
    }

    [[nodiscard]] inline float IkDot(const Vector3 left, const Vector3 right) noexcept
    {
        return left.X * right.X + left.Y * right.Y + left.Z * right.Z;
    }

    [[nodiscard]] inline Vector3 IkCross(const Vector3 left, const Vector3 right) noexcept
    {
        return {left.Y * right.Z - left.Z * right.Y, left.Z * right.X - left.X * right.Z,
                left.X * right.Y - left.Y * right.X};
    }

    [[nodiscard]] inline Vector3 IkTransportDirection(const Vector3 direction, const Vector3 previousNormal,
                                                      const Vector3 currentNormal) noexcept
    {
        const auto from = IkNormalize(previousNormal);
        const auto to = IkNormalize(currentNormal);
        const auto axisVector = IkCross(from, to);
        const auto sine = IkVectorLength(axisVector);
        const auto cosine = std::clamp(IkDot(from, to), -1.0F, 1.0F);
        if (sine <= 0.000001F)
            return cosine >= 0.0F ? direction : IkProjectOntoPlane(direction, to);

        const Vector3 axis{axisVector.X / sine, axisVector.Y / sine, axisVector.Z / sine};
        const auto axisCrossDirection = IkCross(axis, direction);
        const auto axisProjection = IkDot(axis, direction) * (1.0F - cosine);
        return {direction.X * cosine + axisCrossDirection.X * sine + axis.X * axisProjection,
                direction.Y * cosine + axisCrossDirection.Y * sine + axis.Y * axisProjection,
                direction.Z * cosine + axisCrossDirection.Z * sine + axis.Z * axisProjection};
    }

    [[nodiscard]] inline float AutomaticIkResponseBlend(const float deltaSeconds, const float responseTime) noexcept
    {
        if (!std::isfinite(deltaSeconds) || deltaSeconds <= 0.0F)
            return 0.0F;
        if (!std::isfinite(responseTime) || responseTime <= 0.000001F)
            return 1.0F;
        return std::clamp(1.0F - std::exp(-deltaSeconds / responseTime), 0.0F, 1.0F);
    }

    [[nodiscard]] inline std::optional<AutomaticFootGroundingTarget>
    UpdateAutomaticFootGroundingSmoothing(const std::optional<Vector3> desiredPosition,
                                          const std::optional<Vector3> desiredNormal, const Vector3 sampledPosition,
                                          const float deltaSeconds, const float responseTime,
                                          AutomaticFootGroundingSmoothingState& state) noexcept
    {
        if (!Math::IsFinite(sampledPosition) || !std::isfinite(deltaSeconds) || deltaSeconds < 0.0F ||
            !std::isfinite(responseTime) || responseTime < 0.0F ||
            desiredPosition.has_value() != desiredNormal.has_value())
        {
            state = {};
            return std::nullopt;
        }

        const auto responseBlend = AutomaticIkResponseBlend(deltaSeconds, responseTime);
        if (desiredPosition)
        {
            const auto normal = IkNormalize(*desiredNormal);
            if (!Math::IsFinite(*desiredPosition) || IkVectorLength(normal) <= 0.000001F)
            {
                state = {};
                return std::nullopt;
            }

            if (!state.Initialized)
            {
                state.Position = *desiredPosition;
                state.Normal = normal;
                state.Blend = responseTime <= 0.000001F ? 1.0F : responseBlend;
                state.Initialized = true;
            }
            else
            {
                const auto displacement = IkSubtract(*desiredPosition, state.Position);
                const auto upwardTravel = std::max(IkDot(displacement, normal), 0.0F);
                const Vector3 upward{normal.X * upwardTravel, normal.Y * upwardTravel, normal.Z * upwardTravel};
                const auto remaining = IkSubtract(displacement, upward);
                state.Position = {state.Position.X + upward.X + remaining.X * responseBlend,
                                  state.Position.Y + upward.Y + remaining.Y * responseBlend,
                                  state.Position.Z + upward.Z + remaining.Z * responseBlend};
                const Vector3 blendedNormal{state.Normal.X + (normal.X - state.Normal.X) * responseBlend,
                                            state.Normal.Y + (normal.Y - state.Normal.Y) * responseBlend,
                                            state.Normal.Z + (normal.Z - state.Normal.Z) * responseBlend};
                state.Normal = IkNormalize(blendedNormal);
                state.Blend += (1.0F - state.Blend) * responseBlend;
            }
        }
        else
        {
            if (!state.Initialized)
                return std::nullopt;
            if (responseBlend >= 1.0F)
            {
                state = {};
                return std::nullopt;
            }
            state.Position = {state.Position.X + (sampledPosition.X - state.Position.X) * responseBlend,
                              state.Position.Y + (sampledPosition.Y - state.Position.Y) * responseBlend,
                              state.Position.Z + (sampledPosition.Z - state.Position.Z) * responseBlend};
            state.Blend *= 1.0F - responseBlend;
            if (state.Blend <= 0.001F)
            {
                state = {};
                return std::nullopt;
            }
        }
        return AutomaticFootGroundingTarget{state.Position, state.Normal, std::clamp(state.Blend, 0.0F, 1.0F)};
    }

    [[nodiscard]] inline bool ShouldReleaseAutomaticFootPlant(const Vector3 sampledPosition,
                                                              const Vector3 referencePosition,
                                                              const Vector3 referenceNormal, const float legLength,
                                                              const float releaseDistance) noexcept
    {
        const auto normal = IkNormalize(referenceNormal);
        if (IkVectorLength(normal) <= 0.000001F || !std::isfinite(legLength) || legLength <= 0.000001F ||
            !std::isfinite(releaseDistance) || releaseDistance < 0.0F)
        {
            return true;
        }
        const auto displacement = IkSubtract(sampledPosition, referencePosition);
        const auto lift = IkDot(displacement, normal);
        const auto horizontalTravel = IkVectorLength(IkProjectOntoPlane(displacement, normal));
        const auto reachLimit = std::max(releaseDistance, legLength * 0.2F);
        return lift > releaseDistance || horizontalTravel > reachLimit;
    }

    [[nodiscard]] inline bool ShouldReanchorMovingFootSupport(const Vector3 lockedPosition,
                                                              const Vector3 plantedPosition,
                                                              const Vector3 plantedNormal, const float legLength,
                                                              const float releaseDistance) noexcept
    {
        const auto normal = IkNormalize(plantedNormal);
        if (IkVectorLength(normal) <= 0.000001F || !std::isfinite(legLength) || legLength <= 0.000001F ||
            !std::isfinite(releaseDistance) || releaseDistance < 0.0F)
        {
            return true;
        }

        const auto supportTravel =
            IkVectorLength(IkProjectOntoPlane(IkSubtract(lockedPosition, plantedPosition), normal));
        return supportTravel > std::max(releaseDistance, legLength * 0.15F);
    }

    [[nodiscard]] inline bool ShouldReplaceAutomaticFootSupport(const Vector3 currentSurfacePosition,
                                                                const Vector3 currentSurfaceNormal,
                                                                const Vector3 candidateSurfacePosition) noexcept
    {
        const auto normal = IkNormalize(currentSurfaceNormal);
        if (IkVectorLength(normal) <= 0.000001F)
            return true;
        constexpr float SurfaceSwitchTolerance = 0.001F;
        return IkDot(IkSubtract(candidateSurfacePosition, currentSurfacePosition), normal) > SurfaceSwitchTolerance;
    }

    [[nodiscard]] inline bool ForceAutomaticFootPlant(const Vector3 candidatePosition, const Vector3 candidateNormal,
                                                      AutomaticFootPlantState& state) noexcept
    {
        const auto normal = IkNormalize(candidateNormal);
        if (!std::isfinite(candidatePosition.X) || !std::isfinite(candidatePosition.Y) ||
            !std::isfinite(candidatePosition.Z) || IkVectorLength(normal) <= 0.000001F)
        {
            state = {};
            return false;
        }
        state.Position = candidatePosition;
        state.Normal = normal;
        state.Locked = true;
        return true;
    }

    [[nodiscard]] inline Vector3 UpdateAutomaticFootPlant(const Vector3 sampledPosition,
                                                          const Vector3 candidatePosition,
                                                          const Vector3 candidateNormal, const float legLength,
                                                          const float plantDistance, const float releaseDistance,
                                                          AutomaticFootPlantState& state) noexcept
    {
        const auto normal = IkNormalize(candidateNormal);
        if (IkVectorLength(normal) <= 0.000001F || !std::isfinite(legLength) || legLength <= 0.000001F ||
            !std::isfinite(plantDistance) || plantDistance < 0.0F || !std::isfinite(releaseDistance) ||
            releaseDistance < plantDistance)
        {
            state = {};
            return candidatePosition;
        }

        if (state.Locked)
        {
            if (!ShouldReleaseAutomaticFootPlant(sampledPosition, state.Position, state.Normal, legLength,
                                                 releaseDistance))
                return state.Position;
            state = {};
        }

        const auto separation = IkDot(IkSubtract(sampledPosition, candidatePosition), normal);
        if (separation <= plantDistance && separation >= -releaseDistance)
        {
            state.Position = candidatePosition;
            state.Normal = normal;
            state.Locked = true;
        }
        return candidatePosition;
    }

    [[nodiscard]] inline Vector3 AutomaticBipedKneeReference(const Vector3 leftHip, const Vector3 rightHip,
                                                             const Vector3 gravityUp) noexcept
    {
        return IkNormalize(IkCross(IkSubtract(rightHip, leftHip), IkNormalize(gravityUp)));
    }

    [[nodiscard]] inline Vector3 OrientBipedKneeReference(const Vector3 reference, const Vector3 leftHip,
                                                          const Vector3 leftKnee, const Vector3 leftFoot,
                                                          const Vector3 rightHip, const Vector3 rightKnee,
                                                          const Vector3 rightFoot) noexcept
    {
        const auto sampledBend = [](const Vector3 hip, const Vector3 knee, const Vector3 foot)
        {
            const auto legDirection = IkNormalize(IkSubtract(foot, hip));
            return IkNormalize(IkProjectOntoPlane(IkSubtract(knee, hip), legDirection));
        };
        const auto left = sampledBend(leftHip, leftKnee, leftFoot);
        const auto right = sampledBend(rightHip, rightKnee, rightFoot);
        const auto sampled = IkNormalize({left.X + right.X, left.Y + right.Y, left.Z + right.Z});
        if (IkVectorLength(reference) <= 0.000001F || IkVectorLength(sampled) <= 0.000001F ||
            IkDot(reference, sampled) >= 0.0F)
        {
            return reference;
        }
        return {-reference.X, -reference.Y, -reference.Z};
    }

    [[nodiscard]] inline Vector3 StableAutomaticLimbPole(const Vector3 root, const Vector3 middle, const Vector3 end,
                                                         const Vector3 target, const Vector3 preferredBendDirection,
                                                         const float deltaSeconds, const float responseTime,
                                                         const float stability, AutomaticLimbIkState& state) noexcept
    {
        const auto upperLength = IkVectorLength(IkSubtract(middle, root));
        const auto lowerLength = IkVectorLength(IkSubtract(end, middle));
        const auto reach = std::max(upperLength + lowerLength, 0.25F);
        const auto targetDelta = IkSubtract(target, root);
        const auto targetDistance = IkVectorLength(targetDelta);
        Vector3 forward;
        if (targetDistance > reach * 0.005F)
        {
            forward = IkNormalize(targetDelta);
        }
        else if (state.HasForwardDirection)
        {
            forward = state.ForwardDirection;
        }
        else
        {
            forward = IkNormalize(IkSubtract(end, root));
            if (IkVectorLength(forward) <= 0.000001F)
                forward = IkNormalize(IkSubtract(middle, root));
            if (IkVectorLength(forward) <= 0.000001F)
                forward = {1.0F, 0.0F, 0.0F};
        }
        auto sampledBend = IkProjectOntoPlane(IkSubtract(middle, root), forward);
        auto stableBend = state.HasBendDirection && state.HasForwardDirection
                              ? IkProjectOntoPlane(
                                    IkTransportDirection(state.BendDirection, state.ForwardDirection, forward), forward)
                              : Vector3{};
        auto preferredBend = IkProjectOntoPlane(preferredBendDirection, forward);
        const auto bendThreshold = std::max(reach * 0.001F, 0.00001F);
        const auto hasSampledBend = IkVectorLength(sampledBend) > bendThreshold;
        const auto hasStableBend = IkVectorLength(stableBend) > bendThreshold;
        const auto hasPreferredBend = IkVectorLength(preferredBend) > bendThreshold;
        if (hasSampledBend)
            sampledBend = IkNormalize(sampledBend);
        if (hasStableBend)
            stableBend = IkNormalize(stableBend);
        if (hasPreferredBend)
            preferredBend = IkNormalize(preferredBend);

        const auto align = [](Vector3 candidate, const Vector3 reference)
        {
            if (IkDot(candidate, reference) < 0.0F)
                candidate = {-candidate.X, -candidate.Y, -candidate.Z};
            return candidate;
        };
        if (hasPreferredBend)
        {
            if (hasStableBend)
                stableBend = align(stableBend, preferredBend);
            if (hasSampledBend)
                sampledBend = align(sampledBend, preferredBend);
        }
        else if (hasStableBend && hasSampledBend)
            sampledBend = align(sampledBend, stableBend);

        Vector3 bend;
        if (hasSampledBend)
            bend = sampledBend;
        else if (hasStableBend)
            bend = stableBend;
        else if (hasPreferredBend)
            bend = preferredBend;
        else
        {
            const std::array references{Vector3{0.0F, 0.0F, -1.0F}, Vector3{0.0F, 1.0F, 0.0F},
                                        Vector3{1.0F, 0.0F, 0.0F}};
            for (const auto reference : references)
            {
                const auto candidate = IkProjectOntoPlane(reference, forward);
                if (IkVectorLength(candidate) > IkVectorLength(bend))
                    bend = candidate;
            }
            bend = IkNormalize(bend);
        }

        const auto stabilityWeight = std::clamp(std::isfinite(stability) ? stability : 0.0F, 0.0F, 1.0F);
        const auto reference = hasPreferredBend ? preferredBend : stableBend;
        if ((hasPreferredBend || hasStableBend) && stabilityWeight > 0.0F)
        {
            bend = IkNormalize({bend.X + (reference.X - bend.X) * stabilityWeight,
                                bend.Y + (reference.Y - bend.Y) * stabilityWeight,
                                bend.Z + (reference.Z - bend.Z) * stabilityWeight});
        }
        if (hasStableBend)
        {
            bend = align(bend, stableBend);
            const auto responseBlend = AutomaticIkResponseBlend(deltaSeconds, responseTime);
            bend = IkNormalize({stableBend.X + (bend.X - stableBend.X) * responseBlend,
                                stableBend.Y + (bend.Y - stableBend.Y) * responseBlend,
                                stableBend.Z + (bend.Z - stableBend.Z) * responseBlend});
        }
        state.ForwardDirection = forward;
        state.HasForwardDirection = true;
        state.BendDirection = bend;
        state.HasBendDirection = true;
        return {root.X + bend.X * reach, root.Y + bend.Y * reach, root.Z + bend.Z * reach};
    }

    [[nodiscard]] inline Vector3 StableAutomaticLimbPole(const Vector3 root, const Vector3 middle, const Vector3 end,
                                                         const Vector3 target, AutomaticLimbIkState& state) noexcept
    {
        return StableAutomaticLimbPole(root, middle, end, target, {}, 1.0F, 0.0F, 0.0F, state);
    }

    inline void AppendAnimationIkDiagnostic(std::string& destination, const std::string_view diagnostic)
    {
        if (diagnostic.empty())
            return;
        if (!destination.empty())
            destination += '\n';
        destination += diagnostic;
    }

    template <typename... Passes> [[nodiscard]] std::string EvaluateIndependentAnimationIkPasses(Passes&&... passes)
    {
        std::string diagnostics;
        (AppendAnimationIkDiagnostic(diagnostics, std::forward<Passes>(passes)()), ...);
        return diagnostics;
    }
} // namespace Keire::Detail
