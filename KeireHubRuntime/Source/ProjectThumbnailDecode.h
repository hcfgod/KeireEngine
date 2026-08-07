#pragma once

#include "KeireHubRuntime/ProjectMetadataScanner.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace KeireHub::Detail
{
    // Reads and decodes a previously confined PNG on the metadata worker. The returned image is always normalized to
    // ProjectThumbnailImage's fixed display dimensions so downstream snapshots and GPU uploads remain strictly bounded.
    [[nodiscard]] std::optional<ProjectThumbnailImage>
    DecodeProjectThumbnail(const std::filesystem::path& path, std::uint64_t encodedSize, std::string& details) noexcept;
} // namespace KeireHub::Detail
