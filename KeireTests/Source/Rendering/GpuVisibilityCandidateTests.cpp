#include "Keire/Rendering/RenderSystem.h"
#include "KeireInternal/Rendering/GpuVisibilityCandidateInternal.h"

#include <doctest/doctest.h>

TEST_CASE("GPU visibility rejects only conservative candidates with a mask consumer")
{
    using namespace Keire::RenderBackend;

    CHECK(CanGpuReject(GpuVisibilityBoundsPolicy::ConservativeStatic, GpuVisibilityConsumer::IndexedIndirect));
    CHECK(CanGpuReject(GpuVisibilityBoundsPolicy::ConservativeDynamic, GpuVisibilityConsumer::VfxVisibilityMask));
    CHECK_FALSE(CanGpuReject(GpuVisibilityBoundsPolicy::UnknownOrUnbounded, GpuVisibilityConsumer::IndexedIndirect));
    CHECK_FALSE(CanGpuReject(GpuVisibilityBoundsPolicy::ConservativeStatic, GpuVisibilityConsumer::ForcedVisible));
    CHECK_FALSE(CanGpuReject(GpuVisibilityBoundsPolicy::ConservativeStatic, GpuVisibilityConsumer::IndexedIndirect,
                             GpuVisibilityFlags::ForceVisible));
    CHECK_FALSE(CanGpuReject(GpuVisibilityBoundsPolicy::ConservativeDynamic, GpuVisibilityConsumer::IndexedIndirect,
                             GpuVisibilityFlags::StaleBounds));
}

TEST_CASE("GPU visibility accepts only explicitly safe occluders")
{
    using namespace Keire::RenderBackend;

    CHECK(CanContributeOccluder(GpuVisibilityBoundsPolicy::ConservativeStatic, GpuVisibilityConsumer::IndexedIndirect,
                                GpuVisibilityFlags::SafeOccluder));
    CHECK_FALSE(CanContributeOccluder(GpuVisibilityBoundsPolicy::ConservativeStatic,
                                      GpuVisibilityConsumer::IndexedIndirect, GpuVisibilityFlags::None));
    CHECK_FALSE(CanContributeOccluder(GpuVisibilityBoundsPolicy::UnknownOrUnbounded,
                                      GpuVisibilityConsumer::IndexedIndirect, GpuVisibilityFlags::SafeOccluder));
}

TEST_CASE("draw visibility classification keeps stale skinned bounds conservative")
{
    using namespace Keire::RenderBackend;

    const auto staticMesh = GpuVisibilityClassForDraw(false, false);
    const auto skinnedMesh = GpuVisibilityClassForDraw(true, false);
    const auto meshVfx = GpuVisibilityClassForDraw(false, true);

    CHECK(staticMesh == GpuVisibilityClass::StaticMesh);
    CHECK(skinnedMesh == GpuVisibilityClass::SkinnedMesh);
    CHECK(meshVfx == GpuVisibilityClass::MeshVfx);
    CHECK(GpuVisibilityClassForDraw(true, true) == GpuVisibilityClass::SkinnedMesh);

    CHECK_FALSE(RequiresConservativeCpuVisibility(staticMesh));
    CHECK(RequiresConservativeCpuVisibility(skinnedMesh));
    CHECK_FALSE(RequiresConservativeCpuVisibility(meshVfx));
    CHECK(CanGpuVisibilityClassOcclude(staticMesh));
    CHECK_FALSE(CanGpuVisibilityClassOcclude(skinnedMesh));
    CHECK(CanGpuVisibilityClassOcclude(meshVfx));

    const auto skinnedFlags = GpuVisibilityFlagsForDraw(skinnedMesh, false);
    CHECK(HasGpuVisibilityFlag(skinnedFlags, GpuVisibilityFlags::ForceVisible));
    CHECK(HasGpuVisibilityFlag(skinnedFlags, GpuVisibilityFlags::StaleBounds));
}

TEST_CASE("fresh linear-blend pose bounds opt skinned draws into conservative dynamic visibility")
{
    using namespace Keire::RenderBackend;

    constexpr auto skinnedMesh = GpuVisibilityClass::SkinnedMesh;
    CHECK(DefaultGpuVisibilityBoundsPolicy(skinnedMesh, true) == GpuVisibilityBoundsPolicy::ConservativeDynamic);
    CHECK_FALSE(RequiresConservativeCpuVisibility(skinnedMesh, true));
    CHECK(CanGpuVisibilityClassOcclude(skinnedMesh, true));

    const auto freshFlags = GpuVisibilityFlagsForDraw(skinnedMesh, false, true);
    CHECK_FALSE(HasGpuVisibilityFlag(freshFlags, GpuVisibilityFlags::ForceVisible));
    CHECK_FALSE(HasGpuVisibilityFlag(freshFlags, GpuVisibilityFlags::StaleBounds));

    const auto explicitlyVisible = GpuVisibilityFlagsForDraw(skinnedMesh, true, true);
    CHECK(HasGpuVisibilityFlag(explicitlyVisible, GpuVisibilityFlags::ForceVisible));
}

TEST_CASE("GPU visibility mask capabilities remain opt-in")
{
    using namespace Keire::RenderBackend;

    CHECK(CanGpuReject(DefaultGpuVisibilityBoundsPolicy(GpuVisibilityClass::PointLight),
                       GpuVisibilityConsumer::ForwardPlusLightMask));
    CHECK(CanGpuReject(DefaultGpuVisibilityBoundsPolicy(GpuVisibilityClass::SpotLight),
                       GpuVisibilityConsumer::ForwardPlusLightMask));
    CHECK(CanGpuReject(DefaultGpuVisibilityBoundsPolicy(GpuVisibilityClass::ReflectionProbe),
                       GpuVisibilityConsumer::SpatialVolumeMask));
    CHECK(CanGpuReject(DefaultGpuVisibilityBoundsPolicy(GpuVisibilityClass::LightProbeVolume),
                       GpuVisibilityConsumer::SpatialVolumeMask));

    const Keire::RenderCapabilities capabilities;

    CHECK_FALSE(capabilities.GpuOcclusionVfxVisibilityMasks);
    CHECK_FALSE(capabilities.GpuOcclusionLocalLightMasks);
    CHECK_FALSE(capabilities.GpuOcclusionSpatialVolumeMasks);
}

TEST_CASE("local-light and spatial-volume visibility use complete conservative influence bounds")
{
    using namespace Keire::RenderBackend;

    for (const auto visibilityClass : {GpuVisibilityClass::PointLight, GpuVisibilityClass::SpotLight})
    {
        const auto bounds = DefaultGpuVisibilityBoundsPolicy(visibilityClass);
        CHECK(bounds == GpuVisibilityBoundsPolicy::ConservativeDynamic);
        CHECK(CanGpuReject(bounds, GpuVisibilityConsumer::ForwardPlusLightMask));
    }

    for (const auto visibilityClass : {GpuVisibilityClass::ReflectionProbe, GpuVisibilityClass::LightProbeVolume})
    {
        const auto bounds = DefaultGpuVisibilityBoundsPolicy(visibilityClass);
        CHECK(bounds == GpuVisibilityBoundsPolicy::ConservativeDynamic);
        CHECK(CanGpuReject(bounds, GpuVisibilityConsumer::SpatialVolumeMask));
    }

    CHECK(DefaultGpuVisibilityBoundsPolicy(GpuVisibilityClass::NonSpatial) ==
          GpuVisibilityBoundsPolicy::UnknownOrUnbounded);
    CHECK_FALSE(CanGpuReject(DefaultGpuVisibilityBoundsPolicy(GpuVisibilityClass::NonSpatial),
                             GpuVisibilityConsumer::SpatialVolumeMask));
}

TEST_CASE("camera-inside influence volumes remain forced visible for every mask consumer")
{
    using namespace Keire::RenderBackend;

    const auto checkForcedVisible = [](const GpuVisibilityClass visibilityClass, const GpuVisibilityConsumer consumer)
    {
        const auto flags = GpuVisibilityFlagsForDraw(visibilityClass, true, true);
        CHECK(HasGpuVisibilityFlag(flags, GpuVisibilityFlags::ForceVisible));
        CHECK_FALSE(CanGpuReject(DefaultGpuVisibilityBoundsPolicy(visibilityClass), consumer, flags));
    };

    checkForcedVisible(GpuVisibilityClass::PointLight, GpuVisibilityConsumer::ForwardPlusLightMask);
    checkForcedVisible(GpuVisibilityClass::SpotLight, GpuVisibilityConsumer::ForwardPlusLightMask);
    checkForcedVisible(GpuVisibilityClass::ReflectionProbe, GpuVisibilityConsumer::SpatialVolumeMask);
    checkForcedVisible(GpuVisibilityClass::LightProbeVolume, GpuVisibilityConsumer::SpatialVolumeMask);
}
