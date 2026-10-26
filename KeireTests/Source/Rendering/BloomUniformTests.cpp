#include "KeireInternal/Rendering/BloomUniforms.h"

#include <doctest/doctest.h>

#include <limits>
#include <stdexcept>

TEST_CASE("Bloom half extents match round-up graph scaling at small odd and maximum sizes")
{
    using Keire::RenderBackend::BloomHalfExtent;
    CHECK(BloomHalfExtent(0) == 0);
    CHECK(BloomHalfExtent(1) == 1);
    CHECK(BloomHalfExtent(2) == 1);
    CHECK(BloomHalfExtent(3) == 2);
    CHECK(BloomHalfExtent(1919) == 960);
    CHECK(BloomHalfExtent(std::numeric_limits<std::uint32_t>::max()) == 2147483648U);
}

TEST_CASE("Bloom uniforms pack the source footprint pass and symmetric kernel")
{
    using namespace Keire::RenderBackend;
    Keire::BloomSettings settings;
    settings.Enabled = true;
    const auto extract = BuildBloomUniforms(settings, 1920, 1080, BloomPass::Extract);
    CHECK(extract.Parameters.X == doctest::Approx(1.0 / 1920.0));
    CHECK(extract.Parameters.Y == doctest::Approx(1.0 / 1080.0));
    CHECK(extract.Parameters.Z == 1.0F);
    CHECK(extract.Parameters.W == 0.5F);
    CHECK(extract.Direction.X == 0.0F);
    CHECK(extract.Direction.Y == 0.0F);
    CHECK(extract.Direction.Z == 0.0F);
    CHECK(extract.Direction.W == 1.0F);
    const auto horizontal = BuildBloomUniforms(settings, 960, 540, BloomPass::Horizontal);
    const auto vertical = BuildBloomUniforms(settings, 960, 540, BloomPass::Vertical);
    CHECK(horizontal.Direction.X == doctest::Approx(1.0 / 960.0));
    CHECK(horizontal.Direction.Y == 0.0F);
    CHECK(horizontal.Direction.Z == 1.0F);
    CHECK(vertical.Direction.X == 0.0F);
    CHECK(vertical.Direction.Y == doctest::Approx(1.0 / 540.0));
    CHECK(vertical.Direction.Z == 2.0F);
    const auto reference = Keire::Detail::BuildBloomGaussianKernel(settings);
    CHECK(extract.Weights[0].X == static_cast<float>(reference[0]));
    CHECK(extract.Weights[3].X == static_cast<float>(reference[12]));
    CHECK(extract.Weights[6].X == static_cast<float>(reference[24]));
    CHECK(extract.Weights[6].Y == 0.0F);
    CHECK(extract.Weights[6].Z == 0.0F);
    CHECK(extract.Weights[6].W == 0.0F);
    settings.Enabled = false;
    CHECK(BuildBloomUniforms(settings, 1, 1, BloomPass::Extract).Direction.W == 0.0F);
    CHECK_THROWS_AS((void)BuildBloomUniforms(settings, 0, 1, BloomPass::Extract), std::invalid_argument);
    CHECK_THROWS_AS((void)BuildBloomUniforms(settings, 1, 0, BloomPass::Extract), std::invalid_argument);
    CHECK_THROWS_AS((void)BuildBloomUniforms(settings, 1, 1, static_cast<BloomPass>(3)), std::invalid_argument);
}
