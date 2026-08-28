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
    DefaultGpuVisibilityBoundsPolicy(const GpuVisibilityClass visibilityClass) noexcept
    {
        switch (visibilityClass)
        {
        case GpuVisibilityClass::StaticMesh:
        case GpuVisibilityClass::MeshVfx:
            return GpuVisibilityBoundsPolicy::ConservativeStatic;
        default:
            return GpuVisibilityBoundsPolicy::UnknownOrUnbounded;
        }
    }

    [[nodiscard]] constexpr GpuVisibilityFlags GpuVisibilityFlagsForDraw(const GpuVisibilityClass visibilityClass,
                                                                         const bool alwaysVisible) noexcept
    {
        auto flags = alwaysVisible ? GpuVisibilityFlags::ForceVisible : GpuVisibilityFlags::None;
        if (visibilityClass == GpuVisibilityClass::SkinnedMesh)
            flags = flags | GpuVisibilityFlags::ForceVisible | GpuVisibilityFlags::StaleBounds;
        return flags;
    }

    [[nodiscard]] constexpr bool RequiresConservativeCpuVisibility(const GpuVisibilityClass visibilityClass) noexcept
    {
        return !CanGpuReject(DefaultGpuVisibilityBoundsPolicy(visibilityClass), GpuVisibilityConsumer::IndexedIndirect,
                             GpuVisibilityFlagsForDraw(visibilityClass, false));
    }

    [[nodiscard]] constexpr bool CanGpuVisibilityClassOcclude(const GpuVisibilityClass visibilityClass) noexcept
    {
        return CanContributeOccluder(DefaultGpuVisibilityBoundsPolicy(visibilityClass),
                                     GpuVisibilityConsumer::IndexedIndirect, GpuVisibilityFlags::SafeOccluder);
    }
} // namespace Keire::RenderBackend
