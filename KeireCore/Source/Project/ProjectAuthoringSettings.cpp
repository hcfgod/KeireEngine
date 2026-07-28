#include "Keire/Project/ProjectAuthoringSettings.h"

#include "KeireInternal/FileSystem.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <ranges>
#include <stdexcept>
#include <unordered_set>

namespace Keire
{
    namespace
    {
        constexpr std::size_t MaximumSettingsBytes = 1024U * 1024U;

        [[nodiscard]] std::filesystem::path SettingsPath(const std::filesystem::path& projectRoot)
        {
            if (projectRoot.empty())
                throw std::invalid_argument("Project authoring settings require a project root.");
            return projectRoot / "ProjectSettings" / "Authoring.keiresettings";
        }

        [[nodiscard]] bool IsValidLayerName(const std::string_view value)
        {
            return !value.empty() && value.size() <= 64 &&
                   std::ranges::none_of(value, [](const char character)
                                        { return character == '\r' || character == '\n' || character == '\t'; }) &&
                   !std::isspace(static_cast<unsigned char>(value.front())) &&
                   !std::isspace(static_cast<unsigned char>(value.back()));
        }
    } // namespace

    ProjectAuthoringSettings DefaultProjectAuthoringSettings()
    {
        ProjectAuthoringSettings result;
        result.PhysicsLayerNames[0] = "Default";
        result.PhysicsLayerNames[1] = "Player";
        result.PhysicsLayerNames[2] = "Environment";
        result.PhysicsLayerNames[3] = "Gameplay";
        for (std::size_t index = 4; index < result.PhysicsLayerNames.size(); ++index)
            result.PhysicsLayerNames[index] = "Layer " + std::to_string(index);
        result.PhysicsCollisionMatrix.fill(~0U);
        return result;
    }

    void ValidateProjectAuthoringSettings(const ProjectAuthoringSettings& settings)
    {
        if (settings.SchemaVersion != ProjectAuthoringSettingsSchemaVersion)
            throw std::invalid_argument("Project authoring settings use an unsupported schema.");

        std::unordered_set<std::string> names;
        for (const auto& name : settings.PhysicsLayerNames)
        {
            if (!IsValidLayerName(name) || !names.emplace(name).second)
                throw std::invalid_argument("Physics collision layer names must be unique, non-empty, and safe.");
        }
        for (std::size_t first = 0; first < PhysicsCollisionLayerCount; ++first)
        {
            for (std::size_t second = first + 1; second < PhysicsCollisionLayerCount; ++second)
            {
                const bool forward = (settings.PhysicsCollisionMatrix[first] & (1U << second)) != 0;
                const bool reverse = (settings.PhysicsCollisionMatrix[second] & (1U << first)) != 0;
                if (forward != reverse)
                    throw std::invalid_argument("Physics collision matrix must be symmetric.");
            }
        }
    }

    ProjectAuthoringSettings LoadProjectAuthoringSettings(const std::filesystem::path& projectRoot)
    {
        const auto path = SettingsPath(projectRoot);
        if (!std::filesystem::is_regular_file(path))
            return DefaultProjectAuthoringSettings();

        const auto document = nlohmann::json::parse(Detail::ReadTextFile(path, MaximumSettingsBytes));
        if (!document.is_object())
            throw std::runtime_error("Project authoring settings root must be an object.");

        ProjectAuthoringSettings result;
        result.SchemaVersion = document.at("schemaVersion").get<std::uint32_t>();
        if (const auto mixer = document.find("defaultMixer"); mixer != document.end() && !mixer->is_null())
            result.DefaultMixer = AssetId::Parse(mixer->get<std::string>());

        const auto& names = document.at("physicsLayerNames");
        const auto& matrix = document.at("physicsCollisionMatrix");
        if (!names.is_array() || names.size() != PhysicsCollisionLayerCount || !matrix.is_array() ||
            matrix.size() != PhysicsCollisionLayerCount)
        {
            throw std::runtime_error("Project authoring settings must define exactly 32 physics layers.");
        }
        for (std::size_t index = 0; index < PhysicsCollisionLayerCount; ++index)
        {
            result.PhysicsLayerNames[index] = names[index].get<std::string>();
            result.PhysicsCollisionMatrix[index] = matrix[index].get<std::uint32_t>();
        }
        ValidateProjectAuthoringSettings(result);
        return result;
    }

    void SaveProjectAuthoringSettings(const std::filesystem::path& projectRoot,
                                      const ProjectAuthoringSettings& settings)
    {
        ValidateProjectAuthoringSettings(settings);
        nlohmann::json names = nlohmann::json::array();
        nlohmann::json matrix = nlohmann::json::array();
        for (std::size_t index = 0; index < PhysicsCollisionLayerCount; ++index)
        {
            names.push_back(settings.PhysicsLayerNames[index]);
            matrix.push_back(settings.PhysicsCollisionMatrix[index]);
        }
        const nlohmann::json document{{"schemaVersion", settings.SchemaVersion},
                                      {"defaultMixer", settings.DefaultMixer
                                                           ? nlohmann::json(settings.DefaultMixer.ToString())
                                                           : nlohmann::json(nullptr)},
                                      {"physicsLayerNames", std::move(names)},
                                      {"physicsCollisionMatrix", std::move(matrix)}};
        Detail::WriteTextFileAtomically(SettingsPath(projectRoot), document.dump(2) + '\n');
    }
} // namespace Keire
