#include "KeireInternal/Rendering/ForwardPlusInternal.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <stdexcept>
#include <vector>

namespace
{
    bool TileContainsLight(const Keire::RenderBackend::ForwardPlusTileGrid& grid, const std::uint32_t column,
                           const std::uint32_t row, const std::uint32_t light)
    {
        if (column >= grid.Columns || row >= grid.Rows)
            return false;
        const auto tile = static_cast<std::size_t>(row) * grid.Columns + column;
        for (std::uint16_t offset = 0; offset < grid.Counts[tile]; ++offset)
        {
            if (grid.LightIndices[grid.Offsets[tile] + offset] == light)
                return true;
        }
        return false;
    }
} // namespace

TEST_CASE("Forward+ CPU fallback produces deterministic bounded tile lists")
{
    const auto projection = Keire::Math::Perspective(60.0F, 1.0F, 0.1F, 100.0F);
    const std::vector lights{Keire::RenderBackend::ForwardPlusLightBounds{{0.0F, 0.0F, 5.0F}, 1.0F},
                             Keire::RenderBackend::ForwardPlusLightBounds{{20.0F, 0.0F, 5.0F}, 1.0F}};
    const auto first = Keire::RenderBackend::BuildForwardPlusCpuTiles(64, 64, projection, 0.1F, lights);
    const auto second = Keire::RenderBackend::BuildForwardPlusCpuTiles(64, 64, projection, 0.1F, lights);
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
    const auto grid = Keire::RenderBackend::BuildForwardPlusCpuTiles(16, 16, projection, 0.1F, lights);
    REQUIRE(grid.Counts.size() == 1);
    CHECK(grid.Counts.front() == Keire::RenderBackend::ForwardPlusTileGrid::MaximumLightsPerTile);
    CHECK(grid.OverflowedTiles == 1);
}

TEST_CASE("Forward+ perspective tiles conservatively cover sphere silhouettes")
{
    const auto projection = Keire::Math::Perspective(60.0F, 1.0F, 0.1F, 100.0F);

    SUBCASE("centered spheres reach their tangent tiles")
    {
        const std::vector lights{Keire::RenderBackend::ForwardPlusLightBounds{{0.0F, 0.0F, 10.0F}, 2.8F}};
        const auto grid = Keire::RenderBackend::BuildForwardPlusCpuTiles(128, 128, projection, 0.1F, lights);

        CHECK(TileContainsLight(grid, 1, 4, 0));
        CHECK(TileContainsLight(grid, 6, 4, 0));
    }

    SUBCASE("off-axis spheres account for the shifted tangent center")
    {
        const std::vector lights{Keire::RenderBackend::ForwardPlusLightBounds{{-2.85F, 0.0F, 4.0F}, 0.5F}};
        const auto grid = Keire::RenderBackend::BuildForwardPlusCpuTiles(128, 128, projection, 0.1F, lights);

        CHECK(TileContainsLight(grid, 0, 4, 0));
    }
}

TEST_CASE("Forward+ orthographic tiles project light ranges without perspective division")
{
    const auto projection = Keire::Math::Orthographic(10.0F, 1.0F, 0.1F, 100.0F);
    const std::vector lights{Keire::RenderBackend::ForwardPlusLightBounds{{0.5F, 0.0F, 10.0F}, 1.0F}};
    const auto grid = Keire::RenderBackend::BuildForwardPlusCpuTiles(128, 128, projection, 0.1F, lights);

    CHECK(TileContainsLight(grid, 5, 4, 0));
}

TEST_CASE("Forward+ perspective tiles clip near-plane crossings conservatively")
{
    const auto projection = Keire::Math::Perspective(60.0F, 1.0F, 0.1F, 100.0F);
    const std::vector lights{Keire::RenderBackend::ForwardPlusLightBounds{{0.0F, 0.0F, 0.05F}, 1.0F},
                             Keire::RenderBackend::ForwardPlusLightBounds{{0.0F, 0.0F, 0.05F}, 0.01F},
                             Keire::RenderBackend::ForwardPlusLightBounds{{100.0F, 0.0F, 0.5F}, 1.0F}};
    const auto grid = Keire::RenderBackend::BuildForwardPlusCpuTiles(128, 128, projection, 0.1F, lights);

    CHECK(std::ranges::all_of(grid.Counts, [](const auto count) { return count == 1; }));
    CHECK(std::ranges::all_of(grid.LightIndices, [](const auto light) { return light == 0; }));
    CHECK_THROWS_AS((void)Keire::RenderBackend::BuildForwardPlusCpuTiles(128, 128, projection, 0.0F, lights),
                    std::invalid_argument);
}
