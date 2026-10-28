#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>

namespace
{
    using std::clamp;
    using std::pow;

#include "../../../KeireCore/Shaders/DisplayTransfer.hlsli"
} // namespace

TEST_CASE("SDR display transfer encodes mid gray without changing endpoints")
{
    CHECK(EncodeDisplaySrgb(0.0F) == 0.0F);
    CHECK(EncodeDisplaySrgb(1.0F) == doctest::Approx(1.0F));
    CHECK(EncodeDisplaySrgb(0.18F) == doctest::Approx(0.461356F));
    CHECK(EncodeDisplaySrgb(-1.0F) == 0.0F);
    CHECK(EncodeDisplaySrgb(2.0F) == doctest::Approx(1.0F));
}

TEST_CASE("SDR transfer is monotonic and round trips linear scene values")
{
    float previous = 0.0F;
    for (int index = 0; index <= 10000; ++index)
    {
        const float linearValue = static_cast<float>(index) / 10000.0F;
        const float encoded = EncodeDisplaySrgb(linearValue);
        CHECK(encoded >= previous);
        const float decoded = encoded <= 0.04045F ? encoded / 12.92F : std::pow((encoded + 0.055F) / 1.055F, 2.4F);
        CHECK(decoded == doctest::Approx(linearValue).epsilon(0.00001));
        previous = encoded;
    }
}

TEST_CASE("SDR dithering preserves sub-code dark gradients after eight-bit quantization")
{
    constexpr int Samples = 4096;
    for (const float encoded : {0.0F, 0.001F, 0.015F, 0.031F, 0.052F, 0.5F, 1.0F})
    {
        const float linear = DecodeDisplaySrgb(encoded);
        double total = 0.0;
        for (int index = 0; index < Samples; ++index)
        {
            const float noise = (static_cast<float>(index) + 0.5F) / Samples - 0.5F;
            const float dithered = DitherDisplaySrgb(linear, noise);
            REQUIRE(dithered >= 0.0F);
            REQUIRE(dithered <= 1.0F);
            const float display = dithered;
            CHECK(std::abs(display - encoded) <= 0.5F / 255.0F + 0.000001F);
            total += std::round(display * 255.0F) / 255.0;
        }
        CHECK(total / Samples == doctest::Approx(encoded).epsilon(0.000001).scale(1.0));
    }
}
