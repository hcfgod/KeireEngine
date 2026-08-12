#pragma once

#include "Keire/Api.h"
#include "Keire/Assets/Asset.h"
#include "Keire/Audio/AudioSystem.h"
#include "Keire/ECS/EntityLayer.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

namespace Keire
{
    inline constexpr std::uint32_t ProjectAuthoringSettingsSchemaVersion = 2;
    inline constexpr std::size_t PhysicsCollisionLayerCount = EntityLayerCount;

    struct AudioProjectSettings
    {
        std::uint32_t MixSampleRate = 48000;
        std::uint32_t PeriodFrames = 256;
        AudioChannelLayout OutputLayout = AudioChannelLayout::Stereo;
        std::uint32_t MaximumVoices = 256;
        std::uint32_t MaximumVirtualVoices = 1024;
        std::string PlaybackDeviceId;

        [[nodiscard]] bool operator==(const AudioProjectSettings&) const = default;
    };

    struct ProjectAuthoringSettings
    {
        std::uint32_t SchemaVersion = ProjectAuthoringSettingsSchemaVersion;
        AssetId DefaultMixer;
        AudioProjectSettings Audio;
        std::string ExternalEditorId = "system";
        std::filesystem::path ExternalEditorExecutable;
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
