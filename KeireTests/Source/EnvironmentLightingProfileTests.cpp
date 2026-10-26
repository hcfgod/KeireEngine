#include "KeireInternal/Environment/EnvironmentLightingProfile.h"
#include "KeireInternal/Environment/EnvironmentLightingRecord.h"

#include <doctest/doctest.h>

#include <cmath>
#include <limits>
#include <utility>

namespace
{
    using namespace Keire::Detail;

    std::vector<EnvironmentLightingKeyframe> Cycle()
    {
        return {{0.25, 350.0, 0.0, {1.0, 0.0, 0.0}, 0.0}, {0.75, 10.0, 60.0, {0.0, 0.0, 1.0}, 10.0}};
    }
} // namespace

TEST_CASE("Optional ambient animation round trips and interpolates across midnight")
{
    auto keys = Cycle();
    keys[0].Ambient = Keire::EnvironmentAmbientLight{{0.0F, 0.2F, 0.4F}, 0.0F};
    keys[1].Ambient = Keire::EnvironmentAmbientLight{{1.0F, 0.6F, 0.8F}, 2.0F};
    const EnvironmentLightingProfile profile(EnvironmentDirectionalOwnership::Environment, keys);
    for (const double phase : {0.0, 0.5})
    {
        const auto sample = profile.Evaluate({.DayFraction = phase});
        REQUIRE(sample->Ambient);
        CHECK(sample->Ambient->Intensity == doctest::Approx(1.0F));
        CHECK(sample->Ambient->LinearColor[0] == doctest::Approx(0.5F));
    }
    const auto record = EnvironmentLightingRecord::Encode(profile);
    CHECK(record.at("version") == 3);
    CHECK(EnvironmentLightingRecord::Decode(record) == profile);
    keys[0].Ambient.reset();
    CHECK_THROWS_AS(EnvironmentLightingProfile(EnvironmentDirectionalOwnership::Environment, keys),
                    std::invalid_argument);
    keys[0].Ambient = Keire::EnvironmentAmbientLight{{0.1F, 0.2F, 0.3F}, 17.0F};
    CHECK_THROWS_AS(EnvironmentLightingProfile(EnvironmentDirectionalOwnership::Environment, keys),
                    std::invalid_argument);
    auto invalid = record;
    invalid["keyframes"][0]["ambient"]["color"][0] = "bad";
    CHECK_THROWS_AS((void)EnvironmentLightingRecord::Decode(invalid), std::invalid_argument);
    const EnvironmentLightingProfile legacy(EnvironmentDirectionalOwnership::Environment, Cycle());
    CHECK(EnvironmentLightingRecord::Encode(legacy).at("version") == 2);
    CHECK_FALSE(EnvironmentLightingRecord::Decode(EnvironmentLightingRecord::Encode(legacy)).Evaluate({})->Ambient);
}

TEST_CASE("Maximum ambient profile fits the bounded project settings payload")
{
    std::vector<EnvironmentLightingKeyframe> keys;
    for (std::size_t index = 0; index < EnvironmentLightingProfile::MaximumKeyframes; ++index)
    {
        keys.push_back({static_cast<double>(index) / EnvironmentLightingProfile::MaximumKeyframes,
                        359.1234567890123,
                        -89.1234567890123,
                        {0.1234567890123456, 0.2345678901234567, 1.0},
                        std::numeric_limits<float>::max(),
                        Keire::EnvironmentAmbientLight{{0.1234567F, 0.2345678F, 0.3456789F}, 15.123456F}});
    }
    const EnvironmentLightingProfile profile(EnvironmentDirectionalOwnership::Environment, std::move(keys));
    const nlohmann::json document{{"schemaVersion", 1}, {"lighting", EnvironmentLightingRecord::Encode(profile)}};
    CHECK(document.dump(2).size() + 1 <= 64U * 1024U);
    CHECK(EnvironmentLightingRecord::Decode(document.at("lighting")) == profile);
}

TEST_CASE("Environment daily lighting preserves authored ownership and evaluates wrapped phases")
{
    CHECK_FALSE(EnvironmentLightingProfile{}.Evaluate({}));
    const EnvironmentLightingProfile profile(EnvironmentDirectionalOwnership::Environment, Cycle());
    auto sample = profile.Evaluate({0, 0.5});
    REQUIRE(sample);
    CHECK(sample->Direction[0] == doctest::Approx(0.0).epsilon(1e-12));
    CHECK(sample->Direction[1] == doctest::Approx(-0.5));
    CHECK(sample->Direction[2] == doctest::Approx(-std::sqrt(0.75)));
    CHECK(sample->Intensity == 5.0);
    CHECK(sample->LinearColor[0] == 0.5);
    CHECK(sample->LinearColor[1] == 0.0);
    CHECK(sample->LinearColor[2] == 0.5);
    const auto midnight = profile.Evaluate({std::numeric_limits<std::int64_t>::max(), 0.0});
    REQUIRE(midnight);
    CHECK(midnight->Intensity == sample->Intensity);
    CHECK(midnight->Direction[1] == doctest::Approx(sample->Direction[1]));
    const auto ancient = profile.Evaluate({std::numeric_limits<std::int64_t>::min(), 0.0});
    CHECK(ancient->Direction == midnight->Direction);
    CHECK(profile.Evaluate({0, 0.25})->Intensity == 0.0);
    CHECK(profile.Evaluate({0, 0.75})->Intensity == 10.0);
    CHECK(profile.Keyframes() == Cycle());
}

TEST_CASE("Environment daily light directions remain normalized across the day and azimuth seam")
{
    const EnvironmentLightingProfile profile(EnvironmentDirectionalOwnership::Environment, Cycle());
    for (int step = 0; step < 1000; ++step)
    {
        const auto sample = profile.Evaluate({0, static_cast<double>(step) / 1000.0});
        double squaredLength = 0.0;
        for (const auto component : sample->Direction)
        {
            CHECK(std::isfinite(component));
            squaredLength += component * component;
        }
        CHECK(squaredLength == doctest::Approx(1.0).epsilon(1e-12));
        CHECK(sample->Intensity >= 0.0);
        CHECK(sample->Intensity <= 10.0);
    }
    const auto before = profile.Evaluate({0, std::nextafter(1.0, 0.0)});
    const auto after = profile.Evaluate({1, 0.0});
    for (std::size_t channel = 0; channel < 3; ++channel)
    {
        CHECK(before->Direction[channel] == doctest::Approx(after->Direction[channel]).epsilon(1e-12));
    }
}

TEST_CASE("Environment lighting rejects malformed tracks before evaluation")
{
    using Ownership = EnvironmentDirectionalOwnership;
    CHECK_THROWS_AS((void)EnvironmentLightingProfile(static_cast<Ownership>(7)), std::invalid_argument);
    CHECK_THROWS_AS((void)EnvironmentLightingProfile(Ownership::Environment), std::invalid_argument);
    auto keys = Cycle();
    keys[1].DayFraction = keys[0].DayFraction;
    CHECK_THROWS_AS((void)EnvironmentLightingProfile(Ownership::Environment, keys), std::invalid_argument);
    keys = Cycle();
    keys[1].DayFraction = 1.0;
    CHECK_THROWS_AS((void)EnvironmentLightingProfile(Ownership::Environment, keys), std::invalid_argument);
    keys = Cycle();
    keys[0].Intensity = std::numeric_limits<double>::infinity();
    CHECK_THROWS_AS((void)EnvironmentLightingProfile(Ownership::Environment, keys), std::invalid_argument);
    keys = Cycle();
    keys[0].LinearColor[1] = std::numeric_limits<double>::quiet_NaN();
    CHECK_THROWS_AS((void)EnvironmentLightingProfile(Ownership::Environment, keys), std::invalid_argument);
    keys = Cycle();
    keys[0].ElevationDegrees = 91.0;
    CHECK_THROWS_AS((void)EnvironmentLightingProfile(Ownership::Environment, keys), std::invalid_argument);
    keys = Cycle();
    keys[0].AzimuthDegrees = 360.0;
    CHECK_THROWS_AS((void)EnvironmentLightingProfile(Ownership::Environment, keys), std::invalid_argument);
    const EnvironmentLightingProfile profile(Ownership::Environment, Cycle());
    CHECK_THROWS_AS((void)profile.Evaluate({0, -0.1}), std::invalid_argument);
    CHECK_FALSE(EnvironmentLightingProfile(Ownership::Authored, Cycle()).Evaluate({}));
}

TEST_CASE("Environment lighting record round trips and rejects unknown or lossy input shapes")
{
    const EnvironmentLightingProfile profile(EnvironmentDirectionalOwnership::Environment, Cycle());
    const auto encoded = EnvironmentLightingRecord::Encode(profile);
    const auto decoded = EnvironmentLightingRecord::Decode(nlohmann::json::parse(encoded.dump()));
    CHECK(decoded.Ownership() == profile.Ownership());
    CHECK(decoded.Keyframes() == profile.Keyframes());
    CHECK_FALSE(EnvironmentLightingRecord::Decode(EnvironmentLightingRecord::Encode(EnvironmentLightingProfile{}))
                    .Evaluate({}));
    auto invalid = encoded;
    invalid["version"] = 1.0;
    CHECK_THROWS_AS((void)EnvironmentLightingRecord::Decode(invalid), std::invalid_argument);
    invalid = encoded;
    invalid["version"] = 3;
    CHECK_THROWS_AS((void)EnvironmentLightingRecord::Decode(invalid), std::invalid_argument);
    invalid = encoded;
    invalid["ownership"] = "unknown";
    CHECK_THROWS_AS((void)EnvironmentLightingRecord::Decode(invalid), std::invalid_argument);
    invalid = encoded;
    invalid["extra"] = 1;
    CHECK_THROWS_AS((void)EnvironmentLightingRecord::Decode(invalid), std::invalid_argument);
    invalid = encoded;
    invalid["keyframes"][0].erase("phase");
    CHECK_THROWS_AS((void)EnvironmentLightingRecord::Decode(invalid), std::invalid_argument);
    invalid = encoded;
    invalid["keyframes"][0]["color"][1] = true;
    CHECK_THROWS_AS((void)EnvironmentLightingRecord::Decode(invalid), std::invalid_argument);
    invalid = encoded;
    invalid["keyframes"][0]["intensity"] = "10";
    CHECK_THROWS_AS((void)EnvironmentLightingRecord::Decode(invalid), std::invalid_argument);
    invalid = encoded;
    invalid["keyframes"] = nlohmann::json::array();
    for (std::size_t index = 0; index <= EnvironmentLightingProfile::MaximumKeyframes; ++index)
    {
        invalid["keyframes"].push_back(encoded["keyframes"][0]);
    }
    CHECK_THROWS_AS((void)EnvironmentLightingRecord::Decode(invalid), std::invalid_argument);
}

TEST_CASE("Environment shadow records migrate legacy defaults and reject malformed settings")
{
    const EnvironmentLightingProfile profile(
        EnvironmentDirectionalOwnership::Environment, Cycle(),
        {Keire::ShadowQuality::Soft, Keire::ShadowResolutionHint::Medium, 0.7F, 0.02F});
    const auto encoded = EnvironmentLightingRecord::Encode(profile);
    CHECK(encoded.at("version") == 2);
    CHECK(EnvironmentLightingRecord::Decode(nlohmann::json::parse(encoded.dump())) == profile);
    auto legacy = encoded;
    legacy["version"] = 1;
    CHECK_THROWS_AS((void)EnvironmentLightingRecord::Decode(legacy), std::invalid_argument);
    legacy.erase("shadows");
    CHECK(EnvironmentLightingRecord::Decode(legacy).Shadows() == Keire::EnvironmentShadowSettings{});
    for (const auto field : {"quality", "resolution", "strength", "bias"})
    {
        auto invalid = encoded;
        invalid["shadows"].erase(field);
        CHECK_THROWS_AS((void)EnvironmentLightingRecord::Decode(invalid), std::invalid_argument);
        invalid = encoded;
        invalid["shadows"][field] = true;
        CHECK_THROWS_AS((void)EnvironmentLightingRecord::Decode(invalid), std::invalid_argument);
        invalid["shadows"][field] = -1;
        CHECK_THROWS_AS((void)EnvironmentLightingRecord::Decode(invalid), std::invalid_argument);
        invalid["shadows"][field] = 256;
        CHECK_THROWS_AS((void)EnvironmentLightingRecord::Decode(invalid), std::invalid_argument);
    }
    for (const auto field : {"strength", "bias"})
    {
        auto invalid = encoded;
        invalid["shadows"][field] = std::nextafter(1.0, 2.0);
        CHECK_THROWS_AS((void)EnvironmentLightingRecord::Decode(invalid), std::invalid_argument);
    }
}

TEST_CASE("Environment lighting values remain valid after moves copies and self assignment")
{
    const EnvironmentLightingProfile baseline(
        EnvironmentDirectionalOwnership::Environment, Cycle(),
        {Keire::ShadowQuality::Soft, Keire::ShadowResolutionHint::Medium, 0.7F, 0.02F});
    auto original = baseline;
    EnvironmentLightingProfile moved(std::move(original));
    CHECK(moved == baseline);
    CHECK(original.Ownership() == EnvironmentDirectionalOwnership::Authored);
    CHECK(original.Keyframes().empty());
    CHECK(original.Shadows() == Keire::EnvironmentShadowSettings{});
    CHECK_FALSE(original.Evaluate({}));
    original = moved;
    CHECK(original == baseline);
    moved = EnvironmentLightingProfile{};
    CHECK_FALSE(moved.Evaluate({}));
    moved = std::move(original);
    CHECK(moved == baseline);
    CHECK_FALSE(original.Evaluate({}));
    auto& alias = moved;
    moved = alias;
    CHECK(moved == baseline);
    moved = std::move(alias);
    CHECK(moved == baseline);
    CHECK(moved.Evaluate({0, 0.5})->Intensity == 5.0);
    CHECK(EnvironmentLightingRecord::Decode(EnvironmentLightingRecord::Encode(original)) == original);
}
