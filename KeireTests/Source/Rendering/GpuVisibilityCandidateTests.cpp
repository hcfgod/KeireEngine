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
