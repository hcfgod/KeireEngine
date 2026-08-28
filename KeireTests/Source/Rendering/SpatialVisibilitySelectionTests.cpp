#include "KeireInternal/Rendering/SpatialVisibilitySelectionInternal.h"

#include "KeireInternal/Rendering/FrameGraphInternal.h"
#include "KeireInternal/Rendering/RenderBackendInternal.h"
#include "KeireInternal/Rendering/RenderSurfaceStateInternal.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>

namespace
{
    using namespace Keire;
    using namespace Keire::RenderBackend;

    [[nodiscard]] AssetId Id(const std::uint32_t suffix)
    {
        auto value = std::string("00000000-0000-4000-8000-");
        value += std::string(12U - std::to_string(suffix).size(), '0');
        value += std::to_string(suffix);
        return AssetId::Parse(value);
    }

    [[nodiscard]] SpatialVisibilityOwnership Ownership(const std::uint32_t deviceGeneration = 4U)
    {
        return {
            .FrameId = 17U, .SurfaceEpoch = 9U, .FrameSlot = 1U, .DeviceGeneration = deviceGeneration, .Valid = true};
    }

    [[nodiscard]] SpatialReflectionSelectionCandidate
    Reflection(const std::uint32_t id, const std::uint32_t visibilityIndex, const std::int32_t importance,
               const float weight, const float distance, const bool conservativeBoundsCurrent = true)
    {
        SpatialReflectionSelectionCandidate result;
        result.StableId = Id(id);
        result.FlatVisibilityIndex = visibilityIndex;
        result.Importance = importance;
        result.Weight = weight;
        result.Distance = distance;
        result.ConservativeBoundsCurrent = conservativeBoundsCurrent;
        result.Descriptor.ExtentsWeight = {1.0F, 2.0F, 3.0F, weight};
        result.Descriptor.Parameters = {static_cast<float>(id), 1.0F, 1.0F, 4.0F};
        return result;
    }

    [[nodiscard]] SpatialLightProbeSelectionCandidate LightProbe(const std::uint32_t id,
                                                                 const std::uint32_t visibilityIndex,
                                                                 const std::int32_t priority, const float value,
                                                                 const bool conservativeBoundsCurrent = true)
    {
        SpatialLightProbeSelectionCandidate result;
        result.StableId = Id(id);
        result.FlatVisibilityIndex = visibilityIndex;
        result.Priority = priority;
        result.ContainsPoint = true;
        result.SampleValid = true;
        result.ConservativeBoundsCurrent = conservativeBoundsCurrent;
        result.ProbeIrradiance[0] = {value, value + 1.0F, value + 2.0F, 0.0F};
        return result;
    }
} // namespace

TEST_CASE("spatial visibility layout maps all reflections before contribution-ordered light volumes")
{
    const std::array counts{SpatialVisibilityContributionCounts{2U, 1U}, SpatialVisibilityContributionCounts{1U, 2U},
                            SpatialVisibilityContributionCounts{0U, 1U}};
    const auto layout = BuildSpatialVisibilityLayout(counts);
    REQUIRE(layout.Contributions.size() == 3U);
    CHECK(layout.ReflectionProbeCount == 3U);
    CHECK(layout.LightProbeVolumeCount == 4U);
    CHECK(layout.TotalCount() == 7U);
    CHECK(FlatReflectionProbeIndex(layout, 0U, 1U) == 1U);
    CHECK(FlatReflectionProbeIndex(layout, 1U, 0U) == 2U);
    CHECK(FlatLightProbeVolumeIndex(layout, 0U, 0U) == 3U);
    CHECK(FlatLightProbeVolumeIndex(layout, 1U, 1U) == 5U);
    CHECK(FlatLightProbeVolumeIndex(layout, 2U, 0U) == 6U);
    CHECK_THROWS_AS((void)FlatReflectionProbeIndex(layout, 2U, 0U), std::out_of_range);
    CHECK_THROWS_AS((void)FlatLightProbeVolumeIndex(layout, 1U, 2U), std::out_of_range);

    const std::array overflow{SpatialVisibilityContributionCounts{std::numeric_limits<std::uint32_t>::max(), 0U},
                              SpatialVisibilityContributionCounts{0U, 1U}};
    CHECK_THROWS_AS((void)BuildSpatialVisibilityLayout(overflow), std::invalid_argument);
}

TEST_CASE("spatial visibility masks require exact ownership binary values and count")
{
    const auto expected = Ownership();
    const std::array mask{1U, 0U};
    CHECK(CanConsumeSpatialVisibilityMask({mask, expected}, expected, 2U));

    for (const auto stale : std::array{
             SpatialVisibilityOwnership{18U, 9U, 1U, 4U, true},
             SpatialVisibilityOwnership{17U, 10U, 1U, 4U, true},
             SpatialVisibilityOwnership{17U, 9U, 2U, 4U, true},
             SpatialVisibilityOwnership{17U, 9U, 1U, 5U, true},
             SpatialVisibilityOwnership{17U, 9U, 1U, 4U, false},
         })
    {
        CHECK_FALSE(CanConsumeSpatialVisibilityMask({mask, stale}, expected, 2U));
    }
    CHECK_FALSE(CanConsumeSpatialVisibilityMask({mask, expected}, expected, 1U));
    const std::array invalid{1U, 2U};
    CHECK_FALSE(CanConsumeSpatialVisibilityMask({invalid, expected}, expected, 2U));
}

TEST_CASE("spatial selection advances to visible reflection fallbacks and renormalizes their weights")
{
    const auto expected = Ownership();
    const std::array mask{0U, 1U, 1U};
    const std::array reflections{Reflection(1U, 0U, 8, 0.6F, 1.0F), Reflection(2U, 1U, 7, 0.3F, 2.0F),
                                 Reflection(3U, 2U, 7, 0.1F, 3.0F)};
    const auto selected = SelectSpatialLightingForDraw({.Contribution = {0U, 3U, 3U, 0U},
                                                        .ReflectionProbes = reflections,
                                                        .VisibilityMask = {mask, expected},
                                                        .ExpectedOwnership = expected,
                                                        .ExpectedVisibilityCount = 3U});

    REQUIRE(selected.MaskUsable);
    CHECK_FALSE(selected.UsedFailVisible);
    CHECK((selected.Record.Metadata[0] & AssetSpatialSelectionHasReflectionProbe0) != 0U);
    CHECK((selected.Record.Metadata[0] & AssetSpatialSelectionHasReflectionProbe1) != 0U);
    CHECK(selected.Record.Metadata[1] == 1U);
    CHECK(selected.Record.Metadata[2] == 2U);
    CHECK(selected.Record.ReflectionProbes[0].ExtentsWeight.W == doctest::Approx(0.75F));
    CHECK(selected.Record.ReflectionProbes[1].ExtentsWeight.W == doctest::Approx(0.25F));
}

TEST_CASE("spatial selection advances to the next visible light probe volume")
{
    const auto expected = Ownership();
    const std::array mask{1U, 1U, 0U, 1U};
    const std::array probes{LightProbe(1U, 2U, 10, 3.0F), LightProbe(2U, 3U, 5, 7.0F)};
    const auto selected = SelectSpatialLightingForDraw({.Contribution = {0U, 2U, 2U, 2U},
                                                        .LightProbeVolumes = probes,
                                                        .VisibilityMask = {mask, expected},
                                                        .ExpectedOwnership = expected,
                                                        .ExpectedVisibilityCount = 4U});

    REQUIRE(selected.MaskUsable);
    CHECK((selected.Record.Metadata[0] & AssetSpatialSelectionHasLightProbe) != 0U);
    CHECK(selected.Record.Metadata[3] == 3U);
    CHECK(selected.Record.ProbeIrradiance[0].X == doctest::Approx(7.0F));
}

TEST_CASE("stale mask ownership and unknown conservative bounds fail visible")
{
    const auto expected = Ownership();
    const std::array mask{0U};
    const std::array current{Reflection(1U, 0U, 1, 1.0F, 1.0F)};
    const auto stale = SelectSpatialLightingForDraw({.Contribution = {0U, 1U, 1U, 0U},
                                                     .ReflectionProbes = current,
                                                     .VisibilityMask = {mask, Ownership(3U)},
                                                     .ExpectedOwnership = expected,
                                                     .ExpectedVisibilityCount = 1U});
    CHECK_FALSE(stale.MaskUsable);
    CHECK(stale.UsedFailVisible);
    CHECK(stale.Record.Metadata[1] == 0U);
    CHECK((stale.Record.Metadata[0] & AssetSpatialSelectionUsedFailVisible) != 0U);

    const std::array unknown{Reflection(1U, 0U, 1, 1.0F, 1.0F, false)};
    const auto conservative = SelectSpatialLightingForDraw({.Contribution = {0U, 1U, 1U, 0U},
                                                            .ReflectionProbes = unknown,
                                                            .VisibilityMask = {mask, expected},
                                                            .ExpectedOwnership = expected,
                                                            .ExpectedVisibilityCount = 1U});
    CHECK(conservative.MaskUsable);
    CHECK(conservative.UsedFailVisible);
    CHECK(conservative.Record.Metadata[1] == 0U);
}

TEST_CASE("invalid mask data or contribution indices reject the mask and retain deterministic selections")
{
    const auto expected = Ownership();
    const std::array invalidMask{2U, 0U};
    const std::array firstOrder{Reflection(2U, 7U, 3, 0.25F, 2.0F), Reflection(1U, 0U, 3, 0.75F, 1.0F)};
    const std::array secondOrder{firstOrder[1], firstOrder[0]};
    const auto select = [&](const auto& reflections)
    {
        return SelectSpatialLightingForDraw({.Contribution = {0U, 2U, 2U, 0U},
                                             .ReflectionProbes = reflections,
                                             .VisibilityMask = {invalidMask, expected},
                                             .ExpectedOwnership = expected,
                                             .ExpectedVisibilityCount = 2U});
    };
    const auto first = select(firstOrder);
    const auto second = select(secondOrder);
    CHECK_FALSE(first.MaskUsable);
    CHECK(first.UsedFailVisible);
    CHECK(first.Record.Metadata[1] == 0U);
    CHECK(first.Record.Metadata[2] == 7U);
    CHECK(
        std::ranges::equal(std::as_bytes(std::span(&first.Record, 1U)), std::as_bytes(std::span(&second.Record, 1U))));
}

TEST_CASE("empty spatial selections retain UINT_MAX sentinels for CPU fallback diagnostics")
{
    const auto result = SelectSpatialLightingForDraw({});
    CHECK(result.Record.Metadata[1] == InvalidAssetSpatialSelectionIndex);
    CHECK(result.Record.Metadata[2] == InvalidAssetSpatialSelectionIndex);
    CHECK(result.Record.Metadata[3] == InvalidAssetSpatialSelectionIndex);
    const AssetSpatialLightingUniforms uniforms;
    CHECK(uniforms.SpatialSelection[0] == InvalidAssetSpatialSelectionIndex);
    CHECK(uniforms.SpatialSelection[1] == 0U);
    CHECK(uniforms.SpatialSelection[2] == 0U);
    CHECK(uniforms.SpatialSelection[3] == 0U);
}

TEST_CASE("spatial selection frame resources require exact frame ownership")
{
    GpuSpatialSelectionFrameResources resources;
    resources.TakeOwnership(91U, 1U, 7U, 4U);

    CHECK(resources.OwnedBy(91U, 1U, 7U, 4U));
    CHECK_FALSE(resources.OwnedBy(92U, 1U, 7U, 4U));
    CHECK_FALSE(resources.OwnedBy(91U, 2U, 7U, 4U));
    CHECK_FALSE(resources.OwnedBy(91U, 1U, 8U, 4U));
    CHECK_FALSE(resources.OwnedBy(91U, 1U, 7U, 5U));

    resources = {};
    CHECK_FALSE(resources.OwnedBy(91U, 1U, 7U, 4U));
}

TEST_CASE("scene frame graph orders spatial selection after classification and before draw consumers")
{
    const auto graph = BuildStaticSceneFrameGraph();
    const auto position = [&](const FrameGraphPass pass)
    {
        const auto found = std::ranges::find(graph.Compiled.Order, pass);
        REQUIRE(found != graph.Compiled.Order.end());
        return static_cast<std::size_t>(found - graph.Compiled.Order.begin());
    };

    CHECK(position(graph.GpuOcclusionDepthPass) < position(graph.GpuOcclusionPyramidPass));
    CHECK(position(graph.GpuOcclusionPyramidPass) < position(graph.GpuOcclusionCullingPass));
    CHECK(position(graph.GpuOcclusionCullingPass) < position(graph.SpatialSelection));
    CHECK(position(graph.SpatialSelection) < position(graph.Opaque));
    CHECK(position(graph.SpatialSelection) < position(graph.Transparency));
}

TEST_CASE("incomplete current-pose submesh bounds remain stale for visibility consumers")
{
    constexpr std::uint64_t frame = 41U;
    SceneDrawItem draw;
    draw.VisibilityClass = GpuVisibilityClass::SkinnedMesh;
    draw.PoseGeneration = 8U;
    draw.BoundsPoseGeneration = draw.PoseGeneration;
    draw.BoundsFrameIndex = frame;
    draw.CurrentPoseSubmeshBounds.resize(1U);
    draw.SkinnedAssetVertices = reinterpret_cast<SDL_GPUBuffer*>(static_cast<std::uintptr_t>(1U));

    REQUIRE(draw.HasFreshCurrentPoseBounds(frame));
    CHECK_FALSE(draw.HasFreshCurrentPoseBounds(frame, 2U));
    const auto flags =
        GpuVisibilityFlagsForDraw(draw.VisibilityClass, draw.AlwaysVisible, draw.HasFreshCurrentPoseBounds(frame, 2U));
    CHECK(HasGpuVisibilityFlag(flags, GpuVisibilityFlags::ForceVisible));
    CHECK(HasGpuVisibilityFlag(flags, GpuVisibilityFlags::StaleBounds));
    CHECK_FALSE(CanGpuVisibilityClassOcclude(draw.VisibilityClass, draw.HasFreshCurrentPoseBounds(frame, 2U)));
}
