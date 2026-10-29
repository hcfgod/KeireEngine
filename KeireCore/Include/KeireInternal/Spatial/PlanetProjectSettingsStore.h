#pragma once

#include "KeireInternal/FileSystem.h"
#include "KeireInternal/Spatial/PlanetProjectSettingsRecord.h"

#include <cstddef>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace Keire::Detail
{
    class PlanetProjectSettingsStore final
    {
      public:
        [[nodiscard]] static PlanetProjectSettings Load(const std::filesystem::path& projectRoot)
        {
            const auto path = Path(projectRoot);
            if (!std::filesystem::exists(path))
            {
                return {};
            }
            if (!std::filesystem::is_regular_file(path))
            {
                throw std::runtime_error("Planet project settings must be a regular file.");
            }
            return PlanetProjectSettingsRecord::Decode(nlohmann::json::parse(ReadTextFile(path, MaximumBytes)));
        }

        [[nodiscard]] static std::string Serialize(const PlanetProjectSettings& settings)
        {
            const auto text = PlanetProjectSettingsRecord::Encode(settings).dump(2) + '\n';
            if (text.size() > MaximumBytes)
            {
                throw std::length_error("Planet project settings exceed the supported file size.");
            }
            return text;
        }

        static void Save(const std::filesystem::path& projectRoot, const PlanetProjectSettings& settings)
        {
            const auto path = Path(projectRoot);
            const auto text = Serialize(settings);
            std::filesystem::create_directories(path.parent_path());
            WriteTextFileAtomically(path, text);
        }

      private:
        static constexpr std::size_t MaximumBytes = 256U * 1024U;

        [[nodiscard]] static std::filesystem::path Path(const std::filesystem::path& projectRoot)
        {
            if (projectRoot.empty())
            {
                throw std::invalid_argument("Planet project settings require a project root.");
            }
            return projectRoot / "ProjectSettings" / "Planets.keiresettings";
        }
    };
} // namespace Keire::Detail
