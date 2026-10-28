#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <initializer_list>

namespace
{
    using std::max;
    using std::min;

    float saturate(const float value) { return std::clamp(value, 0.0F, 1.0F); }

    float smoothstep(const float minimum, const float maximum, const float value)
    {
        const float fraction = saturate((value - minimum) / (maximum - minimum));
        return fraction * fraction * (3.0F - 2.0F * fraction);
    }

#include "../../../KeireCore/Shaders/CloudVolumeDensity.hlsli"
} // namespace

TEST_CASE("Independent cloud erosion distinguishes explicit zero from legacy coupled uniforms")
{
    for (const float detail : {0.0F, 0.2F, 0.7F, 1.0F})
    {
        CHECK(VolumeCloudErosionAmount(0.0F, detail) == detail);
        CHECK(VolumeCloudErosionAmount(1.0F, detail) == 0.0F);
        CHECK(VolumeCloudErosionAmount(1.25F, detail) == 0.25F);
        CHECK(VolumeCloudErosionAmount(2.0F, detail) == 1.0F);
    }
}

TEST_CASE("Regional cloud coverage retains endpoints and monotonic coverage authoring")
{
    for (const float strength : {0.0F, 0.5F, 1.0F})
    {
        for (const float weather : {0.0F, 0.25F, 0.5F, 0.75F, 1.0F})
        {
            CHECK(VolumeCloudRegionalCoverage(0.0F, weather, strength) == 0.0F);
            CHECK(VolumeCloudRegionalCoverage(1.0F, weather, strength) == 1.0F);
            float previous = 0.0F;
            for (int step = 0; step <= 1000; ++step)
            {
                const float coverage = static_cast<float>(step) / 1000.0F;
                const float value = VolumeCloudRegionalCoverage(coverage, weather, strength);
                CHECK(std::isfinite(value));
                CHECK(value >= previous - 0.000001F);
                CHECK(value >= 0.0F);
                CHECK(value <= 1.0F);
                if (strength == 0.0F || weather == 0.5F)
                {
                    CHECK(value == coverage);
                }
                previous = value;
            }
        }
    }
}

TEST_CASE("Cloud coordinate displacement is centered and bounded")
{
    CHECK(VolumeCloudWarpOffset(0.5F) == 0.0F);
    CHECK(VolumeCloudWarpOffset(-1.0F) == -0.375F);
    CHECK(VolumeCloudWarpOffset(2.0F) == 0.375F);
    for (int index = 0; index <= 100; ++index)
    {
        const float value = static_cast<float>(index) / 100.0F;
        CHECK(VolumeCloudWarpOffset(value) == doctest::Approx(-VolumeCloudWarpOffset(1.0F - value)));
    }
}

TEST_CASE("Cloud filtering scales conservatively with displacement strength and frequency")
{
    CHECK(VolumeCloudStretchBound(0.0F, 1.0F) == 1.0F);
    CHECK(VolumeCloudStretchBound(0.75F, 64.0F) == doctest::Approx(1.4F));
    for (const float period : {1.0F, 2.0F, 4.0F, 8.0F, 16.0F, 32.0F, 64.0F})
    {
        float previous = 1.0F;
        for (int index = 0; index <= 100; ++index)
        {
            const float strength = static_cast<float>(index) / 100.0F;
            const float bound = VolumeCloudStretchBound(strength, period);
            // The exhaustive atlas test bounds the reference displacement derivative by 0.4.
            const float referenceBound = 1.0F + 0.4F * strength / 0.75F * 64.0F / period;
            CHECK(std::isfinite(bound));
            CHECK(bound >= previous);
            CHECK(bound == doctest::Approx(referenceBound));
            previous = bound;
        }
    }
}

TEST_CASE("Cloud detail fades continuously as march spacing exceeds its cell size")
{
    CHECK(VolumeCloudBandWeight(0.0F, 100.0F) == 1.0F);
    CHECK(VolumeCloudBandWeight(25.0F, 100.0F) == 1.0F);
    CHECK(VolumeCloudBandWeight(50.0F, 100.0F) == doctest::Approx(0.5F));
    CHECK(VolumeCloudBandWeight(75.0F, 100.0F) == 0.0F);
    CHECK(VolumeCloudBandWeight(10000.0F, 100.0F) == 0.0F);
    float previous = 1.0F;
    for (int index = 0; index <= 1000; ++index)
    {
        const float weight = VolumeCloudBandWeight(static_cast<float>(index) * 0.1F, 100.0F);
        CHECK(std::isfinite(weight));
        CHECK(weight >= 0.0F);
        CHECK(weight <= previous);
        CHECK(previous - weight < 0.004F);
        previous = weight;
    }
}

TEST_CASE("Cloud erosion resolves fine structure and returns continuously to coarse erosion")
{
    CHECK(VolumeCloudErosion(0.2F, 0.8F, 0.0F, 400.0F) == doctest::Approx(0.8F));
    CHECK(VolumeCloudErosion(0.2F, 0.8F, 50.0F, 400.0F) == doctest::Approx(0.5F));
    CHECK(VolumeCloudErosion(0.2F, 0.8F, 75.0F, 400.0F) == doctest::Approx(0.2F));
    CHECK(VolumeCloudErosion(0.2F, 0.0F, 1000.0F, 400.0F) == VolumeCloudErosion(0.2F, 1.0F, 1000.0F, 400.0F));
    float previous = 0.8F;
    for (int index = 0; index <= 1000; ++index)
    {
        const float erosion = VolumeCloudErosion(0.2F, 0.8F, static_cast<float>(index) * 0.1F, 400.0F);
        CHECK(std::isfinite(erosion));
        CHECK(erosion >= 0.2F);
        CHECK(erosion <= previous);
        CHECK(previous - erosion < 0.003F);
        previous = erosion;
    }
}

TEST_CASE("Cloud density is bounded with empty coverage and empty slab boundaries")
{
    for (const float shape : {0.0F, 0.5F, 1.0F})
    {
        for (const float secondary : {0.0F, 0.5F, 1.0F})
        {
            for (const float coverage : {0.0F, 0.00001F, 0.6F, 1.0F})
            {
                for (const float height : {-1.0F, 0.0F, 0.075F, 0.5F, 0.85F, 1.0F, 2.0F})
                {
                    const float density =
                        VolumeCloudDensityFromSamples(shape, secondary, 1.0F, 0.5F, coverage, 0.2F, height);
                    CHECK(std::isfinite(density));
                    CHECK(density >= 0.0F);
                    CHECK(density <= 1.0F);
                    if (coverage == 0.0F || height <= 0.0F || height >= 1.0F)
                    {
                        CHECK(density == 0.0F);
                    }
                }
            }
        }
    }
}

TEST_CASE("Cloud erosion retains dense cores and removes thin regions without adding density")
{
    for (const float amount : {0.0F, 0.2F, 0.9F})
    {
        CHECK(VolumeCloudDensityFromSamples(1.0F, 0.5F, 0.0F, 1.0F, 1.0F, amount, 0.5F) == doctest::Approx(1.0F));
        float previous = 0.0F;
        for (int index = 0; index <= 100; ++index)
        {
            const float shape = static_cast<float>(index) / 100.0F;
            const float density = VolumeCloudDensityFromSamples(shape, 0.5F, 0.0F, 1.0F, 1.0F, amount, 0.5F);
            CHECK(std::isfinite(density));
            CHECK(density >= previous);
            CHECK(density <= shape + 0.00001F);
            if (shape <= amount)
            {
                CHECK(density == 0.0F);
            }
            previous = density;
        }
    }
    CHECK(VolumeCloudDensityFromSamples(1.0F, 0.5F, 0.0F, 1.0F, 1.0F, 1.0F, 0.5F) == 0.0F);
}

TEST_CASE("Cloud secondary shape disappears when unresolved and erosion does not create density")
{
    const float coarse = VolumeCloudDensityFromSamples(0.7F, 0.0F, 0.0F, 0.5F, 0.6F, 0.2F, 0.5F);
    CHECK(coarse == VolumeCloudDensityFromSamples(0.7F, 1.0F, 0.0F, 0.5F, 0.6F, 0.2F, 0.5F));
    CHECK(coarse == doctest::Approx(0.4F / 0.9F));
    CHECK(VolumeCloudDensityFromSamples(0.7F, 0.0F, 1.0F, 0.5F, 0.6F, 0.2F, 0.5F) < coarse);
    CHECK(VolumeCloudDensityFromSamples(0.7F, 1.0F, 1.0F, 0.5F, 0.6F, 0.2F, 0.5F) > coarse);
    for (const float amount : {0.0F, 0.2F, 0.5F, 1.0F})
    {
        const float body = VolumeCloudDensityFromSamples(0.7F, 0.5F, 1.0F, 0.5F, 0.6F, amount, 0.5F);
        CHECK(body <= 0.5F);
        CHECK(body >= 0.0F);
    }
}

TEST_CASE("Cloud vertical profile tapers occupied bodies instead of only fading opacity")
{
    const auto density = [](const float shape, const float height)
    { return VolumeCloudDensityFromSamples(shape, 0.5F, 0.0F, 0.0F, 0.6F, 0.0F, height); };

    // The body exists in the middle but its thin regions do not extend to the base and crown.
    CHECK(density(0.7F, 0.5F) > 0.0F);
    CHECK(density(0.7F, 0.075F) == 0.0F);
    CHECK(density(0.7F, 0.85F) == 0.0F);
    CHECK(density(1.0F, 0.075F) > 0.0F);
    CHECK(density(1.0F, 0.85F) > 0.0F);

    for (const float shape : {0.0F, 0.4F, 0.7F, 1.0F})
    {
        float previous = density(shape, 0.7F);
        for (int index = 1; index <= 1000; ++index)
        {
            const float height = 0.7F + 0.3F * static_cast<float>(index) / 1000.0F;
            const float current = density(shape, height);
            CHECK(std::isfinite(current));
            CHECK(current <= previous + 0.000001F);
            CHECK(previous - current < 0.003F);
            previous = current;
        }
        CHECK(previous == 0.0F);
    }
}

TEST_CASE("Cloud height profiles support thin layers and sharp authored boundaries")
{
    CHECK(VolumeCloudHeightProfile(0.5F, 0.1F, 0.2F, 0.3F, 0.4F) == 0.0F);
    CHECK(VolumeCloudHeightProfile(0.25F, 0.1F, 0.2F, 0.3F, 0.4F) == 1.0F);
    CHECK(VolumeCloudHeightProfile(0.15F, 0.1F, 0.2F, 0.3F, 0.4F) == doctest::Approx(0.5F));
    CHECK(VolumeCloudHeightProfile(0.35F, 0.1F, 0.2F, 0.3F, 0.4F) == doctest::Approx(0.5F));
    CHECK(VolumeCloudHeightProfile(0.5F, 0.2F, 0.2F, 0.8F, 0.8F) == 1.0F);
    CHECK(VolumeCloudHeightProfile(0.2F, 0.2F, 0.2F, 0.8F, 0.8F) == 0.0F);
    CHECK(VolumeCloudHeightProfile(0.8F, 0.2F, 0.2F, 0.8F, 0.8F) == 0.0F);
}

TEST_CASE("Independent cloud profiles retain legacy density and bound authored envelopes")
{
    for (int index = 0; index <= 100; ++index)
    {
        const float height = static_cast<float>(index) / 100.0F;
        const float original = smoothstep(0.0F, 0.15F, height) * (1.0F - smoothstep(0.7F, 1.0F, height));
        CHECK(VolumeCloudHeightProfile(height, 0.0F, 0.15F, 0.7F, 1.0F) == original);
        for (const float coverage : {0.0F, 0.3F, 0.6F, 1.0F})
        {
            CHECK(VolumeCloudDensityWithProfile(0.7F, 0.6F, 0.2F, 0.3F, coverage, 0.2F, original) ==
                  VolumeCloudDensityFromSamples(0.7F, 0.6F, 0.2F, 0.3F, coverage, 0.2F, height));
        }
    }
    CHECK(VolumeCloudDensityWithProfile(1.0F, 0.5F, 0.0F, 0.0F, 1.0F, 0.0F, -1.0F) == 0.0F);
    CHECK(VolumeCloudDensityWithProfile(1.0F, 0.5F, 0.0F, 0.0F, 1.0F, 0.0F, 2.0F) == 1.0F);
}

TEST_CASE("Collapsed authored cloud profiles never expand into legacy layers")
{
    const float base = static_cast<float>(0.5);
    const float crown = static_cast<float>(0.5000000001);
    REQUIRE(base == crown);
    for (int index = 0; index <= 100; ++index)
    {
        const float height = static_cast<float>(index) / 100.0F;
        CHECK(VolumeCloudAuthoredHeightProfile(height, base, base, crown, crown) == 0.0F);
        CHECK(VolumeCloudAuthoredHeightProfile(height, 0.0F, 0.0F, 0.0F, 0.0F) ==
              VolumeCloudHeightProfile(height, 0.0F, 0.15F, 0.7F, 1.0F));
        CHECK(VolumeCloudAuthoredHeightProfile(height, 0.1F, 0.2F, 0.3F, 0.4F) ==
              VolumeCloudHeightProfile(height, 0.1F, 0.2F, 0.3F, 0.4F));
    }
}

TEST_CASE("Empty cloud coverage or envelope is independent of skipped atlas samples")
{
    for (const float shape : {0.0F, 0.5F, 1.0F})
    {
        for (const float detail : {0.0F, 0.5F, 1.0F})
        {
            for (const float erosion : {0.0F, 0.5F, 1.0F})
            {
                CHECK(VolumeCloudDensityWithProfile(shape, detail, 1.0F, erosion, 0.0F, 1.0F, 1.0F) == 0.0F);
                CHECK(VolumeCloudDensityWithProfile(shape, detail, 1.0F, erosion, 1.0F, 1.0F, 0.0F) == 0.0F);
            }
        }
    }
}
