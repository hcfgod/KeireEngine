#include "Keire/Rendering/RenderSystem.h"

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>

namespace
{
    class TemporaryDirectory final
    {
      public:
        TemporaryDirectory()
            : Path(std::filesystem::temp_directory_path() /
                   ("keire-render-settings-" + Keire::AssetId::Generate().ToString()))
        {
            std::filesystem::create_directories(Path / "ProjectSettings");
        }

        ~TemporaryDirectory()
        {
            std::error_code ignored;
            std::filesystem::remove_all(Path, ignored);
        }

        std::filesystem::path Path;
    };

    void WriteSettings(const std::filesystem::path& root, nlohmann::json document)
    {
        std::ofstream output(root / "ProjectSettings" / "Rendering.keiresettings", std::ios::trunc);
        REQUIRE(output);
        output << document.dump(2) << '\n';
    }

    [[nodiscard]] nlohmann::json LegacySettings(const std::uint32_t schema)
    {
        nlohmann::json result{{"schemaVersion", schema},
                              {"ambientColor", {0.2F, 0.22F, 0.26F, 1.0F}},
                              {"ambientIntensity", 0.75F},
                              {"exposure", 1.0F}};
        if (schema >= 3U)
            result["gpuOcclusion"] = "automatic";
        return result;
    }
} // namespace

TEST_CASE("Rendering settings schema four round trips render path GI quality and occlusion choices")
{
    TemporaryDirectory directory;
    for (const auto mode :
         {Keire::GpuOcclusionMode::Disabled, Keire::GpuOcclusionMode::Automatic, Keire::GpuOcclusionMode::Forced})
    {
        for (const auto renderPath : {Keire::RenderPath::ForwardPlus, Keire::RenderPath::DeferredHybrid})
        {
            for (const auto gi : {Keire::GlobalIlluminationMode::Disabled, Keire::GlobalIlluminationMode::Baked,
                                  Keire::GlobalIlluminationMode::Realtime, Keire::GlobalIlluminationMode::Irradyn,
                                  Keire::GlobalIlluminationMode::Hybrid})
            {
                for (const auto quality : {Keire::IrradynQuality::Performance, Keire::IrradynQuality::Balanced,
                                           Keire::IrradynQuality::Quality})
                {
                    Keire::RenderEnvironmentSettings settings;
                    settings.GpuOcclusion = mode;
                    settings.RequestedRenderPath = renderPath;
                    settings.RequestedGlobalIllumination = gi;
                    settings.RequestedIrradynQuality = quality;
                    Keire::SaveRenderEnvironmentSettings(directory.Path, settings);
                    CHECK(Keire::LoadRenderEnvironmentSettings(directory.Path) == settings);
                }
            }
        }
    }
}

TEST_CASE("Legacy rendering settings migrate to explicit forward rendering with GI disabled")
{
    TemporaryDirectory directory;
    for (const auto schema : {1U, 2U, 3U})
    {
        WriteSettings(directory.Path, LegacySettings(schema));
        const auto migrated = Keire::LoadRenderEnvironmentSettings(directory.Path);
        CHECK(migrated.SchemaVersion == Keire::RenderEnvironmentSettingsSchemaVersion);
        CHECK(migrated.GpuOcclusion == Keire::GpuOcclusionMode::Automatic);
        CHECK(migrated.RequestedRenderPath == Keire::RenderPath::ForwardPlus);
        CHECK(migrated.RequestedGlobalIllumination == Keire::GlobalIlluminationMode::Disabled);
        CHECK(migrated.RequestedIrradynQuality == Keire::IrradynQuality::Balanced);
        CHECK_NOTHROW(Keire::ValidateRenderEnvironmentSettings(migrated));
    }

    auto future = LegacySettings(Keire::RenderEnvironmentSettingsSchemaVersion + 1U);
    WriteSettings(directory.Path, std::move(future));
    CHECK_THROWS_AS((void)Keire::LoadRenderEnvironmentSettings(directory.Path), std::runtime_error);

    auto malformed = LegacySettings(3U);
    malformed["gpuOcclusion"] = "aggressive";
    WriteSettings(directory.Path, std::move(malformed));
    CHECK_THROWS_AS((void)Keire::LoadRenderEnvironmentSettings(directory.Path), std::runtime_error);
}

TEST_CASE("Render feature selection reports deterministic safe fallbacks")
{
    Keire::RenderEnvironmentSettings settings;
    CHECK(Keire::ResolveRenderFeatureSelection(settings, {}) == Keire::RenderFeatureSelection{});

    settings.RequestedRenderPath = Keire::RenderPath::DeferredHybrid;
    settings.RequestedGlobalIllumination = Keire::GlobalIlluminationMode::Irradyn;
    settings.RequestedIrradynQuality = Keire::IrradynQuality::Quality;
    Keire::RenderFeatureCapabilities forwardCapabilities;
    forwardCapabilities.RealtimeGlobalIllumination = true;
    const auto forwardFallback = Keire::ResolveRenderFeatureSelection(settings, forwardCapabilities);
    CHECK(forwardFallback.RequestedPath == Keire::RenderPath::DeferredHybrid);
    CHECK(forwardFallback.EffectivePath == Keire::RenderPath::ForwardPlus);
    CHECK(forwardFallback.PathFallback == Keire::RenderPathFallbackReason::DeferredHybridUnavailable);
    CHECK(forwardFallback.EffectiveGlobalIllumination == Keire::GlobalIlluminationMode::Realtime);
    CHECK(forwardFallback.GlobalIlluminationFallback == Keire::GlobalIlluminationFallbackReason::IrradynUnavailable);
    CHECK(forwardFallback.RequestedIrradynQuality == Keire::IrradynQuality::Quality);

    settings.RequestedRenderPath = Keire::RenderPath::ForwardPlus;
    Keire::RenderFeatureCapabilities wrongPathCapabilities;
    wrongPathCapabilities.RealtimeGlobalIllumination = true;
    wrongPathCapabilities.IrradynGlobalIllumination = true;
    const auto wrongPath = Keire::ResolveRenderFeatureSelection(settings, wrongPathCapabilities);
    CHECK(wrongPath.EffectiveGlobalIllumination == Keire::GlobalIlluminationMode::Realtime);
    CHECK(wrongPath.GlobalIlluminationFallback ==
          Keire::GlobalIlluminationFallbackReason::IrradynRequiresDeferredHybrid);
}

TEST_CASE("Render feature selection retains supported Deferred Hybrid and Irradyn combinations")
{
    Keire::RenderEnvironmentSettings settings;
    settings.RequestedRenderPath = Keire::RenderPath::DeferredHybrid;
    settings.RequestedGlobalIllumination = Keire::GlobalIlluminationMode::Hybrid;
    Keire::RenderFeatureCapabilities capabilities;
    capabilities.DeferredHybrid = true;
    capabilities.BakedGlobalIllumination = true;
    capabilities.RealtimeGlobalIllumination = true;
    capabilities.IrradynGlobalIllumination = true;

    const auto selection = Keire::ResolveRenderFeatureSelection(settings, capabilities);
    CHECK(selection.EffectivePath == Keire::RenderPath::DeferredHybrid);
    CHECK(selection.PathFallback == Keire::RenderPathFallbackReason::None);
    CHECK(selection.EffectiveGlobalIllumination == Keire::GlobalIlluminationMode::Hybrid);
    CHECK(selection.GlobalIlluminationFallback == Keire::GlobalIlluminationFallbackReason::None);

    capabilities.BakedGlobalIllumination = false;
    const auto irradynFallback = Keire::ResolveRenderFeatureSelection(settings, capabilities);
    CHECK(irradynFallback.EffectiveGlobalIllumination == Keire::GlobalIlluminationMode::Irradyn);
    CHECK(irradynFallback.GlobalIlluminationFallback == Keire::GlobalIlluminationFallbackReason::HybridUnavailable);
}
