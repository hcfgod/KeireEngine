#include "KeireInternal/Environment/CloudRecord.h"
#include "KeireInternal/Rendering/CloudUniforms.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

TEST_CASE("Natural cloud noise is seeded periodic finite and spatially varied")
{
    float minimum = 1.0F;
    float maximum = 0.0F;
    float seedDifference = 0.0F;
    for (int z = 0; z < 32; ++z)
    {
        for (int x = 0; x < 32; ++x)
        {
            const float u = static_cast<float>(x) / 32.0F;
            const float v = static_cast<float>(z) / 32.0F;
            const float sample = Keire::Detail::SampleNaturalCloudPattern(u, v, 1U);
            CHECK(std::isfinite(sample));
            CHECK(sample >= 0.0F);
            CHECK(sample <= 1.0F);
            CHECK(Keire::Detail::SampleNaturalCloudPattern(u + 1.0F, v - 1.0F, 1U) == doctest::Approx(sample));
            minimum = std::min(minimum, sample);
            maximum = std::max(maximum, sample);
            seedDifference += std::abs(sample - Keire::Detail::SampleNaturalCloudPattern(u, v, 2U));
        }
    }
    CHECK(maximum - minimum > 0.3F);
    CHECK(seedDifference > 10.0F);
    CHECK(Keire::Detail::SampleNaturalCloudPattern(-0.000001F, 0.37F, 0xffffffffU) ==
          doctest::Approx(Keire::Detail::SampleNaturalCloudPattern(0.000001F, 0.37F, 0xffffffffU)).epsilon(0.002));
    const float filtered = Keire::Detail::SampleNaturalCloudPattern(0.1F, 0.7F, 1U, 1.0F);
    CHECK(filtered == doctest::Approx(Keire::Detail::SampleNaturalCloudPattern(0.6F, 0.2F, 2U, 1.0F)));
    CHECK(Keire::Detail::SampleNaturalCloudPattern(std::numeric_limits<float>::infinity(), 0.0F, 1U) == 0.0F);
}

TEST_CASE("Natural clouds reflect selected lighting instead of emitting white at night")
{
    for (const float density : {0.0F, 0.5F, 1.0F})
    {
        const auto dark = Keire::Detail::EvaluateNaturalCloudLight(density, 0.5F, 1.0F, {}, {});
        CHECK(dark == std::array<float, 3>{});
        const auto night = Keire::Detail::EvaluateNaturalCloudLight(density, -0.5F, 0.0F, {0.005F, 0.01F, 0.02F}, {});
        CHECK(night[0] > 0.0F);
        CHECK(night[2] <= 0.02F);
        CHECK(night[2] > night[0]);
        const auto sunset = Keire::Detail::EvaluateNaturalCloudLight(density, 0.05F, 1.0F, {}, {3.0F, 0.4F, 0.1F});
        CHECK(std::isfinite(sunset[0]));
        CHECK(sunset[0] > sunset[1]);
        CHECK(sunset[1] > sunset[2]);
    }
    const auto edge = Keire::Detail::EvaluateNaturalCloudLight(0.1F, 0.5F, 1.0F, {}, {1.0F, 1.0F, 1.0F});
    const auto core = Keire::Detail::EvaluateNaturalCloudLight(0.9F, 0.5F, 1.0F, {}, {1.0F, 1.0F, 1.0F});
    CHECK(edge[0] > core[0]);
}

TEST_CASE("Cloud v2 preserves explicit appearance and full uint32 seeds while v1 remains legacy")
{
    Keire::CloudSettings settings;
    CHECK(settings.Layers[0].Appearance == Keire::CloudAppearance::NaturalLit);
    settings.Layers[0].Enabled = true;
    settings.Layers[0].Seed = 0xffffffffU;
    settings.Layers[1].Appearance = Keire::CloudAppearance::LegacyUnlit;
    auto record = Keire::Detail::EncodeCloudSettings(settings);
    CHECK(record.at("version") == 2);
    CHECK(Keire::Detail::DecodeCloudSettings(record) == settings);
    const auto uniforms = Keire::RenderBackend::BuildCloudUniforms(settings, {0.0, 0.0, 0.0}, 0.0);
    CHECK(uniforms.Layers[0].Noise[0] == 1U);
    CHECK(uniforms.Layers[0].Noise[1] == 0xffffffffU);
    CHECK(uniforms.Layers[1].Noise[0] == 0U);
    auto legacy = record;
    legacy["version"] = 1;
    for (auto& layer : legacy["layers"])
    {
        layer.erase("appearance");
        layer.erase("seed");
    }
    const auto migrated = Keire::Detail::DecodeCloudSettings(legacy);
    CHECK(migrated.Layers[0].Appearance == Keire::CloudAppearance::LegacyUnlit);
    CHECK(migrated.Layers[0].Coverage == settings.Layers[0].Coverage);
    CHECK(migrated.Layers[0].WindMetersPerSecond == settings.Layers[0].WindMetersPerSecond);
    CHECK(Keire::Detail::DecodeCloudSettings(Keire::Detail::EncodeCloudSettings(migrated)) == migrated);
    record["layers"][0]["seed"] = -1;
    CHECK_THROWS_AS((void)Keire::Detail::DecodeCloudSettings(record), std::invalid_argument);
    record["layers"][0]["seed"] = 0x100000000ULL;
    CHECK_THROWS_AS((void)Keire::Detail::DecodeCloudSettings(record), std::invalid_argument);
    record["layers"][0]["seed"] = 1;
    record["layers"][0]["appearance"] = "volumetric";
    CHECK_THROWS_AS((void)Keire::Detail::DecodeCloudSettings(record), std::invalid_argument);
}

TEST_CASE("Natural cloud premultiplication stays finite for accepted extreme lighting and clear pixels")
{
    const auto extreme = Keire::Detail::EvaluateNaturalCloudLight(
        0.5F, 1.0F, 1.0F, {},
        {std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max()});
    for (const float alpha : {0.0F, 0.5F, 1.0F})
    {
        const auto result = Keire::Detail::PremultiplyNaturalCloudLight({16.0F, 0.5F, 0.0F}, extreme, alpha);
        for (const auto channel : result)
        {
            CHECK(std::isfinite(channel));
            CHECK(channel >= 0.0F);
            CHECK(channel <= 65504.0F);
            if (alpha == 0.0F)
            {
                CHECK(channel == 0.0F);
            }
        }
        CHECK(result[0] == doctest::Approx(65504.0F * alpha));
        CHECK(result[1] == doctest::Approx(65504.0F * alpha));
        CHECK(result[2] == 0.0F);
    }
    const auto ordinary = Keire::Detail::PremultiplyNaturalCloudLight({0.5F, 2.0F, 1.0F}, {2.0F, 3.0F, 0.25F}, 0.5F);
    CHECK(ordinary[0] == 0.5F);
    CHECK(ordinary[1] == 3.0F);
    CHECK(ordinary[2] == 0.125F);
}

TEST_CASE("Cloud noise rejects unsupported periods before float to integer conversion")
{
    CHECK(Keire::Detail::CloudPeriodicNoise(-0.00000001F, 0.25F, 0xffffffffU, 1U) == 0.0F);
    CHECK(Keire::Detail::CloudPeriodicNoise(0.25F, 0.25F, 129U, 1U) == 0.0F);
    CHECK(Keire::Detail::CloudPeriodicNoise(0.25F, 0.25F, 0U, 1U) == 0.0F);
    for (const std::uint32_t period : {1U, 4U, 16U, 32U, 64U, 128U})
    {
        const auto value = Keire::Detail::CloudPeriodicNoise(-0.00000001F, 0.25F, period, 1U);
        CHECK(std::isfinite(value));
        CHECK(value >= 0.0F);
        CHECK(value <= 1.0F);
    }
}
