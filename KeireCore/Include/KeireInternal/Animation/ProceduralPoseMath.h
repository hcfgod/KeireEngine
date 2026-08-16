#pragma once

#include "Keire/Animation/ProceduralMotion.h"
#include "KeireInternal/Animation/RiggingMath.h"

#include <algorithm>
#include <cmath>

namespace Keire::Detail
{
    struct ProceduralArmGeometry
    {
        Vector3 TargetDirection;
        Vector3 PoleDirection;
        float Reach = 0.0F;
    };

    [[nodiscard]] inline float ProceduralFootLateralCorrection(const float currentOffset, const float legLength,
                                                               const float spacingRatio, const float side) noexcept
    {
        return side * spacingRatio * legLength - currentOffset;
    }

    [[nodiscard]] inline float ProceduralLocomotionPoseWeight(const float speed, const float minimumSpeed,
                                                              const float walkSpeed) noexcept
    {
        const auto fullPoseSpeed = std::max(walkSpeed * 0.35F, minimumSpeed + 0.000001F);
        const auto normalized = std::clamp((speed - minimumSpeed) / (fullPoseSpeed - minimumSpeed), 0.0F, 1.0F);
        return normalized * normalized * (3.0F - 2.0F * normalized);
    }

    [[nodiscard]] inline float ProceduralDirectionalStrideRatio(const Vector3 localDirection, const float lateralRatio,
                                                                const float backwardRatio) noexcept
    {
        const auto longitudinalRatio = localDirection.Z < 0.0F ? backwardRatio : 1.0F;
        const auto lateral = localDirection.X * lateralRatio;
        const auto longitudinal = localDirection.Z * longitudinalRatio;
        return std::sqrt(lateral * lateral + longitudinal * longitudinal);
    }

    [[nodiscard]] inline float ProceduralGaitPhaseRate(const float speed, const float runBlend, const float walkSpeed,
                                                       const float sprintSpeed, const float walkCadence,
                                                       const float sprintCadence) noexcept
    {
        const auto blend = std::clamp(runBlend, 0.0F, 1.0F);
        const auto referenceSpeed = walkSpeed + (sprintSpeed - walkSpeed) * blend;
        const auto cadence = walkCadence + (sprintCadence - walkCadence) * blend;
        return cadence * std::clamp(speed / std::max(referenceSpeed, 0.000001F), 0.2F, 1.25F);
    }

    [[nodiscard]] inline float ProceduralStrideLength(const float modelSpeed, const float phaseRate,
                                                      const float legLength, const float strideLengthRatio,
                                                      const float directionalRatio, const float runBlend,
                                                      const float walkSpeed, const float sprintSpeed,
                                                      const float walkCadence, const float sprintCadence) noexcept
    {
        if (modelSpeed <= 0.0F || phaseRate <= 0.000001F)
            return 0.0F;
        const auto blend = std::clamp(runBlend, 0.0F, 1.0F);
        const auto sprintScale =
            sprintSpeed / std::max(walkSpeed, 0.000001F) * walkCadence / std::max(sprintCadence, 0.000001F);
        const auto maximumStride =
            legLength * strideLengthRatio * directionalRatio * (1.0F + (sprintScale - 1.0F) * blend);
        return std::min(modelSpeed / (phaseRate * 2.0F), maximumStride);
    }

    [[nodiscard]] inline float ProceduralFootGroundingWeight(float phase) noexcept
    {
        phase -= std::floor(phase);
        const auto smoothStep = [](const float value) noexcept
        {
            const auto bounded = std::clamp(value, 0.0F, 1.0F);
            return bounded * bounded * (3.0F - 2.0F * bounded);
        };
        if (phase < 0.42F)
            return 0.0F;
        if (phase < 0.52F)
            return smoothStep((phase - 0.42F) / 0.10F);
        if (phase < 0.90F)
            return 1.0F;
        return 1.0F - smoothStep((phase - 0.90F) / 0.10F);
    }

    [[nodiscard]] inline Vector3 ProceduralUnsupportedFootTarget(const Vector3 footPosition, const Vector3 modelUp,
                                                                 const float legLength, const float dropRatio) noexcept
    {
        return RiggingDetail::Subtract(
            footPosition,
            RiggingDetail::Multiply(RiggingDetail::Normalize(modelUp, {0.0F, 1.0F, 0.0F}), legLength * dropRatio));
    }

    [[nodiscard]] inline bool ShouldResetProceduralFootContacts(const bool grounded,
                                                                const ProceduralMotionState state) noexcept
    {
        return !grounded || state == ProceduralMotionState::Takeoff;
    }

    [[nodiscard]] inline ProceduralArmGeometry
    BuildProceduralArmGeometry(const Vector3 modelRight, const Vector3 modelUp, const float side,
                               const float restDropDegrees, const float swingDegrees, const float elbowBendDegrees,
                               const float upperLength, const float lowerLength) noexcept
    {
        constexpr auto degreesToRadians = 0.0174532925199F;
        const auto right = RiggingDetail::Normalize(modelRight, {1.0F, 0.0F, 0.0F});
        const auto up = RiggingDetail::Normalize(modelUp, {0.0F, 1.0F, 0.0F});
        const auto forward = RiggingDetail::Normalize(RiggingDetail::Cross(right, up), {0.0F, 0.0F, 1.0F});
        const auto dropRadians = restDropDegrees * degreesToRadians;
        const auto swingRadians = swingDegrees * degreesToRadians;
        const auto outward = RiggingDetail::Multiply(right, side * std::cos(dropRadians));
        const auto down = RiggingDetail::Multiply(up, -std::sin(dropRadians) * std::cos(swingRadians));
        const auto foreAft = RiggingDetail::Multiply(forward, std::sin(dropRadians) * std::sin(swingRadians));
        const auto bendRadians = elbowBendDegrees * degreesToRadians;
        const auto reach = std::sqrt(std::max(0.0F, upperLength * upperLength + lowerLength * lowerLength +
                                                        2.0F * upperLength * lowerLength * std::cos(bendRadians)));
        return {RiggingDetail::Normalize(RiggingDetail::Add(outward, RiggingDetail::Add(down, foreAft))),
                RiggingDetail::Normalize(RiggingDetail::Add(RiggingDetail::Multiply(right, side * 0.25F),
                                                            RiggingDetail::Multiply(forward, -1.0F))),
                reach};
    }
} // namespace Keire::Detail
