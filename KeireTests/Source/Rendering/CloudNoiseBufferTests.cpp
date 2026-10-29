#include "KeireInternal/Rendering/CloudNoiseBuffer.h"

#include <doctest/doctest.h>

#include <cstdint>
#include <stdexcept>
#include <vector>

namespace
{
    Keire::RenderBackend::CloudNoiseVolume SmallNoiseVolume()
    {
        return {{{4, std::vector<std::uint8_t>(256, 0)}, {2, std::vector<std::uint8_t>(32, 128)}, {1, {1, 2, 3, 255}}}};
    }
} // namespace

TEST_CASE("Cloud noise buffer preserves quantized mip data and explicit offsets")
{
    auto volume = SmallNoiseVolume();
    volume.Mips[0].Rgba[0] = 17;
    volume.Mips[0].Rgba[255] = 254;
    const auto words = Keire::RenderBackend::PackCloudNoiseBuffer(volume);
    REQUIRE(words.size() == 89);
    CHECK(words[0] == 4);
    CHECK(words[1] == 3);
    CHECK(words[4] == 4);
    CHECK(words[5] == 16);
    CHECK(words[8] == 2);
    CHECK(words[9] == 80);
    CHECK(words[12] == 1);
    CHECK(words[13] == 88);
    CHECK(words[16] == 17);
    CHECK(words[79] == 0xFE000000U);
    CHECK(words[80] == 0x80808080U);
    CHECK(words[88] == 0xFF030201U);
    CHECK(Keire::RenderBackend::PackCloudNoiseBuffer(volume) == words);
}

TEST_CASE("Cloud noise buffer rejects incomplete and malformed mip chains")
{
    using Keire::RenderBackend::PackCloudNoiseBuffer;
    CHECK_THROWS_AS((void)PackCloudNoiseBuffer({}), std::invalid_argument);
    auto volume = SmallNoiseVolume();
    volume.Mips.pop_back();
    CHECK_THROWS_AS((void)PackCloudNoiseBuffer(volume), std::invalid_argument);
    volume = SmallNoiseVolume();
    volume.Mips[1].Dimension = 3;
    CHECK_THROWS_AS((void)PackCloudNoiseBuffer(volume), std::invalid_argument);
    volume = SmallNoiseVolume();
    volume.Mips[0].Rgba.pop_back();
    CHECK_THROWS_AS((void)PackCloudNoiseBuffer(volume), std::invalid_argument);
    volume = SmallNoiseVolume();
    volume.Mips.push_back({1, {0, 0, 0, 0}});
    CHECK_THROWS_AS((void)PackCloudNoiseBuffer(volume), std::invalid_argument);
    volume = SmallNoiseVolume();
    volume.Mips[0].Dimension = 128;
    CHECK_THROWS_AS((void)PackCloudNoiseBuffer(volume), std::invalid_argument);
}
