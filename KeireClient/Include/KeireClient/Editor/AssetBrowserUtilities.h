#pragma once

#include "Keire/Core.h"

#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace KeireEditor
{
    [[nodiscard]] std::string DisplayName(const std::filesystem::path& path);
    [[nodiscard]] bool SameOrChild(const std::filesystem::path& parent, const std::filesystem::path& candidate);
    [[nodiscard]] std::string AssetTypeName(const Keire::AssetSourceRecord& record);
    [[nodiscard]] std::vector<Keire::AssetId> DecodeAssetPayload(std::span<const std::byte> bytes);
    [[nodiscard]] std::string EncodeAssetPayload(std::span<const Keire::AssetId> assets);
} // namespace KeireEditor
