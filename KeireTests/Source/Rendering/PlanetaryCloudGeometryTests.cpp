#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace
{
    using uint = std::uint32_t;
    using std::isfinite;
    using std::max;
    using std::min;
    using std::sqrt;

#include "../../../KeireCore/Shaders/PlanetaryCloudIntersections.hlsli"
#include "../../../KeireCore/Shaders/PlanetaryCloudWeather.hlsli"
} // namespace

TEST_CASE("Planetary weather integer wrapping preserves modulo samples for all supported regional periods")
{
    const auto referenceHash = [](const uint x, const uint y, const uint z, const uint repeat, const uint seed)
    {
        uint value = (x % repeat) * 0x8da6b343U ^ (y % repeat) * 0xd8163841U ^ (z % repeat) * 0xcb1ab31fU ^ seed;
        value ^= value >> 16U;
        value *= 0x7feb352dU;
        value ^= value >> 15U;
        value *= 0x846ca68bU;
        return value ^ (value >> 16U);
    };
    for (uint period = 1U; period <= 64U; period *= 2U)
    {
        const uint repeat = 256U / period;
        for (const uint cell : {0U, 1U, repeat - 1U, repeat, repeat + 1U, std::numeric_limits<uint>::max()})
        {
            for (const uint seed : {0U, 1U, 0xa511e9b3U, 0x3912a51U, 0xffffffffU})
            {
                for (uint corner = 0U; corner < 8U; ++corner)
                {
                    const uint x = cell + (corner & 1U);
                    const uint y = repeat - 1U + ((corner >> 1U) & 1U);
                    const uint z = repeat + ((corner >> 2U) & 1U);
                    const uint expected = referenceHash(x, y, z, repeat, seed);
                    CHECK(PlanetaryCloudWeatherHash(x, y, z, repeat, seed) == expected);
                    CHECK(PlanetaryCloudWeatherHash(y, z, x, repeat, seed) == referenceHash(y, z, x, repeat, seed));
                    CHECK(PlanetaryCloudWeatherHash(z, x, y, repeat, seed) == referenceHash(z, x, y, repeat, seed));
                }
            }
        }
    }
}

TEST_CASE("Planetary cloud shells retain thin surface layers and orbital entry")
{
    const auto surface = IntersectPlanetaryCloudShell(6360000.0F, 0.125F, 1.0F, 1000.0F, 500.0F, 100000.0F);
    CHECK(surface.FirstEntry == doctest::Approx(999.875F));
    CHECK(surface.FirstExit == doctest::Approx(1499.875F));

    const auto orbit = IntersectPlanetaryCloudShell(6360000.0F, 1000000.0F, -1.0F, 1000.0F, 500.0F, 2000000.0F);
    CHECK(orbit.FirstEntry == doctest::Approx(998500.0F));
    CHECK(orbit.FirstExit == doctest::Approx(999000.0F));
    CHECK(orbit.SecondExit == orbit.SecondEntry);
}

TEST_CASE("Planetary clouds preserve two limb intervals without sampling the empty core")
{
    const float radius = 1000.0F;
    const float observerRadius = 2000.0F;
    const float impactRadius = 1050.0F;
    const float cosine = -std::sqrt(1.0F - impactRadius * impactRadius / (observerRadius * observerRadius));
    const auto shell = IntersectPlanetaryCloudShell(radius, observerRadius - radius, cosine, 100.0F, 100.0F, 5000.0F);
    CHECK(shell.FirstExit > shell.FirstEntry);
    CHECK(shell.SecondEntry > shell.FirstExit);
    CHECK(shell.SecondExit > shell.SecondEntry);
    CHECK(shell.FirstExit - shell.FirstEntry == doctest::Approx(shell.SecondExit - shell.SecondEntry));
}

TEST_CASE("Planetary clouds clip scene depth planet occlusion and rays outside the shell")
{
    const auto ground = IntersectPlanetaryCloudShell(1000.0F, 500.0F, -1.0F, 100.0F, 100.0F, 5000.0F);
    CHECK(ground.FirstEntry == doctest::Approx(300.0F));
    CHECK(ground.FirstExit == doctest::Approx(400.0F));
    CHECK(ground.SecondExit == ground.SecondEntry);
    CHECK(IntersectPlanetaryCloudShell(1000.0F, 500.0F, -1.0F, 100.0F, 100.0F, 200.0F).FirstExit == 0.0F);
    CHECK(IntersectPlanetaryCloudShell(1000.0F, 500.0F, 1.0F, 100.0F, 100.0F, 5000.0F).FirstExit == 0.0F);
    CHECK(IntersectPlanetaryCloudShell(1000.0F, -1.0F, 1.0F, 100.0F, 100.0F, 5000.0F).FirstExit == 0.0F);
    CHECK(IntersectPlanetaryCloudShell(1000.0F, 0.0F, -1.0F, 100.0F, 100.0F, 5000.0F).FirstExit == 0.0F);
}

TEST_CASE("Planetary clouds handle inside tangent and invalid geometry")
{
    const auto inside = IntersectPlanetaryCloudShell(6360000.0F, 1250.0F, 0.0F, 1000.0F, 500.0F, 100000.0F);
    CHECK(inside.FirstEntry == 0.0F);
    CHECK(inside.FirstExit > 50000.0F);
    CHECK(inside.FirstExit < 60000.0F);
    const float nan = std::numeric_limits<float>::quiet_NaN();
    CHECK_FALSE(PlanetaryCloudSphereRoots(1000.0F, nan, 1.0F, 100.0F).Hit);
    CHECK_FALSE(PlanetaryCloudSphereRoots(0.0F, 1.0F, 1.0F, 100.0F).Hit);
    CHECK(IntersectPlanetaryCloudShell(1000.0F, 0.0F, 1.0F, 100.0F, 0.0F, 10000.0F).FirstExit == 0.0F);
}

TEST_CASE("Planetary cloud shared sampling budget skips vacuum and preserves optical length")
{
    PlanetaryCloudIntervals intervals{1000000.0F, 1001000.0F, 2000000.0F, 2002000.0F};
    const auto limited = LimitPlanetaryCloudTravel(intervals, 1500.0F);
    CHECK(limited.FirstEntry == 1000000.0F);
    CHECK(limited.FirstExit == 1001000.0F);
    CHECK(limited.SecondEntry == 2000000.0F);
    CHECK(limited.SecondExit == 2000500.0F);
    float length = 0.0F;
    for (uint index = 0U; index < 48U; ++index)
    {
        const auto sample = PlanetaryCloudSample(limited, 48U, index);
        CHECK(sample.Exit >= sample.Entry);
        CHECK((sample.Exit <= limited.FirstExit || sample.Entry >= limited.SecondEntry));
        length += sample.Exit - sample.Entry;
    }
    CHECK(length == doctest::Approx(1500.0F));
}
