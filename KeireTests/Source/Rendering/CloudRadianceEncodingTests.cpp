#include <doctest/doctest.h>

#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>

namespace
{
    using uint = std::uint32_t;
    using std::abs;
    using std::exp2;
    using std::log2;
    using std::round;

    struct float4
    {
        float r;
        float g;
        float b;
        float a;
    };

    uint asuint(float value) { return std::bit_cast<uint>(value); }

    float asfloat(uint value) { return std::bit_cast<float>(value); }

#include "../../../KeireCore/Shaders/CloudRadianceEncoding.hlsli"
} // namespace

TEST_CASE("Cloud history preserves dark and bright radiance without changing opacity")
{
    CHECK(DecodeCloudRadianceChannel(EncodeCloudRadianceChannel(0.0F)) == 0.0F);
    CHECK(DecodeCloudRadianceChannel(EncodeCloudRadianceChannel(std::numeric_limits<float>::max())) ==
          std::numeric_limits<float>::max());

    for (int index = 0; index <= 10000; ++index)
    {
        const float value = std::exp2(-26.575424759F + 53.150849518F * static_cast<float>(index) / 10000.0F);
        const float4 original{value, value * 0.5F, value * 0.25F, 0.375F};
        const float4 restored = DecodeCloudRadiance(EncodeCloudRadiance(original));
        CHECK(std::isfinite(restored.r));
        CHECK(restored.r > 0.0F);
        CHECK(std::abs(restored.r - original.r) <= original.r * 0.006F);
        CHECK(std::abs(restored.g - original.g) <= original.g * 0.006F);
        CHECK(std::abs(restored.b - original.b) <= original.b * 0.006F);
        CHECK(restored.a == original.a);
    }
}

TEST_CASE("Cloud history half quantization rounds ties to even for both signs")
{
    CHECK(RoundCloudLogToHalf(1.00048828125F) == 1.0F);
    CHECK(RoundCloudLogToHalf(1.00146484375F) == 1.001953125F);
    CHECK(RoundCloudLogToHalf(-1.00048828125F) == -1.0F);
    CHECK(RoundCloudLogToHalf(-1.00146484375F) == -1.001953125F);
}
