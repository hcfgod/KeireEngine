#include "KeireInternal/Environment/EnvironmentColorTrack.h"
#include "KeireInternal/Environment/EnvironmentColorTrackRecord.h"

#include <doctest/doctest.h>

#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace
{
    using namespace Keire::Detail;
}

TEST_CASE("Daily sky color interpolation wraps midnight without losing distant-date precision")
{
    const EnvironmentColorTrack track({{0.25, {8.0, 2.0, 0.0}}, {0.75, {0.0, 6.0, 12.0}}});
    CHECK(track.Evaluate({.DayFraction = 0.25}) == std::array<double, 3>{8.0, 2.0, 0.0});
    CHECK(track.Evaluate({.DayFraction = 0.75}) == std::array<double, 3>{0.0, 6.0, 12.0});
    for (const auto day :
         {std::numeric_limits<std::int64_t>::min(), std::int64_t{0}, std::numeric_limits<std::int64_t>::max()})
    {
        CHECK(track.Evaluate({.Day = day, .DayFraction = 0.0}) == std::array<double, 3>{4.0, 4.0, 6.0});
        CHECK(track.Evaluate({.Day = day, .DayFraction = 0.5}) == std::array<double, 3>{4.0, 4.0, 6.0});
    }
    const auto beforeMidnight = track.Evaluate({.DayFraction = std::nextafter(1.0, 0.0)});
    const auto midnight = track.Evaluate({});
    for (std::size_t channel = 0; channel < midnight.size(); ++channel)
    {
        CHECK(beforeMidnight[channel] == doctest::Approx(midnight[channel]));
    }
}

TEST_CASE("Each sky key controls its outgoing interpolation including exact hold boundaries")
{
    const EnvironmentColorTrack track({{0.25, {0.0, 0.0, 0.0}, EnvironmentKeyInterpolation::Smooth},
                                       {0.75, {8.0, 8.0, 8.0}, EnvironmentKeyInterpolation::Hold}});
    CHECK(track.Evaluate({.DayFraction = 0.375})[0] == doctest::Approx(1.25));
    CHECK(track.Evaluate({.DayFraction = 0.5})[0] == doctest::Approx(4.0));
    CHECK(track.Evaluate({.DayFraction = 0.75})[0] == 8.0);
    CHECK(track.Evaluate({.DayFraction = 0.0})[0] == 8.0);
    CHECK(track.Evaluate({.DayFraction = std::nextafter(0.25, 0.0)})[0] == 8.0);
    CHECK(track.Evaluate({.DayFraction = 0.25})[0] == 0.0);
    const EnvironmentColorTrack constant({{0.7, {65504.0, 2.0, 0.0}}});
    CHECK(constant.Evaluate({}) == constant.Evaluate({.DayFraction = 0.9}));
}

TEST_CASE("Sky color tracks reject ambiguous times and invalid HDR data")
{
    CHECK_THROWS_AS(EnvironmentColorTrack(std::vector<EnvironmentColorKey>{}), std::invalid_argument);
    CHECK_THROWS_AS(EnvironmentColorTrack(std::vector<EnvironmentColorKey>(129)), std::invalid_argument);
    CHECK_THROWS_AS(EnvironmentColorTrack({{0.5}, {0.5}}), std::invalid_argument);
    CHECK_THROWS_AS(EnvironmentColorTrack({{0.5}, {0.25}}), std::invalid_argument);
    for (const double phase :
         {-0.1, 1.0, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()})
    {
        CHECK_THROWS_AS(EnvironmentColorTrack({{phase}}), std::invalid_argument);
        CHECK_THROWS_AS((void)EnvironmentColorTrack().Evaluate({.DayFraction = phase}), std::invalid_argument);
    }
    for (const double channel :
         {-1.0, 65505.0, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()})
    {
        CHECK_THROWS_AS(EnvironmentColorTrack({{0.0, {0.0, channel, 0.0}}}), std::invalid_argument);
    }
    CHECK_THROWS_AS(EnvironmentColorTrack({{0.0, {}, static_cast<EnvironmentKeyInterpolation>(99)}}),
                    std::invalid_argument);
}

TEST_CASE("Sky color persistence retains all keys and rejects malformed enum values before narrowing")
{
    std::vector<EnvironmentColorKey> keys;
    for (std::size_t index = 0; index < EnvironmentColorTrack::MaximumKeys; ++index)
    {
        keys.push_back({static_cast<double>(index) / EnvironmentColorTrack::MaximumKeys,
                        {65504.0, 2.345678901234, 0.0},
                        static_cast<EnvironmentKeyInterpolation>(index % 3)});
    }
    const EnvironmentColorTrack track(std::move(keys));
    const auto record = EnvironmentColorTrackRecord::Encode(track);
    CHECK(EnvironmentColorTrackRecord::Decode(nlohmann::json::parse(record.dump())) == track);
    for (const auto invalidEnum : {nlohmann::json(-1), nlohmann::json(256), nlohmann::json(4294967296ULL),
                                   nlohmann::json(1.5), nlohmann::json("linear")})
    {
        auto invalid = record;
        invalid["keys"][0]["interpolation"] = invalidEnum;
        CHECK_THROWS_AS((void)EnvironmentColorTrackRecord::Decode(invalid), std::invalid_argument);
    }
    auto invalid = record;
    invalid["version"] = 2;
    CHECK_THROWS_AS((void)EnvironmentColorTrackRecord::Decode(invalid), std::invalid_argument);
    invalid = record;
    invalid["keys"][0]["color"][1] = true;
    CHECK_THROWS_AS((void)EnvironmentColorTrackRecord::Decode(invalid), std::invalid_argument);
    invalid = record;
    invalid["keys"][0]["unknown"] = 1;
    CHECK_THROWS_AS((void)EnvironmentColorTrackRecord::Decode(invalid), std::invalid_argument);
}
