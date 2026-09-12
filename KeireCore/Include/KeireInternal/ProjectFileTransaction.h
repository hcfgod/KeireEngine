#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <span>
#include <vector>

namespace Keire::Detail
{
    struct ProjectFileReplacement
    {
        std::filesystem::path Destination;
        std::optional<std::vector<std::byte>> ExpectedOriginal;
        std::vector<std::byte> Contents;
    };

    /// Publishes a reviewed snapshot. Backups and the recovery journal remain in Library/MaterialShaderUpgrade.
    void PublishMaterialMigrationFiles(const std::filesystem::path& root,
                                       std::span<const ProjectFileReplacement> files);
    [[nodiscard]] bool HasPendingMaterialMigration(const std::filesystem::path& root);
    [[nodiscard]] std::size_t RecoverMaterialMigrationFiles(const std::filesystem::path& root);
} // namespace Keire::Detail
