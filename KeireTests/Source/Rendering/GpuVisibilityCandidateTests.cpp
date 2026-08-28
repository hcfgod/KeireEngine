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

TEST_CASE("GPU visibility mask capabilities remain opt-in")
{
    const Keire::RenderCapabilities capabilities;

    CHECK_FALSE(capabilities.GpuOcclusionVfxVisibilityMasks);
    CHECK_FALSE(capabilities.GpuOcclusionLocalLightMasks);
    CHECK_FALSE(capabilities.GpuOcclusionSpatialVolumeMasks);
}
