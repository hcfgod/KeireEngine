#include "KeireInternal/Environment/FogDensityRecord.h"
#include "KeireInternal/Environment/FogExclusion.h"
#include "KeireInternal/Rendering/FogViewUniforms.h"

#include <doctest/doctest.h>

#include <limits>

TEST_CASE("Fog exclusion GPU packing preserves relative precision and omits inactive regions")
{
    Keire::FogSettings settings;
    settings.Enabled = true;
    settings.ExclusionRegions.resize(4);
    settings.ExclusionRegions[0].Center = {1.0e12 + 0.125, 0, 0};
    settings.ExclusionRegions[1].Enabled = false;
    settings.ExclusionRegions[2].Strength = 0.0;
    const auto packed = Keire::RenderBackend::MakeFogViewUniforms(settings, {1.0e12, 0, 0});
    REQUIRE(packed.Controls[3] == 1);
    CHECK(packed.Exclusions[0].CenterExtinction[0] == 0.125F);
    CHECK(packed.Exclusions[0].CenterExtinction[3] == 1.0F);
    CHECK(packed.Exclusions[1].CenterExtinction[3] == 0.0F);
}

TEST_CASE("Authored fog exclusions persist and suppress both global and local density")
{
    Keire::FogSettings settings;
    settings.Enabled = true;
    settings.LocalVolumes.emplace_back();
    settings.ExclusionRegions.emplace_back();
    settings.ExclusionRegions.front().Strength = 0.5;
    CHECK(Keire::Detail::SampleFogExtinction(settings, {}) == doctest::Approx(0.011));
    auto record = Keire::Detail::EncodeFogDensity(settings);
    CHECK(Keire::Detail::DecodeFogDensity(record) == settings);
    record.erase("exclusionRegions");
    CHECK(Keire::Detail::DecodeFogDensity(record).ExclusionRegions.empty());
    settings.ExclusionRegions.front().Enabled = false;
    CHECK(Keire::Detail::SampleFogExtinction(settings, {}) == doctest::Approx(0.022));
    CHECK(Keire::Detail::DecodeFogDensity(Keire::Detail::EncodeFogDensity(settings)) == settings);
    record = Keire::Detail::EncodeFogDensity(settings);
    record["exclusionRegions"][0]["strength"] = true;
    CHECK_THROWS_AS((void)Keire::Detail::DecodeFogDensity(record), std::invalid_argument);
    settings.ExclusionRegions.resize(Keire::MaximumFogExclusionRegions + 1);
    CHECK_THROWS_AS(Keire::Detail::ValidateFogDensity(settings), std::invalid_argument);
}

TEST_CASE("Fog exclusion has a clear core and continuous soft boundary")
{
    std::array<Keire::FogExclusionRegion, 1> regions{};
    const auto retention = [&](double x) { return Keire::Detail::SampleFogExclusionRetention(regions, {x, 0, 0}); };
    CHECK(retention(0) == 0.0);
    CHECK(retention(8) == doctest::Approx(0.0));
    CHECK(retention(9) == doctest::Approx(0.5));
    CHECK(retention(-9) == doctest::Approx(0.5));
    CHECK(retention(10) == 1.0);
    CHECK(retention(11) == 1.0);
    regions.front().EdgeFraction = 0.0;
    CHECK(retention(9.99) == 0.0);
    CHECK(retention(10) == 1.0);
    regions.front().Enabled = false;
    CHECK(retention(0) == 1.0);
}

TEST_CASE("Fog exclusion overlap is order independent and duplicate invariant")
{
    std::array<Keire::FogExclusionRegion, 2> regions{};
    regions[0].Strength = 0.3;
    regions[1].Strength = 0.7;
    CHECK(Keire::Detail::SampleFogExclusionRetention(regions, {}) == doctest::Approx(0.3));
    std::swap(regions[0], regions[1]);
    CHECK(Keire::Detail::SampleFogExclusionRetention(regions, {}) == doctest::Approx(0.3));
    regions[1] = regions[0];
    CHECK(Keire::Detail::SampleFogExclusionRetention(regions, {9, 0, 0}) == doctest::Approx(0.65));
    regions[0].Center = {1.0e12, 0, 0};
    regions[1].Enabled = false;
    CHECK(Keire::Detail::SampleFogExclusionRetention(regions, {1.0e12 + 9.0, 0, 0}) == doctest::Approx(0.65));
    CHECK(Keire::Detail::SampleFogExclusionRetention({}, {}) == 1.0);
}

TEST_CASE("Fog exclusion validates disabled regions and rejects invalid inputs")
{
    Keire::FogExclusionRegion region;
    CHECK_NOTHROW(Keire::Detail::ValidateFogExclusion(region));
    region.Enabled = false;
    region.HalfExtent[0] = 0.0;
    CHECK_THROWS_AS(Keire::Detail::ValidateFogExclusion(region), std::invalid_argument);
    region = {};
    region.Strength = 1.01;
    CHECK_THROWS_AS(Keire::Detail::ValidateFogExclusion(region), std::invalid_argument);
    region = {};
    region.EdgeFraction = std::numeric_limits<double>::quiet_NaN();
    CHECK_THROWS_AS(Keire::Detail::ValidateFogExclusion(region), std::invalid_argument);
    CHECK_THROWS_AS(
        (void)Keire::Detail::SampleFogExclusionRetention({}, {std::numeric_limits<double>::infinity(), 0, 0}),
        std::invalid_argument);
}
