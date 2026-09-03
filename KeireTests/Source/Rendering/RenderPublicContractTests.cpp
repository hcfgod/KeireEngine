#include "Keire/Core.h"
#include "KeireInternal/RenderInternal.h"

#include <SDL3/SDL.h>
#include <doctest/doctest.h>

#include <cmath>
#include <cstdlib>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
    void UseDummyVideoDriver()
    {
#if defined(_WIN32)
        REQUIRE(_putenv_s("SDL_VIDEODRIVER", "dummy") == 0);
#else
        REQUIRE(setenv("SDL_VIDEODRIVER", "dummy", 1) == 0);
#endif
        REQUIRE(SDL_SetHintWithPriority(SDL_HINT_VIDEO_DRIVER, "dummy", SDL_HINT_OVERRIDE));
    }

    struct PublicRenderContractProbe final
    {
        bool CameraValidationCompleted = false;
        bool EnvironmentValidationCompleted = false;
        bool AdditiveSceneValidationCompleted = false;
        bool SurfaceSampleValidationCompleted = false;
    };

    [[nodiscard]] std::vector<std::pair<std::string_view, Keire::RenderEnvironmentSettings>> InvalidEnvironments()
    {
        std::vector<std::pair<std::string_view, Keire::RenderEnvironmentSettings>> result;
        const auto add = [&result](const std::string_view name, const auto mutate)
        {
            Keire::RenderEnvironmentSettings environment;
            mutate(environment);
            result.emplace_back(name, environment);
        };
        add("schema", [](auto& environment) { environment.SchemaVersion = 2U; });
        add("rotation",
            [](auto& environment) { environment.EnvironmentRotationDegrees = std::numeric_limits<float>::infinity(); });
        add("diffuse intensity", [](auto& environment) { environment.EnvironmentDiffuseIntensity = -0.01F; });
        add("specular intensity", [](auto& environment)
            { environment.EnvironmentSpecularIntensity = std::numeric_limits<float>::quiet_NaN(); });
        add("shadow distance", [](auto& environment) { environment.DirectionalShadowDistance = 0.0F; });
        add("cascade count", [](auto& environment) { environment.DirectionalShadowCascadeCount = 5U; });
        add("shadow resolution", [](auto& environment) { environment.DirectionalShadowResolution = 257U; });
        add("split lambda", [](auto& environment) { environment.DirectionalShadowSplitLambda = 1.01F; });
        add("occlusion mode",
            [](auto& environment) { environment.GpuOcclusion = static_cast<Keire::GpuOcclusionMode>(255U); });
        add("render path",
            [](auto& environment) { environment.RequestedRenderPath = static_cast<Keire::RenderPath>(255U); });
        add("anti-aliasing", [](auto& environment)
            { environment.RequestedAntiAliasing = static_cast<Keire::RenderAntiAliasingMode>(255U); });
        add("dynamic resolution", [](auto& environment)
            { environment.RequestedDynamicResolution = static_cast<Keire::DynamicResolutionMode>(255U); });
        add("render scale", [](auto& environment) { environment.RenderScale = 0.49F; });
        add("dynamic resolution range",
            [](auto& environment)
            {
                environment.MinimumDynamicResolutionScale = 0.9F;
                environment.MaximumDynamicResolutionScale = 0.8F;
            });
        add("dynamic resolution target",
            [](auto& environment) { environment.DynamicResolutionTargetMilliseconds = 50.01F; });
        add("global illumination", [](auto& environment)
            { environment.RequestedGlobalIllumination = static_cast<Keire::GlobalIlluminationMode>(255U); });
        add("Irradyn quality",
            [](auto& environment) { environment.RequestedIrradynQuality = static_cast<Keire::IrradynQuality>(255U); });
        return result;
    }

    class PublicRenderContractLayer final : public Keire::Layer
    {
      public:
        explicit PublicRenderContractLayer(PublicRenderContractProbe& probe)
            : Layer("Public render contract"), m_Probe(probe)
        {
        }

      protected:
        void OnAttach() override
        {
            m_Scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(),
                                                     Keire::SceneAsset::EmptyDefinition("Public render contract"));
            m_AdditionalScene = Keire::CreateRef<Keire::Scene>(
                Keire::AssetId::Generate(), Keire::SceneAsset::EmptyDefinition("Additional render contribution"));
            (void)m_Scene->CreateEntity("Primary renderer").AddComponent<Keire::MeshRendererComponent>();
            (void)m_AdditionalScene->CreateEntity("Additional renderer").AddComponent<Keire::MeshRendererComponent>();
            m_View = Owner().Renderer()->CreateView({.Name = "Public render contract", .Width = 64, .Height = 64});

            const auto surface = m_View->Surface();
            const auto initialGeneration = surface->Generation();
            CHECK_NOTHROW(surface->RequestSampleCount(Keire::RenderSampleCount::Two));
            CHECK(surface->Generation() == initialGeneration + 1U);
            CHECK_NOTHROW(surface->RequestSampleCount(Keire::RenderSampleCount::Two));
            CHECK(surface->Generation() == initialGeneration + 1U);
            CHECK_THROWS_AS(surface->RequestSampleCount(static_cast<Keire::RenderSampleCount>(3U)),
                            std::invalid_argument);
            CHECK(surface->Generation() == initialGeneration + 1U);
            m_Probe.SurfaceSampleValidationCompleted = true;

            auto camera = m_View->Camera();
            camera.NearPlane = std::numeric_limits<float>::min();
            camera.FarPlane = 10'000'000.0F;
            CHECK_NOTHROW(m_View->SetCamera(camera));
            CHECK(m_View->Camera().NearPlane == camera.NearPlane);
            CHECK(m_View->Camera().FarPlane == camera.FarPlane);

            const auto accepted = m_View->Camera();
            for (const auto [nearPlane, farPlane] :
                 {std::pair{std::numeric_limits<float>::quiet_NaN(), 1.0F},
                  std::pair{0.1F, std::numeric_limits<float>::infinity()}, std::pair{0.0F, 1.0F}, std::pair{1.0F, 1.0F},
                  std::pair{2.0F, 1.0F},
                  std::pair{0.1F, std::nextafter(10'000'000.0F, std::numeric_limits<float>::infinity())}})
            {
                CAPTURE(nearPlane);
                CAPTURE(farPlane);
                auto invalid = accepted;
                invalid.NearPlane = nearPlane;
                invalid.FarPlane = farPlane;
                CHECK_THROWS_AS(m_View->SetCamera(invalid), std::invalid_argument);
                CHECK(m_View->Camera().NearPlane == accepted.NearPlane);
                CHECK(m_View->Camera().FarPlane == accepted.FarPlane);
            }
            m_Probe.CameraValidationCompleted = true;
        }

        void OnUpdate(const Keire::Time&) override
        {
            const auto renderer = Owner().Renderer();
            for (const auto& [name, environment] : InvalidEnvironments())
            {
                CAPTURE(name);
                CHECK_THROWS_AS(Keire::ValidateRenderEnvironmentSettings(environment), std::invalid_argument);
                Keire::SceneRenderRequest request{m_Scene, m_View};
                request.Environment = environment;
                CHECK_THROWS_AS(renderer->Submit(std::move(request)), std::invalid_argument);
            }

            Keire::RenderEnvironmentSettings valid;
            valid.AmbientColor = {0.0F, 1.0F, 0.0F, 1.0F};
            valid.AmbientIntensity = 16.0F;
            valid.Exposure = 0.01F;
            valid.EnvironmentDiffuseIntensity = 0.0F;
            valid.EnvironmentSpecularIntensity = 16.0F;
            valid.DirectionalShadowDistance = 100'000.0F;
            valid.DirectionalShadowCascadeCount = 4U;
            valid.DirectionalShadowResolution = 8192U;
            valid.DirectionalShadowSplitLambda = 1.0F;
            valid.GpuOcclusion = Keire::GpuOcclusionMode::Forced;
            CHECK_NOTHROW(Keire::ValidateRenderEnvironmentSettings(valid));

            Keire::SceneRenderRequest rejected{m_Scene, m_View};
            rejected.AdditionalScenes.push_back({});
            CHECK_THROWS_AS(renderer->Submit(std::move(rejected)), std::invalid_argument);

            Keire::SceneRenderRequest request{m_Scene, m_View};
            request.Environment = valid;
            request.AdditionalScenes.push_back({m_AdditionalScene});
            CHECK_NOTHROW(renderer->Submit(std::move(request)));
            CHECK(Keire::RenderSystemInternalAccess::SceneContributionCount(*renderer, *m_View->Surface()) == 2U);
            CHECK(Keire::RenderSystemInternalAccess::SceneDrawItemCount(*renderer, *m_View->Surface()) == 2U);
            Keire::SceneRenderRequest duplicate{m_Scene, m_View};
            CHECK_THROWS_AS(renderer->Submit(std::move(duplicate)), std::logic_error);
            m_Probe.EnvironmentValidationCompleted = true;
            m_Probe.AdditiveSceneValidationCompleted = true;
            Owner().RequestExit();
        }

        void OnDetach() noexcept override
        {
            if (m_Scene)
                m_Scene->Close();
            if (m_AdditionalScene)
                m_AdditionalScene->Close();
        }

      private:
        PublicRenderContractProbe& m_Probe;
        Keire::Ref<Keire::Scene> m_Scene;
        Keire::Ref<Keire::Scene> m_AdditionalScene;
        Keire::Ref<Keire::RenderView> m_View;
    };

    [[nodiscard]] Keire::ApplicationSpecification PublicRenderContractSpecification()
    {
        Keire::ApplicationSpecification specification;
        specification.MainWindow.Title = "Public render contract tests";
        specification.MainWindow.Visible = false;
        specification.Render.Mode = Keire::RenderMode::Headless;
        specification.Ui.Mode = Keire::UiMode::Disabled;
        specification.ManageLogging = false;
        specification.SuspendWhenMainWindowMinimized = false;
        return specification;
    }
} // namespace

TEST_CASE("Public render camera and environment contracts reject invalid values at their API boundaries")
{
    UseDummyVideoDriver();
    PublicRenderContractProbe probe;
    Keire::Application application(PublicRenderContractSpecification());
    (void)application.PushLayer(std::make_unique<PublicRenderContractLayer>(probe));
    CHECK(application.Run() == 0);
    CHECK(probe.CameraValidationCompleted);
    CHECK(probe.EnvironmentValidationCompleted);
    CHECK(probe.AdditiveSceneValidationCompleted);
    CHECK(probe.SurfaceSampleValidationCompleted);
}
