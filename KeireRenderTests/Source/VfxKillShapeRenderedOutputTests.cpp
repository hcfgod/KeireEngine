#include "Keire/Application.h"
#include "Keire/Scenes/Scene.h"
#include "Keire/Vfx/VfxSystem.h"
#include "KeireInternal/RenderInternal.h"

#include <SDL3/SDL.h>
#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace
{
    constexpr std::uint32_t SurfaceSize = 96;

    struct VfxChannelSignal final
    {
        float Weight = 0.0F;
    };

    struct CaptureResults final
    {
        std::vector<std::vector<std::uint8_t>> Frames;
        Keire::RenderStatistics Statistics;
        bool HasStatistics = false;
    };

    [[nodiscard]] VfxChannelSignal MeasureChannelSignal(const std::vector<std::uint8_t>& pixels,
                                                        const std::size_t channel)
    {
        REQUIRE(pixels.size() == static_cast<std::size_t>(SurfaceSize * SurfaceSize * 4));
        REQUIRE(channel < 3);
        VfxChannelSignal result;
        for (std::uint32_t y = 0; y < SurfaceSize; ++y)
        {
            for (std::uint32_t x = 0; x < SurfaceSize; ++x)
            {
                const auto offset = (static_cast<std::size_t>(y) * static_cast<std::size_t>(SurfaceSize) + x) * 4U;
                const auto firstOther = (channel + 1) % 3;
                const auto secondOther = (channel + 2) % 3;
                const auto value = static_cast<float>(pixels[offset + channel]);
                const auto other =
                    static_cast<float>(std::max(pixels[offset + firstOther], pixels[offset + secondOther]));
                result.Weight += std::max(value - other, 0.0F) / 255.0F;
            }
        }
        return result;
    }

    [[nodiscard]] constexpr Keire::AssetId RenderVfxId(const std::uint64_t value) noexcept
    {
        return Keire::AssetId(0x4b494c4c53485045ULL, value);
    }

    [[nodiscard]] Keire::Ref<Keire::VfxEffectAsset> KillShapeEffect(const Keire::VfxKillShapeMode mode)
    {
        Keire::VfxEffectDefinition definition;
        definition.EmitterId = RenderVfxId(mode == Keire::VfxKillShapeMode::Solid ? 1 : 2);
        definition.Name = mode == Keire::VfxKillShapeMode::Solid ? "GPU solid Kill Shape" : "GPU inverted Kill Shape";
        definition.Duration = 4.0F;
        definition.Capacity = 16;
        definition.Modules = {
            {RenderVfxId(3), true, Keire::VfxBurstModule{0.0F, 8, 1, 0.1F}},
            {RenderVfxId(4), true, Keire::VfxShapeModule{}},
            {RenderVfxId(5), true, Keire::VfxInitializeModule{4.0F, 4.0F, {}, {}, {}, {}}},
            {RenderVfxId(6), true,
             Keire::VfxKillShapeModule{Keire::VfxShape::Sphere, {}, {1.0F, 1.0F, 1.0F}, 1.0F, mode}},
            {RenderVfxId(7), true, Keire::VfxSizeOverLifetimeModule{Keire::Curve1D::Constant(0.3F)}},
            {RenderVfxId(8), true,
             Keire::VfxColorOverLifetimeModule{Keire::ColorGradient::Constant({1.0F, 0.0F, 0.0F, 1.0F})}},
            {RenderVfxId(9), true, Keire::VfxRendererModule{}},
        };
        definition = Keire::ConvertVfxEffectToGraph(definition);
        Keire::ValidateVfxEffect(definition);
        return Keire::CreateRef<Keire::VfxEffectAsset>(std::move(definition));
    }

    [[nodiscard]] Keire::ApplicationSpecification RenderTestSpecification()
    {
        const char* backend = SDL_GetEnvironmentVariable(SDL_GetEnvironment(), "KEIRE_GPU_TEST_BACKEND");
        if (backend && *backend && !SDL_SetHintWithPriority(SDL_HINT_GPU_DRIVER, backend, SDL_HINT_OVERRIDE))
            throw std::runtime_error("Could not restore the requested GPU backend after SDL shutdown.");
        Keire::ApplicationSpecification specification;
        specification.MainWindow.Title = "Kéire Kill Shape render tests";
        specification.MainWindow.Width = SurfaceSize;
        specification.MainWindow.Height = SurfaceSize;
        specification.MainWindow.Visible = false;
        specification.Render.Mode = Keire::RenderMode::Rendered;
        specification.Render.PreferredSampleCount = Keire::RenderSampleCount::One;
        specification.Render.MaximumFramesInFlight = 1;
        specification.Render.EnableGpuValidation =
            SDL_GetEnvironmentVariable(SDL_GetEnvironment(), "KEIRE_GPU_VALIDATION") != nullptr;
        specification.Ui.Mode = Keire::UiMode::Disabled;
        specification.Input.Mode = Keire::InputMode::Disabled;
        specification.Scenes.Mode = Keire::SceneMode::Disabled;
        specification.ManageLogging = false;
        specification.SuspendWhenMainWindowMinimized = false;
        return specification;
    }

    class KillShapeCaptureLayer final : public Keire::Layer
    {
      public:
        KillShapeCaptureLayer(Keire::Ref<Keire::VfxEffectAsset> effect, std::shared_ptr<CaptureResults> results)
            : Layer("Kill Shape capture"), m_Effect(std::move(effect)), m_Results(std::move(results))
        {
        }

      protected:
        void OnAttach() override
        {
            m_Scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(),
                                                     Keire::SceneAsset::EmptyDefinition("Kill Shape capture"),
                                                     Keire::ComponentRegistry::CreateDefault());
            Keire::RenderSurfaceSpecification surface;
            surface.Name = "Kill Shape capture";
            surface.Width = SurfaceSize;
            surface.Height = SurfaceSize;
            surface.ClearColor = {0.0F, 0.0F, 0.0F, 1.0F};
            surface.SampleCount = Keire::RenderSampleCount::One;
            surface.Depth = true;
            m_View = Owner().Renderer()->CreateView(surface);
            Keire::RenderCamera camera;
            camera.View = Keire::Math::LookAt({0.0F, 0.0F, 2.5F}, {}, {0.0F, 1.0F, 0.0F});
            camera.Projection = Keire::Math::Perspective(55.0F, 1.0F, 0.1F, 100.0F);
            camera.ClearColor = surface.ClearColor;
            m_View->SetCamera(camera);
            m_Environment.SkyVisible = false;
            m_Environment.AmbientColor = {1.0F, 1.0F, 1.0F, 1.0F};
            m_Environment.AmbientIntensity = 1.0F;

            const auto compiled = Keire::CompileVfxEffect(m_Effect->Definition(), Keire::VfxBackend::Gpu);
            if (!compiled.Valid)
                throw std::logic_error("Rendered Kill Shape effect did not compile for the GPU backend.");
            m_World = Keire::CreateRef<Keire::VfxWorld>(Keire::VfxWorldSpecification{
                .MaximumEffects = 1, .MaximumParticles = 16, .Backend = Keire::VfxBackend::Gpu});
            if (!m_World->Activate({m_Effect}))
                throw std::logic_error("Could not activate the rendered Kill Shape effect.");
            m_World->Update(0.01F);
        }

        void OnDetach() noexcept override
        {
            m_World.Reset();
            m_Effect.Reset();
            if (m_Scene)
                m_Scene->Close();
            m_View.Reset();
            m_Scene.Reset();
        }

        void OnUpdate(const Keire::Time&) override
        {
            if (!m_Submitted)
            {
                Submit();
                m_Submitted = true;
                return;
            }
            m_Results->Frames.push_back(
                Keire::RenderSystemInternalAccess::ReadbackRGBA8(*Owner().Renderer(), *m_View->Surface()));
            const auto statistics = Owner().Renderer()->Statistics();
            m_Results->Statistics.VfxGpuWorlds = std::max(m_Results->Statistics.VfxGpuWorlds, statistics.VfxGpuWorlds);
            m_Results->HasStatistics = true;
            if (m_Results->Frames.size() >= 60)
            {
                Owner().RequestExit();
                return;
            }
            m_World->Update(1.0F / 60.0F);
            Submit();
        }

      private:
        void Submit()
        {
            Owner().Renderer()->Submit({m_Scene, m_View, false, m_Environment, {}, m_World->CaptureRenderSnapshot()});
        }

        Keire::Ref<Keire::VfxEffectAsset> m_Effect;
        std::shared_ptr<CaptureResults> m_Results;
        Keire::Ref<Keire::Scene> m_Scene;
        Keire::Ref<Keire::RenderView> m_View;
        Keire::Ref<Keire::VfxWorld> m_World;
        Keire::RenderEnvironmentSettings m_Environment;
        bool m_Submitted = false;
    };
} // namespace

TEST_CASE("GPU Kill Shape applies Solid and Inverted sphere semantics in render readback")
{
    std::array<float, 2> lateRed{};
    for (const auto mode : {Keire::VfxKillShapeMode::Solid, Keire::VfxKillShapeMode::Inverted})
    {
        CAPTURE(mode);
        const auto results = std::make_shared<CaptureResults>();
        {
            Keire::Application application(RenderTestSpecification());
            (void)application.PushLayer(std::make_unique<KillShapeCaptureLayer>(KillShapeEffect(mode), results));
            REQUIRE(application.Run() == 0);
        }

        REQUIRE(results->Frames.size() == 60);
        const auto resultIndex = mode == Keire::VfxKillShapeMode::Solid ? 0U : 1U;
        for (std::size_t index = 40; index < results->Frames.size(); ++index)
        {
            const auto red = MeasureChannelSignal(results->Frames[index], 0);
            const auto green = MeasureChannelSignal(results->Frames[index], 1);
            lateRed[resultIndex] = std::max(lateRed[resultIndex], std::max(0.0F, red.Weight - green.Weight));
        }
        REQUIRE(results->HasStatistics);
        CHECK(results->Statistics.VfxGpuWorlds > 0);
    }
    CHECK(lateRed[1] > 10.0F);
    CHECK(lateRed[0] < lateRed[1] * 0.2F);
}
