#include "KeireInternal/Environment/WeatherEvaluation.h"

#include <doctest/doctest.h>

#include <cmath>
#include <limits>

TEST_CASE("Weather profile validation rejects nonfinite and out-of-unit values")
{
    using namespace Keire;
    using namespace Keire::Detail;
    CHECK(IsValidWeatherProfile(WeatherProfile{}));
    for (double WeatherProfile::* field :
         {&WeatherProfile::CloudCoverage, &WeatherProfile::RainMillimetersPerHour,
          &WeatherProfile::SnowWaterMillimetersPerHour, &WeatherProfile::TemperatureCelsius,
          &WeatherProfile::SurfaceWetness, &WeatherProfile::SurfaceSnowCoverage})
    {
        for (double invalid : {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(),
                               -std::numeric_limits<double>::infinity()})
        {
            WeatherProfile profile;
            profile.*field = invalid;
            CHECK_FALSE(IsValidWeatherProfile(profile));
        }
    }
    WeatherProfile profile;
    profile.CloudCoverage = 1.01;
    CHECK_FALSE(IsValidWeatherProfile(profile));
    profile = {};
    profile.RainMillimetersPerHour = -0.01;
    CHECK_FALSE(IsValidWeatherProfile(profile));
    profile = {};
    profile.TemperatureCelsius = -273.16;
    CHECK_FALSE(IsValidWeatherProfile(profile));
    for (std::size_t axis = 0; axis < 3; ++axis)
    {
        profile = {};
        profile.WindMetersPerSecond[axis] = std::numeric_limits<double>::quiet_NaN();
        CHECK_FALSE(IsValidWeatherProfile(profile));
        profile.WindMetersPerSecond[axis] = 201.0;
        CHECK_FALSE(IsValidWeatherProfile(profile));
    }
}

TEST_CASE("Weather blends preserve endpoints and interpolate every authored channel")
{
    using namespace Keire;
    using namespace Keire::Detail;
    const WeatherProfile clear;
    const WeatherProfile storm{1.0, 30.0, 4.0, -5.0, 1.0, 0.5, {20.0, -2.0, -10.0}};
    CHECK(BlendWeatherProfiles(clear, storm, 0.0) == clear);
    CHECK(BlendWeatherProfiles(clear, storm, 1.0) == storm);
    const auto middle = BlendWeatherProfiles(clear, storm, 0.5);
    REQUIRE(middle.has_value());
    CHECK(*middle == WeatherProfile{0.5, 15.0, 2.0, 5.0, 0.5, 0.25, {10.0, -1.0, -5.0}});
    for (double weight : {-0.01, 1.01, std::numeric_limits<double>::quiet_NaN()})
    {
        CHECK_FALSE(BlendWeatherProfiles(clear, storm, weight).has_value());
    }
    auto invalid = storm;
    invalid.SurfaceSnowCoverage = 2.0;
    CHECK_FALSE(BlendWeatherProfiles(clear, invalid, 0.0).has_value());
    CHECK(storm.RainMillimetersPerHour == 30.0);
}

TEST_CASE("Weather gust sampling is stateless bounded periodic and seed dependent")
{
    using namespace Keire;
    using namespace Keire::Detail;
    WeatherProfile profile;
    profile.WindMetersPerSecond = {10.0, 2.0, -3.0};
    const WeatherGustSettings gust{12345, 5.0, 12.0};
    for (const double time :
         {-WeatherMaximumEvaluationSeconds, -24.0, -1.0, 0.0, 1.0, 24.0, WeatherMaximumEvaluationSeconds})
    {
        const auto wind = EvaluateWeatherWind(profile, gust, time);
        REQUIRE(wind.has_value());
        CHECK(wind == EvaluateWeatherWind(profile, gust, time));
        CHECK((*wind)[1] == 2.0);
        const double eastGust = (*wind)[0] - 10.0;
        const double northGust = (*wind)[2] + 3.0;
        CHECK(eastGust * eastGust + northGust * northGust == doctest::Approx(25.0));
    }
    CHECK(EvaluateWeatherWind(profile, gust, 1.0) == EvaluateWeatherWind(profile, gust, 13.0));
    CHECK(EvaluateWeatherWind(profile, gust, 1.0) != EvaluateWeatherWind(profile, {0, 5.0, 12.0}, 1.0));
    CHECK(EvaluateWeatherWind(profile, {0, 0.0, 12.0}, 1.0) == profile.WindMetersPerSecond);
    for (const double time : {WeatherMaximumEvaluationSeconds + 1.0, std::numeric_limits<double>::quiet_NaN()})
    {
        CHECK_FALSE(EvaluateWeatherWind(profile, gust, time).has_value());
    }
    CHECK_FALSE(EvaluateWeatherWind(profile, {0, -1.0, 12.0}, 0.0).has_value());
    CHECK_FALSE(EvaluateWeatherWind(profile, {0, 1.0, 0.0}, 0.0).has_value());
}
