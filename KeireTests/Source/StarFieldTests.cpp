#include "KeireInternal/Environment/EnvironmentStarPhase.h"
#include "KeireInternal/Environment/StarFieldPresets.h"
#include "KeireInternal/Environment/StarFieldRecord.h"
#include "KeireInternal/Rendering/StarFieldUniforms.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>

TEST_CASE("Bright night star appearance preserves authored placement and visibility timing")
{
    Keire::StarFieldSettings authored;
    authored.Enabled = true;
    authored.Seed = 9876;
    authored.RotationDegrees = 125.0;
    authored.FadeStartElevation = -5.0;
    authored.FullVisibilityElevation = -20.0;
    authored.TwinkleFrequency = 2.0;
    authored.TwinkleAmplitude = 0.8;
    authored.Density = 0.9;
    authored.AngularRadius = 0.002;
    authored.ColorVariation = 0.1;
    authored.Tint = {0.2, 0.4, 0.8};
    const auto appearance = Keire::Detail::MakeBrightNightStarAppearance(authored);
    CHECK_NOTHROW(Keire::Detail::ValidateStarField(appearance));
    CHECK(appearance.Enabled == authored.Enabled);
    CHECK(appearance.Seed == authored.Seed);
    CHECK(appearance.RotationDegrees == authored.RotationDegrees);
    CHECK(appearance.FadeStartElevation == authored.FadeStartElevation);
    CHECK(appearance.FullVisibilityElevation == authored.FullVisibilityElevation);
    CHECK(appearance.TwinkleFrequency == authored.TwinkleFrequency);
    CHECK(appearance.TwinkleAmplitude == 0.0);
    CHECK(appearance.Intensity == 21.0);
    CHECK(appearance.Density == 0.35);
    CHECK(appearance.AngularRadius == 0.00015);
    CHECK(appearance.ColorVariation == 0.5);
    CHECK(appearance.Tint == Keire::StarFieldSettings{}.Tint);
    CHECK(Keire::Detail::MakeBrightNightStarAppearance(appearance) == appearance);
    CHECK_FALSE(Keire::Detail::MakeBrightNightStarAppearance({}).Enabled);
    CHECK(authored.TwinkleAmplitude == 0.8);
}

TEST_CASE("Star field settings default off and round trip without changing the seed")
{
    Keire::StarFieldSettings settings;
    CHECK_FALSE(settings.Enabled);
    CHECK(settings.TwinkleAmplitude == 0.0);
    settings.Enabled = true;
    settings.Seed = std::numeric_limits<std::uint32_t>::max();
    settings.Tint = {0.2, 0.5, 0.9};
    settings.Intensity = 8.5;
    settings.TwinkleAmplitude = 0.4;
    CHECK(Keire::Detail::DecodeStarField(nlohmann::json::parse(Keire::Detail::EncodeStarField(settings).dump())) ==
          settings);
}

TEST_CASE("Star shader packing preserves seed bits and angle phase conventions")
{
    Keire::StarFieldSettings settings;
    settings.Enabled = true;
    settings.Seed = std::numeric_limits<std::uint32_t>::max();
    settings.RotationDegrees = 180.0;
    settings.Tint = {0.2, 0.4, 0.6};
    settings.TwinkleAmplitude = 0.75;
    const auto packed = Keire::RenderBackend::MakeStarFieldUniforms(settings, 0.375);
    CHECK(packed.Identity[0] == settings.Seed);
    CHECK(packed.Identity[1] == 1U);
    CHECK(packed.TintRotation[0] == doctest::Approx(0.2));
    CHECK(packed.TintRotation[3] == doctest::Approx(std::numbers::pi));
    CHECK(packed.TwinkleFade[0] == 0.75F);
    CHECK(packed.TwinkleFade[1] == 0.375F);
    CHECK(packed.TwinkleFade[2] == settings.FadeStartElevation);
    CHECK(packed.TwinkleFade[3] == settings.FullVisibilityElevation);
    CHECK(Keire::RenderBackend::MakeStarFieldUniforms({}, 0.0).Identity[1] == 0U);
}

TEST_CASE("Star twinkle follows explicit time including extreme days and fractional scrubbing")
{
    using Keire::Detail::EvaluateEnvironmentStarPhase;
    for (const auto day : {std::int64_t{0}, std::int64_t{17}, std::int64_t{-17},
                           std::numeric_limits<std::int64_t>::min(), std::numeric_limits<std::int64_t>::max()})
    {
        // Each day contains an integer number of cycles. Its huge integer part cannot erase local time.
        CHECK(EvaluateEnvironmentStarPhase(0.5, {day, 0.125, 10.0}) == doctest::Approx(0.625));
        CHECK(EvaluateEnvironmentStarPhase(0.5, {day, 0.15, 10.0}) == doctest::Approx(0.75));
        CHECK(EvaluateEnvironmentStarPhase(0.0, {day, 0.15, 10.0}) == 0.0);
    }
    CHECK(EvaluateEnvironmentStarPhase(0.125, {-1, 0.5, 10.0}) == doctest::Approx(0.375));
    CHECK(EvaluateEnvironmentStarPhase(0.125, {1, 0.5, 10.0}) == doctest::Approx(0.875));
    for (const double invalid :
         {-1.0, 10.01, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()})
    {
        CHECK_THROWS_AS((void)EvaluateEnvironmentStarPhase(invalid, {}), std::invalid_argument);
    }
    CHECK_THROWS_AS((void)EvaluateEnvironmentStarPhase(0.5, {0, 1.0, 10.0}), std::invalid_argument);
}

TEST_CASE("Star field validation rejects nonfinite values even while disabled")
{
    constexpr std::array members{&Keire::StarFieldSettings::Density,
                                 &Keire::StarFieldSettings::Intensity,
                                 &Keire::StarFieldSettings::ColorVariation,
                                 &Keire::StarFieldSettings::AngularRadius,
                                 &Keire::StarFieldSettings::RotationDegrees,
                                 &Keire::StarFieldSettings::TwinkleAmplitude,
                                 &Keire::StarFieldSettings::TwinkleFrequency,
                                 &Keire::StarFieldSettings::FadeStartElevation,
                                 &Keire::StarFieldSettings::FullVisibilityElevation};
    for (const double invalid : {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(),
                                 -std::numeric_limits<double>::infinity()})
    {
        for (const auto member : members)
        {
            Keire::StarFieldSettings settings;
            settings.*member = invalid;
            CHECK_THROWS_AS(static_cast<void>(Keire::Detail::EncodeStarField(settings)), std::invalid_argument);
        }
        for (std::size_t channel = 0; channel < 3; ++channel)
        {
            Keire::StarFieldSettings settings;
            settings.Tint[channel] = invalid;
            CHECK_THROWS_AS(static_cast<void>(Keire::Detail::EncodeStarField(settings)), std::invalid_argument);
        }
    }
}

TEST_CASE("Star record rejects missing unknown and incorrectly typed fields")
{
    const auto valid = Keire::Detail::EncodeStarField({});
    for (const auto& [name, value] : valid.items())
    {
        auto missing = valid;
        missing.erase(name);
        CHECK_THROWS_AS(static_cast<void>(Keire::Detail::DecodeStarField(missing)), std::invalid_argument);
        auto wrongType = valid;
        wrongType[name] = "invalid";
        CHECK_THROWS_AS(static_cast<void>(Keire::Detail::DecodeStarField(wrongType)), std::invalid_argument);
    }
    auto extra = valid;
    extra["unknown"] = 1;
    CHECK_THROWS_AS(static_cast<void>(Keire::Detail::DecodeStarField(extra)), std::invalid_argument);
    for (const auto invalid :
         {nlohmann::json(-1), nlohmann::json(4294967296ULL), nlohmann::json(1.5), nlohmann::json(true)})
    {
        auto record = valid;
        record["seed"] = invalid;
        CHECK_THROWS_AS(static_cast<void>(Keire::Detail::DecodeStarField(record)), std::invalid_argument);
    }
    for (const auto invalid : {nlohmann::json(0), nlohmann::json(2), nlohmann::json(1.0), nlohmann::json(true)})
    {
        auto record = valid;
        record["version"] = invalid;
        CHECK_THROWS_AS(static_cast<void>(Keire::Detail::DecodeStarField(record)), std::invalid_argument);
    }
}

TEST_CASE("Star night fade thresholds must be strictly ordered")
{
    Keire::StarFieldSettings settings;
    settings.FullVisibilityElevation = settings.FadeStartElevation;
    CHECK_THROWS_AS(Keire::Detail::ValidateStarField(settings), std::invalid_argument);
    settings.FullVisibilityElevation += 1.0;
    CHECK_THROWS_AS(Keire::Detail::ValidateStarField(settings), std::invalid_argument);
    settings.FullVisibilityElevation = -90.0;
    settings.FadeStartElevation = 90.0;
    CHECK_NOTHROW(Keire::Detail::ValidateStarField(settings));
}

TEST_CASE("Star shader fade stays finite when authored thresholds narrow to the same float")
{
    // Float reference for BuiltinSky.hlsl; backend shader execution is a separate acceptance gate.
    const auto visibility = [](const float start, const float full, const float elevation)
    {
        const float fraction = std::clamp((start - elevation) / std::max(start - full, 1.0e-4F), 0.0F, 1.0F);
        return fraction * fraction * (3.0F - 2.0F * fraction);
    };
    Keire::StarFieldSettings settings;
    settings.FullVisibilityElevation = std::nextafter(settings.FadeStartElevation, -90.0);
    CHECK_NOTHROW(Keire::Detail::ValidateStarField(settings));
    const auto packed = Keire::RenderBackend::MakeStarFieldUniforms(settings, 0.0);
    CHECK(packed.TwinkleFade[2] == packed.TwinkleFade[3]);
    float previous = 1.0F;
    for (int step = -900; step <= 900; ++step)
    {
        const float elevation = static_cast<float>(step) * 0.1F;
        const float actual = visibility(packed.TwinkleFade[2], packed.TwinkleFade[3], elevation);
        CHECK(std::isfinite(actual));
        CHECK(actual >= 0.0F);
        CHECK(actual <= previous);
        previous = actual;
    }
    CHECK(visibility(-2.0F, -12.0F, -12.0F) == 1.0F);
    CHECK(visibility(-2.0F, -12.0F, -7.0F) == 0.5F);
    CHECK(visibility(-2.0F, -12.0F, -2.0F) == 0.0F);
}

TEST_CASE("Star appearance numeric boundaries are enforced without silently clamping")
{
    struct Range
    {
        double Keire::StarFieldSettings::* Member;
        double Minimum;
        double Maximum;
    };
    const std::array ranges{Range{&Keire::StarFieldSettings::Density, 0.0, 1.0},
                            Range{&Keire::StarFieldSettings::Intensity, 0.0, 1000.0},
                            Range{&Keire::StarFieldSettings::ColorVariation, 0.0, 1.0},
                            Range{&Keire::StarFieldSettings::AngularRadius, 0.000001, 0.01},
                            Range{&Keire::StarFieldSettings::RotationDegrees, 0.0, 360.0},
                            Range{&Keire::StarFieldSettings::TwinkleAmplitude, 0.0, 1.0},
                            Range{&Keire::StarFieldSettings::TwinkleFrequency, 0.0, 10.0}};
    for (const auto& range : ranges)
    {
        for (const double valid : {range.Minimum, range.Maximum})
        {
            Keire::StarFieldSettings settings;
            settings.*range.Member = valid;
            CHECK(Keire::Detail::DecodeStarField(Keire::Detail::EncodeStarField(settings)) == settings);
        }
        for (const double invalid : {std::nextafter(range.Minimum, -std::numeric_limits<double>::infinity()),
                                     std::nextafter(range.Maximum, std::numeric_limits<double>::infinity())})
        {
            Keire::StarFieldSettings settings;
            settings.*range.Member = invalid;
            CHECK_THROWS_AS(Keire::Detail::ValidateStarField(settings), std::invalid_argument);
        }
    }
}
