#pragma once

#include <array>
#include <cstdint>

namespace Keire::RenderBackend
{
    // Trace, resolve and composite order matches CloudTemporalPass. Sampled textures precede storage
    // buffers in SDL's fragment resource ABI; composite shadow texture 5 therefore moves storage to t6.
    inline constexpr std::array<std::uint32_t, 3> CloudTemporalSamplerCounts{3U, 4U, 6U};
    inline constexpr std::array<std::uint32_t, 3> CloudTemporalUniformCounts{4U, 3U, 3U};
    inline constexpr std::uint32_t CloudCompositeShadowSampler = 5U;
    inline constexpr std::uint32_t CloudCompositeShadowUniform = 2U;
    inline constexpr std::uint32_t CloudCompositeStorageRegister = 6U;
} // namespace Keire::RenderBackend
