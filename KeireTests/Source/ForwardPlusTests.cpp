#include "KeireInternal/Rendering/ForwardPlusInternal.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <vector>

TEST_CASE("Forward+ CPU fallback produces deterministic bounded tile lists")
{
    const auto projection = Keire::Math::Perspective(60.0F, 1.0F, 0.1F, 100.0F);
    const std::vector lights{Keire::RenderBackend::ForwardPlusLightBounds{{0.0F, 0.0F, 5.0F}, 1.0F},
                             Keire::RenderBackend::ForwardPlusLightBounds{{20.0F, 0.0F, 5.0F}, 1.0F}};
    const auto first = Keire::RenderBackend::BuildForwardPlusCpuTiles(64, 64, projection, lights);
    const auto second = Keire::RenderBackend::BuildForwardPlusCpuTiles(64, 64, projection, lights);
    CHECK(first.Columns == 4);
    CHECK(first.Rows == 4);
    CHECK(first.LightIndices == second.LightIndices);
    CHECK(first.Counts == second.Counts);
    CHECK(first.OverflowedTiles == 0);
    CHECK(std::ranges::find(first.LightIndices, 1U) == first.LightIndices.end());
    REQUIRE(first.Offsets.size() == first.Counts.size());
    for (std::size_t tile = 0; tile < first.Counts.size(); ++tile)
    {
        if (first.Counts[tile] == 0)
            continue;
        REQUIRE(first.Counts[tile] == 1);
        REQUIRE(first.Offsets[tile] < first.LightIndices.size());
        CHECK(first.LightIndices[first.Offsets[tile]] == 0);
    }
}

TEST_CASE("Forward+ CPU fallback reports per-tile overflow without exceeding its ABI limit")
{
    const auto projection = Keire::Math::Perspective(60.0F, 1.0F, 0.1F, 100.0F);
    std::vector<Keire::RenderBackend::ForwardPlusLightBounds> lights(140, {{0.0F, 0.0F, 5.0F}, 10.0F});
    const auto grid = Keire::RenderBackend::BuildForwardPlusCpuTiles(16, 16, projection, lights);
    REQUIRE(grid.Counts.size() == 1);
    CHECK(grid.Counts.front() == Keire::RenderBackend::ForwardPlusTileGrid::MaximumLightsPerTile);
    CHECK(grid.OverflowedTiles == 1);
}
