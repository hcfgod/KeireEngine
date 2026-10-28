#include "KeireInternal/Environment/CloudRecord.h"
#include "KeireInternal/Environment/VolumetricCloudRecord.h"
#include "KeireInternal/Rendering/VolumetricCloudUniforms.h"

#include <doctest/doctest.h>

#include <cmath>
#include <initializer_list>
#include <limits>
#include <stdexcept>

TEST_CASE("Volumetric cloud GPU packing retains seed and camera relative periodic wind")
{
    Keire::VolumetricCloudSettings settings;
    settings.Enabled = true;
    settings.Seed = 0xffffffffU;
    settings.PatternSizeMeters = 100.0;
    settings.WindMetersPerSecond = {10.0, 0.0, 0.0};
    const auto initial = Keire::RenderBackend::MakeVolumetricCloudUniforms(settings, {100.0, 500.0, 0.0}, {});
    CHECK(initial.Geometry[0] == 1000.0F);
    CHECK(initial.PatternOrigin[0] == 1.0F);
    CHECK(initial.Quality[0] == 1U);
    CHECK(initial.Quality[1] == 0xffffffffU);
    CHECK(initial.PatternOrigin[3] == doctest::Approx(settings.MultipleScatteringOcclusion));
    CHECK(initial.Albedo[3] == doctest::Approx(settings.MultipleScatteringContribution));
    const auto moved = Keire::RenderBackend::MakeVolumetricCloudUniforms(settings, {100.0, 500.0, 0.0},
                                                                         {.DayFraction = 0.5, .SecondsPerDay = 20.0});
    CHECK(moved.PatternOrigin[0] == doctest::Approx(0.0F));
    const auto hugeDay = Keire::RenderBackend::MakeVolumetricCloudUniforms(
        settings, {100.0, 500.0, 0.0}, {.Day = std::numeric_limits<std::int64_t>::max()});
    for (const float phase : hugeDay.PatternOrigin)
    {
        CHECK(std::isfinite(phase));
        CHECK(phase >= 0.0F);
        CHECK(phase < 256.0F);
    }
    CHECK_THROWS_AS((void)Keire::RenderBackend::MakeVolumetricCloudUniforms(settings, {1.0e10, 0.0, 0.0}, {}),
                    std::invalid_argument);
}

TEST_CASE("Cloud envelope preserves authored volumes without changing sheet-only records")
{
    Keire::CloudSettings settings;
    CHECK(Keire::Detail::EncodeCloudSettings(settings).at("version") == 2);
    CHECK_FALSE(Keire::Detail::DecodeCloudSettings(Keire::Detail::EncodeCloudSettings(settings)).Volume.Enabled);
    settings.Volume.Enabled = true;
    settings.Volume.ThicknessMeters = 750.0;
    auto record = Keire::Detail::EncodeCloudSettings(settings);
    CHECK(record.at("version") == 3);
    CHECK(Keire::Detail::DecodeCloudSettings(record) == settings);
    settings.Volume.Enabled = false;
    CHECK(Keire::Detail::DecodeCloudSettings(Keire::Detail::EncodeCloudSettings(settings)) == settings);
    record.erase("volume");
    CHECK_THROWS_AS((void)Keire::Detail::DecodeCloudSettings(record), std::invalid_argument);
}

TEST_CASE("Volumetric cloud authoring preserves disabled defaults and customized values")
{
    Keire::VolumetricCloudSettings settings;
    CHECK_FALSE(settings.Enabled);
    CHECK(Keire::Detail::DecodeVolumetricCloudSettings(Keire::Detail::EncodeVolumetricCloudSettings(settings)) ==
          settings);
    settings.Enabled = true;
    settings.Seed = 0xffffffffU;
    settings.BaseHeightMeters = -1000.0;
    settings.WindMetersPerSecond = {-200.0, 3.0, 17.0};
    settings.ScatteringAlbedo = {0.3, 0.5, 1.0};
    settings.ViewSteps = 128;
    settings.ShadowSteps = 16;
    settings.AmbientOcclusionStrength = 0.6;
    settings.MultipleScatteringContribution = 0.7;
    settings.MultipleScatteringOcclusion = 0.2;
    settings.DetailFrequency = 9;
    settings.VariationStrength = 0.4;
    settings.VariationPeriod = 16;
    settings.DensityContrast = 2.5;
    settings.RegionalCoverageStrength = 0.8;
    settings.RegionalCoveragePeriod = 4;
    CHECK(Keire::Detail::DecodeVolumetricCloudSettings(Keire::Detail::EncodeVolumetricCloudSettings(settings)) ==
          settings);
}

TEST_CASE("Volumetric cloud records reject malformed fields rather than silently defaulting")
{
    const auto valid = Keire::Detail::EncodeVolumetricCloudSettings({});
    for (auto field = valid.begin(); field != valid.end(); ++field)
    {
        auto missing = valid;
        missing.erase(field.key());
        CHECK_THROWS_AS((void)Keire::Detail::DecodeVolumetricCloudSettings(missing), std::invalid_argument);
        auto wrongType = valid;
        wrongType[field.key()] = "invalid";
        CHECK_THROWS_AS((void)Keire::Detail::DecodeVolumetricCloudSettings(wrongType), std::invalid_argument);
    }
    for (const auto* field :
         {"seed", "viewSteps", "shadowSteps", "detailFrequency", "variationPeriod", "regionalCoveragePeriod"})
    {
        for (const auto value : {-1.0, 0.5, 4294967296.0})
        {
            auto malformed = valid;
            malformed[field] = value;
            CHECK_THROWS_AS((void)Keire::Detail::DecodeVolumetricCloudSettings(malformed), std::invalid_argument);
        }
    }
    auto future = valid;
    future["version"] = 10;
    CHECK_THROWS_AS((void)Keire::Detail::DecodeVolumetricCloudSettings(future), std::invalid_argument);
}

TEST_CASE("Cloud ambient occlusion preserves legacy illumination and validates authored strength")
{
    auto legacy = Keire::Detail::EncodeVolumetricCloudSettings({});
    legacy["version"] = 5;
    legacy.erase("baseHeightVariationMeters");
    legacy.erase("independentWeather");
    legacy.erase("cloudType");
    legacy.erase("typeVariation");
    legacy.erase("heightProfile");
    legacy.erase("ambientOcclusionStrength");
    CHECK(Keire::Detail::DecodeVolumetricCloudSettings(legacy).AmbientOcclusionStrength == 0.0);
    for (const double strength : {0.0, 0.5, 1.0})
    {
        Keire::VolumetricCloudSettings settings;
        settings.AmbientOcclusionStrength = strength;
        CHECK(Keire::Detail::DecodeVolumetricCloudSettings(Keire::Detail::EncodeVolumetricCloudSettings(settings)) ==
              settings);
    }
    for (const double strength :
         {-0.01, 1.01, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()})
    {
        Keire::VolumetricCloudSettings settings;
        settings.AmbientOcclusionStrength = strength;
        CHECK_THROWS_AS(Keire::Detail::ValidateVolumetricCloudSettings(settings), std::invalid_argument);
    }
}

TEST_CASE("Cloud erosion migrates coupled detail and round trips independently")
{
    for (const double detail : {0.0, 0.2, 0.7, 1.0})
    {
        Keire::VolumetricCloudSettings settings;
        settings.DetailAmount = detail;
        auto legacy = Keire::Detail::EncodeVolumetricCloudSettings(settings);
        legacy["version"] = 4;
        legacy.erase("baseHeightVariationMeters");
        legacy.erase("independentWeather");
        legacy.erase("cloudType");
        legacy.erase("typeVariation");
        legacy.erase("heightProfile");
        legacy.erase("ambientOcclusionStrength");
        legacy.erase("erosionAmount");
        auto migrated = Keire::Detail::DecodeVolumetricCloudSettings(legacy);
        CHECK(migrated.DetailAmount == detail);
        CHECK(migrated.ErosionAmount == detail);
        migrated.ErosionAmount = 1.0 - detail;
        CHECK(Keire::Detail::DecodeVolumetricCloudSettings(Keire::Detail::EncodeVolumetricCloudSettings(migrated)) ==
              migrated);
    }
    for (const double erosion :
         {-0.01, 1.01, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()})
    {
        Keire::VolumetricCloudSettings settings;
        settings.ErosionAmount = erosion;
        CHECK_THROWS_AS(Keire::Detail::ValidateVolumetricCloudSettings(settings), std::invalid_argument);
    }
}

TEST_CASE("Old authored cloud volumes retain single scattering across migration and resave")
{
    auto record = Keire::Detail::EncodeVolumetricCloudSettings({});
    record["version"] = 1;
    record.erase("baseHeightVariationMeters");
    record.erase("independentWeather");
    record.erase("cloudType");
    record.erase("typeVariation");
    record.erase("heightProfile");
    record.erase("ambientOcclusionStrength");
    record.erase("erosionAmount");
    record.erase("multipleScatteringContribution");
    record.erase("multipleScatteringOcclusion");
    record.erase("detailFrequency");
    record.erase("variationStrength");
    record.erase("variationPeriod");
    record.erase("densityContrast");
    record.erase("regionalCoverageStrength");
    record.erase("regionalCoveragePeriod");
    record["enabled"] = true;
    const auto migrated = Keire::Detail::DecodeVolumetricCloudSettings(record);
    CHECK(migrated.Enabled);
    CHECK(migrated.MultipleScatteringContribution == 0.0);
    CHECK(Keire::Detail::DecodeVolumetricCloudSettings(Keire::Detail::EncodeVolumetricCloudSettings(migrated)) ==
          migrated);
    record["multipleScatteringContribution"] = 0.5;
    CHECK_THROWS_AS((void)Keire::Detail::DecodeVolumetricCloudSettings(record), std::invalid_argument);
}

TEST_CASE("Cloud shape controls migrate and reject invalid periodic domains")
{
    auto legacy = Keire::Detail::EncodeVolumetricCloudSettings({});
    legacy["version"] = 2;
    legacy.erase("baseHeightVariationMeters");
    legacy.erase("independentWeather");
    legacy.erase("cloudType");
    legacy.erase("typeVariation");
    legacy.erase("heightProfile");
    legacy.erase("ambientOcclusionStrength");
    legacy.erase("erosionAmount");
    for (const auto* field : {"detailFrequency", "variationStrength", "variationPeriod", "densityContrast",
                              "regionalCoverageStrength", "regionalCoveragePeriod"})
    {
        legacy.erase(field);
    }
    CHECK(Keire::Detail::DecodeVolumetricCloudSettings(legacy) == Keire::VolumetricCloudSettings{});
    for (const unsigned period : {0U, 3U, 63U, 128U})
    {
        Keire::VolumetricCloudSettings settings;
        settings.VariationPeriod = period;
        CHECK_THROWS_AS(Keire::Detail::ValidateVolumetricCloudSettings(settings), std::invalid_argument);
    }
    for (const auto* field : {"variationStrength", "densityContrast"})
    {
        auto invalid = Keire::Detail::EncodeVolumetricCloudSettings({});
        invalid[field] = std::numeric_limits<double>::infinity();
        CHECK_THROWS_AS((void)Keire::Detail::DecodeVolumetricCloudSettings(invalid), std::invalid_argument);
    }
}

TEST_CASE("Regional coverage migrates old volumes and validates its independent periodic domain")
{
    auto legacy = Keire::Detail::EncodeVolumetricCloudSettings({});
    legacy["version"] = 3;
    legacy.erase("baseHeightVariationMeters");
    legacy.erase("independentWeather");
    legacy.erase("cloudType");
    legacy.erase("typeVariation");
    legacy.erase("heightProfile");
    legacy.erase("ambientOcclusionStrength");
    legacy.erase("erosionAmount");
    legacy.erase("regionalCoverageStrength");
    legacy.erase("regionalCoveragePeriod");
    CHECK(Keire::Detail::DecodeVolumetricCloudSettings(legacy) == Keire::VolumetricCloudSettings{});
    for (const unsigned period : {1U, 2U, 4U, 8U, 16U, 32U, 64U})
    {
        for (const double strength : {0.0, 0.5, 1.0})
        {
            Keire::VolumetricCloudSettings settings;
            settings.RegionalCoveragePeriod = period;
            settings.RegionalCoverageStrength = strength;
            CHECK(Keire::Detail::DecodeVolumetricCloudSettings(
                      Keire::Detail::EncodeVolumetricCloudSettings(settings)) == settings);
        }
    }
    for (const unsigned period : {0U, 3U, 65U, 128U})
    {
        Keire::VolumetricCloudSettings settings;
        settings.RegionalCoveragePeriod = period;
        CHECK_THROWS_AS(Keire::Detail::ValidateVolumetricCloudSettings(settings), std::invalid_argument);
    }
    for (const double strength :
         {-0.001, 1.001, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()})
    {
        Keire::VolumetricCloudSettings settings;
        settings.RegionalCoverageStrength = strength;
        CHECK_THROWS_AS(Keire::Detail::ValidateVolumetricCloudSettings(settings), std::invalid_argument);
    }
}

TEST_CASE("Cloud shape controls accept supported boundaries and reject values immediately outside them")
{
    for (const unsigned period : {1U, 2U, 4U, 8U, 16U, 32U, 64U})
    {
        for (const unsigned frequency : {1U, 16U})
        {
            for (const double strength : {0.0, 1.0})
            {
                for (const double contrast : {0.25, 4.0})
                {
                    Keire::VolumetricCloudSettings settings;
                    settings.VariationPeriod = period;
                    settings.DetailFrequency = frequency;
                    settings.VariationStrength = strength;
                    settings.DensityContrast = contrast;
                    CHECK(Keire::Detail::DecodeVolumetricCloudSettings(
                              Keire::Detail::EncodeVolumetricCloudSettings(settings)) == settings);
                }
            }
        }
    }
    for (const unsigned frequency : {0U, 17U})
    {
        Keire::VolumetricCloudSettings settings;
        settings.DetailFrequency = frequency;
        CHECK_THROWS_AS(Keire::Detail::ValidateVolumetricCloudSettings(settings), std::invalid_argument);
    }
    for (const double strength : {-0.001, 1.001})
    {
        Keire::VolumetricCloudSettings settings;
        settings.VariationStrength = strength;
        CHECK_THROWS_AS(Keire::Detail::ValidateVolumetricCloudSettings(settings), std::invalid_argument);
    }
    for (const double contrast : {0.249, 4.001})
    {
        Keire::VolumetricCloudSettings settings;
        settings.DensityContrast = contrast;
        CHECK_THROWS_AS(Keire::Detail::ValidateVolumetricCloudSettings(settings), std::invalid_argument);
    }
}

TEST_CASE("Cloud scattering controls reject nonfinite and out of range values while disabled")
{
    for (const double invalid :
         {-0.01, 1.01, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()})
    {
        Keire::VolumetricCloudSettings settings;
        settings.MultipleScatteringContribution = invalid;
        CHECK_THROWS_AS(Keire::Detail::ValidateVolumetricCloudSettings(settings), std::invalid_argument);
        settings = {};
        settings.MultipleScatteringOcclusion = invalid;
        CHECK_THROWS_AS(Keire::Detail::ValidateVolumetricCloudSettings(settings), std::invalid_argument);
    }
}

TEST_CASE("Volumetric cloud validation rejects invalid disabled settings too")
{
    Keire::VolumetricCloudSettings settings;
    settings.ThicknessMeters = 0.0;
    CHECK_THROWS_AS(Keire::Detail::ValidateVolumetricCloudSettings(settings), std::invalid_argument);
    settings = {};
    settings.Anisotropy = 1.0;
    CHECK_THROWS_AS(Keire::Detail::ValidateVolumetricCloudSettings(settings), std::invalid_argument);
    settings = {};
    settings.ViewSteps = 129;
    CHECK_THROWS_AS(Keire::Detail::ValidateVolumetricCloudSettings(settings), std::invalid_argument);
    settings = {};
    settings.WindMetersPerSecond[1] = std::numeric_limits<double>::infinity();
    CHECK_THROWS_AS(Keire::Detail::ValidateVolumetricCloudSettings(settings), std::invalid_argument);
    settings = {};
    settings.ScatteringAlbedo[2] = std::numeric_limits<double>::quiet_NaN();
    CHECK_THROWS_AS(Keire::Detail::ValidateVolumetricCloudSettings(settings), std::invalid_argument);
}

TEST_CASE("Authored cloud height profiles round trip and migrate the previous schema")
{
    Keire::VolumetricCloudSettings settings;
    settings.HeightProfile = {0.1, 0.2, 0.35, 0.45};
    CHECK(Keire::Detail::DecodeVolumetricCloudSettings(Keire::Detail::EncodeVolumetricCloudSettings(settings)) ==
          settings);
    auto legacy = Keire::Detail::EncodeVolumetricCloudSettings(settings);
    legacy["version"] = 6;
    legacy.erase("baseHeightVariationMeters");
    legacy.erase("independentWeather");
    legacy.erase("cloudType");
    legacy.erase("typeVariation");
    legacy.erase("heightProfile");
    CHECK(Keire::Detail::DecodeVolumetricCloudSettings(legacy).HeightProfile ==
          Keire::VolumetricCloudSettings{}.HeightProfile);
    for (const auto profile :
         {std::array{0.0, 0.4, 0.2, 1.0}, std::array{0.5, 0.5, 0.5, 0.5}, std::array{-0.1, 0.2, 0.7, 1.0},
          std::array{0.0, 0.2, 0.7, 1.1}, std::array{0.0, std::numeric_limits<double>::quiet_NaN(), 0.7, 1.0}})
    {
        settings.HeightProfile = profile;
        CHECK_THROWS_AS(Keire::Detail::ValidateVolumetricCloudSettings(settings), std::invalid_argument);
    }
    settings.HeightProfile = {0.2, 0.2, 0.8, 0.8};
    CHECK_NOTHROW(Keire::Detail::ValidateVolumetricCloudSettings(settings));
}

TEST_CASE("Cloud height profiles reject layers that collapse at GPU precision")
{
    for (const auto profile :
         {std::array{0.5, 0.5, 0.5000000001, 0.5000000001}, std::array{0.0, 0.0, 1.0e-100, 1.0e-100}})
    {
        Keire::VolumetricCloudSettings settings;
        settings.HeightProfile = profile;
        CHECK_THROWS_AS(Keire::Detail::ValidateVolumetricCloudSettings(settings), std::invalid_argument);
    }
    Keire::VolumetricCloudSettings settings;
    settings.HeightProfile = {0.5, 0.5, 0.5001, 0.5001};
    CHECK_NOTHROW(Keire::Detail::ValidateVolumetricCloudSettings(settings));
}

TEST_CASE("Independent cloud weather round trips and leaves old authored models unchanged")
{
    Keire::VolumetricCloudSettings settings;
    settings.IndependentWeather = true;
    settings.CloudType = 0.35;
    settings.TypeVariation = 0.6;
    CHECK(Keire::Detail::DecodeVolumetricCloudSettings(Keire::Detail::EncodeVolumetricCloudSettings(settings)) ==
          settings);
    auto legacy = Keire::Detail::EncodeVolumetricCloudSettings(settings);
    legacy["version"] = 7;
    legacy.erase("baseHeightVariationMeters");
    legacy.erase("independentWeather");
    legacy.erase("cloudType");
    legacy.erase("typeVariation");
    const auto restored = Keire::Detail::DecodeVolumetricCloudSettings(legacy);
    CHECK_FALSE(restored.IndependentWeather);
    CHECK(restored.HeightProfile == settings.HeightProfile);
    for (const double invalid :
         {-0.01, 1.01, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()})
    {
        settings = {};
        settings.CloudType = invalid;
        CHECK_THROWS_AS(Keire::Detail::ValidateVolumetricCloudSettings(settings), std::invalid_argument);
        settings = {};
        settings.TypeVariation = invalid;
        CHECK_THROWS_AS(Keire::Detail::ValidateVolumetricCloudSettings(settings), std::invalid_argument);
    }
}

TEST_CASE("Regional cloud base height preserves legacy records and rejects invalid ranges")
{
    Keire::VolumetricCloudSettings settings;
    settings.IndependentWeather = true;
    settings.BaseHeightVariationMeters = 750.0;
    const auto record = Keire::Detail::EncodeVolumetricCloudSettings(settings);
    CHECK(Keire::Detail::DecodeVolumetricCloudSettings(record) == settings);
    auto legacy = record;
    legacy["version"] = 8;
    legacy.erase("baseHeightVariationMeters");
    const auto migrated = Keire::Detail::DecodeVolumetricCloudSettings(legacy);
    CHECK(migrated.IndependentWeather);
    CHECK(migrated.BaseHeightVariationMeters == 0.0);
    for (const double invalid : {-1.0, 100001.0, std::numeric_limits<double>::infinity()})
    {
        settings.BaseHeightVariationMeters = invalid;
        CHECK_THROWS_AS(Keire::Detail::EncodeVolumetricCloudSettings(settings), std::invalid_argument);
    }
}
