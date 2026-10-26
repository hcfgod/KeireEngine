#include "KeireInternal/Environment/CloudRecord.h"
#include "KeireInternal/Environment/VolumetricCloudRecord.h"
#include "KeireInternal/Rendering/VolumetricCloudUniforms.h"

#include <doctest/doctest.h>

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
    for (const auto* field : {"seed", "viewSteps", "shadowSteps"})
    {
        for (const auto value : {-1.0, 0.5, 4294967296.0})
        {
            auto malformed = valid;
            malformed[field] = value;
            CHECK_THROWS_AS((void)Keire::Detail::DecodeVolumetricCloudSettings(malformed), std::invalid_argument);
        }
    }
    auto future = valid;
    future["version"] = 2;
    CHECK_THROWS_AS((void)Keire::Detail::DecodeVolumetricCloudSettings(future), std::invalid_argument);
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
