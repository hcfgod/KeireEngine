#pragma once

#include "Keire/Rendering/RenderSystem.h"

#include <cstdint>

namespace Keire::RenderBackend
{
    struct GlobalIlluminationPolicy final
    {
        bool EnvironmentDiffuse = false;
        bool EnvironmentSpecular = false;
        bool BakedLighting = false;
        float IrradynStrength = 0.0F;
        std::uint32_t IrradynSampleCount = 0;
        std::uint32_t IrradynRayStepCount = 0;
        std::uint32_t IrradynResolutionDivisor = 1;
        std::uint32_t IrradynSceneCardCount = 0;
        std::uint32_t IrradynSceneCardUpdateBudget = 0;
        float IrradynRadiusPixels = 0.0F;
        float IrradynMaximumDistance = 0.0F;
        float IrradynHistoryWeight = 0.0F;
        float IrradynTargetMilliseconds = 0.0F;
    };

    enum class IrradynParticipation : std::uint32_t
    {
        None = 0,
        Surface = 1U << 0U,
        Translucency = 1U << 1U,
        Hair = 1U << 2U,
        Volume = 1U << 3U,
        Vfx = 1U << 4U,
        WorldPositionOffset = 1U << 5U
    };

    [[nodiscard]] constexpr IrradynParticipation operator|(const IrradynParticipation left,
                                                           const IrradynParticipation right) noexcept
    {
        return static_cast<IrradynParticipation>(static_cast<std::uint32_t>(left) | static_cast<std::uint32_t>(right));
    }

    [[nodiscard]] constexpr bool HasIrradynParticipation(const IrradynParticipation value,
                                                         const IrradynParticipation participation) noexcept
    {
        return (static_cast<std::uint32_t>(value) & static_cast<std::uint32_t>(participation)) != 0U;
    }

    inline constexpr IrradynParticipation IrradynSupportedParticipation =
        IrradynParticipation::Surface | IrradynParticipation::Translucency | IrradynParticipation::Hair |
        IrradynParticipation::Volume | IrradynParticipation::Vfx | IrradynParticipation::WorldPositionOffset;

    [[nodiscard]] constexpr GlobalIlluminationPolicy
    ResolveGlobalIlluminationPolicy(const GlobalIlluminationMode mode, const IrradynQuality quality) noexcept
    {
        GlobalIlluminationPolicy result;
        switch (mode)
        {
        case GlobalIlluminationMode::Disabled:
            return result;
        case GlobalIlluminationMode::Baked:
            result.BakedLighting = true;
            return result;
        case GlobalIlluminationMode::Realtime:
            result.EnvironmentDiffuse = true;
            result.EnvironmentSpecular = true;
            return result;
        case GlobalIlluminationMode::Irradyn:
            result.EnvironmentDiffuse = true;
            result.EnvironmentSpecular = true;
            break;
        case GlobalIlluminationMode::Hybrid:
            result.EnvironmentDiffuse = true;
            result.EnvironmentSpecular = true;
            result.BakedLighting = true;
            break;
        }

        switch (quality)
        {
        case IrradynQuality::Performance:
            result.IrradynStrength = 0.22F;
            result.IrradynSampleCount = 4;
            result.IrradynRayStepCount = 3;
            result.IrradynResolutionDivisor = 4;
            result.IrradynSceneCardCount = 8;
            result.IrradynSceneCardUpdateBudget = 16;
            result.IrradynRadiusPixels = 18.0F;
            result.IrradynMaximumDistance = 2.0F;
            result.IrradynHistoryWeight = 0.82F;
            result.IrradynTargetMilliseconds = 1.5F;
            break;
        case IrradynQuality::Balanced:
            result.IrradynStrength = 0.30F;
            result.IrradynSampleCount = 6;
            result.IrradynRayStepCount = 4;
            result.IrradynResolutionDivisor = 2;
            result.IrradynSceneCardCount = 12;
            result.IrradynSceneCardUpdateBudget = 24;
            result.IrradynRadiusPixels = 26.0F;
            result.IrradynMaximumDistance = 3.0F;
            result.IrradynHistoryWeight = 0.88F;
            result.IrradynTargetMilliseconds = 2.5F;
            break;
        case IrradynQuality::Quality:
            result.IrradynStrength = 0.38F;
            result.IrradynSampleCount = 8;
            result.IrradynRayStepCount = 6;
            result.IrradynResolutionDivisor = 2;
            result.IrradynSceneCardCount = 16;
            result.IrradynSceneCardUpdateBudget = 32;
            result.IrradynRadiusPixels = 36.0F;
            result.IrradynMaximumDistance = 4.0F;
            result.IrradynHistoryWeight = 0.92F;
            result.IrradynTargetMilliseconds = 4.0F;
            break;
        }
        return result;
    }
} // namespace Keire::RenderBackend
