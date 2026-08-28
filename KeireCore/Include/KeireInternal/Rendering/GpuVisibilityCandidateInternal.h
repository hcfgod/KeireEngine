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

    struct GpuVisibilityCapabilityContract final
    {
        bool Culling = false;
        bool StaticMeshes = false;
        bool SkinnedMeshes = false;
        bool MeshVfx = false;
        bool VfxVisibilityMasks = false;
        bool LocalLightMasks = false;
        bool SpatialVolumeMasks = false;
    };

    [[nodiscard]] constexpr GpuVisibilityCapabilityContract
    AdvertisedGpuVisibilityCapabilities(const bool gpuOcclusion) noexcept
    {
        return {.Culling = gpuOcclusion,
                .StaticMeshes = gpuOcclusion,
                .SkinnedMeshes = gpuOcclusion,
                .MeshVfx = gpuOcclusion,
                .VfxVisibilityMasks = gpuOcclusion,
                .LocalLightMasks = gpuOcclusion,
                .SpatialVolumeMasks = gpuOcclusion};
    }

    [[nodiscard]] constexpr GpuVisibilityFlags operator|(const GpuVisibilityFlags left,
                                                         const GpuVisibilityFlags right) noexcept
    {
        return static_cast<GpuVisibilityFlags>(static_cast<std::uint32_t>(left) | static_cast<std::uint32_t>(right));
    }

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

    [[nodiscard]] constexpr GpuVisibilityClass GpuVisibilityClassForDraw(const bool skinned,
                                                                         const bool meshVfx) noexcept
    {
        return skinned   ? GpuVisibilityClass::SkinnedMesh
               : meshVfx ? GpuVisibilityClass::MeshVfx
                         : GpuVisibilityClass::StaticMesh;
    }

    [[nodiscard]] constexpr GpuVisibilityBoundsPolicy
    DefaultGpuVisibilityBoundsPolicy(const GpuVisibilityClass visibilityClass,
                                     const bool freshDynamicBounds = false) noexcept
    {
        switch (visibilityClass)
        {
        case GpuVisibilityClass::StaticMesh:
        case GpuVisibilityClass::MeshVfx:
            return GpuVisibilityBoundsPolicy::ConservativeStatic;
        case GpuVisibilityClass::SkinnedMesh:
            return freshDynamicBounds ? GpuVisibilityBoundsPolicy::ConservativeDynamic
                                      : GpuVisibilityBoundsPolicy::UnknownOrUnbounded;
        case GpuVisibilityClass::PointLight:
        case GpuVisibilityClass::SpotLight:
        case GpuVisibilityClass::ReflectionProbe:
        case GpuVisibilityClass::LightProbeVolume:
            return GpuVisibilityBoundsPolicy::ConservativeDynamic;
        default:
            return GpuVisibilityBoundsPolicy::UnknownOrUnbounded;
        }
    }

    [[nodiscard]] constexpr GpuVisibilityFlags GpuVisibilityFlagsForDraw(const GpuVisibilityClass visibilityClass,
                                                                         const bool alwaysVisible,
                                                                         const bool freshDynamicBounds = false) noexcept
    {
        auto flags = alwaysVisible ? GpuVisibilityFlags::ForceVisible : GpuVisibilityFlags::None;
        if (visibilityClass == GpuVisibilityClass::SkinnedMesh && !freshDynamicBounds)
            flags = flags | GpuVisibilityFlags::ForceVisible | GpuVisibilityFlags::StaleBounds;
        return flags;
    }

    [[nodiscard]] constexpr bool RequiresConservativeCpuVisibility(const GpuVisibilityClass visibilityClass,
                                                                   const bool freshDynamicBounds = false) noexcept
    {
        return !CanGpuReject(DefaultGpuVisibilityBoundsPolicy(visibilityClass, freshDynamicBounds),
                             GpuVisibilityConsumer::IndexedIndirect,
                             GpuVisibilityFlagsForDraw(visibilityClass, false, freshDynamicBounds));
    }

    [[nodiscard]] constexpr bool CanGpuVisibilityClassOcclude(const GpuVisibilityClass visibilityClass,
                                                              const bool freshDynamicBounds = false) noexcept
    {
        return CanContributeOccluder(DefaultGpuVisibilityBoundsPolicy(visibilityClass, freshDynamicBounds),
                                     GpuVisibilityConsumer::IndexedIndirect, GpuVisibilityFlags::SafeOccluder);
    }
} // namespace Keire::RenderBackend
