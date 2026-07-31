#include "KeireInternal/Rendering/RenderBackendInternal.h"

#include <doctest/doctest.h>

#include <cstddef>

TEST_CASE("GPU skinning vertex storage uses explicit 16-byte lanes")
{
    using Keire::RenderBackend::GpuMeshVertex;
    using Keire::RenderBackend::GpuRenderVertex;

    CHECK(alignof(GpuMeshVertex) == 16);
    CHECK(sizeof(GpuMeshVertex) == 80);
    CHECK(offsetof(GpuMeshVertex, Position) == 0);
    CHECK(offsetof(GpuMeshVertex, Normal) == 16);
    CHECK(offsetof(GpuMeshVertex, UV0) == 32);
    CHECK(offsetof(GpuMeshVertex, VertexColor) == 48);
    CHECK(offsetof(GpuMeshVertex, Tangent) == 64);

    CHECK(alignof(GpuRenderVertex) == 16);
    CHECK(sizeof(GpuRenderVertex) == 48);
    CHECK(offsetof(GpuRenderVertex, Position) == 0);
    CHECK(offsetof(GpuRenderVertex, Color) == 16);
    CHECK(offsetof(GpuRenderVertex, Normal) == 32);
}

TEST_CASE("GPU linear blend skinning uses supported compute backends")
{
    using Keire::RenderBackend::SupportsComputeSkinning;

    CHECK(SupportsComputeSkinning("direct3d12", Keire::SkinningMethod::LinearBlend));
    CHECK(SupportsComputeSkinning("vulkan", Keire::SkinningMethod::LinearBlend));
    CHECK(SupportsComputeSkinning("metal", Keire::SkinningMethod::LinearBlend));
    CHECK_FALSE(SupportsComputeSkinning("vulkan", Keire::SkinningMethod::DualQuaternion));
    CHECK_FALSE(SupportsComputeSkinning({}, Keire::SkinningMethod::LinearBlend));
}

TEST_CASE("GPU skinning output slots stay bounded by frames in flight")
{
    using Keire::RenderBackend::SkinningOutputSlot;

    CHECK(SkinningOutputSlot(0, 3) == 0);
    CHECK(SkinningOutputSlot(1, 3) == 0);
    CHECK(SkinningOutputSlot(2, 3) == 1);
    CHECK(SkinningOutputSlot(3, 3) == 2);
    CHECK(SkinningOutputSlot(4, 3) == 0);
    CHECK(SkinningOutputSlot(10, 3) == 0);
    CHECK(SkinningOutputSlot(10, 0) == 0);
}

TEST_CASE("GPU VFX sequencing distinguishes document snapshots from simulation steps")
{
    Keire::RenderBackend::GpuVfxWorldResources resources;

    CHECK(resources.ShouldApplySnapshot(1));
    CHECK(resources.ShouldConsumeSimulationStep(1));
    resources.MarkSnapshotApplied(1);
    resources.MarkSimulationStepConsumed(1);

    CHECK_FALSE(resources.ShouldApplySnapshot(1));
    CHECK_FALSE(resources.ShouldConsumeSimulationStep(1));
    CHECK(resources.ShouldApplySnapshot(2));
    CHECK_FALSE(resources.ShouldConsumeSimulationStep(1));
    resources.MarkSnapshotApplied(2);

    CHECK_FALSE(resources.ShouldApplySnapshot(1));
    CHECK(resources.ShouldConsumeSimulationStep(2));
    resources.MarkSimulationStepConsumed(2);
    resources.InvalidateSequencing();

    CHECK(resources.ShouldApplySnapshot(2));
    CHECK(resources.ShouldConsumeSimulationStep(2));
}

TEST_CASE("SDL frame scheduling follows its bounded device queue")
{
    using Keire::RenderBackend::SdlAllowedFramesInFlight;

    CHECK(SdlAllowedFramesInFlight(0) == 1);
    CHECK(SdlAllowedFramesInFlight(1) == 1);
    CHECK(SdlAllowedFramesInFlight(2) == 2);
    CHECK(SdlAllowedFramesInFlight(3) == 3);
    CHECK(SdlAllowedFramesInFlight(4) == 3);
    CHECK(SdlAllowedFramesInFlight(8) == 3);
}
