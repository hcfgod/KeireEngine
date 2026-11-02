#include "KeireInternal/Rendering/CloudHistoryCompositePolicy.h"

#include <doctest/doctest.h>

#include <array>
#include <set>
#include <stdexcept>

TEST_CASE("cloud history pipeline cache isolates sample counts and attenuation from radiance")
{
    using namespace Keire::RenderBackend;
    using Keire::RenderSampleCount;
    std::set<std::size_t> indices;
    for (const auto pass : {CloudTemporalPass::Trace, CloudTemporalPass::Resolve, CloudTemporalPass::Composite})
    {
        const auto index =
            CloudTemporalPipelineIndex(pass, RenderSampleCount::One, CloudHistoryCompositeMode::Composite);
        CHECK(index < CloudTemporalPipelineCount);
        CHECK(indices.insert(index).second);
    }
    for (const auto samples : {RenderSampleCount::Two, RenderSampleCount::Four, RenderSampleCount::Eight})
    {
        for (const auto mode : {CloudHistoryCompositeMode::Radiance, CloudHistoryCompositeMode::Attenuation})
        {
            const auto index = CloudTemporalPipelineIndex(CloudTemporalPass::Composite, samples, mode);
            CHECK(index < CloudTemporalPipelineCount);
            CHECK(indices.insert(index).second);
        }
    }
    CHECK(indices.size() == CloudTemporalPipelineCount);
}

TEST_CASE("cloud history rejects multisample trace and resolved-color replacement modes")
{
    using namespace Keire::RenderBackend;
    using Keire::RenderSampleCount;
    CHECK_THROWS_AS(CloudTemporalPipelineIndex(CloudTemporalPass::Trace, RenderSampleCount::Four,
                                               CloudHistoryCompositeMode::Radiance),
                    std::invalid_argument);
    CHECK_THROWS_AS(CloudTemporalPipelineIndex(CloudTemporalPass::Resolve, RenderSampleCount::Four,
                                               CloudHistoryCompositeMode::Composite),
                    std::invalid_argument);
    CHECK_THROWS_AS(CloudTemporalPipelineIndex(CloudTemporalPass::Composite, RenderSampleCount::Four,
                                               CloudHistoryCompositeMode::Composite),
                    std::invalid_argument);
    CHECK_THROWS_AS(CloudTemporalPipelineIndex(CloudTemporalPass::Composite, RenderSampleCount::One,
                                               CloudHistoryCompositeMode::Radiance),
                    std::invalid_argument);
    CHECK_THROWS_AS(CloudTemporalPipelineIndex(CloudTemporalPass::Composite, static_cast<RenderSampleCount>(3),
                                               CloudHistoryCompositeMode::Radiance),
                    std::invalid_argument);
    CHECK_THROWS_AS(CloudTemporalPipelineIndex(static_cast<CloudTemporalPass>(99), RenderSampleCount::One,
                                               CloudHistoryCompositeMode::Composite),
                    std::invalid_argument);
}
