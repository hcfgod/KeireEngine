#pragma once

#include "KeireInternal/Environment/EnvironmentLightingRecord.h"
#include "KeireInternal/FileSystem.h"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace Keire::Detail
{
    // Project-default profile adapter, separate from reusable environment assets and transient preview state.
    class EnvironmentLightingSettingsStore final
    {
      public:
        [[nodiscard]] static EnvironmentLightingProfile Load(const std::filesystem::path& projectRoot)
        {
            const auto path = Path(projectRoot);
            if (!std::filesystem::exists(path))
            {
                return EnvironmentLightingProfile{};
            }
            if (!std::filesystem::is_regular_file(path))
            {
                throw std::runtime_error("Environment lighting settings must be a regular file.");
            }
            const auto document = nlohmann::json::parse(ReadTextFile(path, MaximumBytes));
            if (!document.is_object() || document.size() != 2 || !document.contains("schemaVersion") ||
                !document.at("schemaVersion").is_number_integer() || document.at("schemaVersion") != 1 ||
                !document.contains("lighting"))
            {
                throw std::invalid_argument("Unsupported or malformed environment lighting settings.");
            }
            return EnvironmentLightingRecord::Decode(document.at("lighting"));
        }

        [[nodiscard]] static std::string Serialize(const EnvironmentLightingProfile& profile)
        {
            const nlohmann::json document{{"schemaVersion", 1},
                                          {"lighting", EnvironmentLightingRecord::Encode(profile)}};
            const auto text = document.dump(2) + '\n';
            if (text.size() > MaximumBytes)
            {
                throw std::length_error("Environment lighting settings exceed the supported file size.");
            }
            return text;
        }

        static void Save(const std::filesystem::path& projectRoot, const EnvironmentLightingProfile& profile)
        {
            const auto path = Path(projectRoot);
            const auto text = Serialize(profile);
            std::filesystem::create_directories(path.parent_path());
            WriteTextFileAtomically(path, text);
        }

      private:
        static constexpr std::size_t MaximumBytes = 64U * 1024U;

        [[nodiscard]] static std::filesystem::path Path(const std::filesystem::path& projectRoot)
        {
            if (projectRoot.empty())
            {
                throw std::invalid_argument("Environment lighting settings require a project root.");
            }
            return projectRoot / "ProjectSettings" / "EnvironmentLighting.keiresettings";
        }
    };
} // namespace Keire::Detail
