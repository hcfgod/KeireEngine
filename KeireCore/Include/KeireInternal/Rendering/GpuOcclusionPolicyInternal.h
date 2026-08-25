#pragma once

#include <cstdint>

namespace Keire::RenderBackend::GpuOcclusionPolicy
{
    inline constexpr std::uint32_t AutomaticMinimumCandidates = 128U;
    inline constexpr std::uint64_t AutomaticMinimumCandidateTriangles = 100'000U;
    inline constexpr float AutomaticMinimumOccluderCoverage = 0.05F;
    inline constexpr float AutomaticMaximumDepthCostRatio = 0.5F;
    inline constexpr float AutomaticMinimumOccluderPixels = 4096.0F;
    inline constexpr float ForcedMinimumOccluderPixels = 256.0F;
    inline constexpr std::uint32_t AutomaticQualifyingFrames = 2U;
    inline constexpr std::uint32_t AutomaticMinimumActiveFrames = 30U;
    inline constexpr std::uint32_t AutomaticCoverageColumns = 16U;
    inline constexpr std::uint32_t AutomaticCoverageRows = 9U;
} // namespace Keire::RenderBackend::GpuOcclusionPolicy
