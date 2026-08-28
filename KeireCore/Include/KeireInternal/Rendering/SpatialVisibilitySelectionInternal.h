#pragma once

#include "Keire/Assets/Asset.h"
#include "KeireInternal/Rendering/RenderShaderDataInternal.h"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace Keire::RenderBackend
{
    inline constexpr std::uint32_t MaximumSpatialSelectionCandidatesPerDraw = 64U;

    struct alignas(16) GpuSpatialSelectionDraw final
    {
        /// Reflection first/count followed by light-probe-volume first/count.
        std::array<std::uint32_t, 4> Ranges{};
    };

    struct alignas(16) GpuSpatialReflectionCandidate final
    {
        AssetReflectionProbeUniform Descriptor;
        /// Spatial mask index, signed importance bits, then reserved zeros.
        std::array<std::uint32_t, 4> Metadata{InvalidAssetSpatialSelectionIndex, 0U, 0U, 0U};
    };

    struct alignas(16) GpuSpatialLightProbeCandidate final
    {
        std::array<Vector4, 9> ProbeIrradiance{};
        /// Spatial mask index followed by reserved zeros.
        std::array<std::uint32_t, 4> Metadata{InvalidAssetSpatialSelectionIndex, 0U, 0U, 0U};
    };

    static_assert(sizeof(GpuSpatialSelectionDraw) == 16U);
    static_assert(sizeof(GpuSpatialReflectionCandidate) == 176U);
    static_assert(sizeof(GpuSpatialLightProbeCandidate) == 160U);

    struct SpatialVisibilityOwnership final
    {
        std::uint64_t FrameId = 0;
        std::uint64_t SurfaceEpoch = 0;
        std::uint32_t FrameSlot = 0;
        std::uint32_t DeviceGeneration = 0;
        bool Valid = false;

        [[nodiscard]] bool OwnedBy(const SpatialVisibilityOwnership& expected) const noexcept
        {
            return Valid && expected.Valid && FrameId == expected.FrameId && SurfaceEpoch == expected.SurfaceEpoch &&
                   FrameSlot == expected.FrameSlot && DeviceGeneration == expected.DeviceGeneration;
        }
    };

    struct SpatialVisibilityMaskView final
    {
        std::span<const std::uint32_t> Values;
        SpatialVisibilityOwnership Ownership;
    };

    struct SpatialVisibilityContributionCounts final
    {
        std::uint32_t ReflectionProbes = 0;
        std::uint32_t LightProbeVolumes = 0;
    };

    struct SpatialVisibilityContributionRange final
    {
        std::uint32_t ReflectionProbeFirst = 0;
        std::uint32_t ReflectionProbeCount = 0;
        std::uint32_t LightProbeVolumeFirst = 0;
        std::uint32_t LightProbeVolumeCount = 0;
    };

    struct SpatialVisibilityLayout final
    {
        std::vector<SpatialVisibilityContributionRange> Contributions;
        std::uint32_t ReflectionProbeCount = 0;
        std::uint32_t LightProbeVolumeCount = 0;

        [[nodiscard]] std::uint32_t TotalCount() const noexcept { return ReflectionProbeCount + LightProbeVolumeCount; }
    };

    [[nodiscard]] SpatialVisibilityLayout
    BuildSpatialVisibilityLayout(std::span<const SpatialVisibilityContributionCounts> contributions);
    [[nodiscard]] std::uint32_t FlatReflectionProbeIndex(const SpatialVisibilityLayout& layout,
                                                         std::uint32_t contribution, std::uint32_t localIndex);
    [[nodiscard]] std::uint32_t FlatLightProbeVolumeIndex(const SpatialVisibilityLayout& layout,
                                                          std::uint32_t contribution, std::uint32_t localIndex);
    [[nodiscard]] bool CanConsumeSpatialVisibilityMask(const SpatialVisibilityMaskView& mask,
                                                       const SpatialVisibilityOwnership& expected,
                                                       std::uint32_t expectedCount) noexcept;

    struct SpatialReflectionSelectionCandidate final
    {
        AssetId StableId;
        AssetReflectionProbeUniform Descriptor;
        std::uint32_t FlatVisibilityIndex = InvalidAssetSpatialSelectionIndex;
        std::int32_t Importance = 0;
        float Weight = 0.0F;
        float Distance = 0.0F;
        bool Eligible = true;
        bool ConservativeBoundsCurrent = false;
    };

    struct SpatialLightProbeSelectionCandidate final
    {
        AssetId StableId;
        std::array<Vector4, 9> ProbeIrradiance{};
        std::uint32_t FlatVisibilityIndex = InvalidAssetSpatialSelectionIndex;
        std::int32_t Priority = 0;
        bool ContainsPoint = false;
        bool SampleValid = false;
        bool ConservativeBoundsCurrent = false;
    };

    struct SpatialDrawSelectionInput final
    {
        SpatialVisibilityContributionRange Contribution;
        std::span<const SpatialReflectionSelectionCandidate> ReflectionProbes;
        std::span<const SpatialLightProbeSelectionCandidate> LightProbeVolumes;
        SpatialVisibilityMaskView VisibilityMask;
        SpatialVisibilityOwnership ExpectedOwnership;
        std::uint32_t ExpectedVisibilityCount = 0;
    };

    struct SpatialDrawSelectionResult final
    {
        AssetSpatialSelectionRecord Record;
        bool MaskUsable = false;
        bool UsedFailVisible = false;
    };

    [[nodiscard]] SpatialDrawSelectionResult SelectSpatialLightingForDraw(const SpatialDrawSelectionInput& input);
} // namespace Keire::RenderBackend
