#pragma once

#include "Keire/Api.h"
#include "Keire/Assets/Asset.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

namespace Keire
{
    inline constexpr std::uint32_t ProjectAuthoringSettingsSchemaVersion = 1;
    inline constexpr std::size_t PhysicsCollisionLayerCount = 32;

    struct ProjectAuthoringSettings
    {
        std::uint32_t SchemaVersion = ProjectAuthoringSettingsSchemaVersion;
        AssetId DefaultMixer;
        std::array<std::string, PhysicsCollisionLayerCount> PhysicsLayerNames;
        std::array<std::uint32_t, PhysicsCollisionLayerCount> PhysicsCollisionMatrix;

        [[nodiscard]] bool operator==(const ProjectAuthoringSettings&) const = default;
    };

    [[nodiscard]] KEIRE_API ProjectAuthoringSettings DefaultProjectAuthoringSettings();
    KEIRE_API void ValidateProjectAuthoringSettings(const ProjectAuthoringSettings& settings);
    [[nodiscard]] KEIRE_API ProjectAuthoringSettings
    LoadProjectAuthoringSettings(const std::filesystem::path& projectRoot);
    KEIRE_API void SaveProjectAuthoringSettings(const std::filesystem::path& projectRoot,
                                                const ProjectAuthoringSettings& settings);
} // namespace Keire
