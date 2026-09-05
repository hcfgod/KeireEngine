#include "Keire/Application.h"
#include "Keire/ECS/Components/DirectionalLightComponent.h"
#include "Keire/ECS/Components/MeshRendererComponent.h"
#include "Keire/ECS/Components/TransformComponent.h"
#include "Keire/Scenes/Scene.h"
#include "KeireInternal/RenderInternal.h"
#include "KeireRenderTests/RenderedOutputTestSupport.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>
#include <utility>
#include <vector>

namespace
{
    struct AntiAliasingCase final
    {
        Keire::RenderPath Path = Keire::RenderPath::ForwardPlus;
        Keire::RenderAntiAliasingMode AntiAliasing = Keire::RenderAntiAliasingMode::None;
        Keire::GlobalIlluminationMode GlobalIllumination = Keire::GlobalIlluminationMode::Disabled;
    };

    struct AntiAliasingCaseResult final
    {
        AntiAliasingCase Requested;
        Keire::RenderFeatureSelection Selection;
        Keire::RenderSampleCount SurfaceSamples = Keire::RenderSampleCount::One;
        std::vector<std::uint8_t> Pixels;
    };

    struct AntiAliasingMatrixResults final
    {
        std::vector<AntiAliasingCaseResult> Cases;
    };

    class AntiAliasingMatrixLayer final : public Keire::Layer
    {
      public:
        explicit AntiAliasingMatrixLayer(std::shared_ptr<AntiAliasingMatrixResults> results)
            : Layer("Anti-aliasing rendered matrix"), m_Results(std::move(results))
        {
        }

      protected:
        void OnAttach() override
        {
            m_Scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Parse("711ace00-0000-4000-8000-000000000031"),
                                                     Keire::SceneAsset::EmptyDefinition("Anti-aliasing matrix"),
                                                     Keire::ComponentRegistry::CreateDefault());
            auto object = m_Scene->CreateEntity("Rendered cube");
            m_Renderer = object.AddComponent<Keire::MeshRendererComponent>();

            auto light = m_Scene->CreateEntity("Directional light");
            m_LightTransform = light.GetComponent<Keire::TransformComponent>();
            m_Light = light.AddComponent<Keire::DirectionalLightComponent>();
            m_Light->SetIntensity(2.0F);
            m_LightTransform->SetLocalEulerAngles({25.0F, 160.0F, 0.0F});

            Keire::RenderSurfaceSpecification surface;
            surface.Name = "Anti-aliasing rendered matrix";
            surface.Width = SurfaceSize;
            surface.Height = SurfaceSize;
            surface.SampleCount = Keire::RenderSampleCount::One;
            surface.ClearColor = {0.0F, 0.0F, 0.0F, 1.0F};
            m_View = Owner().Renderer()->CreateView(surface);
            Keire::RenderCamera camera;
            camera.View = Keire::Math::LookAt({0.0F, 0.0F, 2.5F}, {}, {0.0F, 1.0F, 0.0F});
            camera.Projection = Keire::Math::Perspective(55.0F, 1.0F, 0.1F, 100.0F);
            camera.ClearColor = surface.ClearColor;
            m_View->SetCamera(camera);

            const auto capabilities = Owner().Renderer()->FeatureCapabilities();
            constexpr std::array paths{Keire::RenderPath::ForwardPlus, Keire::RenderPath::DeferredHybrid};
            constexpr std::array antiAliasingModes{
                Keire::RenderAntiAliasingMode::None, Keire::RenderAntiAliasingMode::Fxaa,
                Keire::RenderAntiAliasingMode::Taa, Keire::RenderAntiAliasingMode::Msaa2,
                Keire::RenderAntiAliasingMode::Msaa4};
            constexpr std::array illuminationModes{
                Keire::GlobalIlluminationMode::Disabled, Keire::GlobalIlluminationMode::Baked,
                Keire::GlobalIlluminationMode::Realtime, Keire::GlobalIlluminationMode::Irradyn,
                Keire::GlobalIlluminationMode::Hybrid};
            for (const auto antiAliasing : antiAliasingModes)
            {
                if ((antiAliasing == Keire::RenderAntiAliasingMode::Msaa2 && !capabilities.Msaa2) ||
                    (antiAliasing == Keire::RenderAntiAliasingMode::Msaa4 && !capabilities.Msaa4))
                    continue;
                for (const auto path : paths)
                {
                    if (path == Keire::RenderPath::DeferredHybrid && !capabilities.DeferredHybrid)
                        continue;
                    if (path == Keire::RenderPath::DeferredHybrid &&
                        (antiAliasing == Keire::RenderAntiAliasingMode::Msaa2 ||
                         antiAliasing == Keire::RenderAntiAliasingMode::Msaa4) &&
                        !capabilities.DeferredMultisample)
                        continue;
                    for (const auto illumination : illuminationModes)
                        m_Cases.push_back({path, antiAliasing, illumination});
                }
            }
        }

        void OnDetach() noexcept override
        {
            if (m_Scene)
                m_Scene->Close();
            m_Light.Reset();
            m_LightTransform.Reset();
            m_Renderer.Reset();
            m_View.Reset();
            m_Scene.Reset();
        }

        void OnUpdate(const Keire::Time&) override
        {
            if (m_Submitted)
            {
                m_Results->Cases.back().Pixels =
                    Keire::RenderSystemInternalAccess::ReadbackRGBA8(*Owner().Renderer(), *m_View->Surface());
                ++m_NextCase;
                m_Submitted = false;
            }
            if (m_NextCase == m_Cases.size())
            {
                Owner().RequestExit();
                return;
            }

            const auto& requested = m_Cases[m_NextCase];
            Keire::RenderEnvironmentSettings environment;
            environment.AmbientIntensity = 0.0F;
            environment.SkyVisible = false;
            environment.RequestedRenderPath = requested.Path;
            environment.RequestedAntiAliasing = requested.AntiAliasing;
            environment.RequestedGlobalIllumination = requested.GlobalIllumination;
            const auto selection =
                Keire::ResolveRenderFeatureSelection(environment, Owner().Renderer()->FeatureCapabilities());
            const auto samples = Keire::ResolveRenderSurfaceSampleCount(selection);
            m_View->Surface()->RequestSampleCount(samples);
            m_Results->Cases.push_back({requested, selection, samples, {}});
            Owner().Renderer()->Submit({m_Scene, m_View, false, environment});
            m_Submitted = true;
        }

      private:
        std::shared_ptr<AntiAliasingMatrixResults> m_Results;
        Keire::Ref<Keire::Scene> m_Scene;
        Keire::Ref<Keire::RenderView> m_View;
        Keire::Ref<Keire::MeshRendererComponent> m_Renderer;
        Keire::Ref<Keire::TransformComponent> m_LightTransform;
        Keire::Ref<Keire::DirectionalLightComponent> m_Light;
        std::vector<AntiAliasingCase> m_Cases;
        std::size_t m_NextCase = 0;
        bool m_Submitted = false;
    };

    struct TemporalStabilityResults final
    {
        std::array<std::vector<std::vector<std::uint8_t>>, 2> Frames;
    };

    class TemporalStabilityLayer final : public Keire::Layer
    {
      public:
        explicit TemporalStabilityLayer(std::shared_ptr<TemporalStabilityResults> results)
            : Layer("Temporal anti-aliasing stability"), m_Results(std::move(results))
        {
        }

      protected:
        void OnAttach() override
        {
            m_Scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Parse("711ace00-0000-4000-8000-000000000032"),
                                                     Keire::SceneAsset::EmptyDefinition("Temporal stability"),
                                                     Keire::ComponentRegistry::CreateDefault());
            auto object = m_Scene->CreateEntity("Static cube");
            m_Renderer = object.AddComponent<Keire::MeshRendererComponent>();
            object.GetComponent<Keire::TransformComponent>()->SetLocalEulerAngles({17.0F, 31.0F, 9.0F});

            Keire::RenderSurfaceSpecification surface;
            surface.Name = "Temporal anti-aliasing stability";
            surface.Width = SurfaceSize;
            surface.Height = SurfaceSize;
            surface.SampleCount = Keire::RenderSampleCount::One;
            surface.ClearColor = {0.0F, 0.0F, 0.0F, 1.0F};
            m_View = Owner().Renderer()->CreateView(surface);
            Keire::RenderCamera camera;
            camera.View = Keire::Math::LookAt({0.0F, 0.0F, 2.5F}, {}, {0.0F, 1.0F, 0.0F});
            camera.Projection = Keire::Math::Perspective(55.0F, 1.0F, 0.1F, 100.0F);
            camera.ClearColor = surface.ClearColor;
            m_View->SetCamera(camera);
        }

        void OnDetach() noexcept override
        {
            if (m_Scene)
                m_Scene->Close();
            m_Renderer.Reset();
            m_View.Reset();
            m_Scene.Reset();
        }

        void OnUpdate(const Keire::Time&) override
        {
            if (m_Submitted)
            {
                m_Results->Frames[m_PathIndex].push_back(
                    Keire::RenderSystemInternalAccess::ReadbackRGBA8(*Owner().Renderer(), *m_View->Surface()));
                ++m_FrameInPath;
                m_Submitted = false;
            }
            if (m_FrameInPath == FramesPerPath)
            {
                ++m_PathIndex;
                m_FrameInPath = 0U;
            }
            if (m_PathIndex == Paths.size())
            {
                Owner().RequestExit();
                return;
            }

            Keire::RenderEnvironmentSettings environment;
            environment.AmbientColor = {1.0F, 1.0F, 1.0F, 1.0F};
            environment.AmbientIntensity = 1.0F;
            environment.SkyVisible = false;
            environment.RequestedRenderPath = Paths[m_PathIndex];
            environment.RequestedAntiAliasing = Keire::RenderAntiAliasingMode::Taa;
            environment.RequestedGlobalIllumination = Keire::GlobalIlluminationMode::Realtime;
            Owner().Renderer()->Submit({m_Scene, m_View, false, environment});
            m_Submitted = true;
        }

      private:
        static constexpr std::array Paths{Keire::RenderPath::ForwardPlus, Keire::RenderPath::DeferredHybrid};
        static constexpr std::size_t FramesPerPath = 40U;

        std::shared_ptr<TemporalStabilityResults> m_Results;
        Keire::Ref<Keire::Scene> m_Scene;
        Keire::Ref<Keire::RenderView> m_View;
        Keire::Ref<Keire::MeshRendererComponent> m_Renderer;
        std::size_t m_PathIndex = 0U;
        std::size_t m_FrameInPath = 0U;
        bool m_Submitted = false;
    };

    [[nodiscard]] std::pair<float, float> LuminanceCentroid(const std::vector<std::uint8_t>& pixels)
    {
        float total = 0.0F;
        float weightedX = 0.0F;
        float weightedY = 0.0F;
        for (std::uint32_t y = 0U; y < SurfaceSize; ++y)
        {
            for (std::uint32_t x = 0U; x < SurfaceSize; ++x)
            {
                const auto offset = (static_cast<std::size_t>(y) * SurfaceSize + x) * 4U;
                const float luminance = 0.2126F * static_cast<float>(pixels[offset]) +
                                        0.7152F * static_cast<float>(pixels[offset + 1U]) +
                                        0.0722F * static_cast<float>(pixels[offset + 2U]);
                total += luminance;
                weightedX += luminance * static_cast<float>(x);
                weightedY += luminance * static_cast<float>(y);
            }
        }
        return total > 0.0F ? std::pair{weightedX / total, weightedY / total} : std::pair{0.0F, 0.0F};
    }

    [[nodiscard]] bool HasVisibleRgb(const std::vector<std::uint8_t>& pixels) noexcept
    {
        for (std::size_t offset = 0U; offset + 3U < pixels.size(); offset += 4U)
        {
            if (std::max({pixels[offset], pixels[offset + 1U], pixels[offset + 2U]}) > 64U)
                return true;
        }
        return false;
    }
} // namespace

TEST_CASE("every supported anti-aliasing mode renders across paths and requested GI modes")
{
    const auto results = std::make_shared<AntiAliasingMatrixResults>();
    Keire::Application application(RenderTestSpecification());
    (void)application.PushLayer(std::make_unique<AntiAliasingMatrixLayer>(results));
    REQUIRE(application.Run() == 0);
    REQUIRE_FALSE(results->Cases.empty());
    for (const auto& result : results->Cases)
    {
        CAPTURE(result.Requested.Path);
        CAPTURE(result.Requested.AntiAliasing);
        CAPTURE(result.Requested.GlobalIllumination);
        CHECK(result.Selection.EffectivePath == result.Requested.Path);
        CHECK(result.Selection.PathFallback == Keire::RenderPathFallbackReason::None);
        CHECK(result.Selection.EffectiveAntiAliasing == result.Requested.AntiAliasing);
        CHECK(result.Selection.AntiAliasingFallback == Keire::AntiAliasingFallbackReason::None);
        if (result.Requested.Path == Keire::RenderPath::ForwardPlus &&
            result.Requested.GlobalIllumination == Keire::GlobalIlluminationMode::Irradyn)
        {
            CHECK(result.Selection.EffectiveGlobalIllumination == Keire::GlobalIlluminationMode::Realtime);
            CHECK(result.Selection.GlobalIlluminationFallback ==
                  Keire::GlobalIlluminationFallbackReason::IrradynRequiresDeferredHybrid);
        }
        else if (result.Requested.Path == Keire::RenderPath::ForwardPlus &&
                 result.Requested.GlobalIllumination == Keire::GlobalIlluminationMode::Hybrid)
        {
            CHECK(result.Selection.EffectiveGlobalIllumination == Keire::GlobalIlluminationMode::Realtime);
            CHECK(result.Selection.GlobalIlluminationFallback ==
                  Keire::GlobalIlluminationFallbackReason::HybridUnavailable);
        }
        else
        {
            CHECK(result.Selection.EffectiveGlobalIllumination == result.Requested.GlobalIllumination);
            CHECK(result.Selection.GlobalIlluminationFallback == Keire::GlobalIlluminationFallbackReason::None);
        }
        CHECK(result.SurfaceSamples == Keire::ResolveRenderSurfaceSampleCount(result.Selection));
        if (result.Requested.Path == Keire::RenderPath::DeferredHybrid &&
            (result.Requested.AntiAliasing == Keire::RenderAntiAliasingMode::Msaa2 ||
             result.Requested.AntiAliasing == Keire::RenderAntiAliasingMode::Msaa4))
        {
            CHECK(result.SurfaceSamples != Keire::RenderSampleCount::One);
        }
        REQUIRE(result.Pixels.size() == static_cast<std::size_t>(SurfaceSize * SurfaceSize * 4U));
        CHECK(HasVisibleRgb(result.Pixels));
    }
}

TEST_CASE("static TAA output remains spatially stable in Forward+ and Deferred Hybrid")
{
    const auto results = std::make_shared<TemporalStabilityResults>();
    Keire::Application application(RenderTestSpecification());
    (void)application.PushLayer(std::make_unique<TemporalStabilityLayer>(results));
    REQUIRE(application.Run() == 0);
    for (const auto& pathFrames : results->Frames)
    {
        REQUIRE(pathFrames.size() == 40U);
        float minimumX = std::numeric_limits<float>::max();
        float minimumY = std::numeric_limits<float>::max();
        float maximumX = std::numeric_limits<float>::lowest();
        float maximumY = std::numeric_limits<float>::lowest();
        for (const auto& frame : std::span(pathFrames).last(8U))
        {
            REQUIRE(frame.size() == static_cast<std::size_t>(SurfaceSize * SurfaceSize * 4U));
            const auto [x, y] = LuminanceCentroid(frame);
            minimumX = std::min(minimumX, x);
            minimumY = std::min(minimumY, y);
            maximumX = std::max(maximumX, x);
            maximumY = std::max(maximumY, y);
        }
        INFO("TAA centroid X motion: ", maximumX - minimumX);
        INFO("TAA centroid Y motion: ", maximumY - minimumY);
        CHECK(maximumX - minimumX < 0.1F);
        CHECK(maximumY - minimumY < 0.1F);
    }
}
