#pragma once

#include "Keire/Api.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Keire
{
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
    };

    struct ShaderGraphMigrationReport
    {
        std::vector<ShaderGraphMigrationItem> Items;

        [[nodiscard]] std::size_t PendingCount() const noexcept;
        [[nodiscard]] bool CanApply() const noexcept;
    };

    [[nodiscard]] KEIRE_API ShaderGraphMigrationReport
    InspectShaderGraphMigration(const std::filesystem::path& projectRoot);
    [[nodiscard]] KEIRE_API ShaderGraphMigrationReport
    ApplyShaderGraphMigration(const std::filesystem::path& projectRoot);
} // namespace Keire
