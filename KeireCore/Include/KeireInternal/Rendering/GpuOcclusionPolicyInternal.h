#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <optional>
#include <utility>

namespace Keire::RenderBackend::GpuOcclusionPolicy
{
    struct ResourceExtent final
    {
        std::uint32_t Width = 0;
        std::uint32_t Height = 0;

        [[nodiscard]] constexpr bool operator==(const ResourceExtent&) const noexcept = default;
    };

    struct AllocationRetrySlotState final
    {
        std::uint32_t FailureCount = 0;
        std::uint32_t FramesRemaining = 0;
        bool MaximumDelayWarningPublished = false;
    };

    // RenderSharedState validates RenderSpecification::MaximumFramesInFlight to the range 1..8.
    inline constexpr std::size_t MaximumAllocationRetrySlots = 8U;

    struct AllocationRetryState final
    {
        ResourceExtent Extent;
        std::array<AllocationRetrySlotState, MaximumAllocationRetrySlots> Slots{};
    };

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
    inline constexpr std::uint32_t AllocationRetryMaximumFrames = 120U;
    inline constexpr std::uint64_t ConservativeTextureBytesPerTexel = 4U;
    inline constexpr std::uint64_t MaximumTextureBytesPerSurface = 256ULL * 1024ULL * 1024ULL;

    [[nodiscard]] constexpr ResourceExtent ResolveConservativeResourceExtent(const std::uint32_t width,
                                                                             const std::uint32_t height) noexcept
    {
        // Directly rasterizing occluders at a lower resolution can close presentation-pixel gaps and falsely reject
        // geometry. Keep the depth target pixel-for-pixel until a conservative full-resolution reduction exists.
        return width == 0U || height == 0U ? ResourceExtent{} : ResourceExtent{width, height};
    }

    [[nodiscard]] constexpr std::optional<std::uint64_t>
    EstimateTextureMemoryBytes(const ResourceExtent extent, const std::uint32_t pyramidLevels,
                               const std::uint32_t frameSlots) noexcept
    {
        if (extent.Width == 0U || extent.Height == 0U || frameSlots == 0U)
            return std::nullopt;
        const auto checkedMultiply =
            [](const std::uint64_t left, const std::uint64_t right, std::uint64_t& result) noexcept
        {
            if (left != 0U && right > std::numeric_limits<std::uint64_t>::max() / left)
                return false;
            result = left * right;
            return true;
        };
        const auto checkedAdd = [](const std::uint64_t left, const std::uint64_t right, std::uint64_t& result) noexcept
        {
            if (right > std::numeric_limits<std::uint64_t>::max() - left)
                return false;
            result = left + right;
            return true;
        };

        std::uint64_t totalTexels = 0U;
        if (!checkedMultiply(extent.Width, extent.Height, totalTexels))
            return std::nullopt;
        auto width = extent.Width / 2U + extent.Width % 2U;
        auto height = extent.Height / 2U + extent.Height % 2U;
        for (std::uint32_t level = 0U; level < pyramidLevels; ++level)
        {
            std::uint64_t levelTexels = 0U;
            if (!checkedMultiply(width, height, levelTexels) || !checkedAdd(totalTexels, levelTexels, totalTexels))
                return std::nullopt;
            if (width == 1U && height == 1U)
                break;
            width = width / 2U + width % 2U;
            height = height / 2U + height % 2U;
        }

        std::uint64_t bytesPerSlot = 0U;
        std::uint64_t bytesPerSurface = 0U;
        if (!checkedMultiply(totalTexels, ConservativeTextureBytesPerTexel, bytesPerSlot) ||
            !checkedMultiply(bytesPerSlot, frameSlots, bytesPerSurface))
        {
            return std::nullopt;
        }
        return bytesPerSurface;
    }

    [[nodiscard]] constexpr bool TextureMemoryWithinBudget(const std::uint64_t bytes) noexcept
    {
        return bytes <= MaximumTextureBytesPerSurface;
    }

    constexpr void ResetAllocationRetry(AllocationRetryState& state) noexcept { state = {}; }

    constexpr void PrepareAllocationRetryExtent(AllocationRetryState& state, const ResourceExtent extent) noexcept
    {
        if (state.Extent != extent)
            state = {.Extent = extent};
    }

    [[nodiscard]] constexpr bool BeginAllocationAttempt(AllocationRetryState& state, const std::size_t slot) noexcept
    {
        auto& slotState = state.Slots[slot];
        if (slotState.FramesRemaining == 0U)
            return true;
        --slotState.FramesRemaining;
        return false;
    }

    [[nodiscard]] constexpr bool AllocationRetryPending(const AllocationRetryState& state,
                                                        const std::size_t slot) noexcept
    {
        return state.Slots[slot].FramesRemaining > 0U;
    }

    [[nodiscard]] constexpr bool AnyAllocationRetryPending(const AllocationRetryState& state) noexcept
    {
        for (const auto& slotState : state.Slots)
        {
            if (slotState.FramesRemaining > 0U)
                return true;
        }
        return false;
    }

    [[nodiscard]] constexpr bool RegisterAllocationFailure(AllocationRetryState& state, const std::size_t slot) noexcept
    {
        auto& slotState = state.Slots[slot];
        const bool firstFailure = slotState.FailureCount == 0U;
        if (slotState.FailureCount < 32U)
            ++slotState.FailureCount;
        const auto shift = slotState.FailureCount > 8U ? 7U : slotState.FailureCount - 1U;
        const auto delay = std::uint32_t{1U} << shift;
        slotState.FramesRemaining = delay < AllocationRetryMaximumFrames ? delay : AllocationRetryMaximumFrames;

        const bool reachedMaximum = slotState.FramesRemaining == AllocationRetryMaximumFrames;
        const bool publishMaximumWarning = reachedMaximum && !slotState.MaximumDelayWarningPublished;
        slotState.MaximumDelayWarningPublished = slotState.MaximumDelayWarningPublished || reachedMaximum;
        return firstFailure || publishMaximumWarning;
    }

    constexpr void RegisterAllocationSuccess(AllocationRetryState& state, const std::size_t slot) noexcept
    {
        state.Slots[slot] = {};
    }

    template <typename Create, typename OnFailure>
    [[nodiscard]] bool TryCreateOptionalVisualization(Create&& create, OnFailure&& onFailure)
    {
        try
        {
            std::forward<Create>(create)();
            return true;
        }
        catch (const std::exception& error)
        {
            std::forward<OnFailure>(onFailure)(error);
            return false;
        }
    }
} // namespace Keire::RenderBackend::GpuOcclusionPolicy
