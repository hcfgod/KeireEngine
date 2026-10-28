#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

namespace Keire::RenderBackend
{
    enum class CloudVolumePass : std::uint8_t
    {
        Radiance,
        Attenuation,
        Composite
    };

    inline constexpr std::size_t CloudVolumePipelineCount = 9;

    // Four sample-count slots have separate radiance/attenuation passes; composite is single-sample only.
    [[nodiscard]] constexpr std::optional<std::size_t> CloudVolumePipelineIndex(std::size_t sampleIndex,
                                                                                CloudVolumePass pass) noexcept
    {
        if (sampleIndex >= 4)
        {
            return std::nullopt;
        }

        switch (pass)
        {
        case CloudVolumePass::Radiance:
            return sampleIndex * 2;
        case CloudVolumePass::Attenuation:
            return sampleIndex * 2 + 1;
        case CloudVolumePass::Composite:
            return sampleIndex == 0 ? std::optional<std::size_t>{8} : std::nullopt;
        }

        return std::nullopt;
    }
} // namespace Keire::RenderBackend
