#pragma once

#include "Keire/Core.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace KeireEditor
{
    enum class AssetBrowserOpenAction : std::uint8_t
    {
        External,
        InputActions,
        AnimationGraph,
        AudioMixer,
        VfxEffect,
        Material,
        MaterialGraph,
        ShaderGraph,
        Scene,
        Prefab
    };

    [[nodiscard]] std::string DisplayName(const std::filesystem::path& path);
    [[nodiscard]] bool SameOrChild(const std::filesystem::path& parent, const std::filesystem::path& candidate);
    [[nodiscard]] std::string AssetTypeName(const Keire::AssetSourceRecord& record);
    [[nodiscard]] AssetBrowserOpenAction ResolveAssetBrowserOpenAction(const std::filesystem::path& path) noexcept;
    [[nodiscard]] std::optional<Keire::AssetId>
    ResolveMaterialGraphEditorTarget(const Keire::MaterialGraphDefinition& definition) noexcept;
    [[nodiscard]] std::vector<Keire::AssetId> DecodeAssetPayload(std::span<const std::byte> bytes);
    [[nodiscard]] std::string EncodeAssetPayload(std::span<const Keire::AssetId> assets);
} // namespace KeireEditor
