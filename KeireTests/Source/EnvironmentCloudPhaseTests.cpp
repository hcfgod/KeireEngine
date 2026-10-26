#include "KeireInternal/Environment/EnvironmentCloudPhase.h"

#include <doctest/doctest.h>

#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>

TEST_CASE("Environment cloud phase agrees with ordinary constant wind and a day-zero epoch")
{
    Keire::CloudSettings settings;
    settings.Layers[0].WindMetersPerSecond = {5.0, -3.0};
    settings.Layers[1].WindMetersPerSecond = {0.0, 13.0};
    settings.Layers[1].PatternSizeMeters = 4000.0;
    for (const std::int64_t day : {-17, -1, 0, 1, 17})
    {
        for (const double fraction : {0.0, 0.125, 0.5, 0.875})
        {
            const Keire::Detail::EnvironmentClockState clock{day, fraction, 1200.0, -2.0, true};
            const auto result = Keire::Detail::EvaluateEnvironmentCloudPhase(settings, clock);
            for (std::size_t layer = 0; layer < settings.Layers.size(); ++layer)
            {
                for (std::size_t axis = 0; axis < 2; ++axis)
                {
                    const double reference = Keire::Detail::CloudPhaseDetail::Wrap(
                        (static_cast<double>(day) + fraction) * clock.SecondsPerDay *
                        settings.Layers[layer].WindMetersPerSecond[axis] / settings.Layers[layer].PatternSizeMeters);
                    CHECK(result[layer][axis] == doctest::Approx(reference).epsilon(1.0e-12));
                }
            }
        }
    }
}

TEST_CASE("Environment cloud phase preserves local fractions at huge signed days")
{
    Keire::CloudSettings settings;
    settings.Layers[0].PatternSizeMeters = 1024.0;
    settings.Layers[0].WindMetersPerSecond = {1.0, -1.0};
    for (const auto day : {std::numeric_limits<std::int64_t>::min(), std::int64_t{-1234567890123456789LL},
                           std::int64_t{1234567890123456789LL}, std::numeric_limits<std::int64_t>::max()})
    {
        Keire::Detail::EnvironmentClockState clock{day, 0.0, 1024.0};
        const auto start = Keire::Detail::EvaluateEnvironmentCloudPhase(settings, clock);
        CHECK(start[0][0] == 0.0);
        clock.DayFraction = 0.125;
        const auto advanced = Keire::Detail::EvaluateEnvironmentCloudPhase(settings, clock);
        CHECK(advanced[0][0] == 0.125);
        CHECK(advanced[0][1] == 0.875);
        clock.Paused = true;
        clock.Rate = -100.0;
        CHECK(Keire::Detail::EvaluateEnvironmentCloudPhase(settings, clock) == advanced);
        clock.SecondsPerDay = 256.0;
        clock.DayFraction = 0.5;
        const double expected = (static_cast<double>(day % 4) + 0.5) * 0.25;
        CHECK(Keire::Detail::EvaluateEnvironmentCloudPhase(settings, clock)[0][0] ==
              (expected < 0.0 ? expected + 1.0 : expected));
    }
}

TEST_CASE("Environment cloud phase crosses day boundaries and reverses without resetting")
{
    Keire::CloudSettings settings;
    settings.Layers[0].PatternSizeMeters = 1024.0;
    settings.Layers[0].WindMetersPerSecond = {1.0, -1.0};
    Keire::Detail::EnvironmentClock clock({-1, 0.75, 512.0});
    CHECK(Keire::Detail::EvaluateEnvironmentCloudPhase(settings, clock.State())[0][0] == 0.875);
    clock.Advance(128.0);
    CHECK(clock.State().Day == 0);
    CHECK(Keire::Detail::EvaluateEnvironmentCloudPhase(settings, clock.State())[0][0] == 0.0);
    clock.Configure(512.0, -1.0, false);
    clock.Advance(128.0);
    CHECK(Keire::Detail::EvaluateEnvironmentCloudPhase(settings, clock.State())[0][0] == 0.875);
    clock.Configure(512.0, 1.0, true);
    clock.Advance(1000.0);
    CHECK(Keire::Detail::EvaluateEnvironmentCloudPhase(settings, clock.State())[0][0] == 0.875);
}

TEST_CASE("Environment cloud phase accepts extreme finite durations without intermediate overflow")
{
    Keire::CloudSettings settings;
    settings.Layers[0].WindMetersPerSecond = {1000.0, -1000.0};
    settings.Layers[0].PatternSizeMeters = 100.0;
    for (const double duration : {std::numeric_limits<double>::denorm_min(), std::numeric_limits<double>::min(),
                                  1.0e200, std::numeric_limits<double>::max()})
    {
        const Keire::Detail::EnvironmentClockState clock{std::numeric_limits<std::int64_t>::min(), 0.375, duration};
        const auto phase = Keire::Detail::EvaluateEnvironmentCloudPhase(settings, clock);
        CHECK(phase == Keire::Detail::EvaluateEnvironmentCloudPhase(settings, clock));
        for (const auto& layer : phase)
        {
            for (const double axis : layer)
            {
                CHECK(std::isfinite(axis));
                CHECK(axis >= 0.0);
                CHECK(axis < 1.0);
            }
        }
    }
    settings.Layers[0].WindMetersPerSecond = {1.0, -1.0};
    settings.Layers[0].PatternSizeMeters = 1024.0;
    const Keire::Detail::EnvironmentClockState tinyFraction{0, std::ldexp(1.0, -1000), std::ldexp(1.0, 1000)};
    const auto phase = Keire::Detail::EvaluateEnvironmentCloudPhase(settings, tinyFraction);
    CHECK(phase[0][0] == 1.0 / 1024.0);
    CHECK(phase[0][1] == 1023.0 / 1024.0);
}

TEST_CASE("Environment cloud phase rejects invalid disabled settings and clock states")
{
    Keire::CloudSettings settings;
    Keire::Detail::EnvironmentClockState clock;
    settings.Layers[1].PatternSizeMeters = 0.0;
    CHECK_THROWS_AS((void)Keire::Detail::EvaluateEnvironmentCloudPhase(settings, clock), std::invalid_argument);
    settings = {};
    for (const double invalid :
         {0.0, -1.0, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()})
    {
        clock.SecondsPerDay = invalid;
        CHECK_THROWS_AS((void)Keire::Detail::EvaluateEnvironmentCloudPhase(settings, clock), std::invalid_argument);
    }
    clock = {};
    clock.DayFraction = 1.0;
    CHECK_THROWS_AS((void)Keire::Detail::EvaluateEnvironmentCloudPhase(settings, clock), std::invalid_argument);
    clock = {};
    clock.Rate = std::numeric_limits<double>::infinity();
    CHECK_THROWS_AS((void)Keire::Detail::EvaluateEnvironmentCloudPhase(settings, clock), std::invalid_argument);
}
