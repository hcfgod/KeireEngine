#include "KeireInternal/Rendering/PlanetWeatherComposition.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

TEST_CASE("Planet weather orders interleaved shells and integrates foreground air exactly once")
{
    using namespace Keire::Detail;
    const PlanetWeatherTransport cloud{{1.0, 1.0, 1.0}, {0.5, 0.5, 0.5}};
    const std::array events{PlanetWeatherShellEvent{3.0, Keire::AssetId(1, 1), 0, 1, cloud},
                            PlanetWeatherShellEvent{1.0, Keire::AssetId(1, 1), 0, 0, cloud},
                            PlanetWeatherShellEvent{2.0, Keire::AssetId(1, 2), 0, 0, cloud}};
    double integratedLength = 0.0;
    const auto result =
        ComposePlanetWeather(events, 4.0,
                             [&](const double begin, const double end)
                             {
                                 integratedLength += end - begin;
                                 const double transmission = std::exp(-(end - begin));
                                 return PlanetWeatherTransport{{}, {transmission, transmission, transmission}};
                             });
    CHECK(integratedLength == 4.0);
    CHECK(result.Radiance[0] == doctest::Approx(std::exp(-1.0) + 0.5 * std::exp(-2.0) + 0.25 * std::exp(-3.0)));
    CHECK(result.Transmission[0] == doctest::Approx(0.125 * std::exp(-4.0)));
    const auto clipped = ComposePlanetWeather(events, 2.0, [](double, double) { return PlanetWeatherTransport{}; });
    CHECK(clipped.Radiance[0] == 1.0);
    CHECK(clipped.Transmission[0] == 0.5);
}

TEST_CASE("Planet weather equal-distance ties remain stable across input ordering")
{
    using namespace Keire::Detail;
    const PlanetWeatherTransport red{{2.0, 0.0, 0.0}, {0.25, 0.25, 0.25}};
    const PlanetWeatherTransport blue{{0.0, 0.0, 4.0}, {0.5, 0.5, 0.5}};
    std::array events{PlanetWeatherShellEvent{1.0, Keire::AssetId(1, 2), 0, 0, blue},
                      PlanetWeatherShellEvent{1.0, Keire::AssetId(1, 1), 0, 0, red}};
    const auto vacuum = [](double begin, double end)
    {
        CHECK(end > begin);
        return PlanetWeatherTransport{};
    };
    const auto first = ComposePlanetWeather(events, 2.0, vacuum);
    std::ranges::reverse(events);
    const auto second = ComposePlanetWeather(events, 2.0, vacuum);
    CHECK(first.Radiance == second.Radiance);
    CHECK(first.Transmission == second.Transmission);
    CHECK(first.Radiance == std::array<double, 3>{2.0, 0.0, 1.0});
    CHECK(first.Transmission[0] == 0.125);
    events[1] = events[0];
    events[1].Distance = 1.5;
    CHECK_THROWS_AS(ComposePlanetWeather(events, 2.0, vacuum), std::invalid_argument);
}

TEST_CASE("Planet weather invalid transport leaves accumulated radiance unchanged")
{
    using namespace Keire::Detail;
    PlanetWeatherTransport front{{1.0, 2.0, 3.0}, {0.5, 0.25, 0.125}};
    const auto baseline = front;
    PlanetWeatherTransport invalid;
    invalid.Radiance[2] = std::numeric_limits<double>::infinity();
    CHECK_THROWS_AS(AppendPlanetWeatherTransport(front, invalid), std::invalid_argument);
    CHECK(front.Radiance == baseline.Radiance);
    CHECK(front.Transmission == baseline.Transmission);
    invalid.Radiance[2] = 0.0;
    invalid.Transmission[1] = 1.5;
    CHECK_THROWS_AS(AppendPlanetWeatherTransport(front, invalid), std::invalid_argument);
    CHECK(front.Radiance == baseline.Radiance);
    CHECK(front.Transmission == baseline.Transmission);
}
