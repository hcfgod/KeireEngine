#pragma once

#include "Keire/Api.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Keire
{
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
        Present
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
