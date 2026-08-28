#include "KeireInternal/Rendering/RenderSurfaceStateInternal.h"

#include <doctest/doctest.h>

#include <cstdint>

TEST_CASE("GPU VFX mask outputs require exact frame slot surface epoch and device generation ownership")
{
    using namespace Keire::RenderBackend;

    constexpr std::uint64_t frame = 91U;
    constexpr std::uint32_t slot = 1U;
    constexpr std::uint64_t epoch = 8U;
    constexpr std::uint32_t deviceGeneration = 4U;
    constexpr std::uint64_t world = 12U;
    constexpr std::uint32_t handleIndex = 5U;
    constexpr std::uint32_t handleGeneration = 3U;

    GpuVfxFrameResources resources;
    resources.Outputs.push_back({world, handleIndex, handleGeneration});
    resources.TakeOwnership(frame, slot, epoch, deviceGeneration);

    REQUIRE(ResolveGpuVfxFrameOutput(resources, frame, slot, epoch, deviceGeneration, world, handleIndex,
                                     handleGeneration));
    CHECK_FALSE(ResolveGpuVfxFrameOutput(resources, frame + 1U, slot, epoch, deviceGeneration, world, handleIndex,
                                         handleGeneration));
    CHECK_FALSE(ResolveGpuVfxFrameOutput(resources, frame, slot + 1U, epoch, deviceGeneration, world, handleIndex,
                                         handleGeneration));
    CHECK_FALSE(ResolveGpuVfxFrameOutput(resources, frame, slot, epoch + 1U, deviceGeneration, world, handleIndex,
                                         handleGeneration));
    CHECK_FALSE(ResolveGpuVfxFrameOutput(resources, frame, slot, epoch, deviceGeneration + 1U, world, handleIndex,
                                         handleGeneration));
    CHECK_FALSE(ResolveGpuVfxFrameOutput(resources, frame, slot, epoch, deviceGeneration, world, handleIndex + 1U,
                                         handleGeneration));

    resources.OwnershipValid = false;
    CHECK_FALSE(ResolveGpuVfxFrameOutput(resources, frame, slot, epoch, deviceGeneration, world, handleIndex,
                                         handleGeneration));
}

TEST_CASE("GPU VFX frame output lookup preserves deterministic stored emitter order")
{
    using namespace Keire::RenderBackend;

    GpuVfxFrameResources resources;
    resources.Outputs.push_back({7U, 9U, 1U});
    resources.Outputs.push_back({7U, 2U, 4U});
    resources.TakeOwnership(10U, 0U, 3U, 2U);

    const auto* first = ResolveGpuVfxFrameOutput(resources, 10U, 0U, 3U, 2U, 7U, 9U, 1U);
    const auto* second = ResolveGpuVfxFrameOutput(resources, 10U, 0U, 3U, 2U, 7U, 2U, 4U);
    REQUIRE(first);
    REQUIRE(second);
    CHECK(first == &resources.Outputs[0]);
    CHECK(second == &resources.Outputs[1]);
}
