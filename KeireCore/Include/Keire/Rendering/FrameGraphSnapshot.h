#pragma once

#include "Keire/Api.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Keire
{
    inline constexpr std::uint32_t FrameGraphSnapshotSchemaVersion = 2;

    enum class FrameGraphSnapshotResourceKind : std::uint8_t
    {
        Texture,
        Buffer
    };

    enum class FrameGraphSnapshotPassKind : std::uint8_t
    {
        Upload,
        Graphics,
        Compute,
        Transfer,
        Present
    };

    enum class FrameGraphSnapshotResourceState : std::uint8_t
    {
        Undefined,
        External,
        CopyDestination,
        ShaderRead,
        ColorAttachment,
        StorageRead,
        StorageWrite,
        CopySource,
        Present,
        DepthStencilAttachment
    };

    enum class FrameGraphSnapshotTextureFormat : std::uint8_t
    {
        Undefined,
        Rgba8Unorm,
        Rgba8Srgb,
        Rgba16Float,
        Rg16Float,
        R32Float,
        D32Float
    };

    enum class FrameGraphSnapshotResourceUsage : std::uint16_t
    {
        None = 0,
        Sampled = 1U << 0U,
        ColorAttachment = 1U << 1U,
        DepthStencilAttachment = 1U << 2U,
        Storage = 1U << 3U,
        TransferSource = 1U << 4U,
        TransferDestination = 1U << 5U,
        Present = 1U << 6U,
        IndirectArguments = 1U << 7U
    };

    struct FrameGraphSnapshotTransition
    {
        std::uint32_t Resource = 0;
        FrameGraphSnapshotResourceState Before = FrameGraphSnapshotResourceState::Undefined;
        FrameGraphSnapshotResourceState After = FrameGraphSnapshotResourceState::Undefined;
    };

    struct FrameGraphSnapshotPass
    {
        std::uint32_t Index = 0;
        std::uint32_t Order = 0;
        std::string Name;
        FrameGraphSnapshotPassKind Kind = FrameGraphSnapshotPassKind::Graphics;
        std::vector<std::uint32_t> Reads;
        std::vector<std::uint32_t> Writes;
        std::vector<FrameGraphSnapshotTransition> Transitions;
    };

    struct FrameGraphSnapshotResource
    {
        std::uint32_t Index = 0;
        std::string Name;
        FrameGraphSnapshotResourceKind Kind = FrameGraphSnapshotResourceKind::Texture;
        std::uint32_t FirstPass = 0;
        std::uint32_t LastPass = 0;
        std::uint32_t PhysicalAliasSlot = 0;
        std::uint64_t CompatibilityKey = 0;
        std::uint64_t EstimatedBytes = 0;
        bool Imported = false;
        bool Used = false;
        FrameGraphSnapshotTextureFormat TextureFormat = FrameGraphSnapshotTextureFormat::Undefined;
        FrameGraphSnapshotResourceUsage Usage = FrameGraphSnapshotResourceUsage::None;
        std::uint8_t SampleCount = 1;
        std::uint8_t WidthScaleNumerator = 1;
        std::uint8_t WidthScaleDenominator = 1;
        std::uint8_t HeightScaleNumerator = 1;
        std::uint8_t HeightScaleDenominator = 1;
    };

    struct FrameGraphSnapshot
    {
        std::uint64_t Frame = 0;
        std::vector<FrameGraphSnapshotPass> Passes;
        std::vector<FrameGraphSnapshotResource> Resources;
        std::uint64_t ActiveTransientBytes = 0;
        std::uint64_t TheoreticalUnaliasedBytes = 0;
        std::uint64_t SavedAliasingBytes = 0;
        std::uint64_t FenceRetiredBytes = 0;
    };

    KEIRE_API void ExportFrameGraphJson(const FrameGraphSnapshot& snapshot, const std::filesystem::path& path);
    KEIRE_API void ExportFrameGraphDot(const FrameGraphSnapshot& snapshot, const std::filesystem::path& path);
} // namespace Keire
