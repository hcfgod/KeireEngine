#include "KeireRenderTests/RenderedOutputTestSupport.h"

#include "Keire/ECS/Components/MeshRendererComponent.h"
#include "Keire/ECS/Components/TransformComponent.h"
#include "Keire/Scenes/Scene.h"
#include "Keire/Ui.h"
#include "Keire/Vfx/VfxSystem.h"
#include "KeireInternal/RenderInternal.h"
#include "KeireRenderTests/RenderAssetFixture.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#if defined(KEIRE_ENABLE_TEST_HOOKS)
using KeireRenderTests::Detail::RenderAssetFixture;

namespace
{
    [[nodiscard]] constexpr Keire::AssetId RecoveryVfxId(const std::uint64_t value) noexcept
    {
        return Keire::AssetId(0x5245434f56455259ULL, value);
    }

    [[nodiscard]] Keire::Ref<Keire::VfxEffectAsset> RecoveryGpuEffect()
    {
        Keire::VfxEffectDefinition definition;
        definition.EmitterId = RecoveryVfxId(1U);
        definition.Name = "Device recovery deterministic GPU emitter";
        definition.Duration = 4.0F;
        definition.Capacity = 4U;
        definition.Modules = {
            {RecoveryVfxId(2U), true, Keire::VfxBurstModule{0.0F, 1U, 1U, 0.1F}},
            {RecoveryVfxId(3U), true, Keire::VfxShapeModule{}},
            {RecoveryVfxId(4U), true, Keire::VfxInitializeModule{4.0F, 4.0F, {}, {}, {}, {}}},
            {RecoveryVfxId(5U), true, Keire::VfxSizeOverLifetimeModule{Keire::Curve1D::Constant(0.2F)}},
            {RecoveryVfxId(6U), true,
             Keire::VfxColorOverLifetimeModule{Keire::ColorGradient::Constant({1.0F, 0.0F, 0.0F, 1.0F})}},
            {RecoveryVfxId(7U), true, Keire::VfxRendererModule{}},
        };
        definition = Keire::ConvertVfxEffectToGraph(definition);
        Keire::ValidateVfxEffect(definition);
        return Keire::CreateRef<Keire::VfxEffectAsset>(std::move(definition));
    }

    struct RecoveryRestorationResults final
    {
        std::optional<Keire::GpuDeviceLossDiagnostic> Diagnostic;
        Keire::RenderRecoveryResourceCounts Before;
        Keire::RenderRecoveryResourceCounts AfterRetry;
        Keire::RenderRecoveryResourceCounts AfterContinuation;
        std::array<std::uint64_t, 2> VfxSignatures{};
        std::uint64_t RetriedVfxSnapshots = 0U;
        std::vector<std::uint8_t> BeforePixels;
        std::vector<std::uint8_t> AfterPixels;
        std::uint64_t SurfaceEpochBefore = 0U;
        std::uint64_t SurfaceEpochAfter = 0U;
        std::uint64_t SurfaceResourceGenerationBefore = 0U;
        std::uint64_t SurfaceResourceGenerationAfter = 0U;
        bool SurfaceAvailableAfter = false;
        bool Closed = false;
    };

    class RecoveryRestorationLayer final : public Keire::Layer
    {
      public:
        RecoveryRestorationLayer(const Keire::AssetId mesh, const Keire::AssetId material,
                                 RecoveryRestorationResults& results)
            : Layer("Rendered recovery resource restoration"), m_Mesh(mesh), m_Material(material), m_Results(results)
        {
        }

      protected:
        void OnAttach() override
        {
            m_WarmupStartedAt = std::chrono::steady_clock::now();
            m_Scene = Keire::CreateRef<Keire::Scene>(
                Keire::AssetId::Generate(), Keire::SceneAsset::EmptyDefinition("Rendered recovery restoration"),
                Keire::ComponentRegistry::CreateDefault());
            auto object = m_Scene->CreateEntity("Recovery asset-backed mesh");
            const auto renderer = object.AddComponent<Keire::MeshRendererComponent>();
            renderer->SetMesh(m_Mesh);
            renderer->SetMaterial(m_Material);

            Keire::RenderSurfaceSpecification surface;
            surface.Name = "Rendered recovery restoration";
            surface.Width = SurfaceSize;
            surface.Height = SurfaceSize;
            surface.ClearColor = {0.0F, 0.0F, 0.0F, 1.0F};
            surface.SampleCount = Keire::RenderSampleCount::One;
            m_View = Owner().Renderer()->CreateView(surface);
            Keire::RenderCamera camera;
            camera.View = Keire::Math::LookAt({0.0F, 0.0F, 2.5F}, {}, {0.0F, 1.0F, 0.0F});
            camera.Projection = Keire::Math::Perspective(55.0F, 1.0F, 0.1F, 100.0F);
            camera.ClearColor = surface.ClearColor;
            m_View->SetCamera(camera);
            Keire::RenderSystemInternalAccess::SetPresentationSurface(*Owner().Renderer(), m_View->Surface());

            m_Vfx = Keire::CreateRef<Keire::VfxWorld>(Keire::VfxWorldSpecification{
                .MaximumEffects = 1U, .MaximumParticles = 4U, .Backend = Keire::VfxBackend::Gpu});
            if (!m_Vfx->Activate({RecoveryGpuEffect(), 1U, {}, {}, 73U, {}}))
                throw std::logic_error("Could not activate the recovery GPU VFX fixture.");
            m_Vfx->Update(1.0F / 60.0F);
            m_Snapshot = m_Vfx->CaptureRenderSnapshot();
            if (m_Snapshot.GpuEmitters().empty())
                throw std::logic_error("Recovery GPU VFX fixture did not capture an emitter.");
        }

        void OnUpdate(const Keire::Time&) override
        {
            const auto renderer = Owner().Renderer();
            switch (m_Phase)
            {
            case Phase::WarmAssets:
                WarmAssets(*renderer);
                break;
            case Phase::WaitForRecovery:
                if (const auto diagnostic = renderer->LastDeviceLoss();
                    diagnostic && diagnostic->RecoverySucceeded &&
                    renderer->DeviceState() == Keire::RenderDeviceState::Running)
                {
                    renderer->Flush();
                    m_Results.Diagnostic = diagnostic;
                    m_Results.AfterPixels =
                        Keire::RenderSystemInternalAccess::ReadbackRGBA8(*renderer, *m_View->Surface());
                    m_Results.AfterRetry = Keire::RenderSystemInternalAccess::RecoveryResourceCountsForTest(*renderer);
                    m_Results.VfxSignatures =
                        Keire::RenderSystemInternalAccess::LastVfxRetrySignaturesForTest(*renderer);
                    m_Results.RetriedVfxSnapshots =
                        Keire::RenderSystemInternalAccess::LastRetriedVfxSnapshotCount(*renderer);
                    m_Results.SurfaceEpochAfter = m_View->Surface()->Generation();
                    m_Results.SurfaceResourceGenerationAfter =
                        Keire::RenderSystemInternalAccess::SurfaceResourceGenerationForTest(*m_View->Surface());
                    m_Results.SurfaceAvailableAfter = m_View->Surface()->Available();
                    Submit();
                    m_Phase = Phase::ObserveContinuation;
                }
                break;
            case Phase::ObserveContinuation:
                renderer->Flush();
                m_Results.AfterContinuation =
                    Keire::RenderSystemInternalAccess::RecoveryResourceCountsForTest(*renderer);
                Owner().RequestExit();
                m_Phase = Phase::Complete;
                break;
            case Phase::Complete:
                break;
            }
        }

        void OnUi(Keire::UiFrame& ui) override
        {
            auto window = ui.BeginWindow("Device recovery UI restoration");
            if (window)
                ui.Text("Immutable rendered UI survives GPU device recreation.");
        }

        void OnDetach() noexcept override
        {
            try
            {
                const auto renderer = Owner().Renderer();
                renderer->Close();
                m_Results.Closed = renderer->DeviceState() == Keire::RenderDeviceState::Closed;
            }
            catch (...)
            {
            }
            if (m_Scene)
                m_Scene->Close();
            m_Vfx.Reset();
            m_View.Reset();
            m_Scene.Reset();
        }

      private:
        enum class Phase : std::uint8_t
        {
            WarmAssets,
            WaitForRecovery,
            ObserveContinuation,
            Complete
        };

        void WarmAssets(Keire::RenderSystem& renderer)
        {
            if (m_SubmittedWarmupFrame)
            {
                renderer.Flush();
                const auto counts = Keire::RenderSystemInternalAccess::RecoveryResourceCountsForTest(renderer);
                if (counts.Meshes != 0U && counts.Textures != 0U && counts.Materials != 0U && counts.Shaders != 0U &&
                    counts.GpuVfxWorlds != 0U && counts.RenderedEditorUiFrames != 0U)
                {
                    m_Results.Before = counts;
                    m_Results.BeforePixels =
                        Keire::RenderSystemInternalAccess::ReadbackRGBA8(renderer, *m_View->Surface());
                    m_Results.SurfaceEpochBefore = m_View->Surface()->Generation();
                    m_Results.SurfaceResourceGenerationBefore =
                        Keire::RenderSystemInternalAccess::SurfaceResourceGenerationForTest(*m_View->Surface());
                    Keire::RenderSystemInternalAccess::InjectDeviceLoss(renderer);
                    Submit();
                    m_Phase = Phase::WaitForRecovery;
                    return;
                }
            }
            if (std::chrono::steady_clock::now() - m_WarmupStartedAt >= std::chrono::seconds(10))
            {
                const auto counts = Keire::RenderSystemInternalAccess::RecoveryResourceCountsForTest(renderer);
                throw std::runtime_error(
                    "Recovery restoration assets did not become resident within 10 seconds (meshes=" +
                    std::to_string(counts.Meshes) + ", textures=" + std::to_string(counts.Textures) +
                    ", materials=" + std::to_string(counts.Materials) + ", shaders=" + std::to_string(counts.Shaders) +
                    ", GPU VFX worlds=" + std::to_string(counts.GpuVfxWorlds) +
                    ", rendered UI frames=" + std::to_string(counts.RenderedEditorUiFrames) + ").");
            }
            Submit();
            m_SubmittedWarmupFrame = true;
        }

        void Submit()
        {
            Keire::RenderEnvironmentSettings environment;
            environment.AmbientColor = {1.0F, 1.0F, 1.0F, 1.0F};
            environment.AmbientIntensity = 1.0F;
            environment.RequestedAntiAliasing = Keire::RenderAntiAliasingMode::None;
            Owner().Renderer()->Submit({m_Scene, m_View, false, environment, {}, m_Snapshot});
        }

        Keire::AssetId m_Mesh;
        Keire::AssetId m_Material;
        RecoveryRestorationResults& m_Results;
        Keire::Ref<Keire::Scene> m_Scene;
        Keire::Ref<Keire::RenderView> m_View;
        Keire::Ref<Keire::VfxWorld> m_Vfx;
        Keire::VfxRenderSnapshot m_Snapshot;
        Phase m_Phase = Phase::WarmAssets;
        std::chrono::steady_clock::time_point m_WarmupStartedAt{};
        bool m_SubmittedWarmupFrame = false;
    };

    enum class RecoveryScenario : std::uint8_t
    {
        ImmediateSuccess,
        HealthyCandidateFailure,
        ThirdAttemptSuccess,
        LostCandidateFailure,
        Exhaustion,
        Retirement,
        RetirementDepthTwo,
        Shutdown,
        BeforeCallbackFailure,
        AfterCallbackFailure,
        ClosingFromAfterCallback,
        RetryPublication
    };

    struct RecoveryResults final
    {
        std::optional<Keire::GpuDeviceLossDiagnostic> Diagnostic;
        Keire::RenderStatistics Statistics;
        Keire::RenderStatistics StatisticsAfterRetryPublication;
        std::vector<Keire::RenderFrameTimeline> Timelines;
        std::uint64_t AbandonedHandles = 0;
        std::uint64_t LostGenerationGpuCleanupCalls = 0;
        std::uint64_t HealthyCandidateCleanups = 0;
        std::uint64_t ReleasedInjectedLostDevices = 0;
        std::uint64_t RetriedVfxSnapshots = 0;
        float LastBackoffMilliseconds = 0.0F;
        Keire::RenderDeviceState StateBeforeRetryPublication = Keire::RenderDeviceState::Closed;
        Keire::RenderDeviceState StateAfterRetryPublication = Keire::RenderDeviceState::Closed;
        std::optional<Keire::GpuDeviceLossDiagnostic> DiagnosticBeforeRetryPublication;
        bool ObservedBeforeRetryPublication = false;
        bool Closed = false;
    };

    [[nodiscard]] std::size_t RetryCount(const std::vector<Keire::RenderFrameTimeline>& timelines)
    {
        return static_cast<std::size_t>(
            std::ranges::count(timelines, true, &Keire::RenderFrameTimeline::RetriedAfterDeviceLoss));
    }

    class RecoveryScenarioLayer final : public Keire::Layer
    {
      public:
        RecoveryScenarioLayer(const RecoveryScenario scenario, RecoveryResults& results)
            : Layer("Rendered device recovery"), m_Scenario(scenario), m_Results(results)
        {
        }

      protected:
        void OnAttach() override
        {
            m_Scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(),
                                                     Keire::SceneAsset::EmptyDefinition("Device recovery"));
            m_View = Owner().Renderer()->CreateView({.Name = "Device recovery", .Width = 32, .Height = 32});
            if (m_Scenario == RecoveryScenario::BeforeCallbackFailure)
            {
                Keire::RenderSystemInternalAccess::SetDeviceRecoveryCallbacks(
                    *Owner().Renderer(), [] { throw std::runtime_error("Injected pre-recovery callback failure."); },
                    {});
            }
            else if (m_Scenario == RecoveryScenario::AfterCallbackFailure)
            {
                Keire::RenderSystemInternalAccess::SetDeviceRecoveryCallbacks(
                    *Owner().Renderer(), {},
                    [this](SDL_GPUDevice*, SDL_GPUTextureFormat, SDL_GPUPresentMode)
                    {
                        if (!m_AfterCallbackFailed)
                        {
                            m_AfterCallbackFailed = true;
                            throw std::runtime_error("Injected post-create recovery callback failure.");
                        }
                    });
            }
            else if (m_Scenario == RecoveryScenario::ClosingFromAfterCallback)
            {
                Keire::RenderSystemInternalAccess::SetDeviceRecoveryCallbacks(
                    *Owner().Renderer(), {},
                    [this](SDL_GPUDevice*, SDL_GPUTextureFormat, SDL_GPUPresentMode)
                    {
                        const auto renderer = Owner().Renderer();
                        Keire::RenderSystemInternalAccess::SetDeviceRecoveryStateForTest(
                            *renderer, Keire::RenderDeviceState::Closing);
                        Owner().RequestExit();
                    });
            }
            else if (m_Scenario == RecoveryScenario::RetryPublication)
            {
                Keire::RenderSystemInternalAccess::SetDeviceRecoveryCallbacks(
                    *Owner().Renderer(), {},
                    [this](SDL_GPUDevice*, SDL_GPUTextureFormat, SDL_GPUPresentMode)
                    {
                        const auto renderer = Owner().Renderer();
                        m_Results.StateBeforeRetryPublication = renderer->DeviceState();
                        m_Results.DiagnosticBeforeRetryPublication = renderer->LastDeviceLoss();
                        m_Results.ObservedBeforeRetryPublication = true;
                    });
            }
        }

        void OnUpdate(const Keire::Time&) override
        {
            const auto renderer = Owner().Renderer();
            if (!m_Injected)
            {
                if (m_Scenario == RecoveryScenario::HealthyCandidateFailure ||
                    m_Scenario == RecoveryScenario::ThirdAttemptSuccess || m_Scenario == RecoveryScenario::Exhaustion)
                {
                    Keire::RenderSystemInternalAccess::InjectRecoveryCandidateFailure(
                        *renderer, Keire::RenderRecoveryCandidateFault::HealthyFailure,
                        m_Scenario == RecoveryScenario::ThirdAttemptSuccess ||
                                m_Scenario == RecoveryScenario::Exhaustion
                            ? 2U
                            : 1U);
                }
                else if (m_Scenario == RecoveryScenario::LostCandidateFailure)
                {
                    Keire::RenderSystemInternalAccess::InjectRecoveryCandidateFailure(
                        *renderer, Keire::RenderRecoveryCandidateFault::DeviceLoss);
                }

                if (m_Scenario == RecoveryScenario::Retirement || m_Scenario == RecoveryScenario::RetirementDepthTwo)
                {
                    Keire::RenderSystemInternalAccess::InjectDeviceLossAtRetirement(
                        *renderer, m_Scenario == RecoveryScenario::RetirementDepthTwo ? 2U : 1U);
                }
                else
                    Keire::RenderSystemInternalAccess::InjectDeviceLoss(*renderer);
                renderer->Submit({m_Scene, m_View});
                m_Injected = true;
                if (m_Scenario == RecoveryScenario::Shutdown)
                    Owner().RequestExit();
                return;
            }

            const auto diagnostic = renderer->LastDeviceLoss();
            if (!diagnostic || !diagnostic->RecoverySucceeded)
                return;
            renderer->Flush();
            m_Results.StatisticsAfterRetryPublication = renderer->Statistics();
            m_Results.StateAfterRetryPublication = renderer->DeviceState();
            Capture(*renderer);
            Owner().RequestExit();
        }

        void OnDetach() noexcept override
        {
            try
            {
                const auto renderer = Owner().Renderer();
                if (!m_Results.Diagnostic)
                    m_Results.Diagnostic = renderer->LastDeviceLoss();
                renderer->Close();
                m_Results.Closed = renderer->DeviceState() == Keire::RenderDeviceState::Closed;
                Capture(*renderer);
            }
            catch (...)
            {
            }
            if (m_Scene)
                m_Scene->Close();
            m_View.Reset();
            m_Scene.Reset();
        }

      private:
        void Capture(Keire::RenderSystem& renderer)
        {
            m_Results.Diagnostic = renderer.LastDeviceLoss();
            m_Results.Statistics = renderer.Statistics();
            m_Results.Timelines = renderer.RecentFrameTimelines();
            m_Results.AbandonedHandles =
                Keire::RenderSystemInternalAccess::LostGenerationAbandonedHandleCount(renderer);
            m_Results.LostGenerationGpuCleanupCalls =
                Keire::RenderSystemInternalAccess::LostGenerationGpuCleanupCallCount(renderer);
            m_Results.HealthyCandidateCleanups =
                Keire::RenderSystemInternalAccess::HealthyRecoveryCandidateCleanupCount(renderer);
            m_Results.ReleasedInjectedLostDevices =
                Keire::RenderSystemInternalAccess::ReleasedInjectedLostDeviceCountForTest(renderer);
            m_Results.RetriedVfxSnapshots = Keire::RenderSystemInternalAccess::LastRetriedVfxSnapshotCount(renderer);
            m_Results.LastBackoffMilliseconds =
                Keire::RenderSystemInternalAccess::LastRecoveryBackoffMillisecondsForTest(renderer);
            m_Results.Closed = renderer.DeviceState() == Keire::RenderDeviceState::Closed;
        }

        RecoveryScenario m_Scenario;
        RecoveryResults& m_Results;
        Keire::Ref<Keire::Scene> m_Scene;
        Keire::Ref<Keire::RenderView> m_View;
        bool m_Injected = false;
        bool m_AfterCallbackFailed = false;
    };

    struct StabilityResetResults final
    {
        std::optional<Keire::GpuDeviceLossDiagnostic> First;
        std::optional<Keire::GpuDeviceLossDiagnostic> Second;
        std::uint32_t AttemptsAfterStability = 99U;
        std::uint32_t AttemptsAfterSecondRecovery = 99U;
        std::vector<Keire::RenderFrameTimeline> Timelines;
    };

    class StabilityResetLayer final : public Keire::Layer
    {
      public:
        explicit StabilityResetLayer(StabilityResetResults& results)
            : Layer("Device recovery stability reset"), m_Results(results)
        {
        }

      protected:
        void OnAttach() override
        {
            m_Scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(),
                                                     Keire::SceneAsset::EmptyDefinition("Recovery stability"));
            m_View = Owner().Renderer()->CreateView({.Name = "Recovery stability", .Width = 32, .Height = 32});
        }

        void OnUpdate(const Keire::Time&) override
        {
            const auto renderer = Owner().Renderer();
            switch (m_Phase)
            {
            case Phase::InjectFirst:
                Keire::RenderSystemInternalAccess::InjectDeviceLoss(*renderer);
                renderer->Submit({m_Scene, m_View});
                m_Phase = Phase::WaitFirst;
                break;
            case Phase::WaitFirst:
                if (const auto diagnostic = renderer->LastDeviceLoss(); diagnostic && diagnostic->RecoverySucceeded)
                {
                    renderer->Flush();
                    m_Results.First = diagnostic;
                    Keire::RenderSystemInternalAccess::SatisfyRecoveryStabilityWindowForTest(*renderer);
                    renderer->Submit({m_Scene, m_View});
                    m_Phase = Phase::RetireStabilityFrame;
                }
                break;
            case Phase::RetireStabilityFrame:
                renderer->Flush();
                m_Results.AttemptsAfterStability =
                    Keire::RenderSystemInternalAccess::RecoveryAttemptCountForTest(*renderer);
                Keire::RenderSystemInternalAccess::InjectDeviceLoss(*renderer);
                renderer->Submit({m_Scene, m_View});
                m_Phase = Phase::WaitSecond;
                break;
            case Phase::WaitSecond:
                if (const auto diagnostic = renderer->LastDeviceLoss();
                    diagnostic && diagnostic->RecoverySucceeded && m_Results.First &&
                    diagnostic->RecoveredDeviceGeneration > m_Results.First->RecoveredDeviceGeneration)
                {
                    renderer->Flush();
                    m_Results.Second = diagnostic;
                    m_Results.AttemptsAfterSecondRecovery =
                        Keire::RenderSystemInternalAccess::RecoveryAttemptCountForTest(*renderer);
                    m_Results.Timelines = renderer->RecentFrameTimelines();
                    Owner().RequestExit();
                    m_Phase = Phase::Complete;
                }
                break;
            case Phase::Complete:
                break;
            }
        }

        void OnDetach() noexcept override
        {
            try
            {
                Owner().Renderer()->Close();
            }
            catch (...)
            {
            }
            if (m_Scene)
                m_Scene->Close();
        }

      private:
        enum class Phase : std::uint8_t
        {
            InjectFirst,
            WaitFirst,
            RetireStabilityFrame,
            WaitSecond,
            Complete
        };

        StabilityResetResults& m_Results;
        Keire::Ref<Keire::Scene> m_Scene;
        Keire::Ref<Keire::RenderView> m_View;
        Phase m_Phase = Phase::InjectFirst;
    };

    [[nodiscard]] Keire::ApplicationSpecification RecoverySpecification(const std::uint32_t attempts)
    {
        auto specification = RenderTestSpecification();
        specification.Render.DeviceLossRecoveryAttempts = attempts;
        return specification;
    }
} // namespace

TEST_CASE("rendered device recovery rebuilds UI and surfaces then retries one immutable VFX packet")
{
    RecoveryResults results;
    auto specification = RecoverySpecification(1U);
    specification.Ui.Mode = Keire::UiMode::Rendered;
    Keire::Application application(specification);
    (void)application.PushLayer(std::make_unique<RecoveryScenarioLayer>(RecoveryScenario::ImmediateSuccess, results));
    CHECK(application.Run() == 0);
    REQUIRE(results.Diagnostic);
    CHECK(results.Diagnostic->Operation == "test frame injection");
    CHECK(results.Diagnostic->RecoverySucceeded);
    CHECK(results.Diagnostic->RecoveryAttempt == 1U);
    CHECK(results.Diagnostic->RecoveredDeviceGeneration > results.Diagnostic->DeviceGeneration);
    CHECK(results.Diagnostic->Backend.size() > 0U);
    CHECK(results.Diagnostic->Adapter.size() > 0U);
    CHECK(RetryCount(results.Timelines) == 1U);
    CHECK(results.RetriedVfxSnapshots == 1U);
    CHECK(results.LostGenerationGpuCleanupCalls == 0U);
    CHECK(results.ReleasedInjectedLostDevices == 1U);
    CHECK(results.Statistics.OutstandingFrames == 0U);
    CHECK(results.Closed);
}

TEST_CASE("rendered recovery restores asset caches surface pixels UI and deterministic GPU VFX state")
{
    RenderAssetFixture assets;
    RecoveryRestorationResults results;
    auto specification = RecoverySpecification(1U);
    specification.Assets.Mode = Keire::AssetMode::Development;
    specification.Assets.DevelopmentCatalog = assets.Catalog;
    // Vulkan does not publish a swapchain image for a hidden window. This test exercises actual Dear ImGui GPU
    // recording across recovery, so it needs an available presentation target on every supported backend.
    specification.MainWindow.Visible = true;
    specification.TargetFrameRate = 240U;
    specification.Ui.Mode = Keire::UiMode::Rendered;
    Keire::Application application(specification);
    (void)application.PushLayer(std::make_unique<RecoveryRestorationLayer>(assets.Mesh, assets.Material, results));
    CHECK(application.Run() == 0);

    REQUIRE(results.Diagnostic);
    CHECK(results.Diagnostic->RecoverySucceeded);
    CHECK(results.Diagnostic->RecoveryAttempt == 1U);
    CHECK(results.SurfaceAvailableAfter);
    CHECK(results.SurfaceEpochAfter == results.SurfaceEpochBefore);
    CHECK(results.SurfaceResourceGenerationAfter > results.SurfaceResourceGenerationBefore);
    REQUIRE(results.BeforePixels.size() == static_cast<std::size_t>(SurfaceSize * SurfaceSize * 4U));
    REQUIRE(results.AfterPixels.size() == results.BeforePixels.size());
    CHECK(results.AfterPixels == results.BeforePixels);
    CHECK(results.Before.Meshes > 0U);
    CHECK(results.Before.Textures > 0U);
    CHECK(results.Before.Materials > 0U);
    CHECK(results.Before.Shaders > 0U);
    CHECK(results.Before.GpuVfxWorlds > 0U);
    CHECK(results.AfterRetry.Meshes > 0U);
    CHECK(results.AfterRetry.Textures > 0U);
    CHECK(results.AfterRetry.Materials > 0U);
    CHECK(results.AfterRetry.Shaders > 0U);
    CHECK(results.AfterRetry.GpuVfxWorlds > 0U);
    CHECK(results.AfterRetry.RenderedEditorUiFrames > results.Before.RenderedEditorUiFrames);
    CHECK(results.AfterContinuation.RenderedEditorUiFrames > results.AfterRetry.RenderedEditorUiFrames);
    CHECK(results.VfxSignatures[0] != 0U);
    CHECK(results.VfxSignatures[0] == results.VfxSignatures[1]);
    CHECK(results.RetriedVfxSnapshots == 1U);
    CHECK(results.Closed);
}

TEST_CASE("device recovery publishes Running only after the interrupted immutable frame resubmits")
{
    RecoveryResults results;
    Keire::Application application(RecoverySpecification(1U));
    (void)application.PushLayer(std::make_unique<RecoveryScenarioLayer>(RecoveryScenario::RetryPublication, results));
    CHECK(application.Run() == 0);

    CHECK(results.ObservedBeforeRetryPublication);
    CHECK(results.StateBeforeRetryPublication == Keire::RenderDeviceState::Recovering);
    REQUIRE(results.DiagnosticBeforeRetryPublication);
    CHECK_FALSE(results.DiagnosticBeforeRetryPublication->RecoverySucceeded);
    CHECK(results.DiagnosticBeforeRetryPublication->RecoveredDeviceGeneration == 0U);

    REQUIRE(results.Diagnostic);
    CHECK(results.Diagnostic->RecoverySucceeded);
    CHECK(results.Diagnostic->RecoveredDeviceGeneration > results.Diagnostic->DeviceGeneration);
    CHECK(results.StateAfterRetryPublication == Keire::RenderDeviceState::Running);
    CHECK(RetryCount(results.Timelines) == 1U);
    CHECK(results.Statistics.AcceptedFrames == results.Statistics.RetiredFrames);
    CHECK(results.Statistics.OutstandingFrames == 0U);
}

TEST_CASE("healthy and lost recovery candidates use distinct cleanup contracts before attempt two succeeds")
{
    for (const auto scenario : {RecoveryScenario::HealthyCandidateFailure, RecoveryScenario::LostCandidateFailure})
    {
        CAPTURE(static_cast<std::uint32_t>(scenario));
        RecoveryResults results;
        Keire::Application application(RecoverySpecification(2U));
        (void)application.PushLayer(std::make_unique<RecoveryScenarioLayer>(scenario, results));
        CHECK(application.Run() == 0);
        REQUIRE(results.Diagnostic);
        CHECK(results.Diagnostic->RecoverySucceeded);
        CHECK(results.Diagnostic->RecoveryAttempt == 2U);
        CHECK(RetryCount(results.Timelines) == 1U);
        CHECK(results.LostGenerationGpuCleanupCalls == 0U);
        CHECK(results.HealthyCandidateCleanups == (scenario == RecoveryScenario::HealthyCandidateFailure ? 1U : 0U));
        CHECK(results.ReleasedInjectedLostDevices == (scenario == RecoveryScenario::LostCandidateFailure ? 2U : 1U));
        CHECK(results.Closed);
    }
}

TEST_CASE("three recovery attempts are valid and every retry observes the bounded backoff")
{
    CHECK(Keire::RenderSpecification{}.DeviceLossRecoveryAttempts == 2U);
    RecoveryResults results;
    Keire::Application application(RecoverySpecification(3U));
    (void)application.PushLayer(
        std::make_unique<RecoveryScenarioLayer>(RecoveryScenario::ThirdAttemptSuccess, results));
    CHECK(application.Run() == 0);
    REQUIRE(results.Diagnostic);
    CHECK(results.Diagnostic->RecoverySucceeded);
    CHECK(results.Diagnostic->RecoveryAttempt == 3U);
    CHECK(results.HealthyCandidateCleanups == 2U);
    CHECK(results.LastBackoffMilliseconds >= 240.0F);
    CHECK(RetryCount(results.Timelines) == 1U);
    CHECK(results.Closed);
}

TEST_CASE("device loss discovered during idle fence retirement retains one accepted packet for one retry")
{
    RecoveryResults results;
    auto specification = RecoverySpecification(1U);
    specification.Render.MaximumFramesInFlight = 1U;
    Keire::Application application(specification);
    (void)application.PushLayer(std::make_unique<RecoveryScenarioLayer>(RecoveryScenario::Retirement, results));
    CHECK(application.Run() == 0);
    REQUIRE(results.Diagnostic);
    CHECK(results.Diagnostic->Operation == "test fence retirement injection");
    CHECK(results.Diagnostic->RecoverySucceeded);
    CHECK(results.AbandonedHandles >= 1U);
    CHECK(results.LostGenerationGpuCleanupCalls == 0U);
    CHECK(results.Statistics.AcceptedFrames == results.Statistics.RetiredFrames);
    CHECK(results.Statistics.OutstandingFrames == 0U);
    CHECK(RetryCount(results.Timelines) == 1U);
}

TEST_CASE("depth-two retirement recovery counts logical presentations once and preserves monotonic frame IDs")
{
    RecoveryResults results;
    auto specification = RecoverySpecification(1U);
    specification.Render.MaximumFramesInFlight = 2U;
    Keire::Application application(specification);
    (void)application.PushLayer(std::make_unique<RecoveryScenarioLayer>(RecoveryScenario::RetirementDepthTwo, results));
    CHECK(application.Run() == 0);
    REQUIRE(results.Diagnostic);
    CHECK(results.Diagnostic->Operation == "test fence retirement injection");
    CHECK(results.Diagnostic->RecoverySucceeded);
    CHECK(results.StatisticsAfterRetryPublication.AcceptedFrames == 2U);
    CHECK(results.StatisticsAfterRetryPublication.PresentedFrames == 2U);
    CHECK(results.StatisticsAfterRetryPublication.RetiredFrames == 1U);
    CHECK(results.StatisticsAfterRetryPublication.CancelledFrames == 1U);
    CHECK(results.StatisticsAfterRetryPublication.LastAcceptedFrame == 2U);
    CHECK(results.StatisticsAfterRetryPublication.LastPresentedFrame == 2U);
    CHECK(results.StatisticsAfterRetryPublication.LastRetiredFrame == 1U);
    CHECK(results.StatisticsAfterRetryPublication.OutstandingFrames == 0U);
    CHECK(RetryCount(results.Timelines) == 1U);
    CHECK(results.Closed);
}

TEST_CASE("recovery exhaustion preserves its actionable incident and closes without lost-generation cleanup")
{
    RecoveryResults results;
    Keire::Application application(RecoverySpecification(2U));
    (void)application.PushLayer(std::make_unique<RecoveryScenarioLayer>(RecoveryScenario::Exhaustion, results));
    CHECK_THROWS_AS((void)application.Run(), Keire::GpuDeviceLostError);
    REQUIRE(results.Diagnostic);
    CHECK_FALSE(results.Diagnostic->RecoverySucceeded);
    CHECK(results.Diagnostic->RecoveryAttempt == 2U);
    CHECK(results.Diagnostic->Operation == "test frame injection");
    CHECK(results.Diagnostic->Backend.size() > 0U);
    CHECK(results.Diagnostic->Adapter.size() > 0U);
    CHECK(results.HealthyCandidateCleanups == 2U);
    CHECK(results.LostGenerationGpuCleanupCalls == 0U);
    CHECK(results.Statistics.OutstandingFrames == 0U);
    CHECK(results.Closed);
}

TEST_CASE("recovery callback failures are contained before and after candidate creation")
{
    {
        RecoveryResults results;
        Keire::Application application(RecoverySpecification(2U));
        (void)application.PushLayer(
            std::make_unique<RecoveryScenarioLayer>(RecoveryScenario::BeforeCallbackFailure, results));
        CHECK_THROWS_AS((void)application.Run(), Keire::GpuDeviceLostError);
        REQUIRE(results.Diagnostic);
        CHECK_FALSE(results.Diagnostic->RecoverySucceeded);
        CHECK(results.Diagnostic->RecoveryAttempt == 0U);
        CHECK(results.HealthyCandidateCleanups == 0U);
        CHECK(results.Closed);
    }
    {
        RecoveryResults results;
        Keire::Application application(RecoverySpecification(2U));
        (void)application.PushLayer(
            std::make_unique<RecoveryScenarioLayer>(RecoveryScenario::AfterCallbackFailure, results));
        CHECK(application.Run() == 0);
        REQUIRE(results.Diagnostic);
        CHECK(results.Diagnostic->RecoverySucceeded);
        CHECK(results.Diagnostic->RecoveryAttempt == 2U);
        CHECK(results.HealthyCandidateCleanups == 1U);
        CHECK(results.Closed);
    }
}

TEST_CASE("device loss racing shutdown performs no recovery attempt and Close remains idempotent")
{
    RecoveryResults results;
    Keire::Application application(RecoverySpecification(2U));
    (void)application.PushLayer(std::make_unique<RecoveryScenarioLayer>(RecoveryScenario::Shutdown, results));
    CHECK(application.Run() == 0);
    if (results.Diagnostic)
    {
        CHECK_FALSE(results.Diagnostic->RecoverySucceeded);
        CHECK(results.Diagnostic->RecoveryAttempt == 0U);
    }
    CHECK(results.HealthyCandidateCleanups == 0U);
    CHECK(results.LostGenerationGpuCleanupCalls == 0U);
    CHECK(results.Statistics.OutstandingFrames == 0U);
    CHECK(results.Closed);
}

TEST_CASE("Closing from a post-create recovery callback cleans the healthy candidate without state resurrection")
{
    RecoveryResults results;
    Keire::Application application(RecoverySpecification(2U));
    (void)application.PushLayer(
        std::make_unique<RecoveryScenarioLayer>(RecoveryScenario::ClosingFromAfterCallback, results));
    CHECK_THROWS_AS((void)application.Run(), Keire::GpuDeviceLostError);
    CHECK(results.HealthyCandidateCleanups == 1U);
    CHECK(results.LostGenerationGpuCleanupCalls == 0U);
    CHECK(results.Closed);
}

TEST_CASE("recovery attempts reset only after both stability thresholds then a repeated loss starts at one")
{
    StabilityResetResults results;
    Keire::Application application(RecoverySpecification(2U));
    (void)application.PushLayer(std::make_unique<StabilityResetLayer>(results));
    CHECK(application.Run() == 0);
    REQUIRE(results.First);
    REQUIRE(results.Second);
    CHECK(results.AttemptsAfterStability == 0U);
    CHECK(results.AttemptsAfterSecondRecovery == 1U);
    CHECK(results.First->RecoveryAttempt == 1U);
    CHECK(results.Second->RecoveryAttempt == 1U);
    CHECK(results.Second->RecoveredDeviceGeneration > results.First->RecoveredDeviceGeneration);
    CHECK(RetryCount(results.Timelines) == 2U);
}
#endif
