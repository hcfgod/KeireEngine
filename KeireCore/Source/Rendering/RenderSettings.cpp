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

        constexpr std::size_t MaximumSettingsBytes = 64ULL * 1024U;

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

        [[nodiscard]] std::string_view GpuOcclusionModeName(const GpuOcclusionMode mode)
        {
            switch (mode)
            {
            case GpuOcclusionMode::Disabled:
                return "disabled";
            case GpuOcclusionMode::Automatic:
                return "automatic";
            case GpuOcclusionMode::Forced:
                return "forced";
            }
            throw std::invalid_argument("GPU occlusion mode is unsupported.");
        }

        [[nodiscard]] GpuOcclusionMode ParseGpuOcclusionMode(const std::string_view value)
        {
            if (value == "disabled")
                return GpuOcclusionMode::Disabled;
            if (value == "automatic")
                return GpuOcclusionMode::Automatic;
            if (value == "forced")
                return GpuOcclusionMode::Forced;
            throw std::runtime_error("Rendering gpuOcclusion mode is unsupported.");
        }

        [[nodiscard]] std::string_view RenderPathName(const RenderPath path)
        {
            switch (path)
            {
            case RenderPath::ForwardPlus:
                return "forwardPlus";
            case RenderPath::DeferredHybrid:
                return "deferredHybrid";
            }
            throw std::invalid_argument("Render path is unsupported.");
        }

        [[nodiscard]] RenderPath ParseRenderPath(const std::string_view value)
        {
            if (value == "forwardPlus")
                return RenderPath::ForwardPlus;
            if (value == "deferredHybrid")
                return RenderPath::DeferredHybrid;
            throw std::runtime_error("Rendering renderPath is unsupported.");
        }

        [[nodiscard]] std::string_view GlobalIlluminationModeName(const GlobalIlluminationMode mode)
        {
            switch (mode)
            {
            case GlobalIlluminationMode::Disabled:
                return "disabled";
            case GlobalIlluminationMode::Baked:
                return "baked";
            case GlobalIlluminationMode::Realtime:
                return "realtime";
            case GlobalIlluminationMode::Irradyn:
                return "irradyn";
            case GlobalIlluminationMode::Hybrid:
                return "hybrid";
            }
            throw std::invalid_argument("Global illumination mode is unsupported.");
        }

        [[nodiscard]] GlobalIlluminationMode ParseGlobalIlluminationMode(const std::string_view value)
        {
            if (value == "disabled")
                return GlobalIlluminationMode::Disabled;
            if (value == "baked")
                return GlobalIlluminationMode::Baked;
            if (value == "realtime")
                return GlobalIlluminationMode::Realtime;
            if (value == "irradyn")
                return GlobalIlluminationMode::Irradyn;
            if (value == "hybrid")
                return GlobalIlluminationMode::Hybrid;
            throw std::runtime_error("Rendering globalIllumination mode is unsupported.");
        }

        [[nodiscard]] std::string_view IrradynQualityName(const IrradynQuality quality)
        {
            switch (quality)
            {
            case IrradynQuality::Performance:
                return "performance";
            case IrradynQuality::Balanced:
                return "balanced";
            case IrradynQuality::Quality:
                return "quality";
            }
            throw std::invalid_argument("Irradyn quality is unsupported.");
        }

        [[nodiscard]] IrradynQuality ParseIrradynQuality(const std::string_view value)
        {
            if (value == "performance")
                return IrradynQuality::Performance;
            if (value == "balanced")
                return IrradynQuality::Balanced;
            if (value == "quality")
                return IrradynQuality::Quality;
            throw std::runtime_error("Rendering irradynQuality is unsupported.");
        }

        void Validate(const RenderEnvironmentSettings& settings)
        {
            if (settings.SchemaVersion != RenderEnvironmentSettingsSchemaVersion)
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
            (void)GpuOcclusionModeName(settings.GpuOcclusion);
            (void)RenderPathName(settings.RequestedRenderPath);
            (void)GlobalIlluminationModeName(settings.RequestedGlobalIllumination);
            (void)IrradynQualityName(settings.RequestedIrradynQuality);
        }
    } // namespace

    RenderEnvironmentSettings LoadRenderEnvironmentSettings(const std::filesystem::path& projectRoot)
    {
        const auto path = SettingsPath(projectRoot);
        if (!std::filesystem::is_regular_file(path))
            return {};

        const auto document = Json::parse(Detail::ReadTextFile(path, MaximumSettingsBytes));
        const auto sourceSchemaVersion = document.at("schemaVersion").get<std::uint32_t>();
        if (sourceSchemaVersion < 1U || sourceSchemaVersion > RenderEnvironmentSettingsSchemaVersion)
            throw std::runtime_error("Rendering project settings use an unsupported schema version.");
        RenderEnvironmentSettings result;
        const auto& ambient = document.at("ambientColor");
        if (!ambient.is_array() || ambient.size() != 4)
            throw std::runtime_error("Rendering ambientColor must contain four channels.");
        result.AmbientColor = {ambient.at(0).get<float>(), ambient.at(1).get<float>(), ambient.at(2).get<float>(),
                               ambient.at(3).get<float>()};
        result.AmbientIntensity = document.at("ambientIntensity").get<float>();
        result.Exposure = document.at("exposure").get<float>();
        if (sourceSchemaVersion >= 2U)
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
        if (sourceSchemaVersion >= 3U)
            result.GpuOcclusion = ParseGpuOcclusionMode(document.at("gpuOcclusion").get<std::string>());
        if (sourceSchemaVersion >= 4U)
        {
            result.RequestedRenderPath = ParseRenderPath(document.at("renderPath").get<std::string>());
            result.RequestedGlobalIllumination =
                ParseGlobalIlluminationMode(document.at("globalIllumination").get<std::string>());
            result.RequestedIrradynQuality = ParseIrradynQuality(document.at("irradynQuality").get<std::string>());
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
        document["gpuOcclusion"] = GpuOcclusionModeName(settings.GpuOcclusion);
        document["renderPath"] = RenderPathName(settings.RequestedRenderPath);
        document["globalIllumination"] = GlobalIlluminationModeName(settings.RequestedGlobalIllumination);
        document["irradynQuality"] = IrradynQualityName(settings.RequestedIrradynQuality);
        Detail::WriteTextFileAtomically(SettingsPath(projectRoot), document.dump(2) + '\n');
    }

    RenderFeatureSelection ResolveRenderFeatureSelection(const RenderEnvironmentSettings& settings,
                                                         const RenderFeatureCapabilities& capabilities)
    {
        Validate(settings);
        RenderFeatureSelection result;
        result.RequestedPath = settings.RequestedRenderPath;
        result.EffectivePath = settings.RequestedRenderPath;
        result.RequestedGlobalIllumination = settings.RequestedGlobalIllumination;
        result.EffectiveGlobalIllumination = settings.RequestedGlobalIllumination;
        result.RequestedIrradynQuality = settings.RequestedIrradynQuality;

        if (result.RequestedPath == RenderPath::DeferredHybrid && !capabilities.DeferredHybrid)
        {
            result.EffectivePath = RenderPath::ForwardPlus;
            result.PathFallback = RenderPathFallbackReason::DeferredHybridUnavailable;
        }

        const bool irradynPathAvailable =
            !capabilities.IrradynRequiresDeferredHybrid || result.EffectivePath == RenderPath::DeferredHybrid;
        const bool irradynAvailable = capabilities.IrradynGlobalIllumination && irradynPathAvailable;
        switch (result.RequestedGlobalIllumination)
        {
        case GlobalIlluminationMode::Disabled:
            break;
        case GlobalIlluminationMode::Baked:
            if (!capabilities.BakedGlobalIllumination)
            {
                result.EffectiveGlobalIllumination = GlobalIlluminationMode::Disabled;
                result.GlobalIlluminationFallback = GlobalIlluminationFallbackReason::BakedUnavailable;
            }
            break;
        case GlobalIlluminationMode::Realtime:
            if (!capabilities.RealtimeGlobalIllumination)
            {
                result.EffectiveGlobalIllumination = GlobalIlluminationMode::Disabled;
                result.GlobalIlluminationFallback = GlobalIlluminationFallbackReason::RealtimeUnavailable;
            }
            break;
        case GlobalIlluminationMode::Irradyn:
            if (!irradynAvailable)
            {
                if (capabilities.RealtimeGlobalIllumination)
                    result.EffectiveGlobalIllumination = GlobalIlluminationMode::Realtime;
                else if (capabilities.BakedGlobalIllumination)
                    result.EffectiveGlobalIllumination = GlobalIlluminationMode::Baked;
                else
                    result.EffectiveGlobalIllumination = GlobalIlluminationMode::Disabled;
                result.GlobalIlluminationFallback =
                    capabilities.IrradynGlobalIllumination
                        ? GlobalIlluminationFallbackReason::IrradynRequiresDeferredHybrid
                        : GlobalIlluminationFallbackReason::IrradynUnavailable;
            }
            break;
        case GlobalIlluminationMode::Hybrid:
            if (!capabilities.BakedGlobalIllumination || !irradynAvailable)
            {
                if (irradynAvailable)
                    result.EffectiveGlobalIllumination = GlobalIlluminationMode::Irradyn;
                else if (capabilities.RealtimeGlobalIllumination)
                    result.EffectiveGlobalIllumination = GlobalIlluminationMode::Realtime;
                else if (capabilities.BakedGlobalIllumination)
                    result.EffectiveGlobalIllumination = GlobalIlluminationMode::Baked;
                else
                    result.EffectiveGlobalIllumination = GlobalIlluminationMode::Disabled;
                result.GlobalIlluminationFallback = GlobalIlluminationFallbackReason::HybridUnavailable;
            }
            break;
        }
        return result;
    }
} // namespace Keire
