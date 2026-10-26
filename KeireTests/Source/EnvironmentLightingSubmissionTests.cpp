#include "KeireInternal/Environment/EnvironmentLightingEvaluation.h"
#include "KeireInternal/Rendering/EnvironmentLightingValidation.h"

#include <doctest/doctest.h>

#include <limits>

TEST_CASE("Environment clock snapshots validate independently of directional ownership")
{
    const Keire::Detail::EnvironmentClockState clock{1234567890123456789LL, 0.375, 2400.0, -0.5, true};
    auto snapshot = Keire::Detail::EvaluateEnvironmentLighting(Keire::Detail::EnvironmentLightingProfile{}, clock,
                                                               Keire::AssetId(1, 2), 1);
    CHECK_FALSE(snapshot.Directional);
    REQUIRE(snapshot.Time);
    CHECK(snapshot.Time->Day == clock.Day);
    CHECK(snapshot.Time->DayFraction == clock.DayFraction);
    CHECK(snapshot.Time->SecondsPerDay == clock.SecondsPerDay);
    for (const double fraction : {-0.1, 1.0, std::numeric_limits<double>::quiet_NaN()})
    {
        snapshot.Time->DayFraction = fraction;
        CHECK_THROWS_AS(Keire::Detail::ValidateEnvironmentLighting(snapshot), std::invalid_argument);
    }
    snapshot.Time->DayFraction = 0.5;
    for (const double duration : {0.0, -1.0, std::numeric_limits<double>::infinity()})
    {
        snapshot.Time->SecondsPerDay = duration;
        CHECK_THROWS_AS(Keire::Detail::ValidateEnvironmentLighting(snapshot), std::invalid_argument);
    }
}

TEST_CASE("Ambient snapshot validation applies even without a directional override")
{
    Keire::EnvironmentLightingSnapshot snapshot{Keire::AssetId(1, 2), 1, std::nullopt,
                                                Keire::EnvironmentAmbientLight{}};
    CHECK_NOTHROW(Keire::Detail::ValidateEnvironmentLighting(snapshot));
    for (const float value :
         {-1.0F, 17.0F, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()})
    {
        snapshot.Ambient->Intensity = value;
        CHECK_THROWS_AS(Keire::Detail::ValidateEnvironmentLighting(snapshot), std::invalid_argument);
    }
    snapshot.Ambient->Intensity = 1.0F;
    snapshot.Ambient->LinearColor[1] = 1.1F;
    CHECK_THROWS_AS(Keire::Detail::ValidateEnvironmentLighting(snapshot), std::invalid_argument);
}

TEST_CASE("Environment shadow submission settings reject invalid values even when disabled")
{
    using namespace Keire;
    EnvironmentLightingSnapshot snapshot{AssetId(1, 2), 1, EnvironmentDirectionalLight{}};
    CHECK(snapshot.Directional->Shadows.Quality == ShadowQuality::Disabled);
    for (const auto quality : {ShadowQuality::Disabled, ShadowQuality::Hard, ShadowQuality::Soft})
    {
        for (const auto resolution : {ShadowResolutionHint::Low, ShadowResolutionHint::Medium,
                                      ShadowResolutionHint::High, ShadowResolutionHint::VeryHigh})
        {
            snapshot.Directional->Shadows = {quality, resolution, 0.0F, 1.0F};
            CHECK_NOTHROW(Detail::ValidateEnvironmentLighting(snapshot));
        }
    }
    snapshot.Directional->Shadows = {};
    snapshot.Directional->Shadows.Quality = static_cast<ShadowQuality>(255);
    CHECK_THROWS_AS(Detail::ValidateEnvironmentLighting(snapshot), std::invalid_argument);
    snapshot.Directional->Shadows = {};
    snapshot.Directional->Shadows.Resolution = static_cast<ShadowResolutionHint>(255);
    CHECK_THROWS_AS(Detail::ValidateEnvironmentLighting(snapshot), std::invalid_argument);
    for (const float value :
         {-0.1F, 1.1F, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()})
    {
        snapshot.Directional->Shadows = {};
        snapshot.Directional->Shadows.Strength = value;
        CHECK_THROWS_AS(Detail::ValidateEnvironmentLighting(snapshot), std::invalid_argument);
        snapshot.Directional->Shadows = {};
        snapshot.Directional->Shadows.Bias = value;
        CHECK_THROWS_AS(Detail::ValidateEnvironmentLighting(snapshot), std::invalid_argument);
    }
}

TEST_CASE("Daily profile evaluation produces validated renderer values without losing lifetime identity")
{
    using namespace Keire;
    using namespace Keire::Detail;
    const AssetId source(123, 456);
    const EnvironmentLightingProfile profile(
        EnvironmentDirectionalOwnership::Environment,
        {{0.0, 350.0, -60.0, {0.1, 0.2, 0.3}, 0.0}, {0.5, 10.0, 70.0, {1.0, 0.7, 0.4}, 12.0}},
        {ShadowQuality::Hard, ShadowResolutionHint::Low, 0.4F, 0.01F});
    for (int phase = 0; phase < 100; ++phase)
    {
        const EnvironmentClockState clock{std::numeric_limits<std::int64_t>::max(), phase / 100.0};
        const auto evaluated = profile.Evaluate(clock);
        const auto snapshot = EvaluateEnvironmentLighting(profile, clock, source, 9);
        REQUIRE(snapshot.Directional);
        CHECK(snapshot.Source == source);
        CHECK(snapshot.Revision == 9);
        CHECK(snapshot.Directional->Shadows == profile.Shadows());
        CHECK(snapshot.Directional->Intensity == static_cast<float>(evaluated->Intensity));
        CHECK_NOTHROW(ValidateEnvironmentLighting(snapshot));
    }
    const EnvironmentLightingProfile authored;
    CHECK_FALSE(EvaluateEnvironmentLighting(authored, {}, source, 1).Directional);
    CHECK_THROWS_AS((void)EvaluateEnvironmentLighting(profile, {}, {}, 1), std::invalid_argument);
    CHECK_THROWS_AS((void)EvaluateEnvironmentLighting(profile, {}, source, 0), std::invalid_argument);
    CHECK_THROWS_AS((void)EvaluateEnvironmentLighting(profile, {0, 1.0}, source, 1), std::invalid_argument);
}

TEST_CASE("Environment lighting submissions validate identity and renderer representable values")
{
    using namespace Keire;
    CHECK_NOTHROW(Detail::ValidateEnvironmentLighting(std::nullopt));
    EnvironmentLightingSnapshot snapshot{AssetId(1, 2), 1, std::nullopt};
    CHECK_NOTHROW(Detail::ValidateEnvironmentLighting(snapshot));
    snapshot.Directional = EnvironmentDirectionalLight{};
    CHECK_NOTHROW(Detail::ValidateEnvironmentLighting(snapshot));
    const auto valid = snapshot;
    snapshot.Source = {};
    CHECK_THROWS_AS(Detail::ValidateEnvironmentLighting(snapshot), std::invalid_argument);
    snapshot = valid;
    snapshot.Revision = 0;
    CHECK_THROWS_AS(Detail::ValidateEnvironmentLighting(snapshot), std::invalid_argument);
    for (const auto value :
         {0.0F, 2.0F, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()})
    {
        snapshot = valid;
        snapshot.Directional->Direction = {0.0F, value, 0.0F};
        CHECK_THROWS_AS(Detail::ValidateEnvironmentLighting(snapshot), std::invalid_argument);
    }
    for (const auto value : {-1.0F, 2.0F, std::numeric_limits<float>::quiet_NaN()})
    {
        snapshot = valid;
        snapshot.Directional->LinearColor[1] = value;
        CHECK_THROWS_AS(Detail::ValidateEnvironmentLighting(snapshot), std::invalid_argument);
    }
    for (const auto value : {-1.0F, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()})
    {
        snapshot = valid;
        snapshot.Directional->Intensity = value;
        CHECK_THROWS_AS(Detail::ValidateEnvironmentLighting(snapshot), std::invalid_argument);
    }
    snapshot = valid;
    snapshot.Directional->Intensity = 0.0F;
    CHECK_NOTHROW(Detail::ValidateEnvironmentLighting(snapshot));
    snapshot.Directional->Intensity = std::numeric_limits<float>::max();
    CHECK_NOTHROW(Detail::ValidateEnvironmentLighting(snapshot));
}
