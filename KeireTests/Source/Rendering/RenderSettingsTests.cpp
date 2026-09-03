#include "Keire/Rendering/RenderSystem.h"
#include "KeireInternal/Rendering/DynamicResolutionInternal.h"
#include "KeireInternal/Rendering/GlobalIlluminationPolicyInternal.h"
#include "KeireInternal/Rendering/TemporalAntiAliasingInternal.h"

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
        if (schema >= 4U)
        {
            result["renderPath"] = "forwardPlus";
            result["globalIllumination"] = "disabled";
            result["irradynQuality"] = "balanced";
        }
        return result;
    }
} // namespace

TEST_CASE("temporal jitter is bounded deterministic and projection-agnostic")
{
    using namespace Keire::RenderBackend::TemporalAntiAliasing;

    for (std::uint64_t frame = 0; frame < 8U; ++frame)
    {
        const auto pixels = TemporalJitterPixels(frame);
        CHECK(pixels.X >= -0.5F);
        CHECK(pixels.X < 0.5F);
        CHECK(pixels.Y >= -0.5F);
        CHECK(pixels.Y < 0.5F);
        CHECK(TemporalJitterPixels(frame + 8U) == pixels);
    }

    const auto ndc = TemporalJitterNdc(0U, 200U, 100U);
    CHECK(ndc.X == doctest::Approx(0.0F));
    CHECK(ndc.Y == doctest::Approx(1.0F / 300.0F));
    const auto jittered = ApplyTemporalProjectionJitter(Keire::Matrix4{}, ndc);
    CHECK(jittered.Elements[12] == doctest::Approx(ndc.X));
    CHECK(jittered.Elements[13] == doctest::Approx(ndc.Y));
    CHECK(TemporalJitterNdc(0U, 0U, 100U) == Keire::Vector2{});
}

TEST_CASE("temporal reprojection applies the current jitter to both camera frames")
{
    using namespace Keire::RenderBackend::TemporalAntiAliasing;

    const auto viewProjection = Keire::Math::Multiply(Keire::Math::Perspective(55.0F, 16.0F / 9.0F, 0.1F, 100.0F),
                                                      Keire::Math::LookAt({0.0F, 2.0F, -5.0F}, {}, {0.0F, 1.0F, 0.0F}));
    const auto jitter = TemporalJitterNdc(2U, 1920U, 1080U);
    const auto current = ApplyTemporalProjectionJitter(viewProjection, jitter);
    const auto previous = ApplyTemporalProjectionJitter(viewProjection, jitter);
    CHECK(current == previous);

    const auto wrongPrevious = ApplyTemporalProjectionJitter(viewProjection, TemporalJitterNdc(1U, 1920U, 1080U));
    CHECK_FALSE(current == wrongPrevious);
}

TEST_CASE("Rendering settings schema five round trips pipeline temporal and GI choices")
{
    TemporaryDirectory directory;
    for (const auto mode :
         {Keire::GpuOcclusionMode::Disabled, Keire::GpuOcclusionMode::Automatic, Keire::GpuOcclusionMode::Forced})
    {
        for (const auto renderPath :
             {Keire::RenderPath::Automatic, Keire::RenderPath::ForwardPlus, Keire::RenderPath::DeferredHybrid})
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
                    settings.RequestedAntiAliasing = Keire::RenderAntiAliasingMode::Taa;
                    settings.RequestedDynamicResolution = Keire::DynamicResolutionMode::Automatic;
                    settings.RenderScale = 0.8F;
                    settings.MinimumDynamicResolutionScale = 0.6F;
                    settings.MaximumDynamicResolutionScale = 0.9F;
                    settings.DynamicResolutionTargetMilliseconds = 16.667F;
                    settings.RequestedGlobalIllumination = gi;
                    settings.RequestedIrradynQuality = quality;
                    Keire::SaveRenderEnvironmentSettings(directory.Path, settings);
                    CHECK(Keire::LoadRenderEnvironmentSettings(directory.Path) == settings);
                }
            }
        }
    }
}

TEST_CASE("global illumination modes select distinct renderer channels")
{
    using Keire::GlobalIlluminationMode;
    using Keire::IrradynQuality;
    using Keire::RenderBackend::ResolveGlobalIlluminationPolicy;

    const auto disabled = ResolveGlobalIlluminationPolicy(GlobalIlluminationMode::Disabled, IrradynQuality::Balanced);
    CHECK_FALSE(disabled.EnvironmentDiffuse);
    CHECK_FALSE(disabled.EnvironmentSpecular);
    CHECK_FALSE(disabled.BakedLighting);
    CHECK(disabled.IrradynSampleCount == 0);

    const auto baked = ResolveGlobalIlluminationPolicy(GlobalIlluminationMode::Baked, IrradynQuality::Balanced);
    CHECK_FALSE(baked.EnvironmentDiffuse);
    CHECK_FALSE(baked.EnvironmentSpecular);
    CHECK(baked.BakedLighting);
    CHECK(baked.IrradynSampleCount == 0);

    const auto realtime = ResolveGlobalIlluminationPolicy(GlobalIlluminationMode::Realtime, IrradynQuality::Balanced);
    CHECK(realtime.EnvironmentDiffuse);
    CHECK(realtime.EnvironmentSpecular);
    CHECK_FALSE(realtime.BakedLighting);
    CHECK(realtime.IrradynSampleCount == 0);

    const auto irradyn = ResolveGlobalIlluminationPolicy(GlobalIlluminationMode::Irradyn, IrradynQuality::Balanced);
    CHECK(irradyn.EnvironmentDiffuse);
    CHECK(irradyn.EnvironmentSpecular);
    CHECK_FALSE(irradyn.BakedLighting);
    CHECK(irradyn.IrradynSampleCount == 6);
    CHECK(irradyn.IrradynStrength > 0.0F);

    const auto hybrid = ResolveGlobalIlluminationPolicy(GlobalIlluminationMode::Hybrid, IrradynQuality::Quality);
    CHECK(hybrid.EnvironmentDiffuse);
    CHECK(hybrid.EnvironmentSpecular);
    CHECK(hybrid.BakedLighting);
    CHECK(hybrid.IrradynSampleCount == 8);
    CHECK(hybrid.IrradynStrength > irradyn.IrradynStrength);
}

TEST_CASE("Legacy rendering settings migrate to explicit forward rendering with GI disabled")
{
    TemporaryDirectory directory;
    for (const auto schema : {1U, 2U, 3U, 4U})
    {
        WriteSettings(directory.Path, LegacySettings(schema));
        const auto migrated = Keire::LoadRenderEnvironmentSettings(directory.Path);
        CHECK(migrated.SchemaVersion == Keire::RenderEnvironmentSettingsSchemaVersion);
        CHECK(migrated.GpuOcclusion == Keire::GpuOcclusionMode::Automatic);
        CHECK(migrated.RequestedRenderPath == Keire::RenderPath::ForwardPlus);
        CHECK(migrated.RequestedAntiAliasing == Keire::RenderAntiAliasingMode::None);
        CHECK(migrated.RequestedDynamicResolution == Keire::DynamicResolutionMode::Disabled);
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

TEST_CASE("deferred temporal anti-aliasing and dynamic resolution resolve explicitly")
{
    Keire::RenderEnvironmentSettings settings;
    settings.RequestedRenderPath = Keire::RenderPath::Automatic;
    settings.RequestedAntiAliasing = Keire::RenderAntiAliasingMode::Taa;
    settings.RequestedDynamicResolution = Keire::DynamicResolutionMode::Automatic;
    settings.RenderScale = 0.75F;

    Keire::RenderFeatureCapabilities capabilities;
    capabilities.DeferredHybrid = true;
    capabilities.TemporalAntiAliasing = true;
    capabilities.DynamicResolution = true;
    const auto selected = Keire::ResolveRenderFeatureSelection(settings, capabilities);
    CHECK(selected.RequestedPath == Keire::RenderPath::Automatic);
    CHECK(selected.EffectivePath == Keire::RenderPath::DeferredHybrid);
    CHECK(selected.EffectiveAntiAliasing == Keire::RenderAntiAliasingMode::Taa);
    CHECK(selected.EffectiveDynamicResolution == Keire::DynamicResolutionMode::Automatic);
    CHECK_FALSE(selected.TemporalUpsampling);

    settings.RequestedRenderPath = Keire::RenderPath::ForwardPlus;
    capabilities.Fxaa = true;
    const auto forwardTaa = Keire::ResolveRenderFeatureSelection(settings, capabilities);
    CHECK(forwardTaa.EffectiveAntiAliasing == Keire::RenderAntiAliasingMode::Taa);
    CHECK(forwardTaa.AntiAliasingFallback == Keire::AntiAliasingFallbackReason::None);
    CHECK_FALSE(forwardTaa.TemporalUpsampling);

    settings.RequestedRenderPath = Keire::RenderPath::Automatic;

    capabilities.TemporalAntiAliasing = false;
    const auto temporalFallback = Keire::ResolveRenderFeatureSelection(settings, capabilities);
    CHECK(temporalFallback.EffectiveAntiAliasing == Keire::RenderAntiAliasingMode::Fxaa);
    CHECK(temporalFallback.AntiAliasingFallback == Keire::AntiAliasingFallbackReason::TemporalUnavailable);
    CHECK(temporalFallback.EffectiveDynamicResolution == Keire::DynamicResolutionMode::Automatic);
    CHECK(temporalFallback.DynamicResolutionFallback == Keire::DynamicResolutionFallbackReason::None);

    settings.RequestedAntiAliasing = Keire::RenderAntiAliasingMode::Msaa4;
    capabilities.Msaa4 = true;
    capabilities.DeferredMultisample = true;
    const auto deferredMsaa = Keire::ResolveRenderFeatureSelection(settings, capabilities);
    CHECK(deferredMsaa.EffectivePath == Keire::RenderPath::DeferredHybrid);
    CHECK(deferredMsaa.EffectiveAntiAliasing == Keire::RenderAntiAliasingMode::Msaa4);
    CHECK(deferredMsaa.AntiAliasingFallback == Keire::AntiAliasingFallbackReason::None);
}

TEST_CASE("dynamic resolution is anti-aliasing independent across every render path and GI request")
{
    Keire::RenderFeatureCapabilities capabilities;
    capabilities.DeferredHybrid = true;
    capabilities.BakedGlobalIllumination = true;
    capabilities.RealtimeGlobalIllumination = true;
    capabilities.IrradynGlobalIllumination = true;
    capabilities.Fxaa = true;
    capabilities.TemporalAntiAliasing = true;
    capabilities.Msaa2 = true;
    capabilities.Msaa4 = true;
    capabilities.DeferredMultisample = true;
    capabilities.DynamicResolution = true;

    for (const auto path :
         {Keire::RenderPath::Automatic, Keire::RenderPath::ForwardPlus, Keire::RenderPath::DeferredHybrid})
    {
        for (const auto antiAliasing : {Keire::RenderAntiAliasingMode::None, Keire::RenderAntiAliasingMode::Fxaa,
                                        Keire::RenderAntiAliasingMode::Taa, Keire::RenderAntiAliasingMode::Msaa2,
                                        Keire::RenderAntiAliasingMode::Msaa4})
        {
            for (const auto dynamicResolution :
                 {Keire::DynamicResolutionMode::Disabled, Keire::DynamicResolutionMode::Automatic})
            {
                for (const auto illumination :
                     {Keire::GlobalIlluminationMode::Disabled, Keire::GlobalIlluminationMode::Baked,
                      Keire::GlobalIlluminationMode::Realtime, Keire::GlobalIlluminationMode::Irradyn,
                      Keire::GlobalIlluminationMode::Hybrid})
                {
                    Keire::RenderEnvironmentSettings settings;
                    settings.RequestedRenderPath = path;
                    settings.RequestedAntiAliasing = antiAliasing;
                    settings.RequestedDynamicResolution = dynamicResolution;
                    settings.RequestedGlobalIllumination = illumination;
                    const auto selection = Keire::ResolveRenderFeatureSelection(settings, capabilities);
                    CAPTURE(path);
                    CAPTURE(antiAliasing);
                    CAPTURE(dynamicResolution);
                    CAPTURE(illumination);
                    CHECK(selection.RequestedPath == path);
                    CHECK(selection.RequestedAntiAliasing == antiAliasing);
                    CHECK(selection.RequestedDynamicResolution == dynamicResolution);
                    CHECK(selection.RequestedGlobalIllumination == illumination);
                    CHECK(selection.EffectiveAntiAliasing == antiAliasing);
                    CHECK(selection.AntiAliasingFallback == Keire::AntiAliasingFallbackReason::None);
                    CHECK(selection.EffectiveDynamicResolution == dynamicResolution);
                    CHECK(selection.DynamicResolutionFallback == Keire::DynamicResolutionFallbackReason::None);
                }
            }
        }
    }
}

TEST_CASE("dynamic resolution controller reacts conservatively to completed GPU pressure")
{
    Keire::RenderEnvironmentSettings settings;
    settings.RequestedDynamicResolution = Keire::DynamicResolutionMode::Automatic;
    settings.RenderScale = 1.0F;
    settings.MinimumDynamicResolutionScale = 0.6F;
    settings.MaximumDynamicResolutionScale = 1.0F;
    settings.DynamicResolutionTargetMilliseconds = 16.667F;
    Keire::RenderFeatureCapabilities capabilities;
    capabilities.DynamicResolution = true;
    const auto selection = Keire::ResolveRenderFeatureSelection(settings, capabilities);
    Keire::Internal::DynamicResolutionController controller;
    Keire::RenderStatistics statistics;
    statistics.GpuCompletionLatencyMilliseconds = 40.0F;
    statistics.OutstandingFrames = 1;
    for (std::uint64_t frame = 1; frame <= 8; ++frame)
    {
        statistics.Frame = frame;
        (void)controller.Update(settings, selection, statistics);
    }
    const float reducedScale = controller.Scale();
    CHECK(reducedScale < 1.0F);
    CHECK(reducedScale >= 0.6F);

    statistics.GpuCompletionLatencyMilliseconds = 4.0F;
    for (std::uint64_t frame = 9; frame <= 16; ++frame)
    {
        statistics.Frame = frame;
        (void)controller.Update(settings, selection, statistics);
    }
    CHECK(controller.Scale() > reducedScale);
    CHECK(controller.Scale() <= 1.0F);

    settings.RequestedDynamicResolution = Keire::DynamicResolutionMode::Disabled;
    settings.RenderScale = 0.72F;
    const auto disabledSelection = Keire::ResolveRenderFeatureSelection(settings, capabilities);
    CHECK(controller.Update(settings, disabledSelection, statistics) == doctest::Approx(0.72F));
}

TEST_CASE("render surface scaling preserves presentation size while bounding internal pixels")
{
    CHECK(Keire::Internal::ScaledRenderSurfaceExtent(640.0F, 360.0F, 1.5F, 0.5F) ==
          std::pair<std::uint32_t, std::uint32_t>{480U, 270U});
    CHECK(Keire::Internal::ScaledRenderSurfaceExtent(0.0F, 0.0F, 0.0F, 0.0F) ==
          std::pair<std::uint32_t, std::uint32_t>{1U, 1U});
    CHECK(Keire::Internal::ScaledRenderSurfaceExtent(20'000.0F, 20'000.0F, 2.0F, 1.0F) ==
          std::pair<std::uint32_t, std::uint32_t>{16'384U, 16'384U});
}

TEST_CASE("effective anti-aliasing selects the render-surface sample contract")
{
    Keire::RenderFeatureSelection selection;
    for (const auto mode :
         {Keire::RenderAntiAliasingMode::None, Keire::RenderAntiAliasingMode::Fxaa, Keire::RenderAntiAliasingMode::Taa})
    {
        selection.EffectiveAntiAliasing = mode;
        CHECK(Keire::ResolveRenderSurfaceSampleCount(selection) == Keire::RenderSampleCount::One);
    }

    selection.EffectiveAntiAliasing = Keire::RenderAntiAliasingMode::Msaa2;
    CHECK(Keire::ResolveRenderSurfaceSampleCount(selection) == Keire::RenderSampleCount::Two);
    selection.EffectiveAntiAliasing = Keire::RenderAntiAliasingMode::Msaa4;
    CHECK(Keire::ResolveRenderSurfaceSampleCount(selection) == Keire::RenderSampleCount::Four);
}
