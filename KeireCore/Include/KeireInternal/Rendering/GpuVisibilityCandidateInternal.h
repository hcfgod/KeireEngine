#pragma once

#include <cstdint>

namespace Keire::RenderBackend
{
    enum class GpuVisibilityClass : std::uint32_t
    {
        StaticMesh,
        SkinnedMesh,
        MeshVfx,
        SpriteVfx,
        RibbonVfx,
        VolumetricVfx,
        PointLight,
        SpotLight,
        ReflectionProbe,
        LightProbeVolume,
        Decal,
        EditorSpatialOverlay,
        NonSpatial
    };

    enum class GpuVisibilityBoundsPolicy : std::uint8_t
    {
        ConservativeStatic,
        ConservativeDynamic,
        UnknownOrUnbounded
    };

    enum class GpuVisibilityConsumer : std::uint8_t
    {
        IndexedIndirect,
        VfxVisibilityMask,
        ForwardPlusLightMask,
        SpatialVolumeMask,
        ForcedVisible
    };

    enum class GpuVisibilityFlags : std::uint32_t
    {
        None = 0U,
        ForceVisible = 1U << 0U,
        SafeOccluder = 1U << 1U,
        StaleBounds = 1U << 2U
    };

    [[nodiscard]] constexpr bool HasGpuVisibilityFlag(const GpuVisibilityFlags value,
                                                      const GpuVisibilityFlags flag) noexcept
    {
        return (static_cast<std::uint32_t>(value) & static_cast<std::uint32_t>(flag)) != 0U;
    }

    [[nodiscard]] constexpr bool CanGpuReject(const GpuVisibilityBoundsPolicy bounds,
                                              const GpuVisibilityConsumer consumer,
                                              const GpuVisibilityFlags flags = GpuVisibilityFlags::None) noexcept
    {
        return bounds != GpuVisibilityBoundsPolicy::UnknownOrUnbounded &&
               consumer != GpuVisibilityConsumer::ForcedVisible &&
               !HasGpuVisibilityFlag(flags, GpuVisibilityFlags::ForceVisible) &&
               !HasGpuVisibilityFlag(flags, GpuVisibilityFlags::StaleBounds);
    }

    [[nodiscard]] constexpr bool CanContributeOccluder(const GpuVisibilityBoundsPolicy bounds,
                                                       const GpuVisibilityConsumer consumer,
                                                       const GpuVisibilityFlags flags) noexcept
    {
        return CanGpuReject(bounds, consumer, flags) && HasGpuVisibilityFlag(flags, GpuVisibilityFlags::SafeOccluder);
    }
} // namespace Keire::RenderBackend
