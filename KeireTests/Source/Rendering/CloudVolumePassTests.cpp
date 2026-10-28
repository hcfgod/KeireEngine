#include "KeireInternal/Rendering/CloudVolumePass.h"

#include <doctest/doctest.h>

#include <array>
#include <initializer_list>
#include <limits>

TEST_CASE("Cloud volume passes occupy unique bounded pipeline slots")
{
    using namespace Keire::RenderBackend;
    std::array<bool, CloudVolumePipelineCount> occupied{};
    for (std::size_t samples = 0; samples < 4; ++samples)
    {
        for (const auto pass : {CloudVolumePass::Radiance, CloudVolumePass::Attenuation, CloudVolumePass::Composite})
        {
            const auto index = CloudVolumePipelineIndex(samples, pass);
            if (samples != 0 && pass == CloudVolumePass::Composite)
            {
                CHECK_FALSE(index.has_value());
                continue;
            }

            REQUIRE(index.has_value());
            REQUIRE(*index < occupied.size());
            CHECK_FALSE(occupied[*index]);
            occupied[*index] = true;
        }
    }

    for (const bool used : occupied)
    {
        CHECK(used);
    }
}

TEST_CASE("Cloud volume rejects unsupported sample counts and pass values")
{
    using namespace Keire::RenderBackend;
    CHECK_FALSE(CloudVolumePipelineIndex(4, CloudVolumePass::Radiance));
    CHECK_FALSE(CloudVolumePipelineIndex(std::numeric_limits<std::size_t>::max(), CloudVolumePass::Attenuation));
    CHECK_FALSE(CloudVolumePipelineIndex(0, static_cast<CloudVolumePass>(255)));
}
