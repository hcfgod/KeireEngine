#include "KeireInternal/Rendering/CloudHistoryCompositeBlend.h"

#include <doctest/doctest.h>

#include <array>
#include <stdexcept>

namespace
{
    using Rgba = std::array<double, 4>;

    double BlendFactor(const SDL_GPUBlendFactor factor, const Rgba& source, const std::size_t channel)
    {
        switch (factor)
        {
        case SDL_GPU_BLENDFACTOR_ZERO:
            return 0.0;
        case SDL_GPU_BLENDFACTOR_ONE:
            return 1.0;
        case SDL_GPU_BLENDFACTOR_SRC_COLOR:
            return source[channel];
        case SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA:
            return 1.0 - source[3];
        default:
            throw std::invalid_argument("Unexpected factor in cloud blend contract.");
        }
    }

    Rgba ApplyBlend(const SDL_GPUColorTargetBlendState& state, const Rgba& source, const Rgba& destination)
    {
        REQUIRE(state.enable_blend);
        REQUIRE(state.color_blend_op == SDL_GPU_BLENDOP_ADD);
        REQUIRE(state.alpha_blend_op == SDL_GPU_BLENDOP_ADD);
        Rgba result{};
        for (std::size_t channel = 0; channel < 4; ++channel)
        {
            const bool alpha = channel == 3;
            const auto sourceFactor = alpha ? state.src_alpha_blendfactor : state.src_color_blendfactor;
            const auto destinationFactor = alpha ? state.dst_alpha_blendfactor : state.dst_color_blendfactor;
            result[channel] = source[channel] * BlendFactor(sourceFactor, source, channel) +
                              destination[channel] * BlendFactor(destinationFactor, source, channel);
        }
        return result;
    }
} // namespace

TEST_CASE("cloud history blend preserves different MSAA edge samples with colored attenuation")
{
    using namespace Keire::RenderBackend;
    const auto attenuate = CloudHistoryCompositeBlend(CloudHistoryCompositeMode::Attenuation);
    const auto radiance = CloudHistoryCompositeBlend(CloudHistoryCompositeMode::Radiance);
    const Rgba transmission{0.2, 0.4, 0.6, 0.0};
    const Rgba medium{1.0, 2.0, 3.0, 0.3};
    const Rgba first{10.0, 0.0, 0.0, 0.2};
    const Rgba second{0.0, 0.0, 12.0, 0.8};
    const auto firstAttenuated = ApplyBlend(attenuate, transmission, first);
    const auto secondAttenuated = ApplyBlend(attenuate, transmission, second);
    CHECK(firstAttenuated[3] == first[3]);
    CHECK(secondAttenuated[3] == second[3]);
    const auto firstResult = ApplyBlend(radiance, medium, firstAttenuated);
    const auto secondResult = ApplyBlend(radiance, medium, secondAttenuated);
    const Rgba expectedFirst{3.0, 2.0, 3.0, 0.44};
    const Rgba expectedSecond{1.0, 2.0, 10.2, 0.86};
    for (std::size_t channel = 0; channel < 4; ++channel)
    {
        CHECK(firstResult[channel] == doctest::Approx(expectedFirst[channel]));
        CHECK(secondResult[channel] == doctest::Approx(expectedSecond[channel]));
    }
    CHECK(firstResult != secondResult);
    CHECK_FALSE(CloudHistoryCompositeBlend(CloudHistoryCompositeMode::Composite).enable_blend);
}

TEST_CASE("empty cloud history is an identity and opaque cloud history keeps sample alpha valid")
{
    using namespace Keire::RenderBackend;
    const auto attenuate = CloudHistoryCompositeBlend(CloudHistoryCompositeMode::Attenuation);
    const auto radiance = CloudHistoryCompositeBlend(CloudHistoryCompositeMode::Radiance);
    const Rgba background{7.0, 11.0, 13.0, 0.25};
    const auto clear =
        ApplyBlend(radiance, {0.0, 0.0, 0.0, 0.0}, ApplyBlend(attenuate, {1.0, 1.0, 1.0, 0.0}, background));
    CHECK(clear == background);
    const auto opaque =
        ApplyBlend(radiance, {2.0, 3.0, 4.0, 1.0}, ApplyBlend(attenuate, {0.0, 0.0, 0.0, 0.0}, background));
    CHECK(opaque == Rgba{2.0, 3.0, 4.0, 1.0});
}
