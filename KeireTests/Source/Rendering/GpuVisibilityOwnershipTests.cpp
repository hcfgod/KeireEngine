#include "KeireInternal/Rendering/GpuVisibilityCandidateInternal.h"
#include "KeireInternal/Rendering/RenderBackendInternal.h"
#include "KeireInternal/Rendering/RenderSurfaceStateInternal.h"

#include <doctest/doctest.h>

#include <cstdint>
#include <vector>

namespace
{
    constexpr std::uint64_t Frame = 41U;
    constexpr std::uint64_t SurfaceEpoch = 7U;
    constexpr std::uint32_t DeviceGeneration = 3U;

    void AssignOwnership(Keire::RenderBackend::GpuOcclusionFrameResources& resources, const std::uint32_t frameSlot)
    {
        resources.FrameId = Frame;
        resources.FrameSlot = frameSlot;
        resources.SurfaceEpoch = SurfaceEpoch;
        resources.DeviceGeneration = DeviceGeneration;
        resources.OwnershipValid = true;
    }
} // namespace

TEST_CASE("GPU visibility worksets reject stale frame slot surface epoch and device generation")
{
    using namespace Keire::RenderBackend;

    for (const auto depth : {1U, 2U, 3U})
    {
        CAPTURE(depth);
        std::vector<GpuOcclusionFrameResources> occlusion(depth);
        std::vector<ForwardPlusGpuResources> forwardPlus(depth);

        for (std::uint32_t slot = 0; slot < depth; ++slot)
        {
            CAPTURE(slot);
            AssignOwnership(occlusion[slot], slot);
            forwardPlus[slot].TakeOwnership(Frame, slot, SurfaceEpoch, DeviceGeneration);

            CHECK(occlusion[slot].OwnedBy(Frame, slot, SurfaceEpoch, DeviceGeneration));
            CHECK(forwardPlus[slot].OwnedBy(Frame, slot, SurfaceEpoch, DeviceGeneration));

            CHECK_FALSE(occlusion[slot].OwnedBy(Frame + 1U, slot, SurfaceEpoch, DeviceGeneration));
            CHECK_FALSE(occlusion[slot].OwnedBy(Frame, slot + 1U, SurfaceEpoch, DeviceGeneration));
            CHECK_FALSE(occlusion[slot].OwnedBy(Frame, slot, SurfaceEpoch + 1U, DeviceGeneration));
            CHECK_FALSE(occlusion[slot].OwnedBy(Frame, slot, SurfaceEpoch, DeviceGeneration + 1U));

            CHECK_FALSE(forwardPlus[slot].OwnedBy(Frame + 1U, slot, SurfaceEpoch, DeviceGeneration));
            CHECK_FALSE(forwardPlus[slot].OwnedBy(Frame, slot + 1U, SurfaceEpoch, DeviceGeneration));
            CHECK_FALSE(forwardPlus[slot].OwnedBy(Frame, slot, SurfaceEpoch + 1U, DeviceGeneration));
            CHECK_FALSE(forwardPlus[slot].OwnedBy(Frame, slot, SurfaceEpoch, DeviceGeneration + 1U));
        }
    }
}

TEST_CASE("resize and recovery ownership changes cannot consume stale local-light masks")
{
    using namespace Keire::RenderBackend;

    GpuOcclusionFrameResources visibility;
    ForwardPlusGpuResources forwardPlus;
    AssignOwnership(visibility, 1U);
    forwardPlus.TakeOwnership(Frame, 1U, SurfaceEpoch, DeviceGeneration);
    visibility.LocalLightVisibilityCount = 3U;

    CHECK(visibility.OwnedBy(Frame, 1U, SurfaceEpoch, DeviceGeneration));
    CHECK(forwardPlus.OwnedBy(Frame, 1U, SurfaceEpoch, DeviceGeneration));

    SUBCASE("surface resize advances the epoch")
    {
        CHECK_FALSE(visibility.OwnedBy(Frame, 1U, SurfaceEpoch + 1U, DeviceGeneration));
        CHECK_FALSE(forwardPlus.OwnedBy(Frame, 1U, SurfaceEpoch + 1U, DeviceGeneration));
    }

    SUBCASE("device recovery advances the generation")
    {
        CHECK_FALSE(visibility.OwnedBy(Frame, 1U, SurfaceEpoch, DeviceGeneration + 1U));
        CHECK_FALSE(forwardPlus.OwnedBy(Frame, 1U, SurfaceEpoch, DeviceGeneration + 1U));
    }

    SUBCASE("an invalidated workset rejects every identity")
    {
        visibility.OwnershipValid = false;
        forwardPlus.OwnershipValid = false;
        CHECK_FALSE(visibility.OwnedBy(Frame, 1U, SurfaceEpoch, DeviceGeneration));
        CHECK_FALSE(forwardPlus.OwnedBy(Frame, 1U, SurfaceEpoch, DeviceGeneration));
    }
}

TEST_CASE("surface frame ownership provides one writer per bounded slot")
{
    using namespace Keire::RenderBackend;

    for (const auto depth : {1U, 2U, 3U})
    {
        CAPTURE(depth);
        SurfaceResources resources;
        resources.Worksets.resize(depth);
        resources.FinalOutputs.resize(depth + 1U);
        for (std::uint32_t index = 0; index <= depth; ++index)
            resources.FinalOutputs[index] = reinterpret_cast<SDL_GPUTexture*>(static_cast<std::uintptr_t>(index + 1U));

        CHECK(resources.PublishedColor() == resources.FinalOutputs.front());
        for (std::uint32_t slot = 0; slot < depth; ++slot)
            CHECK(resources.WriterColor(slot) == resources.FinalOutputs[slot + 1U]);
        CHECK(resources.WriterColor(depth) == nullptr);
    }
}

TEST_CASE("GPU VFX outputs and masks stay in the same bounded workset generation")
{
    using namespace Keire::RenderBackend;

    for (const auto depth : {1U, 2U, 3U})
    {
        CAPTURE(depth);
        SurfaceResources resources;
        resources.Worksets.resize(depth);
        for (std::uint32_t slot = 0; slot < depth; ++slot)
        {
            CAPTURE(slot);
            auto& workset = resources.Worksets[slot];
            AssignOwnership(workset.GpuOcclusion, slot);
            workset.GpuOcclusion.VfxVisibilityCount = 4U;
            workset.GpuVfx.Outputs.push_back({12U, slot + 2U, 5U, 0U,
                                              reinterpret_cast<SDL_GPUBuffer*>(static_cast<std::uintptr_t>(1U)),
                                              reinterpret_cast<SDL_GPUBuffer*>(static_cast<std::uintptr_t>(2U)),
                                              reinterpret_cast<SDL_GPUBuffer*>(static_cast<std::uintptr_t>(3U)), 4U});
            workset.GpuVfx.TakeOwnership(Frame, slot, SurfaceEpoch, DeviceGeneration);

            CHECK(workset.GpuOcclusion.OwnedBy(Frame, slot, SurfaceEpoch, DeviceGeneration));
            REQUIRE(ResolveGpuVfxFrameOutput(workset.GpuVfx, Frame, slot, SurfaceEpoch, DeviceGeneration, 12U,
                                             slot + 2U, 5U));
            CHECK_FALSE(ResolveGpuVfxFrameOutput(workset.GpuVfx, Frame + 1U, slot, SurfaceEpoch, DeviceGeneration, 12U,
                                                 slot + 2U, 5U));
            CHECK_FALSE(ResolveGpuVfxFrameOutput(workset.GpuVfx, Frame, slot + 1U, SurfaceEpoch, DeviceGeneration, 12U,
                                                 slot + 2U, 5U));
            CHECK_FALSE(ResolveGpuVfxFrameOutput(workset.GpuVfx, Frame, slot, SurfaceEpoch + 1U, DeviceGeneration, 12U,
                                                 slot + 2U, 5U));
            CHECK_FALSE(ResolveGpuVfxFrameOutput(workset.GpuVfx, Frame, slot, SurfaceEpoch, DeviceGeneration + 1U, 12U,
                                                 slot + 2U, 5U));
        }
    }
}

TEST_CASE("resize and recovery invalidate prior GPU VFX output generations fail visibly")
{
    using namespace Keire::RenderBackend;

    GpuVfxFrameResources output;
    output.Outputs.push_back({17U, 4U, 6U});
    output.TakeOwnership(Frame, 0U, SurfaceEpoch, DeviceGeneration);

    REQUIRE(ResolveGpuVfxFrameOutput(output, Frame, 0U, SurfaceEpoch, DeviceGeneration, 17U, 4U, 6U));
    CHECK_FALSE(ResolveGpuVfxFrameOutput(output, Frame, 0U, SurfaceEpoch + 1U, DeviceGeneration, 17U, 4U, 6U));
    CHECK_FALSE(ResolveGpuVfxFrameOutput(output, Frame, 0U, SurfaceEpoch, DeviceGeneration + 1U, 17U, 4U, 6U));

    output.TakeOwnership(Frame + 1U, 0U, SurfaceEpoch + 1U, DeviceGeneration + 1U);
    CHECK_FALSE(ResolveGpuVfxFrameOutput(output, Frame, 0U, SurfaceEpoch, DeviceGeneration, 17U, 4U, 6U));
    CHECK(ResolveGpuVfxFrameOutput(output, Frame + 1U, 0U, SurfaceEpoch + 1U, DeviceGeneration + 1U, 17U, 4U, 6U));
}

TEST_CASE("visibility consumer counters reset when a bounded workset changes ownership")
{
    using namespace Keire::RenderBackend;

    GpuVfxFrameResources vfx;
    GpuSpatialSelectionFrameResources spatial;
    vfx.ConsumedDraws = 7U;
    spatial.ConsumedDraws = 5U;

    vfx.TakeOwnership(Frame, 2U, SurfaceEpoch, DeviceGeneration);
    spatial.TakeOwnership(Frame, 2U, SurfaceEpoch, DeviceGeneration);

    CHECK(vfx.ConsumedDraws == 0U);
    CHECK(spatial.ConsumedDraws == 0U);
    CHECK(vfx.OwnedBy(Frame, 2U, SurfaceEpoch, DeviceGeneration));
    CHECK(spatial.OwnedBy(Frame, 2U, SurfaceEpoch, DeviceGeneration));
}

TEST_CASE("current-pose generation mismatches force skinned draws visible")
{
    using namespace Keire::RenderBackend;

    constexpr std::uint64_t frame = 23U;
    SceneDrawItem draw;
    draw.VisibilityClass = GpuVisibilityClass::SkinnedMesh;
    draw.PoseGeneration = 9U;
    draw.BoundsPoseGeneration = draw.PoseGeneration;
    draw.BoundsFrameIndex = frame;
    draw.CurrentPoseSubmeshBounds.resize(2U);
    draw.SkinnedAssetVertices = reinterpret_cast<SDL_GPUBuffer*>(static_cast<std::uintptr_t>(1U));

    REQUIRE(draw.HasFreshCurrentPoseBounds(frame, 2U));
    const auto freshFlags = GpuVisibilityFlagsForDraw(draw.VisibilityClass, draw.AlwaysVisible, true);
    CHECK_FALSE(HasGpuVisibilityFlag(freshFlags, GpuVisibilityFlags::ForceVisible));
    CHECK_FALSE(HasGpuVisibilityFlag(freshFlags, GpuVisibilityFlags::StaleBounds));
    CHECK(CanGpuReject(DefaultGpuVisibilityBoundsPolicy(draw.VisibilityClass, true),
                       GpuVisibilityConsumer::IndexedIndirect, freshFlags));

    const auto checkMismatch = [&draw, frame]()
    {
        REQUIRE_FALSE(draw.HasFreshCurrentPoseBounds(frame, 2U));
        const auto staleFlags = GpuVisibilityFlagsForDraw(draw.VisibilityClass, draw.AlwaysVisible, false);
        CHECK(HasGpuVisibilityFlag(staleFlags, GpuVisibilityFlags::ForceVisible));
        CHECK(HasGpuVisibilityFlag(staleFlags, GpuVisibilityFlags::StaleBounds));
        CHECK_FALSE(CanGpuReject(DefaultGpuVisibilityBoundsPolicy(draw.VisibilityClass, false),
                                 GpuVisibilityConsumer::IndexedIndirect, staleFlags));
    };

    SUBCASE("pose generation changed after bounds were recorded")
    {
        ++draw.PoseGeneration;
        checkMismatch();
    }

    SUBCASE("bounds came from an earlier frame")
    {
        --draw.BoundsFrameIndex;
        checkMismatch();
    }

    SUBCASE("skinning output is not resident")
    {
        draw.SkinnedAssetVertices = nullptr;
        checkMismatch();
    }

    SUBCASE("bounds do not cover every submesh")
    {
        draw.CurrentPoseSubmeshBounds.pop_back();
        checkMismatch();
    }
}
