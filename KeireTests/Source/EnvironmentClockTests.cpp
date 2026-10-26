#include "KeireInternal/Environment/EnvironmentClock.h"
#include "KeireInternal/Environment/EnvironmentClockRecord.h"
#include "KeireInternal/Environment/EnvironmentRuntimeManifest.h"

#include <doctest/doctest.h>

#include <cmath>
#include <limits>
#include <stdexcept>

TEST_CASE("Runtime environment manifest migrates legacy time and requires explicit new records")
{
    using Manifest = Keire::Detail::EnvironmentRuntimeManifest;
    using Record = Keire::Detail::EnvironmentClockRecord;
    CHECK(Manifest::Decode({{"schemaVersion", 4}}) == Keire::Detail::EnvironmentClockState{});
    const Keire::Detail::EnvironmentClockState authored{9223372036854775806LL, 0.75, 4321.0, -0.5, true};
    const nlohmann::json cooked{{"schemaVersion", 5}, {"environmentTime", Record::Encode(authored)}};
    CHECK(Manifest::Decode(nlohmann::json::parse(cooked.dump())) == authored);
    auto malformed = cooked;
    malformed.erase("environmentTime");
    CHECK_THROWS_AS((void)Manifest::Decode(malformed), std::invalid_argument);
    for (const auto version : {nlohmann::json(3), nlohmann::json(4), nlohmann::json(6), nlohmann::json(5.0),
                               nlohmann::json("5"), nlohmann::json(4294967301ULL)})
    {
        malformed = cooked;
        malformed["schemaVersion"] = version;
        CHECK_THROWS_AS((void)Manifest::Decode(malformed), std::invalid_argument);
    }
    malformed = cooked;
    malformed["environmentTime"]["dayFraction"] = 1.0;
    CHECK_THROWS_AS((void)Manifest::Decode(malformed), std::invalid_argument);
}

TEST_CASE("Runtime lighting manifests preserve profiles and reject partial or mislabeled records")
{
    using namespace Keire::Detail;
    const EnvironmentClockState time{9223372036854775806LL, 0.75, 4321.0, -0.5, true};
    const EnvironmentLightingProfile lighting(
        EnvironmentDirectionalOwnership::Environment,
        {{0.0, 0.0, -80.0, {0.1, 0.2, 0.3}, 0.0}, {0.5, 180.0, 80.0, {1.0, 0.9, 0.8}, 12.0}});
    const nlohmann::json cooked{{"schemaVersion", EnvironmentRuntimeManifest::LightingSchema},
                                {"environmentTime", EnvironmentClockRecord::Encode(time)},
                                {"environmentLighting", EnvironmentLightingRecord::Encode(lighting)}};
    const auto restored = EnvironmentRuntimeManifest::DecodeState(nlohmann::json::parse(cooked.dump()));
    CHECK(restored.Time == time);
    CHECK(restored.Lighting == lighting);
    CHECK_THROWS_AS((void)EnvironmentRuntimeManifest::Decode(cooked), std::invalid_argument);
    for (const auto version : {4, 5, 7})
    {
        auto invalid = cooked;
        invalid["schemaVersion"] = version;
        CHECK_THROWS_AS((void)EnvironmentRuntimeManifest::DecodeState(invalid), std::invalid_argument);
    }
    for (const auto field : {"environmentTime", "environmentLighting"})
    {
        auto invalid = cooked;
        invalid.erase(field);
        CHECK_THROWS_AS((void)EnvironmentRuntimeManifest::DecodeState(invalid), std::invalid_argument);
    }
    auto invalid = cooked;
    invalid["environmentLighting"]["version"] = 2;
    CHECK_THROWS_AS((void)EnvironmentRuntimeManifest::Decode(invalid), std::invalid_argument);
    auto legacy = cooked;
    legacy["schemaVersion"] = 5;
    legacy.erase("environmentLighting");
    const auto legacyState = EnvironmentRuntimeManifest::DecodeState(legacy);
    CHECK(legacyState.Time == time);
    CHECK(legacyState.Lighting.Ownership() == EnvironmentDirectionalOwnership::Authored);
    legacy["schemaVersion"] = 4;
    legacy.erase("environmentTime");
    CHECK(EnvironmentRuntimeManifest::DecodeState(legacy).Lighting.Ownership() ==
          EnvironmentDirectionalOwnership::Authored);
}

TEST_CASE("Environment clock record preserves distant dates through JSON text")
{
    using Record = Keire::Detail::EnvironmentClockRecord;
    for (const auto day : {std::numeric_limits<std::int64_t>::min(), std::int64_t{-1}, std::int64_t{0},
                           std::numeric_limits<std::int64_t>::max()})
    {
        const Keire::Detail::EnvironmentClockState source{day, 0.12345678901234567, 9876.54321, -0.125, true};
        const auto encoded = Record::Encode(source);
        CHECK(encoded.at("day").is_string());
        CHECK(Record::Decode(nlohmann::json::parse(encoded.dump())) == source);
        Keire::Detail::EnvironmentClock restored;
        restored.Restore(Record::Decode(encoded));
        CHECK(restored.State() == source);
        CHECK(restored.DiscontinuityRevision() == 2);
    }
}

TEST_CASE("Environment clock record rejects lossy or malformed fields")
{
    using Record = Keire::Detail::EnvironmentClockRecord;
    const auto baseline = Record::Encode({});
    for (const auto text :
         {"", "01", "-0", "+1", "1.5", "1e3", " 1", "1 ", "9223372036854775808", "-9223372036854775809"})
    {
        auto invalid = baseline;
        invalid["day"] = text;
        CHECK_THROWS_AS((void)Record::Decode(invalid), std::invalid_argument);
    }
    for (const auto& value : {nlohmann::json(1), nlohmann::json(1.5), nlohmann::json(nullptr)})
    {
        auto invalid = baseline;
        invalid["day"] = value;
        CHECK_THROWS_AS((void)Record::Decode(invalid), std::invalid_argument);
    }
    for (const auto key : {"version", "day", "dayFraction", "secondsPerDay", "rate", "paused"})
    {
        auto invalid = baseline;
        invalid.erase(key);
        CHECK_THROWS_AS((void)Record::Decode(invalid), std::invalid_argument);
    }
    for (const auto key : {"dayFraction", "secondsPerDay", "rate"})
    {
        auto invalid = baseline;
        invalid[key] = "1";
        CHECK_THROWS_AS((void)Record::Decode(invalid), std::invalid_argument);
        invalid[key] = std::numeric_limits<double>::infinity();
        CHECK_THROWS_AS((void)Record::Decode(invalid), std::invalid_argument);
    }
    auto invalid = baseline;
    invalid["version"] = 1.0;
    CHECK_THROWS_AS((void)Record::Decode(invalid), std::invalid_argument);
    invalid["version"] = 2;
    CHECK_THROWS_AS((void)Record::Decode(invalid), std::invalid_argument);
    invalid = baseline;
    invalid["paused"] = 1;
    CHECK_THROWS_AS((void)Record::Decode(invalid), std::invalid_argument);
    invalid = baseline;
    invalid["unknown"] = 1;
    CHECK_THROWS_AS((void)Record::Decode(invalid), std::invalid_argument);
    CHECK_THROWS_AS((void)Record::Encode({0, 1.0}), std::invalid_argument);
    CHECK_THROWS_AS((void)Record::Encode({0, 0.0, 0.0}), std::invalid_argument);
}

TEST_CASE("Environment clock preserves local precision at distant calendar days")
{
    for (const auto day :
         {std::int64_t{0}, std::numeric_limits<std::int64_t>::max() - 1, std::numeric_limits<std::int64_t>::min() + 1})
    {
        Keire::Detail::EnvironmentClock clock({day, 0.25, 1024.0});
        clock.Advance(1.0);
        CHECK(clock.State().Day == day);
        CHECK(clock.State().DayFraction == 0.25 + 1.0 / 1024.0);
        clock.Advance(767.0);
        CHECK(clock.State().Day == day + 1);
        CHECK(clock.State().DayFraction == 0.0);
        CHECK(clock.DiscontinuityRevision() == 1);
    }
}

TEST_CASE("Environment clock supports reverse pause and explicit seek")
{
    Keire::Detail::EnvironmentClock clock({0, 0.25, 100.0});
    clock.Configure(100.0, -1.0, false);
    clock.Advance(50.0);
    CHECK(clock.State().Day == -1);
    CHECK(clock.State().DayFraction == 0.75);
    clock.Configure(100.0, 3.0, true);
    const auto paused = clock.State();
    clock.Advance(10000.0);
    CHECK(clock.State() == paused);
    clock.Seek(12, 0.5);
    CHECK(clock.State().Paused);
    CHECK(clock.State().Rate == 3.0);
    CHECK(clock.DiscontinuityRevision() == 2);
    clock.Restore(paused);
    CHECK(clock.State() == paused);
    CHECK(clock.DiscontinuityRevision() == 3);
}

TEST_CASE("Environment clock rejects invalid transitions atomically")
{
    Keire::Detail::EnvironmentClock clock({std::numeric_limits<std::int64_t>::max(), 0.75, 1.0});
    const auto before = clock.State();
    CHECK_THROWS_AS(clock.Advance(1.0), std::overflow_error);
    CHECK_THROWS_AS(clock.Advance(std::numeric_limits<double>::max()), std::overflow_error);
    CHECK_THROWS_AS(clock.Advance(-1.0), std::invalid_argument);
    CHECK_THROWS_AS(clock.Seek(1, 1.0), std::invalid_argument);
    CHECK_THROWS_AS(clock.Configure(0.0, 1.0, false), std::invalid_argument);
    CHECK_THROWS_AS(clock.Configure(1.0, std::numeric_limits<double>::infinity(), false), std::invalid_argument);
    CHECK(clock.State() == before);
    CHECK(clock.DiscontinuityRevision() == 1);
}

TEST_CASE("Environment clock keeps local phase across huge whole-day skips")
{
    Keire::Detail::EnvironmentClock clock({0, 0.125, 1.0});
    clock.Advance(4503599627370496.0);
    CHECK(clock.State().Day == 4503599627370496LL);
    CHECK(clock.State().DayFraction == 0.125);
    clock.Configure(1.0, -1.0, false);
    clock.Advance(4503599627370496.0);
    CHECK(clock.State().Day == 0);
    CHECK(clock.State().DayFraction == 0.125);
}

TEST_CASE("Environment clock scales extreme finite durations without intermediate range loss")
{
    for (const double direction : {-1.0, 1.0})
    {
        for (const bool largeRatio : {false, true})
        {
            if (!largeRatio && direction < 0.0)
            {
                continue;
            }
            const double elapsed = std::ldexp(1.0, largeRatio ? 600 : -600);
            const double duration = std::ldexp(1.0, largeRatio ? -450 : 500);
            const double rate = direction * std::ldexp(1.0, largeRatio ? -1050 : 1000);
            Keire::Detail::EnvironmentClock clock({0, 0.0, duration, rate});
            clock.Advance(elapsed);
            CHECK(clock.State().Day == (largeRatio ? static_cast<int>(direction) : (direction < 0.0 ? -1 : 0)));
            if (largeRatio)
            {
                CHECK(clock.State().DayFraction == 0.0);
            }
            else if (direction > 0.0)
            {
                CHECK(clock.State().DayFraction == std::ldexp(1.0, -100));
            }
        }

        Keire::Detail::EnvironmentClock clock({0, 0.25, std::ldexp(1.0, -500), direction * std::ldexp(1.0, -1000)});
        clock.Advance(std::ldexp(1.0, 500));
        CHECK(clock.State().Day == (direction < 0.0 ? -1 : 1));
        CHECK(clock.State().DayFraction == 0.25);
    }
}
