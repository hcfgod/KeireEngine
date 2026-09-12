#pragma once

#include "Keire/Api.h"
#include "Keire/Assets/Asset.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <stop_token>
#include <string>
#include <vector>

namespace Keire
{
    struct AssetDatabaseSpecification;
    enum class ShaderGraphMigrationDisposition : std::uint8_t
    {
        Migrate,
        AlreadyMigrated,
        Conflict,
        Invalid
    };

    struct ShaderGraphMigrationItem
    {
        std::filesystem::path MaterialGraph;
        std::filesystem::path ShaderGraph;
        ShaderGraphMigrationDisposition Disposition = ShaderGraphMigrationDisposition::Invalid;
        std::string Diagnostic;
        AssetId SourceAsset;
        AssetId GeneratedShaderAsset;
        bool CreatesShader = false;
    };

    struct ShaderGraphMigrationReport
    {
        std::vector<ShaderGraphMigrationItem> Items;
        /// Identifies authoring inputs, extracted programs, and the published catalog/index for reviewed publication.
        std::string ReviewFingerprint;

        [[nodiscard]] std::size_t PendingCount() const noexcept;
        [[nodiscard]] bool CanApply() const noexcept;
    };

    [[nodiscard]] KEIRE_API ShaderGraphMigrationReport
    InspectShaderGraphMigration(const std::filesystem::path& projectRoot);
    [[nodiscard]] KEIRE_API ShaderGraphMigrationReport
    ApplyShaderGraphMigration(const std::filesystem::path& projectRoot);
    [[nodiscard]] KEIRE_API ShaderGraphMigrationReport
    ApplyShaderGraphMigration(const std::filesystem::path& projectRoot, const ShaderGraphMigrationReport& reviewed);
    /// Validates imports in an isolated snapshot, then commits sources, source index, and runtime catalog together.
    /// The caller must suspend asset operations/readers until this returns and reload the published database.
    /// Requires the standard Assets and Library/AssetCache directories. Cancellation never publishes partial results.
    [[nodiscard]] KEIRE_API ShaderGraphMigrationReport
    ApplyShaderGraphMigration(const std::filesystem::path& projectRoot, const ShaderGraphMigrationReport& reviewed,
                              const AssetDatabaseSpecification& specification, std::stop_token cancellation = {});
    /// Rolls back interrupted publication before loading project assets. Committed backups are retained.
    [[nodiscard]] KEIRE_API std::size_t RecoverShaderGraphMigration(const std::filesystem::path& projectRoot);
} // namespace Keire
