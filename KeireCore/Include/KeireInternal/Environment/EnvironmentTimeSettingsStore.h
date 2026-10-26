#pragma once

#include "KeireInternal/Environment/EnvironmentClockRecord.h"
#include "KeireInternal/FileSystem.h"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace Keire::Detail
{
    // Authored starting time only. Runtime saves and transient Editor preview have separate ownership.
    class EnvironmentTimeSettingsStore final
    {
      public:
        [[nodiscard]] static EnvironmentClockState Load(const std::filesystem::path& projectRoot)
        {
            const auto path = Path(projectRoot);
            if (!std::filesystem::exists(path))
            {
                return {};
            }
            if (!std::filesystem::is_regular_file(path))
            {
                throw std::runtime_error("Environment time settings must be a regular file.");
            }
            const auto document = nlohmann::json::parse(ReadTextFile(path, MaximumBytes));
            if (!document.is_object() || document.size() != 2 || !document.contains("schemaVersion") ||
                !document.at("schemaVersion").is_number_integer() || document.at("schemaVersion") != 1 ||
                !document.contains("initialTime"))
            {
                throw std::invalid_argument("Unsupported or malformed environment time settings.");
            }
            return EnvironmentClockRecord::Decode(document.at("initialTime"));
        }

        [[nodiscard]] static std::string Serialize(const EnvironmentClockState authored)
        {
            const nlohmann::json document{{"schemaVersion", 1},
                                          {"initialTime", EnvironmentClockRecord::Encode(authored)}};
            return document.dump(2) + '\n';
        }

        static void Save(const std::filesystem::path& projectRoot, const EnvironmentClockState authored)
        {
            const auto path = Path(projectRoot);
            const auto text = Serialize(authored);
            std::filesystem::create_directories(path.parent_path());
            WriteTextFileAtomically(path, text);
        }

      private:
        static constexpr std::size_t MaximumBytes = 64U * 1024U;

        [[nodiscard]] static std::filesystem::path Path(const std::filesystem::path& projectRoot)
        {
            if (projectRoot.empty())
            {
                throw std::invalid_argument("Environment time settings require a project root.");
            }
            return projectRoot / "ProjectSettings" / "EnvironmentTime.keiresettings";
        }
    };
} // namespace Keire::Detail
