#include "Keire/Time.h"

#include <doctest/doctest.h>

#include <limits>
#include <stdexcept>

TEST_CASE("Time rejects unrepresentable advancement without publishing partial clocks")
{
    Keire::TimeSpecification specification;
    double delta = std::numeric_limits<double>::max();
    SUBCASE("Elapsed real time overflow") {}
    SUBCASE("Fixed step count overflow")
    {
        specification.FixedDeltaTime = Keire::TimeStep::FromSeconds(1e-30);
        delta = 1.0;
    }
    Keire::Time time(specification);
    time.SetPaused(true);
    time.AdvanceFrame(Keire::TimeStep::FromSeconds(delta));
    time.SetPaused(false);
    const auto before = time;
    CHECK_THROWS_AS(time.AdvanceFrame(Keire::TimeStep::FromSeconds(delta)), std::overflow_error);
    CHECK(time.FrameCount() == before.FrameCount());
    CHECK(time.RealtimeSinceStartup() == before.RealtimeSinceStartup());
    CHECK(time.UnscaledTime() == before.UnscaledTime());
    CHECK(time.TimeSinceStartup() == before.TimeSinceStartup());
    CHECK(time.DeltaTime() == before.DeltaTime());
    CHECK(time.SmoothDeltaTime() == before.SmoothDeltaTime());
    CHECK(time.PendingFixedSteps() == before.PendingFixedSteps());
    CHECK(time.DroppedSimulationTime() == before.DroppedSimulationTime());
    CHECK(time.InterpolationAlpha() == before.InterpolationAlpha());
    CHECK_NOTHROW(time.AdvanceFrame(Keire::TimeStep{}));
}

TEST_CASE("TimeStep exposes type-safe duration arithmetic")
{
    const auto first = Keire::TimeStep::FromMilliseconds(250.0);
    const auto second = Keire::TimeStep::FromSeconds(0.5);
    CHECK(first.Seconds() == doctest::Approx(0.25));
    CHECK(second.Milliseconds() == doctest::Approx(500.0));
    CHECK((first + second).Seconds() == doctest::Approx(0.75));
    CHECK((second - first).Seconds() == doctest::Approx(0.25));
    CHECK((first * 2.0) == second);
    CHECK(2.0 * first == second);
}

TEST_CASE("Time clamps frames and bounds fixed simulation backlog")
{
    Keire::Time time;
    time.AdvanceFrame(Keire::TimeStep::FromMilliseconds(500.0));

    CHECK(time.RawDeltaTime().Seconds() == doctest::Approx(0.5));
    CHECK(time.UnscaledDeltaTime().Seconds() == doctest::Approx(0.25));
    CHECK(time.DeltaTime().Seconds() == doctest::Approx(0.25));
    CHECK(time.PendingFixedSteps() == 8);
    CHECK(time.DroppedSimulationTime().Seconds() == doctest::Approx(7.0 / 60.0));
    CHECK(time.InterpolationAlpha() == doctest::Approx(0.0).epsilon(0.000001));

    std::uint32_t consumed = 0;
    while (time.ConsumeFixedStep())
    {
        ++consumed;
    }
    CHECK(consumed == 8);
    CHECK(time.FixedTickCount() == 8);
    CHECK(time.FixedTime().Seconds() == doctest::Approx(8.0 / 60.0));
    CHECK(time.FrameCount() == 1);
    CHECK(time.RealtimeSinceStartup().Seconds() == doctest::Approx(0.5));
    CHECK(time.UnscaledTime().Seconds() == doctest::Approx(0.25));
}

TEST_CASE("Time scale and pause affect simulation without losing real time")
{
    Keire::Time time;
    time.SetTimeScale(0.5);
    time.AdvanceFrame(Keire::TimeStep::FromMilliseconds(100.0));
    CHECK(time.DeltaTime().Seconds() == doctest::Approx(0.05));
    CHECK(time.TimeSinceStartup().Seconds() == doctest::Approx(0.05));
    CHECK(time.PendingFixedSteps() == 3);
    while (time.ConsumeFixedStep())
    {
    }

    time.SetPaused(true);
    time.AdvanceFrame(Keire::TimeStep::FromMilliseconds(100.0));
    CHECK(time.RawDeltaTime().Seconds() == doctest::Approx(0.1));
    CHECK(time.DeltaTime().Seconds() == 0.0);
    CHECK(time.SmoothDeltaTime().Seconds() == 0.0);
    CHECK(time.PendingFixedSteps() == 0);
    CHECK(time.TimeScale() == 0.5);
    CHECK(time.RealtimeSinceStartup().Seconds() == doctest::Approx(0.2));
    CHECK(time.TimeSinceStartup().Seconds() == doctest::Approx(0.05));

    time.SetPaused(false);
    time.AdvanceFrame(Keire::TimeStep::FromMilliseconds(100.0));
    CHECK(time.DeltaTime().Seconds() == doctest::Approx(0.05));
}

TEST_CASE("Time can suspend scaled simulation while retaining real and unscaled clocks")
{
    Keire::Time time;
    time.AdvanceFrame(Keire::TimeStep::FromMilliseconds(100.0), true);
    CHECK(time.RawDeltaTime().Seconds() == doctest::Approx(0.1));
    CHECK(time.UnscaledDeltaTime().Seconds() == doctest::Approx(0.1));
    CHECK(time.DeltaTime().Seconds() == 0.0);
    CHECK(time.RealtimeSinceStartup().Seconds() == doctest::Approx(0.1));
    CHECK(time.UnscaledTime().Seconds() == doctest::Approx(0.1));
    CHECK(time.TimeSinceStartup().Seconds() == 0.0);
    CHECK(time.PendingFixedSteps() == 0);
    CHECK_FALSE(time.Paused());
}

TEST_CASE("Time can discard rejected fixed steps without advancing committed simulation time")
{
    Keire::Time time;
    time.AdvanceFrame(Keire::TimeStep::FromMilliseconds(100.0));
    REQUIRE(time.PendingFixedSteps() == 6);

    CHECK(time.DiscardFixedSteps() == 6);
    CHECK(time.PendingFixedSteps() == 0);
    CHECK(time.FixedTickCount() == 0);
    CHECK(time.FixedTime().Seconds() == 0.0);
    CHECK(time.DroppedSimulationTime().Seconds() == 0.0);

    time.AdvanceFrame(Keire::TimeStep::FromMilliseconds(100.0));
    REQUIRE(time.ConsumeFixedStep());
    CHECK(time.DiscardFixedSteps() == 5);
    CHECK(time.FixedTickCount() == 1);
    CHECK(time.FixedTime().Seconds() == doctest::Approx(1.0 / 60.0));
}

TEST_CASE("Time validates configuration, scale, and frame samples")
{
    Keire::TimeSpecification invalidSpecification;
    invalidSpecification.MaximumFixedStepsPerFrame = 0;
    CHECK_THROWS_AS((void)Keire::Time{invalidSpecification}, std::invalid_argument);

    Keire::Time time;
    CHECK_THROWS_AS(time.SetTimeScale(-1.0), std::invalid_argument);
    CHECK_THROWS_AS(time.SetTimeScale(101.0), std::invalid_argument);
    CHECK_THROWS_AS(time.AdvanceFrame(Keire::TimeStep::FromSeconds(-0.1)), std::invalid_argument);

    time.AdvanceFrame(Keire::TimeStep::FromMilliseconds(100.0));
    CHECK_THROWS_AS(time.AdvanceFrame(Keire::TimeStep{}), std::logic_error);
}
