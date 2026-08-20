#include "Keire/Rendering/RenderSystem.h"

#include "KeireInternal/FileSystem.h"

#include <nlohmann/json.hpp>

#include <cmath>
#include <stdexcept>

namespace Keire
{
    namespace
    {
        using Json = nlohmann::json;

        constexpr std::size_t MaximumSettingsBytes = 64U * 1024U;

        [[nodiscard]] std::filesystem::path SettingsPath(const std::filesystem::path& projectRoot)
        {
            if (projectRoot.empty())
                throw std::invalid_argument("Rendering project settings require a project root.");
            return projectRoot / "ProjectSettings" / "Rendering.keiresettings";
        }

        [[nodiscard]] bool ValidColor(const Color color) noexcept
        {
            const auto valid = [](const float value) { return std::isfinite(value) && value >= 0.0F && value <= 1.0F; };
            return valid(color.Red) && valid(color.Green) && valid(color.Blue) && valid(color.Alpha);
        }

        void Validate(const RenderEnvironmentSettings& settings)
        {
            if (settings.SchemaVersion != 1 && settings.SchemaVersion != 2)
                throw std::invalid_argument("Rendering project settings use an unsupported schema version.");
            if (!ValidColor(settings.AmbientColor))
                throw std::invalid_argument("Ambient color channels must be finite values in 0..1.");
            if (!std::isfinite(settings.AmbientIntensity) || settings.AmbientIntensity < 0.0F ||
                settings.AmbientIntensity > 16.0F)
            {
                throw std::invalid_argument("Ambient intensity must be a finite value in 0..16.");
            }
            if (!std::isfinite(settings.Exposure) || settings.Exposure < 0.01F || settings.Exposure > 16.0F)
                throw std::invalid_argument("Rendering exposure must be a finite value in 0.01..16.");
            if (!std::isfinite(settings.EnvironmentRotationDegrees) ||
                !std::isfinite(settings.EnvironmentDiffuseIntensity) || settings.EnvironmentDiffuseIntensity < 0.0F ||
                settings.EnvironmentDiffuseIntensity > 16.0F || !std::isfinite(settings.EnvironmentSpecularIntensity) ||
                settings.EnvironmentSpecularIntensity < 0.0F || settings.EnvironmentSpecularIntensity > 16.0F)
                throw std::invalid_argument("Environment rotation/intensities are invalid.");
            if (!std::isfinite(settings.DirectionalShadowDistance) || settings.DirectionalShadowDistance <= 0.0F ||
                settings.DirectionalShadowDistance > 100'000.0F || settings.DirectionalShadowCascadeCount < 1U ||
                settings.DirectionalShadowCascadeCount > 4U || settings.DirectionalShadowResolution < 256U ||
                settings.DirectionalShadowResolution > 8192U ||
                (settings.DirectionalShadowResolution & (settings.DirectionalShadowResolution - 1U)) != 0U ||
                !std::isfinite(settings.DirectionalShadowSplitLambda) || settings.DirectionalShadowSplitLambda < 0.0F ||
                settings.DirectionalShadowSplitLambda > 1.0F)
                throw std::invalid_argument("Directional shadow settings are outside supported production limits.");
        }
    } // namespace

    RenderEnvironmentSettings LoadRenderEnvironmentSettings(const std::filesystem::path& projectRoot)
    {
        const auto path = SettingsPath(projectRoot);
        if (!std::filesystem::is_regular_file(path))
            return {};

        const auto document = Json::parse(Detail::ReadTextFile(path, MaximumSettingsBytes));
        RenderEnvironmentSettings result;
        result.SchemaVersion = document.at("schemaVersion").get<std::uint32_t>();
        const auto& ambient = document.at("ambientColor");
        if (!ambient.is_array() || ambient.size() != 4)
            throw std::runtime_error("Rendering ambientColor must contain four channels.");
        result.AmbientColor = {ambient.at(0).get<float>(), ambient.at(1).get<float>(), ambient.at(2).get<float>(),
                               ambient.at(3).get<float>()};
        result.AmbientIntensity = document.at("ambientIntensity").get<float>();
        result.Exposure = document.at("exposure").get<float>();
        if (result.SchemaVersion >= 2)
        {
            const auto environment = document.value("environment", std::string{});
            result.Environment = environment.empty() ? AssetId{} : AssetId::Parse(environment);
            result.EnvironmentRotationDegrees = document.value("environmentRotationDegrees", 0.0F);
            result.EnvironmentDiffuseIntensity = document.value("environmentDiffuseIntensity", 1.0F);
            result.EnvironmentSpecularIntensity = document.value("environmentSpecularIntensity", 1.0F);
            result.SkyVisible = document.value("skyVisible", true);
            result.DirectionalShadowDistance = document.value("directionalShadowDistance", 100.0F);
            result.DirectionalShadowCascadeCount = document.value("directionalShadowCascadeCount", 4U);
            result.DirectionalShadowResolution = document.value("directionalShadowResolution", 2048U);
            result.DirectionalShadowSplitLambda = document.value("directionalShadowSplitLambda", 0.65F);
        }
        Validate(result);
        return result;
    }

    void ValidateRenderEnvironmentSettings(const RenderEnvironmentSettings& settings) { Validate(settings); }

    void SaveRenderEnvironmentSettings(const std::filesystem::path& projectRoot,
                                       const RenderEnvironmentSettings& settings)
    {
        Validate(settings);
        std::filesystem::create_directories(SettingsPath(projectRoot).parent_path());
        Json document{{"schemaVersion", settings.SchemaVersion},
                      {"ambientColor",
                       {settings.AmbientColor.Red, settings.AmbientColor.Green, settings.AmbientColor.Blue,
                        settings.AmbientColor.Alpha}},
                      {"ambientIntensity", settings.AmbientIntensity},
                      {"exposure", settings.Exposure}};
        if (settings.SchemaVersion >= 2)
        {
            document["environment"] = settings.Environment ? settings.Environment.ToString() : std::string{};
            document["environmentRotationDegrees"] = settings.EnvironmentRotationDegrees;
            document["environmentDiffuseIntensity"] = settings.EnvironmentDiffuseIntensity;
            document["environmentSpecularIntensity"] = settings.EnvironmentSpecularIntensity;
            document["skyVisible"] = settings.SkyVisible;
            document["directionalShadowDistance"] = settings.DirectionalShadowDistance;
            document["directionalShadowCascadeCount"] = settings.DirectionalShadowCascadeCount;
            document["directionalShadowResolution"] = settings.DirectionalShadowResolution;
            document["directionalShadowSplitLambda"] = settings.DirectionalShadowSplitLambda;
        }
        Detail::WriteTextFileAtomically(SettingsPath(projectRoot), document.dump(2) + '\n');
    }
} // namespace Keire
