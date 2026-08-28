#include "KeireInternal/Rendering/VfxVisibilityPlanInternal.h"

#include <doctest/doctest.h>

#include <array>
#include <cstdint>

namespace
{
    using namespace Keire;
    using namespace Keire::RenderBackend;

    [[nodiscard]] GpuVfxVisibilityInput Gpu(const std::uint32_t index, const std::uint32_t generation,
                                            const VfxRendererType renderer, const std::uint32_t capacity,
                                            const bool conservative = true)
    {
        return {{index, generation}, renderer, capacity, conservative};
    }
} // namespace

TEST_CASE("VFX visibility planning preserves contribution order and sorts GPU handle generations")
{
    using namespace Keire;
    using namespace Keire::RenderBackend;

    const std::array firstEmitters{Gpu(8, 1, VfxRendererType::Sprite, 2), Gpu(2, 3, VfxRendererType::Ribbon, 40),
                                   Gpu(2, 2, VfxRendererType::Mesh, 3)};
    const std::array secondEmitters{Gpu(1, 9, VfxRendererType::Sprite, 4)};
    const std::array snapshots{VfxVisibilitySnapshotInput{{}, firstEmitters},
                               VfxVisibilitySnapshotInput{{}, secondEmitters}};

    const auto plan = BuildVfxVisibilityPlan(snapshots);
    REQUIRE(plan.Entries.size() == 4);
    CHECK(plan.Entries[0].SnapshotIndex == 0);
    CHECK(plan.Entries[0].SourceIndex == 2);
    CHECK(plan.Entries[0].Handle == VfxVisibilityHandleIdentity{2, 2});
    CHECK(plan.Entries[0].First == 0);
    CHECK(plan.Entries[0].Count == 3);
    CHECK(plan.Entries[1].Handle == VfxVisibilityHandleIdentity{2, 3});
    CHECK(plan.Entries[1].RequestedCount == 1);
    CHECK(plan.Entries[1].First == 3);
    CHECK(plan.Entries[2].Handle == VfxVisibilityHandleIdentity{8, 1});
    CHECK(plan.Entries[2].First == 4);
    CHECK(plan.Entries[3].SnapshotIndex == 1);
    CHECK(plan.Entries[3].Handle == VfxVisibilityHandleIdentity{1, 9});
    CHECK(plan.Entries[3].First == 6);
    CHECK(plan.CandidateCount == 10);
}

TEST_CASE("VFX visibility planning excludes CPU Mesh geometry and forces unsupported paths visible")
{
    using namespace Keire;
    using namespace Keire::RenderBackend;

    const std::array cpuParticles{
        CpuVfxVisibilityInput{VfxRendererType::Sprite}, CpuVfxVisibilityInput{VfxRendererType::Mesh},
        CpuVfxVisibilityInput{VfxRendererType::Ribbon}, CpuVfxVisibilityInput{VfxRendererType::Volumetric}};
    const std::array gpuEmitters{Gpu(2, 1, VfxRendererType::Sprite, 8, false),
                                 Gpu(4, 1, VfxRendererType::Volumetric, 16)};
    const std::array snapshots{VfxVisibilitySnapshotInput{cpuParticles, gpuEmitters}};

    const auto plan = BuildVfxVisibilityPlan(snapshots);
    REQUIRE(plan.Entries.size() == 6);
    CHECK(plan.CandidateCount == 0);
    CHECK(plan.GeometryOwnedEntries == 1);
    CHECK(plan.ForceVisibleEntries == 5);
    CHECK(plan.Entries[1].Disposition == VfxVisibilityPlanDisposition::GeometryOwned);
    CHECK(plan.Entries[1].Reason == VfxVisibilityPlanReason::CpuMeshAlreadyCapturedAsGeometry);
    CHECK(plan.Entries[4].Reason == VfxVisibilityPlanReason::UnsafeBounds);
    CHECK(plan.Entries[5].Reason == VfxVisibilityPlanReason::UnsupportedRenderer);
    for (const auto& entry : plan.Entries)
    {
        CHECK(entry.First == InvalidVfxVisibilityOffset);
        CHECK(entry.Count == 0);
    }
}

TEST_CASE("VFX visibility planning reserves whole ranges atomically and keeps later fitting groups")
{
    using namespace Keire;
    using namespace Keire::RenderBackend;

    const std::array emitters{Gpu(1, 1, VfxRendererType::Sprite, 4), Gpu(2, 1, VfxRendererType::Mesh, 2),
                              Gpu(3, 1, VfxRendererType::Ribbon, 500), Gpu(4, 1, VfxRendererType::Sprite, 0)};
    const std::array snapshots{VfxVisibilitySnapshotInput{{}, emitters}};

    const auto plan = BuildVfxVisibilityPlan(snapshots, 5);
    REQUIRE(plan.Entries.size() == 4);
    CHECK(plan.Entries[0].Disposition == VfxVisibilityPlanDisposition::CandidateRange);
    CHECK(plan.Entries[0].First == 0);
    CHECK(plan.Entries[0].Count == 4);
    CHECK(plan.Entries[1].Disposition == VfxVisibilityPlanDisposition::ForceVisible);
    CHECK(plan.Entries[1].Reason == VfxVisibilityPlanReason::CandidateLimit);
    CHECK(plan.Entries[1].First == InvalidVfxVisibilityOffset);
    CHECK(plan.Entries[1].Count == 0);
    CHECK(plan.Entries[2].Disposition == VfxVisibilityPlanDisposition::CandidateRange);
    CHECK(plan.Entries[2].First == 4);
    CHECK(plan.Entries[2].Count == 1);
    CHECK(plan.Entries[3].Reason == VfxVisibilityPlanReason::InvalidRange);
    CHECK(plan.CandidateCount == 5);
    CHECK(plan.OverflowedEntries == 1);
    CHECK(plan.ForceVisibleEntries == 2);
}

TEST_CASE("VFX visibility planning is deterministic and bounded by the frame candidate ceiling")
{
    using namespace Keire;
    using namespace Keire::RenderBackend;

    static_assert(MaximumVfxVisibilityCandidates == 262'144U);
    const std::array emitters{Gpu(9, 7, VfxRendererType::Sprite, MaximumVfxVisibilityCandidates),
                              Gpu(10, 1, VfxRendererType::Ribbon, 1)};
    const std::array snapshots{VfxVisibilitySnapshotInput{{}, emitters}};

    const auto first = BuildVfxVisibilityPlan(snapshots);
    const auto second = BuildVfxVisibilityPlan(snapshots);
    CHECK(first == second);
    CHECK(first.CandidateCount == MaximumVfxVisibilityCandidates);
    REQUIRE(first.Entries.size() == 2);
    CHECK(first.Entries[1].Reason == VfxVisibilityPlanReason::CandidateLimit);
    CHECK(first.OverflowedEntries == 1);
}
