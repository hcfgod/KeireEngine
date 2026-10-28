#include "KeireInternal/Rendering/CloudLightCacheGrid.h"
#include "KeireInternal/Rendering/CloudLightCacheKey.h"

#include <doctest/doctest.h>

#include <cmath>
#include <limits>

TEST_CASE("Cloud cache vertical motion preserves the canonical base phase across the noise wrap")
{
    using namespace Keire::RenderBackend;
    Keire::VolumetricCloudSettings settings;
    settings.Enabled = true;
    settings.BaseHeightMeters = 1500.0;
    settings.ThicknessMeters = 1200.0;
    settings.PatternSizeMeters = 3000.0;
    settings.WindMetersPerSecond = {};
    Keire::Detail::EnvironmentClockState clock;
    const auto base = MakeVolumetricCloudUniforms(settings, {0.0, settings.BaseHeightMeters, 0.0}, clock);
    std::optional<CloudLightCacheKey> expected;
    for (int step = 0; step <= 2000; ++step)
    {
        CloudVolumeViewUniforms view{};
        view.Volume = MakeVolumetricCloudUniforms(settings, {0.0, -100.0 + step * 0.1, 0.0}, clock);
        auto grid = MakeCloudLightCacheGrid(view.Volume, 500.0F);
        REQUIRE(grid);
        AlignCloudLightCacheBuild(view.Volume, *grid, base.PatternOrigin[1]);
        const auto key = MakeCloudLightCacheKey(view, grid->Origin, grid->CellSize, grid->Dimensions);
        if (!expected)
        {
            expected = key;
        }
        CHECK(key == *expected);
    }
}

TEST_CASE("Canonical cloud base phase follows vertical wind and authored base height")
{
    using namespace Keire::RenderBackend;
    Keire::VolumetricCloudSettings settings;
    settings.Enabled = true;
    settings.BaseHeightMeters = 1500.0;
    settings.PatternSizeMeters = 3000.0;
    settings.WindMetersPerSecond = {0.0, 2.0, 0.0};
    Keire::Detail::EnvironmentClockState clock;
    const auto phase = [&]
    { return MakeVolumetricCloudUniforms(settings, {0.0, settings.BaseHeightMeters, 0.0}, clock).PatternOrigin[1]; };
    const auto initial = phase();
    clock.DayFraction += 0.01;
    CHECK(phase() != initial);
    const auto advected = phase();
    settings.BaseHeightMeters += 100.0;
    CHECK(phase() != advected);
}

TEST_CASE("Cloud lighting grid covers the slab and stays aligned during observer motion")
{
    using namespace Keire::RenderBackend;
    VolumetricCloudUniforms volume{};
    volume.Geometry = {1500.0F, 1200.0F, 3000.0F, 100000.0F};
    const auto initial = MakeCloudLightCacheGrid(volume);
    REQUIRE(initial);
    CHECK(initial->Origin[1] == 1500.0F);
    CHECK(initial->Origin[1] + initial->CellSize[1] * 32.0F == 2700.0F);

    volume.PatternOrigin[0] = 0.015625F;
    const auto moved = MakeCloudLightCacheGrid(volume);
    REQUIRE(moved);
    CHECK(moved->Origin[0] + 46.875F == initial->Origin[0]);
    CHECK(moved->Origin[2] == initial->Origin[2]);

    volume.PatternOrigin[0] += 256.0F;
    const auto wrapped = MakeCloudLightCacheGrid(volume);
    REQUIRE(wrapped);
    CHECK(wrapped->Origin == moved->Origin);
}

TEST_CASE("Cloud cache build alignment preserves cloud-space sample locations")
{
    using namespace Keire::RenderBackend;
    for (const float phase : {0.0F, 0.0078125F, 0.03125F, 3.125F, 255.984375F})
    {
        VolumetricCloudUniforms volume{};
        volume.Geometry = {1500.0F, 1200.0F, 3000.0F, 100000.0F};
        volume.PatternOrigin = {phase, 0.5F, phase, 0.25F};
        const auto grid = MakeCloudLightCacheGrid(volume, 500.0F);
        REQUIRE(grid);
        auto alignedVolume = volume;
        auto alignedGrid = *grid;
        AlignCloudLightCacheBuild(alignedVolume, alignedGrid);
        CHECK(alignedVolume.Geometry[0] == 0.0F);
        CHECK(alignedVolume.PatternOrigin[1] == 1.0F);
        CHECK(alignedGrid.Origin[1] == -500.0F);
        CHECK(alignedVolume.PatternOrigin[3] == volume.PatternOrigin[3]);
        for (const auto axis : {0U, 2U})
        {
            for (const auto index : {0U, 64U, 128U})
            {
                const double original = grid->Origin[axis] + index * grid->CellSize[axis] +
                                        static_cast<double>(volume.PatternOrigin[axis]) * volume.Geometry[2];
                const double aligned = alignedGrid.Origin[axis] + index * alignedGrid.CellSize[axis] +
                                       static_cast<double>(alignedVolume.PatternOrigin[axis]) * volume.Geometry[2];
                CHECK(aligned == doctest::Approx(original).epsilon(1e-7));
            }
        }
    }
}

TEST_CASE("Cloud cache alignment handles fractional pattern sizes and wrap-adjacent phases")
{
    using namespace Keire::RenderBackend;
    for (const float patternSize : {1.0F, 1.3F, 1234.567F, 3000.0F, 99999.9F, 100000.0F})
    {
        for (const float phase : {0.00001F, 0.012345F, 0.031249F, 0.031251F, 127.12345F, 255.99998F})
        {
            CAPTURE(patternSize);
            CAPTURE(phase);
            VolumetricCloudUniforms volume{};
            volume.Geometry = {1500.0F, 1200.0F, patternSize, 100000.0F};
            volume.PatternOrigin = {phase, 0.0F, phase, 0.0F};
            const auto grid = MakeCloudLightCacheGrid(volume);
            REQUIRE(grid);
            auto aligned = volume;
            auto buildGrid = *grid;
            AlignCloudLightCacheBuild(aligned, buildGrid);
            for (const auto index : {0U, 64U, 128U})
            {
                const double originalSample = static_cast<double>(grid->Origin[0]) +
                                              index * static_cast<double>(grid->CellSize[0]) +
                                              static_cast<double>(phase) * patternSize;
                const double alignedSample = static_cast<double>(buildGrid.Origin[0]) +
                                             index * static_cast<double>(buildGrid.CellSize[0]) +
                                             static_cast<double>(aligned.PatternOrigin[0]) * patternSize;
                // Bound coordinate-rounding error relative to a cache cell, including near-zero positions.
                CHECK(std::abs(originalSample - alignedSample) <= grid->CellSize[0] * 0.00001);
            }
        }
    }
}

TEST_CASE("Cloud lighting grid rejects invalid dimensions and phase")
{
    using namespace Keire::RenderBackend;
    VolumetricCloudUniforms volume{};
    CHECK_FALSE(MakeCloudLightCacheGrid(volume));
    volume.Geometry = {1500.0F, 1200.0F, 3000.0F, 100000.0F};
    volume.PatternOrigin[2] = std::numeric_limits<float>::quiet_NaN();
    CHECK_FALSE(MakeCloudLightCacheGrid(volume));
    volume.PatternOrigin[2] = 0.0F;
    volume.Geometry[1] = -1.0F;
    CHECK_FALSE(MakeCloudLightCacheGrid(volume));
}

TEST_CASE("Cloud lighting grid covers every regional base height")
{
    using namespace Keire::RenderBackend;
    VolumetricCloudUniforms volume{};
    volume.Geometry = {1500.0F, 1200.0F, 3000.0F, 100000.0F};
    const auto grid = MakeCloudLightCacheGrid(volume, 750.0F);
    REQUIRE(grid);
    CHECK(grid->Origin[1] == 750.0F);
    CHECK(grid->Origin[1] + grid->CellSize[1] * 32.0F == 3450.0F);
    CHECK_FALSE(MakeCloudLightCacheGrid(volume, -1.0F));
    CHECK_FALSE(MakeCloudLightCacheGrid(volume, 100001.0F));
    CHECK_FALSE(MakeCloudLightCacheGrid(volume, std::numeric_limits<float>::quiet_NaN()));
    volume.Geometry[1] = -1.0F;
    CHECK_FALSE(MakeCloudLightCacheGrid(volume, 750.0F));
}
