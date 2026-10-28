#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <limits>

namespace
{
    using uint = std::uint32_t;
    using std::clamp;
    using std::floor;
    using std::max;

    float saturate(const float value) { return std::clamp(value, 0.0F, 1.0F); }

    float smoothstep(const float minimum, const float maximum, const float value)
    {
        const float fraction = saturate((value - minimum) / (maximum - minimum));
        return fraction * fraction * (3.0F - 2.0F * fraction);
    }

#include "../../../KeireCore/Shaders/CloudWeather.hlsli"
} // namespace

TEST_CASE("Cloud weather hash preserves signed coordinate wrapping for every supported octave period")
{
    const auto referenceHash = [](const int x, const int z, const int period, const uint seed)
    {
        const auto wrap = [period](const int coordinate)
        {
            const auto remainder = static_cast<std::int64_t>(coordinate) % period;
            return static_cast<uint>((remainder + period) % period);
        };
        uint value = wrap(x) * 0x8da6b343U ^ wrap(z) * 0xcb1ab31fU ^ seed;
        value ^= value >> 16U;
        value *= 0x7feb352dU;
        value ^= value >> 15U;
        value *= 0x846ca68bU;
        return value ^ (value >> 16U);
    };

    for (int period = 4; period <= 1024; period *= 2)
    {
        for (const int coordinate : {std::numeric_limits<int>::min(), -1000001, -1025, -1024, -1, 0, 1, 1023, 1024,
                                     1000001, std::numeric_limits<int>::max()})
        {
            for (const uint seed : {0U, 1U, 0xffffffffU})
            {
                CHECK(VolumeCloudWeatherHash(coordinate, 17, period, seed) ==
                      referenceHash(coordinate, 17, period, seed));
                CHECK(VolumeCloudWeatherHash(-17, coordinate, period, seed) ==
                      referenceHash(-17, coordinate, period, seed));
            }
        }
    }
}

TEST_CASE("Independent cloud weather repeats continuously across the host coordinate wrap")
{
    for (const float size : {1.0F, 2.0F, 4.0F, 8.0F, 16.0F, 32.0F, 64.0F})
    {
        for (const uint seed : {0U, 1U, 17U, 0xffffffffU})
        {
            for (const float coordinate : {-256.25F, -0.25F, 0.0F, 0.25F, 255.75F})
            {
                const float original = VolumeCloudWeatherField(coordinate, 1.125F, 0.0F, size, seed);
                CHECK(std::isfinite(original));
                CHECK(original >= 0.0F);
                CHECK(original <= 1.0F);
                CHECK(VolumeCloudWeatherField(coordinate + 256.0F, 1.125F, 0.0F, size, seed) == original);
                CHECK(VolumeCloudWeatherField(coordinate, 257.125F, 0.0F, size, seed) == original);
            }
            const float before = VolumeCloudWeatherField(-0.0001F, 3.0F, 0.0F, size, seed);
            const float after = VolumeCloudWeatherField(0.0001F, 3.0F, 0.0F, size, seed);
            CHECK(std::abs(before - after) < 0.00001F);
        }
    }
}

TEST_CASE("Cloud weather preserves independent seeds and fades unresolved detail to the mean")
{
    double difference = 0.0;
    double variation = 0.0;
    for (int index = 0; index < 256; ++index)
    {
        const float coordinate = static_cast<float>(index) * 0.125F;
        const float coverage = VolumeCloudWeatherField(coordinate, 2.75F, 0.0F, 8.0F, 1U);
        const float type = VolumeCloudWeatherField(coordinate, 2.75F, 0.0F, 8.0F, 0xa511e9b3U);
        difference += std::abs(coverage - type);
        variation += std::abs(coverage - 0.5F);
        CHECK(VolumeCloudWeatherField(coordinate, 2.75F, 8.0F, 8.0F, 1U) == 0.5F);
        CHECK(VolumeCloudWeatherField(coordinate, 2.75F, -1.0F, 8.0F, 1U) == coverage);
    }
    CHECK(difference / 256.0 > 0.05);
    CHECK(variation / 256.0 > 0.05);
}

TEST_CASE("Regional cloud bases vary continuously within the authored displacement")
{
    float minimumOffset = 1000.0F;
    float maximumOffset = -1000.0F;
    for (int index = -128; index <= 128; ++index)
    {
        const float x = static_cast<float>(index) * 0.25F;
        const float offset = VolumeCloudWeatherBaseOffset(x, 1.5F, 8.0F, 1U, 750.0F);
        CHECK(std::isfinite(offset));
        CHECK(offset >= -750.0F);
        CHECK(offset <= 750.0F);
        CHECK(VolumeCloudWeatherBaseOffset(x + 256.0F, 1.5F, 8.0F, 1U, 750.0F) == offset);
        CHECK(VolumeCloudWeatherBaseOffset(x, 1.5F, 8.0F, 1U, 0.0F) == 0.0F);
        minimumOffset = std::min(minimumOffset, offset);
        maximumOffset = std::max(maximumOffset, offset);
    }
    CHECK(minimumOffset < 0.0F);
    CHECK(maximumOffset > 0.0F);
    const float before = VolumeCloudWeatherBaseOffset(-0.0001F, 1.5F, 8.0F, 1U, 750.0F);
    const float after = VolumeCloudWeatherBaseOffset(0.0001F, 1.5F, 8.0F, 1U, 750.0F);
    CHECK(std::abs(before - after) < 0.1F);
}

TEST_CASE("Independent weather coverage preserves endpoints and increases monotonically")
{
    for (int index = 0; index <= 100; ++index)
    {
        const float weather = static_cast<float>(index) / 100.0F;
        CHECK(VolumeCloudWeatherCoverage(0.0F, weather, 1.0F) == 0.0F);
        CHECK(VolumeCloudWeatherCoverage(1.0F, weather, 1.0F) == doctest::Approx(1.0F));
        float previous = 0.0F;
        for (int step = 0; step <= 100; ++step)
        {
            const float coverage = static_cast<float>(step) / 100.0F;
            CHECK(VolumeCloudWeatherCoverage(coverage, weather, 0.0F) == coverage);
            const float value = VolumeCloudWeatherCoverage(coverage, weather, 1.0F);
            CHECK(value >= previous);
            CHECK(value >= 0.0F);
            CHECK(value <= 1.0F);
            previous = value;
        }
    }
}

TEST_CASE("Independent weather produces clear regions and dense banks in the spatial field")
{
    unsigned clear = 0;
    unsigned dense = 0;
    unsigned transition = 0;
    for (int z = 0; z < 128; ++z)
    {
        for (int x = 0; x < 128; ++x)
        {
            const float worldX = static_cast<float>(x) * 0.5F;
            const float worldZ = static_cast<float>(z) * 0.5F;
            const float weather = VolumeCloudWeatherField(worldX, worldZ, 0.0F, 8.0F, 1U);
            const float coverage = VolumeCloudWeatherCoverage(0.6F, weather, 1.0F);
            clear += coverage == 0.0F;
            dense += coverage == 1.0F;
            transition += coverage > 0.0F && coverage < 1.0F;
            const float nearbyWeather = VolumeCloudWeatherField(worldX + 0.0001F, worldZ, 0.0F, 8.0F, 1U);
            CHECK(std::abs(coverage - VolumeCloudWeatherCoverage(0.6F, nearbyWeather, 1.0F)) < 0.001F);
        }
    }
    CHECK(clear > 100);
    CHECK(dense > 100);
    CHECK(transition > 100);
}
