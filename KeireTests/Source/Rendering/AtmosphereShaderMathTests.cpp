#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <optional>

namespace
{
    // Compile the scalar shader kernel itself; backend execution is still separately required.
    using std::exp;
#include "../../../KeireCore/Shaders/VolumeTransport.hlsli"

    // Float reference for BuiltinSky.hlsl, not a claim of executing a backend shader in this test.
    [[nodiscard]] std::optional<std::array<float, 2>> SphereInterval(const float originRadius, const float along,
                                                                     const float radius)
    {
        const float constant = (originRadius - radius) * (originRadius + radius);
        const float discriminant = std::fma(along, along, -constant);
        const float roundoff = 4.0F * 1.192092896e-7F * std::max(along * along, std::abs(constant));
        if (discriminant < -roundoff)
        {
            return std::nullopt;
        }
        const float root = std::sqrt(std::max(discriminant, 0.0F));
        const float stableRoot = -along - (along >= 0.0F ? root : -root);
        const float otherRoot = stableRoot != 0.0F ? constant / stableRoot : 0.0F;
        const std::array interval{std::min(stableRoot, otherRoot), std::max(stableRoot, otherRoot)};
        return interval[1] >= 0.0F ? std::optional{interval} : std::nullopt;
    }

    [[nodiscard]] float SunCoverage(const float radius, const float distance, const float halfPixel)
    {
        const float filteredRadius = std::max(radius, halfPixel);
        const float areaRatio = radius / filteredRadius;
        const float fraction = std::clamp((distance - filteredRadius + halfPixel) / (2.0F * halfPixel), 0.0F, 1.0F);
        const float smoothed = fraction * fraction * (3.0F - 2.0F * fraction);
        return (1.0F - smoothed) * areaRatio * areaRatio;
    }
} // namespace

TEST_CASE("Atmosphere segment integration preserves thin and optically thick limits")
{
    CHECK(VolumeSegmentWeight(0.0F) == 1.0F);
    float previous = 1.0F;
    for (const float depth : {1.0e-9F, 1.0e-6F, 0.001F, 0.009999F, 0.01F, 0.010001F, 0.1F, 1.0F, 10.0F, 100.0F})
    {
        const double expected = -std::expm1(-static_cast<double>(depth)) / depth;
        const float actual = VolumeSegmentWeight(depth);
        CHECK(std::isfinite(actual));
        CHECK(actual > 0.0F);
        CHECK(actual <= previous);
        CHECK(actual == doctest::Approx(expected).epsilon(4.0e-6).scale(0.0));
        previous = actual;
    }
}

TEST_CASE("Atmosphere homogeneous segment radiance is stable across sample budgets")
{
    for (const float depth : {0.0F, 1.0e-6F, 0.01F, 0.1F, 1.0F, 10.0F, 100.0F})
    {
        const double expected = depth == 0.0F ? 1.0 : -std::expm1(-static_cast<double>(depth)) / depth;
        for (const int samples : {1, 2, 8, 24, 64, 128})
        {
            const float segmentDepth = depth / static_cast<float>(samples);
            float radiance = 0.0F;
            for (int index = 0; index < samples; ++index)
            {
                radiance += std::exp(-segmentDepth * static_cast<float>(index)) * VolumeSegmentWeight(segmentDepth) /
                            static_cast<float>(samples);
            }
            CHECK(radiance == doctest::Approx(expected).epsilon(8.0e-6).scale(0.0));
        }
    }
}

TEST_CASE("Atmosphere float surface roots cannot cross behind an inward observer")
{
    constexpr float radius = 6360.0F;
    for (const float vertical : {-1.0F, -0.9F, -0.7F, -0.5F, -0.3F, -0.001F, -0.0001F})
    {
        const auto interval = SphereInterval(radius, radius * vertical, radius);
        REQUIRE(interval);
        CHECK((*interval)[0] == 0.0F);
        CHECK((*interval)[1] > 0.0F);
    }
    const auto outward = SphereInterval(radius, radius, radius);
    REQUIRE(outward);
    CHECK((*outward)[0] < 0.0F);
    const auto tangent = SphereInterval(radius, 0.0F, radius);
    REQUIRE(tangent);
    CHECK((*tangent)[0] == 0.0F);
    // Ground classification additionally requires dot(origin, direction) < 0, so the tangent remains visible.
    const auto elevatedInward = SphereInterval(radius + 1.0F, -(radius + 1.0F), radius);
    REQUIRE(elevatedInward);
    CHECK((*elevatedInward)[0] == doctest::Approx(1.0F));
    CHECK_FALSE(SphereInterval(radius + 1.0F, 0.0F, radius));
}

TEST_CASE("Atmosphere solar solid angle retains the supported minimum radius")
{
    for (const float radius : {0.00001F, 0.0001F, 0.004675F, 0.05F})
    {
        const float sineHalf = std::sin(radius * 0.5F);
        const float area = 4.0F * std::numbers::pi_v<float> * sineHalf * sineHalf;
        const double expected = 4.0 * std::numbers::pi * std::pow(std::sin(static_cast<double>(radius) * 0.5), 2.0);
        CHECK(area > 0.0F);
        CHECK(std::isfinite(20.0F / area));
        CHECK(area == doctest::Approx(expected).epsilon(1.0e-6).scale(0.0));
    }
}

TEST_CASE("Atmosphere solar edge coverage is smooth and unresolved discs retain area scaling")
{
    constexpr float radius = 0.004675F;
    constexpr float halfPixel = 0.0005F;
    CHECK(SunCoverage(radius, 0.0F, halfPixel) == 1.0F);
    CHECK(SunCoverage(radius, radius, halfPixel) == doctest::Approx(0.5F));
    CHECK(SunCoverage(radius, radius + 2.0F * halfPixel, halfPixel) == 0.0F);
    CHECK(SunCoverage(radius, radius - halfPixel * 0.5F, halfPixel) >
          SunCoverage(radius, radius + halfPixel * 0.5F, halfPixel));
    const float tiny = SunCoverage(0.00001F, 0.0F, halfPixel);
    const float doubled = SunCoverage(0.00002F, 0.0F, halfPixel);
    CHECK(tiny > 0.0F);
    CHECK(tiny < 0.001F);
    CHECK(doubled == doctest::Approx(4.0F * tiny));
}
