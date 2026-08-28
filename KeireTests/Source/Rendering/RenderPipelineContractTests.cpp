#include "Keire/Core.h"
#include "Keire/ECS/Components/DirectionalLightComponent.h"

#include "KeireInternal/RenderInternal.h"
#include "KeireInternal/Rendering/RenderFramePacketInternal.h"
#include "KeireInternal/Rendering/RenderPipelineStateInternal.h"
#include "KeireInternal/Rendering/RenderSurfaceStateInternal.h"

#include <SDL3/SDL.h>
#include <doctest/doctest.h>
#include <imgui.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace
{
    struct ImGuiContextScope final
    {
        ImGuiContext* Previous = ImGui::GetCurrentContext();
        ImGuiContext* Context = ImGui::CreateContext();

        ~ImGuiContextScope()
        {
            ImGui::SetCurrentContext(Context);
            ImGui::DestroyContext(Context);
            ImGui::SetCurrentContext(Previous);
        }
    };

    void ArbitraryImGuiCallback(const ImDrawList*, const ImDrawCmd*) {}

    void BeginImGuiPacketFrame(ImGuiContextScope& scope)
    {
        ImGui::SetCurrentContext(scope.Context);
        auto& io = ImGui::GetIO();
        io.DisplaySize = {64.0F, 64.0F};
        io.DeltaTime = 1.0F / 60.0F;
        io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
        ImGui::NewFrame();
    }

    void UsePipelineDummyVideoDriver()
    {
#if defined(_WIN32)
        REQUIRE(_putenv_s("SDL_VIDEODRIVER", "dummy") == 0);
#else
        REQUIRE(setenv("SDL_VIDEODRIVER", "dummy", 1) == 0);
#endif
        REQUIRE(SDL_SetHintWithPriority(SDL_HINT_VIDEO_DRIVER, "dummy", SDL_HINT_OVERRIDE));
    }

    struct PipelineProbe final
    {
        Keire::RenderStatistics Statistics;
        std::vector<Keire::RenderFrameTimeline> Timelines;
        bool FlushCompleted = false;
#if defined(KEIRE_ENABLE_TEST_HOOKS)
        bool DistinctRenderThread = false;
#endif
    };

    class PipelineContractLayer final : public Keire::Layer
    {
      public:
        explicit PipelineContractLayer(PipelineProbe& probe, const std::uint32_t targetFrames = 8U)
            : Layer("pipeline-contract"), m_Probe(probe), m_TargetFrames(targetFrames)
        {
        }

      protected:
        void OnAttach() override
        {
#if defined(KEIRE_ENABLE_TEST_HOOKS)
            m_Probe.DistinctRenderThread =
                Keire::RenderSystemInternalAccess::StartThreadedHeadlessForTest(*Owner().Renderer());
            REQUIRE(m_Probe.DistinctRenderThread);
#endif
            m_Scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(),
                                                     Keire::SceneAsset::EmptyDefinition("pipeline-contract"));
            m_View = Owner().Renderer()->CreateView({.Name = "pipeline-contract", .Width = 32, .Height = 32});
        }

        void OnUpdate(const Keire::Time&) override
        {
            Owner().Renderer()->Submit({m_Scene, m_View});
            if (++m_Submitted == m_TargetFrames)
                Owner().RequestExit();
        }

        void OnDetach() noexcept override
        {
            try
            {
                Owner().Renderer()->Flush();
                m_Probe.FlushCompleted = true;
                m_Probe.Statistics = Owner().Renderer()->Statistics();
                m_Probe.Timelines = Owner().Renderer()->RecentFrameTimelines();
            }
            catch (...)
            {
            }
            if (m_Scene)
                m_Scene->Close();
        }

      private:
        PipelineProbe& m_Probe;
        Keire::Ref<Keire::Scene> m_Scene;
        Keire::Ref<Keire::RenderView> m_View;
        std::uint32_t m_TargetFrames = 8;
        std::uint32_t m_Submitted = 0;
    };

#if defined(KEIRE_ENABLE_TEST_HOOKS)
    struct DeferredCaptureProbe final
    {
        std::atomic<std::uint64_t> SceneEnumerationsWhileBlocked{0};
        std::atomic<std::uint64_t> RuntimeUiEnumerationsWhileBlocked{0};
        std::atomic<bool> AcceptedFrameBlocked{false};
        std::atomic<bool> AdmissionWaiterObserved{false};
        bool DistinctRenderThread = false;
        Keire::RenderStatistics Statistics;
    };

    class DeferredCaptureLayer final : public Keire::Layer
    {
      public:
        explicit DeferredCaptureLayer(DeferredCaptureProbe& probe) : Layer("deferred-capture"), m_Probe(probe) {}

      protected:
        void OnAttach() override
        {
            m_Probe.DistinctRenderThread =
                Keire::RenderSystemInternalAccess::StartThreadedHeadlessForTest(*Owner().Renderer());
            REQUIRE(m_Probe.DistinctRenderThread);
            m_Scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(),
                                                     Keire::SceneAsset::EmptyDefinition("deferred-capture"));
            m_View = Owner().Renderer()->CreateView({.Name = "deferred-capture", .Width = 32, .Height = 32});
            m_Ui = Keire::CreateRef<Keire::RuntimeUiTree>();
            const auto button = m_Ui->Create(Keire::RuntimeUiElementType::Button);
            Keire::RuntimeUiStyle style;
            style.Width = 32.0F;
            style.Height = 32.0F;
            REQUIRE(m_Ui->SetStyle(button, style));
            m_Ui->Layout(32.0F, 32.0F);
        }

        void OnUpdate(const Keire::Time&) override
        {
            const auto renderer = Owner().Renderer();
            REQUIRE(renderer);
            if (m_Frame == 0U)
            {
                Keire::RenderSystemInternalAccess::BlockNextAcceptedFrame(*renderer);
                renderer->Submit({m_Scene, m_View});
                renderer->SubmitRuntimeUi(m_Ui);
                ++m_Frame;
                return;
            }

            if (m_Frame == 1U)
            {
                const bool acceptedFrameBlocked =
                    Keire::RenderSystemInternalAccess::WaitForAcceptedFrameBlock(*renderer);
                m_Probe.AcceptedFrameBlocked.store(acceptedFrameBlocked, std::memory_order_release);
                if (!acceptedFrameBlocked)
                {
                    Keire::RenderSystemInternalAccess::ReleaseAcceptedFrameBlock(*renderer);
                    FAIL_CHECK("The accepted frame did not reach the deterministic render-thread barrier.");
                    Owner().RequestExit();
                    return;
                }
                renderer->Submit({m_Scene, m_View});
                renderer->SubmitRuntimeUi(m_Ui);
                auto* const application = &Owner();
                m_Observer = std::jthread(
                    [application, renderer, &probe = m_Probe]
                    {
                        const bool waiter = Keire::RenderSystemInternalAccess::WaitForFrameAdmissionWaiter(*renderer);
                        probe.AdmissionWaiterObserved.store(waiter, std::memory_order_release);
                        probe.SceneEnumerationsWhileBlocked.store(
                            Keire::RenderSystemInternalAccess::SceneCaptureEnumerationCount(*renderer),
                            std::memory_order_release);
                        probe.RuntimeUiEnumerationsWhileBlocked.store(
                            Keire::RenderSystemInternalAccess::RuntimeUiCaptureEnumerationCount(*renderer),
                            std::memory_order_release);
                        Keire::RenderSystemInternalAccess::ReleaseAcceptedFrameBlock(*renderer);
                        application->RequestExit();
                    });
                ++m_Frame;
                return;
            }
            FAIL_CHECK("Admission scheduling began an unexpected third application frame.");
            Owner().RequestExit();
        }

        void OnDetach() noexcept override
        {
            if (const auto renderer = Owner().Renderer())
                Keire::RenderSystemInternalAccess::ReleaseAcceptedFrameBlock(*renderer);
            if (m_Observer.joinable())
                m_Observer.join();
            try
            {
                Owner().Renderer()->Flush();
                m_Probe.Statistics = Owner().Renderer()->Statistics();
            }
            catch (...)
            {
            }
            if (m_Scene)
                m_Scene->Close();
        }

      private:
        DeferredCaptureProbe& m_Probe;
        Keire::Ref<Keire::Scene> m_Scene;
        Keire::Ref<Keire::RenderView> m_View;
        Keire::Ref<Keire::RuntimeUiTree> m_Ui;
        std::jthread m_Observer;
        std::uint32_t m_Frame = 0;
    };

    struct CaptureFailureProbe final
    {
        Keire::RenderStatistics Statistics;
        std::size_t AvailableSlots = 0;
    };

    struct TerminalQueueFailureProbe final
    {
        Keire::RenderStatistics Statistics;
        std::vector<Keire::RenderFrameTimeline> Timelines;
        std::array<std::string, 2> RepeatedFailures;
        std::size_t AvailableSlots = 0;
        std::size_t SurfaceLifetimeOwners = 0;
        bool DistinctRenderThread = false;
        bool Closed = false;
    };

    class TerminalQueueFailureLayer final : public Keire::Layer
    {
      public:
        explicit TerminalQueueFailureLayer(TerminalQueueFailureProbe& probe)
            : Layer("terminal-queue-failure"), m_Probe(probe)
        {
        }

      protected:
        void OnAttach() override
        {
            m_Probe.DistinctRenderThread =
                Keire::RenderSystemInternalAccess::StartThreadedHeadlessForTest(*Owner().Renderer());
            REQUIRE(m_Probe.DistinctRenderThread);
            m_Scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(),
                                                     Keire::SceneAsset::EmptyDefinition("terminal-queue-failure"));
            m_View = Owner().Renderer()->CreateView({.Name = "terminal-queue-failure", .Width = 32, .Height = 32});
        }

        void OnUpdate(const Keire::Time&) override
        {
            const auto renderer = Owner().Renderer();
            REQUIRE(renderer);
            if (m_Frame == 0U)
            {
                Keire::RenderSystemInternalAccess::BlockNextAcceptedFrame(*renderer);
                Keire::RenderSystemInternalAccess::InjectTerminalFailureAtNextAcceptedFrame(*renderer);
                renderer->Submit({m_Scene, m_View});
            }
            else if (m_Frame == 1U)
            {
                REQUIRE(Keire::RenderSystemInternalAccess::WaitForAcceptedFrameBlock(*renderer));
                renderer->Submit({m_Scene, m_View});
            }
            else if (m_Frame == 2U)
            {
                renderer->Submit({m_Scene, m_View});
            }
            else
            {
                const auto surface = m_View->Surface();
                const auto state = std::static_pointer_cast<Keire::RenderBackend::RenderSurfaceState>(
                    Keire::RenderSystemInternalAccess::SurfaceLease(*surface));
                REQUIRE(state);
                m_OldSurfaceLifetime = state->Lifetime;
                surface->RequestSize(64U, 64U);
                Keire::RenderSystemInternalAccess::ReleaseAcceptedFrameBlock(*renderer);
                Owner().RequestExit();
            }
            ++m_Frame;
        }

        void OnDetach() noexcept override
        {
            try
            {
                const auto renderer = Owner().Renderer();
                Keire::RenderSystemInternalAccess::ReleaseAcceptedFrameBlock(*renderer);
                for (auto& message : m_Probe.RepeatedFailures)
                {
                    try
                    {
                        renderer->Flush();
                    }
                    catch (const std::exception& error)
                    {
                        message = error.what();
                    }
                }
                m_Probe.Statistics = renderer->Statistics();
                m_Probe.Timelines = renderer->RecentFrameTimelines();
                m_Probe.AvailableSlots = Keire::RenderSystemInternalAccess::AvailableFrameSlotCount(*renderer);
                m_Probe.SurfaceLifetimeOwners = m_OldSurfaceLifetime.use_count();
                renderer->Close();
                renderer->Close();
                m_Probe.Closed = renderer->DeviceState() == Keire::RenderDeviceState::Closed;
            }
            catch (...)
            {
            }
            if (m_Scene)
                m_Scene->Close();
        }

      private:
        TerminalQueueFailureProbe& m_Probe;
        Keire::Ref<Keire::Scene> m_Scene;
        Keire::Ref<Keire::RenderView> m_View;
        std::weak_ptr<const Keire::RenderBackend::RenderSurfaceEpochLease> m_OldSurfaceLifetime;
        std::uint32_t m_Frame = 0;
    };

    class CaptureFailureLayer final : public Keire::Layer
    {
      public:
        explicit CaptureFailureLayer(CaptureFailureProbe& probe) : Layer("capture-failure"), m_Probe(probe) {}

      protected:
        void OnAttach() override
        {
            m_Scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(),
                                                     Keire::SceneAsset::EmptyDefinition("capture-failure"));
            m_View = Owner().Renderer()->CreateView({.Name = "capture-failure", .Width = 32, .Height = 32});
        }

        void OnUpdate(const Keire::Time&) override
        {
            Keire::RenderSystemInternalAccess::InjectCaptureFailure(*Owner().Renderer());
            Owner().Renderer()->Submit({m_Scene, m_View});
        }

        void OnDetach() noexcept override
        {
            m_Probe.Statistics = Owner().Renderer()->Statistics();
            m_Probe.AvailableSlots = Keire::RenderSystemInternalAccess::AvailableFrameSlotCount(*Owner().Renderer());
            if (m_Scene)
                m_Scene->Close();
        }

      private:
        CaptureFailureProbe& m_Probe;
        Keire::Ref<Keire::Scene> m_Scene;
        Keire::Ref<Keire::RenderView> m_View;
    };

    struct DirectionalFallbackProbe final
    {
        std::vector<std::uint64_t> CapturedLights;
        std::uint64_t FirstLight = 0;
        std::uint64_t ReloadedLight = 0;
    };

    class DirectionalFallbackLayer final : public Keire::Layer
    {
      public:
        explicit DirectionalFallbackLayer(DirectionalFallbackProbe& probe)
            : Layer("directional-fallback"), m_Probe(probe)
        {
        }

      protected:
        void OnAttach() override
        {
            m_Active = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(),
                                                      Keire::SceneAsset::EmptyDefinition("active-without-light"));
            m_First = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(),
                                                     Keire::SceneAsset::EmptyDefinition("first-light"));
            m_Reloaded = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(),
                                                        Keire::SceneAsset::EmptyDefinition("reloaded-light"));
            auto firstLight = m_First->CreateEntity("first directional");
            REQUIRE(firstLight.AddComponent<Keire::DirectionalLightComponent>());
            m_Probe.FirstLight = firstLight.Id().Value().Low();
            auto reloadedLight = m_Reloaded->CreateEntity("reloaded directional");
            REQUIRE(reloadedLight.AddComponent<Keire::DirectionalLightComponent>());
            m_Probe.ReloadedLight = reloadedLight.Id().Value().Low();
            m_View = Owner().Renderer()->CreateView({.Name = "directional-fallback", .Width = 32, .Height = 32});
        }

        void OnUpdate(const Keire::Time&) override
        {
            if (m_Frame != 0U)
            {
                m_Probe.CapturedLights.push_back(
                    Keire::RenderSystemInternalAccess::LastCapturedDirectionalLightEntity(*Owner().Renderer()));
            }
            Keire::SceneRenderRequest request{m_Active, m_View};
            if (m_Frame == 0U)
                request.AdditionalScenes = {{m_First, {}}, {m_Reloaded, {}}};
            else
                request.AdditionalScenes = {{m_Reloaded, {}}, {m_First, {}}};
            Owner().Renderer()->Submit(std::move(request));
            if (++m_Frame == 2U)
                Owner().RequestExit();
        }

        void OnDetach() noexcept override
        {
            m_Probe.CapturedLights.push_back(
                Keire::RenderSystemInternalAccess::LastCapturedDirectionalLightEntity(*Owner().Renderer()));
            for (auto scene : {m_Active, m_First, m_Reloaded})
                if (scene)
                    scene->Close();
        }

      private:
        DirectionalFallbackProbe& m_Probe;
        Keire::Ref<Keire::Scene> m_Active;
        Keire::Ref<Keire::Scene> m_First;
        Keire::Ref<Keire::Scene> m_Reloaded;
        Keire::Ref<Keire::RenderView> m_View;
        std::uint32_t m_Frame = 0;
    };

    struct DeviceFailureClassificationProbe final
    {
        std::vector<Keire::GpuDeviceLossDiagnostic> Diagnostics;
    };

    class DeviceFailureClassificationLayer final : public Keire::Layer
    {
      public:
        explicit DeviceFailureClassificationLayer(DeviceFailureClassificationProbe& probe)
            : Layer("device-failure-classification"), m_Probe(probe)
        {
        }

      protected:
        void OnUpdate(const Keire::Time&) override
        {
            for (const auto operation : {"SDL_MapGPUTransferBuffer", "SDL_BeginGPURenderPass(occlusion debug)"})
            {
                const auto diagnostic = Keire::RenderSystemInternalAccess::ClassifyDeviceFailureForTest(
                    *Owner().Renderer(), operation, "DXGI_ERROR_DEVICE_REMOVED: device lost");
                REQUIRE(diagnostic);
                m_Probe.Diagnostics.push_back(*diagnostic);
            }
            const auto extracted = Keire::RenderSystemInternalAccess::ClassifyDeviceFailureForTest(
                *Owner().Renderer(), "SDL GPU frame execution",
                "SDL_AcquireGPUCommandBuffer(surface) failed: VK_ERROR_DEVICE_LOST");
            REQUIRE(extracted);
            m_Probe.Diagnostics.push_back(*extracted);
            Owner().RequestExit();
        }

      private:
        DeviceFailureClassificationProbe& m_Probe;
    };

    struct AdmissionRecoveryProbe final
    {
        std::uint64_t EnumerationsBeforeResume = 0;
        std::size_t SlotsBeforeResume = 0;
        Keire::RenderStatistics Statistics;
    };

    class AdmissionRecoveryLayer final : public Keire::Layer
    {
      public:
        explicit AdmissionRecoveryLayer(AdmissionRecoveryProbe& probe) : Layer("admission-recovery"), m_Probe(probe) {}

      protected:
        void OnAttach() override
        {
            m_Scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(),
                                                     Keire::SceneAsset::EmptyDefinition("admission-recovery"));
            m_View = Owner().Renderer()->CreateView({.Name = "admission-recovery", .Width = 32, .Height = 32});
        }

        void OnUpdate(const Keire::Time&) override
        {
            const auto renderer = Owner().Renderer();
            REQUIRE(renderer);
            if (m_Frame++ == 0U)
            {
                Keire::RenderSystemInternalAccess::InjectRecoveryAtAdmissionBarrier(*renderer);
                renderer->Submit({m_Scene, m_View});
                m_Recovery = std::jthread(
                    [renderer]
                    {
                        std::this_thread::sleep_for(std::chrono::milliseconds(50));
                        Keire::RenderSystemInternalAccess::SetDeviceRecoveryStateForTest(
                            *renderer, Keire::RenderDeviceState::Running);
                    });
                return;
            }
            m_Probe.EnumerationsBeforeResume =
                Keire::RenderSystemInternalAccess::SceneCaptureEnumerationCount(*renderer);
            m_Probe.SlotsBeforeResume = Keire::RenderSystemInternalAccess::AvailableFrameSlotCount(*renderer);
            Owner().RequestExit();
        }

        void OnDetach() noexcept override
        {
            if (m_Recovery.joinable())
                m_Recovery.join();
            try
            {
                Owner().Renderer()->Flush();
                m_Probe.Statistics = Owner().Renderer()->Statistics();
            }
            catch (...)
            {
            }
            if (m_Scene)
                m_Scene->Close();
        }

      private:
        AdmissionRecoveryProbe& m_Probe;
        Keire::Ref<Keire::Scene> m_Scene;
        Keire::Ref<Keire::RenderView> m_View;
        std::jthread m_Recovery;
        std::uint32_t m_Frame = 0;
    };

    struct PipelineInvariantProbe final
    {
        bool WorkerFlushRejected = false;
        bool WorkerFlushStateUnchanged = false;
        bool RecoveryFlushSignalled = false;
        bool RecoveryFlushStateUnchanged = false;
        bool CompletionExactlyOnce = false;
        bool RecoveryPendingCloseCompleted = false;
    };

    class PipelineInvariantLayer final : public Keire::Layer
    {
      public:
        explicit PipelineInvariantLayer(PipelineInvariantProbe& probe) : Layer("pipeline-invariants"), m_Probe(probe) {}

      protected:
        void OnUpdate(const Keire::Time&) override
        {
            const auto renderer = Owner().Renderer();
            REQUIRE(renderer);
            const auto statisticsBefore = renderer->Statistics();
            std::jthread worker(
                [renderer, &probe = m_Probe]
                {
                    try
                    {
                        renderer->Flush();
                    }
                    catch (const std::logic_error& error)
                    {
                        probe.WorkerFlushRejected =
                            std::string_view(error.what()).find("owner thread") != std::string_view::npos;
                    }
                });
            worker.join();
            const auto statisticsAfterWorker = renderer->Statistics();
            m_Probe.WorkerFlushStateUnchanged =
                statisticsAfterWorker.AcceptedFrames == statisticsBefore.AcceptedFrames &&
                statisticsAfterWorker.RetiredFrames == statisticsBefore.RetiredFrames &&
                statisticsAfterWorker.OutstandingFrames == statisticsBefore.OutstandingFrames;

            Keire::RenderSystemInternalAccess::SetDeviceRecoveryStateForTest(*renderer,
                                                                             Keire::RenderDeviceState::RecoveryPending);
            try
            {
                renderer->Flush();
            }
            catch (const Keire::RenderRecoveryBoundaryRequired&)
            {
                m_Probe.RecoveryFlushSignalled = true;
            }
            const auto statisticsAfterRecovery = renderer->Statistics();
            m_Probe.RecoveryFlushStateUnchanged =
                renderer->DeviceState() == Keire::RenderDeviceState::RecoveryPending &&
                statisticsAfterRecovery.AcceptedFrames == statisticsBefore.AcceptedFrames &&
                statisticsAfterRecovery.RetiredFrames == statisticsBefore.RetiredFrames &&
                statisticsAfterRecovery.OutstandingFrames == statisticsBefore.OutstandingFrames;
            Keire::RenderSystemInternalAccess::SetDeviceRecoveryStateForTest(*renderer,
                                                                             Keire::RenderDeviceState::Running);
            m_Probe.CompletionExactlyOnce = Keire::RenderSystemInternalAccess::CompleteFrameTwiceForTest(*renderer);
            Owner().RequestExit();
        }

        void OnDetach() noexcept override
        {
            try
            {
                const auto renderer = Owner().Renderer();
                Keire::RenderSystemInternalAccess::SetDeviceRecoveryStateForTest(
                    *renderer, Keire::RenderDeviceState::RecoveryPending);
                renderer->Close();
                renderer->Close();
                m_Probe.RecoveryPendingCloseCompleted = renderer->DeviceState() == Keire::RenderDeviceState::Closed;
            }
            catch (...)
            {
            }
        }

      private:
        PipelineInvariantProbe& m_Probe;
    };
#endif

    [[nodiscard]] Keire::ApplicationSpecification PipelineSpecification(const std::uint32_t depth)
    {
        Keire::ApplicationSpecification specification;
        specification.MainWindow.Title = "pipeline-contract";
        specification.MainWindow.Visible = false;
        specification.SuspendWhenMainWindowMinimized = false;
        specification.ManageLogging = false;
        specification.Render.Mode = Keire::RenderMode::Headless;
        specification.Render.MaximumFramesInFlight = depth;
        specification.Ui.Mode = Keire::UiMode::Disabled;
        return specification;
    }
} // namespace

TEST_CASE("Render pipeline accepts depths one through three without drops and flushes in frame order")
{
    UsePipelineDummyVideoDriver();
    CHECK(Keire::RenderSpecification{}.MaximumFramesInFlight == 2U);
    for (const auto depth : std::array{1U, 2U, 3U})
    {
        CAPTURE(depth);
        PipelineProbe probe;
        Keire::Application application(PipelineSpecification(depth));
        (void)application.PushLayer(std::make_unique<PipelineContractLayer>(probe));
        CHECK(application.Run() == 0);
#if defined(KEIRE_ENABLE_TEST_HOOKS)
        CHECK(probe.DistinctRenderThread);
#endif
        CHECK(probe.FlushCompleted);
        CHECK(probe.Statistics.AllowedFramesInFlight == depth);
        CHECK(probe.Statistics.AcceptedFrames == 8U);
        CHECK(probe.Statistics.RetiredFrames == 8U);
        CHECK(probe.Statistics.CancelledFrames == 0U);
        CHECK(probe.Statistics.OutstandingFrames == 0U);
        CHECK(probe.Statistics.FramesInFlightHighWaterMark >= 1U);
        CHECK(probe.Statistics.FramesInFlightHighWaterMark <= depth);
        CHECK(probe.Statistics.OwnerUpdateMilliseconds >= 0.0F);
        REQUIRE(probe.Timelines.size() == 8U);
        for (std::size_t index = 0; index < probe.Timelines.size(); ++index)
        {
            CHECK(probe.Timelines[index].Frame == index + 1U);
            CHECK(probe.Timelines[index].OwnerUpdateMilliseconds >= 0.0F);
            CHECK_FALSE(probe.Timelines[index].Cancelled);
        }
    }
}

TEST_CASE("Render pipeline rejects frame bounds outside one through three")
{
    UsePipelineDummyVideoDriver();
    for (const auto depth : std::array{0U, 4U})
    {
        CAPTURE(depth);
        Keire::Application application(PipelineSpecification(depth));
        CHECK_THROWS_AS((void)application.Run(), std::invalid_argument);
    }
}

TEST_CASE("Render pipeline rejects device recovery attempts above the zero through three contract")
{
    UsePipelineDummyVideoDriver();
    CHECK(Keire::RenderSpecification{}.DeviceLossRecoveryAttempts == 2U);
    auto valid = PipelineSpecification(1U);
    valid.Render.DeviceLossRecoveryAttempts = 3U;
    {
        PipelineProbe probe;
        Keire::Application application(valid);
        (void)application.PushLayer(std::make_unique<PipelineContractLayer>(probe, 1U));
        CHECK(application.Run() == 0);
    }

    auto invalid = PipelineSpecification(1U);
    invalid.Render.DeviceLossRecoveryAttempts = 4U;
    Keire::Application application(invalid);
    CHECK_THROWS_AS((void)application.Run(), std::invalid_argument);
}

TEST_CASE("Render pipeline healthy shutdown drains sustained bounded frame traffic")
{
    UsePipelineDummyVideoDriver();
    for (const auto depth : std::array{1U, 2U, 3U})
    {
        CAPTURE(depth);
        PipelineProbe probe;
        Keire::Application application(PipelineSpecification(depth));
        (void)application.PushLayer(std::make_unique<PipelineContractLayer>(probe, 64U));
        CHECK(application.Run() == 0);
        CHECK(probe.FlushCompleted);
        CHECK(probe.Statistics.AcceptedFrames == 64U);
        CHECK(probe.Statistics.RetiredFrames == 64U);
        CHECK(probe.Statistics.CancelledFrames == 0U);
        CHECK(probe.Statistics.OutstandingFrames == 0U);
        CHECK(probe.Statistics.FramesInFlightHighWaterMark <= depth);
    }
}

TEST_CASE("Render device lifecycle transitions never overwrite concurrent closing or closed states")
{
    using Keire::RenderBackend::PublishTerminalDeviceFailure;
    using Keire::RenderBackend::TryBeginDeviceRecovery;
    for (const auto terminal :
         {Keire::RenderDeviceState::Closing, Keire::RenderDeviceState::Closed, Keire::RenderDeviceState::Failed})
    {
        std::atomic lifecycle{terminal};
        CHECK_FALSE(TryBeginDeviceRecovery(lifecycle));
        CHECK(lifecycle.load() == terminal);
    }

    std::atomic recovery{Keire::RenderDeviceState::Running};
    CHECK(TryBeginDeviceRecovery(recovery));
    CHECK(recovery.load() == Keire::RenderDeviceState::RecoveryPending);
    for (const auto state : {Keire::RenderDeviceState::Running, Keire::RenderDeviceState::RecoveryPending,
                             Keire::RenderDeviceState::Recovering, Keire::RenderDeviceState::Failed,
                             Keire::RenderDeviceState::Closing, Keire::RenderDeviceState::Closed})
    {
        std::atomic lifecycle{state};
        PublishTerminalDeviceFailure(lifecycle);
        const auto expected = state == Keire::RenderDeviceState::Closing || state == Keire::RenderDeviceState::Closed
                                  ? state
                                  : Keire::RenderDeviceState::Failed;
        CHECK(lifecycle.load() == expected);
    }
}

TEST_CASE("GPU device-loss exceptions retain actionable bounded identity without raw driver detail")
{
    Keire::GpuDeviceLostError error({.Operation = "SDL_SubmitGPUCommandBuffer(surface)",
                                     .Backend = "vulkan",
                                     .Adapter = "Test Adapter",
                                     .DriverName = "Test Driver",
                                     .DriverVersion = "1.2.3",
                                     .DriverDetail = "C:/Users/Private/secret.txt",
                                     .Frame = 17U,
                                     .DeviceGeneration = 4U,
                                     .RecoveryAttempt = 2U});
    const std::string_view message = error.what();
    CHECK(message.find("SDL_SubmitGPUCommandBuffer(surface)") != std::string_view::npos);
    CHECK(message.find("vulkan") != std::string_view::npos);
    CHECK(message.find("Test Adapter") != std::string_view::npos);
    CHECK(message.find("frame=17") != std::string_view::npos);
    CHECK(message.find("deviceGeneration=4") != std::string_view::npos);
    CHECK(message.find("Test Driver") != std::string_view::npos);
    CHECK(message.find("driverVersion=1.2.3") != std::string_view::npos);
    CHECK(message.find("recoveryAttempt=2") != std::string_view::npos);
    CHECK(message.find("Private") == std::string_view::npos);
    CHECK(message.find("secret.txt") == std::string_view::npos);
}

TEST_CASE("ImGui packet capture rejects overflowing texture dimensions before copying pixels")
{
    ImTextureData texture;
    texture.Create(ImTextureFormat_RGBA32, 1, 1);
    texture.Width = (std::numeric_limits<int>::max)();
    texture.Height = (std::numeric_limits<int>::max)();

    ImVector<ImTextureData*> textures;
    textures.push_back(&texture);
    ImDrawData drawData;
    drawData.Valid = true;
    drawData.Textures = &textures;

    CHECK_THROWS_WITH_AS((void)Keire::RenderBackend::OwnedImGuiDrawData::Capture(&drawData, {}),
                         "Dear ImGui texture snapshots exceed the 64 MiB frame-packet bound.", std::length_error);
}

TEST_CASE("ImGui packet capture acknowledges texture requests transactionally and preserves their snapshot")
{
    ImGuiContextScope contextScope;
    ImGui::SetCurrentContext(contextScope.Context);

    SUBCASE("successful update")
    {
        ImTextureData texture;
        texture.Create(ImTextureFormat_RGBA32, 2, 2);
        const std::array<unsigned char, 16> pixels = {1U, 2U,  3U,  4U,  5U,  6U,  7U,  8U,
                                                      9U, 10U, 11U, 12U, 13U, 14U, 15U, 16U};
        std::memcpy(texture.Pixels, pixels.data(), pixels.size());
        texture.UpdateRect = {1U, 0U, 1U, 2U};
        texture.Updates.push_back(texture.UpdateRect);
        texture.SetStatus(ImTextureStatus_WantUpdates);

        ImVector<ImTextureData*> textures;
        textures.push_back(&texture);
        ImDrawData drawData;
        drawData.Valid = true;
        drawData.Textures = &textures;

        const auto captured = Keire::RenderBackend::OwnedImGuiDrawData::Capture(&drawData, {});
        REQUIRE(captured);
        CHECK(texture.Status == ImTextureStatus_OK);
        const ImTextureID logicalId = texture.GetTexID();
        CHECK(logicalId != ImTextureID_Invalid);
        CHECK(texture.UpdateRect.x == 1U);
        CHECK(texture.UpdateRect.y == 0U);
        CHECK(texture.UpdateRect.w == 1U);
        CHECK(texture.UpdateRect.h == 2U);
        REQUIRE(texture.Updates.Size == 1);

        const auto resolved = captured->ResolveForRender({});
        REQUIRE(resolved);
        REQUIRE(resolved->Data()->Textures);
        REQUIRE(resolved->Data()->Textures->Size == 1);
        const ImTextureData* snapshot = (*resolved->Data()->Textures)[0];
        REQUIRE(snapshot);
        CHECK(snapshot->Status == ImTextureStatus_WantCreate);
        CHECK(snapshot->UpdateRect.x == 1U);
        CHECK(snapshot->UpdateRect.y == 0U);
        CHECK(snapshot->UpdateRect.w == 1U);
        CHECK(snapshot->UpdateRect.h == 2U);
        REQUIRE(snapshot->Updates.Size == 1);
        CHECK(std::memcmp(snapshot->Pixels, pixels.data(), pixels.size()) == 0);
        CHECK(texture.GetTexID() == logicalId);
    }

    SUBCASE("failed capture")
    {
        ImTextureData valid;
        valid.Create(ImTextureFormat_RGBA32, 1, 1);
        REQUIRE(valid.Status == ImTextureStatus_WantCreate);

        ImTextureData invalid;
        invalid.Format = ImTextureFormat_RGBA32;
        invalid.Width = 1;
        invalid.Height = 1;
        invalid.BytesPerPixel = 4;
        invalid.Pixels = nullptr;
        invalid.SetStatus(ImTextureStatus_WantCreate);

        ImVector<ImTextureData*> textures;
        textures.push_back(&valid);
        textures.push_back(&invalid);
        ImDrawData drawData;
        drawData.Valid = true;
        drawData.Textures = &textures;

        CHECK_THROWS_AS((void)Keire::RenderBackend::OwnedImGuiDrawData::Capture(&drawData, {}), std::invalid_argument);
        CHECK(valid.Status == ImTextureStatus_WantCreate);
        CHECK(valid.GetTexID() == ImTextureID_Invalid);
    }

    SUBCASE("committed destroy")
    {
        ImTextureData texture;
        texture.Create(ImTextureFormat_RGBA32, 1, 1);
        ImVector<ImTextureData*> textures;
        textures.push_back(&texture);
        ImDrawData drawData;
        drawData.Valid = true;
        drawData.Textures = &textures;

        REQUIRE(Keire::RenderBackend::OwnedImGuiDrawData::Capture(&drawData, {}));
        REQUIRE(texture.Status == ImTextureStatus_OK);
        REQUIRE(texture.GetTexID() != ImTextureID_Invalid);
        texture.SetStatus(ImTextureStatus_WantDestroy);

        REQUIRE(Keire::RenderBackend::OwnedImGuiDrawData::Capture(&drawData, {}));
        CHECK(texture.Status == ImTextureStatus_Destroyed);
        CHECK(texture.GetTexID() == ImTextureID_Invalid);
        CHECK(texture.WantDestroyNextFrame);
    }
}

TEST_CASE("ImGui live atlas acknowledgement remains valid through the next owner frame")
{
    ImGuiContextScope contextScope;
    BeginImGuiPacketFrame(contextScope);
    ImGui::GetForegroundDrawList()->AddRectFilled({4.0F, 4.0F}, {28.0F, 28.0F}, IM_COL32_WHITE);
    ImGui::Render();

    ImDrawData* firstFrame = ImGui::GetDrawData();
    REQUIRE(firstFrame);
    REQUIRE(firstFrame->Textures);
    REQUIRE(firstFrame->Textures->Size > 0);
    ImTextureData* liveAtlas = (*firstFrame->Textures)[0];
    REQUIRE(liveAtlas);
    REQUIRE(liveAtlas->Status == ImTextureStatus_WantCreate);
    const auto captured = Keire::RenderBackend::OwnedImGuiDrawData::Capture(firstFrame, {});
    REQUIRE(captured);
    CHECK(liveAtlas->Status == ImTextureStatus_OK);
    const ImTextureID logicalId = liveAtlas->GetTexID();
    REQUIRE(logicalId != ImTextureID_Invalid);

    BeginImGuiPacketFrame(contextScope);
    ImGui::GetForegroundDrawList()->AddRectFilled({8.0F, 8.0F}, {24.0F, 24.0F}, IM_COL32_WHITE);
    ImGui::Render();

    CHECK(liveAtlas->Status == ImTextureStatus_OK);
    CHECK(liveAtlas->GetTexID() == logicalId);
}

TEST_CASE("ImGui frame packets preserve finalized non-empty draw output")
{
    ImGuiContextScope contextScope;
    BeginImGuiPacketFrame(contextScope);
    ImGui::GetForegroundDrawList()->AddRectFilled({4.0F, 4.0F}, {28.0F, 28.0F}, IM_COL32(255, 255, 255, 255));
    ImGui::Render();

    auto* finalized = ImGui::GetDrawData();
    REQUIRE(finalized);
    REQUIRE(finalized->Valid);
    REQUIRE(finalized->CmdListsCount > 0);
    REQUIRE(finalized->TotalVtxCount > 0);
    REQUIRE(finalized->TotalIdxCount > 0);
    const auto expectedLists = finalized->CmdListsCount;
    const auto expectedVertices = finalized->TotalVtxCount;
    const auto expectedIndices = finalized->TotalIdxCount;

    const auto captured = Keire::RenderBackend::OwnedImGuiDrawData::Capture(finalized, {});
    REQUIRE(captured);
    const auto resolved = captured->ResolveForRender({});
    REQUIRE(resolved);
    const auto* renderView = resolved->Data();
    REQUIRE(renderView);
    CHECK(renderView->Valid);
    CHECK(renderView->CmdListsCount == expectedLists);
    CHECK(renderView->CmdLists.Size == expectedLists);
    CHECK(renderView->TotalVtxCount == expectedVertices);
    CHECK(renderView->TotalIdxCount == expectedIndices);
    REQUIRE(renderView->CmdListsCount > 0);
    CHECK(renderView->CmdLists[0] != finalized->CmdLists[0]);
    CHECK(renderView->CmdLists[0]->VtxBuffer.Data != finalized->CmdLists[0]->VtxBuffer.Data);
    CHECK(renderView->CmdLists[0]->IdxBuffer.Data != finalized->CmdLists[0]->IdxBuffer.Data);
}

TEST_CASE("ImGui frame packets reject raw GPU textures borrowed pixels and arbitrary callbacks")
{
    SUBCASE("raw GPU texture")
    {
        ImGuiContextScope contextScope;
        BeginImGuiPacketFrame(contextScope);
        ImGui::GetForegroundDrawList()->AddImage(ImTextureRef(static_cast<ImTextureID>(0x1234U)), {4.0F, 4.0F},
                                                 {28.0F, 28.0F});
        ImGui::Render();

        CHECK_THROWS_WITH_AS((void)Keire::RenderBackend::OwnedImGuiDrawData::Capture(ImGui::GetDrawData(), {}),
                             "Dear ImGui draw data contains an unregistered raw GPU texture; use a UI image or render "
                             "surface lease.",
                             std::invalid_argument);
    }

    SUBCASE("borrowed texture pixels")
    {
        ImTextureData texture;
        texture.Format = ImTextureFormat_RGBA32;
        texture.Width = 1;
        texture.Height = 1;
        texture.BytesPerPixel = 4;
        texture.Pixels = nullptr;
        ImVector<ImTextureData*> textures;
        textures.push_back(&texture);
        ImDrawData drawData;
        drawData.Valid = true;
        drawData.Textures = &textures;

        CHECK_THROWS_WITH_AS(
            (void)Keire::RenderBackend::OwnedImGuiDrawData::Capture(&drawData, {}),
            "Dear ImGui asynchronous packets require owned RGBA32 texture pixels; borrowed backend-only texture data "
            "is unsupported.",
            std::invalid_argument);
    }

    SUBCASE("arbitrary callback")
    {
        ImGuiContextScope contextScope;
        BeginImGuiPacketFrame(contextScope);
        auto* drawList = ImGui::GetForegroundDrawList();
        drawList->AddCallback(&ArbitraryImGuiCallback, nullptr);
        drawList->AddRectFilled({4.0F, 4.0F}, {28.0F, 28.0F}, IM_COL32_WHITE);
        ImGui::Render();

        CHECK_THROWS_WITH_AS((void)Keire::RenderBackend::OwnedImGuiDrawData::Capture(ImGui::GetDrawData(), {}),
                             "Dear ImGui user callbacks cannot be executed from an asynchronous render packet.",
                             std::invalid_argument);
    }
}

#if defined(KEIRE_ENABLE_TEST_HOOKS)
TEST_CASE("Render admission waits before enumerating pending scene and runtime UI state")
{
    UsePipelineDummyVideoDriver();
    DeferredCaptureProbe probe;
    auto specification = PipelineSpecification(1U);
    Keire::Application application(specification);
    (void)application.PushLayer(std::make_unique<DeferredCaptureLayer>(probe));
    CHECK(application.Run() == 0);
    CHECK(probe.DistinctRenderThread);
    CHECK(probe.AcceptedFrameBlocked.load(std::memory_order_acquire));
    CHECK(probe.AdmissionWaiterObserved.load(std::memory_order_acquire));
    CHECK(probe.SceneEnumerationsWhileBlocked.load(std::memory_order_acquire) == 1U);
    CHECK(probe.RuntimeUiEnumerationsWhileBlocked.load(std::memory_order_acquire) == 1U);
    CHECK(probe.Statistics.AcceptedFrames == 2U);
    CHECK(probe.Statistics.RetiredFrames == 2U);
}

TEST_CASE("Render capture failure returns its unaccepted slot exactly once")
{
    UsePipelineDummyVideoDriver();
    CaptureFailureProbe probe;
    Keire::Application application(PipelineSpecification(2U));
    (void)application.PushLayer(std::make_unique<CaptureFailureLayer>(probe));
    CHECK_THROWS_WITH_AS((void)application.Run(), "Injected immutable frame capture failure.", std::runtime_error);
    CHECK(probe.Statistics.AcceptedFrames == 0U);
    CHECK(probe.Statistics.RetiredFrames == 0U);
    CHECK(probe.Statistics.CancelledFrames == 0U);
    CHECK(probe.AvailableSlots == 2U);
}

TEST_CASE("First terminal frame failure cancels a saturated queue in order and releases every packet lease")
{
    UsePipelineDummyVideoDriver();
    TerminalQueueFailureProbe probe;
    Keire::Application application(PipelineSpecification(3U));
    (void)application.PushLayer(std::make_unique<TerminalQueueFailureLayer>(probe));
    CHECK_THROWS_WITH((void)application.Run(), "Injected accepted-frame terminal failure.");
    CHECK(probe.DistinctRenderThread);
    CHECK(probe.Closed);
    CHECK(probe.Statistics.AcceptedFrames == 3U);
    CHECK(probe.Statistics.RetiredFrames == 0U);
    CHECK(probe.Statistics.CancelledFrames == 3U);
    CHECK(probe.Statistics.OutstandingFrames == 0U);
    CHECK(probe.AvailableSlots == 3U);
    CHECK(probe.SurfaceLifetimeOwners == 1U);
    CHECK(probe.RepeatedFailures[0] == "Injected accepted-frame terminal failure.");
    CHECK(probe.RepeatedFailures[1] == probe.RepeatedFailures[0]);
    REQUIRE(probe.Timelines.size() == 3U);
    for (std::size_t index = 0; index < probe.Timelines.size(); ++index)
    {
        CHECK(probe.Timelines[index].Frame == index + 1U);
        CHECK(probe.Timelines[index].Cancelled);
    }
}

TEST_CASE("Additive directional lighting falls back in stable contribution and reload order")
{
    UsePipelineDummyVideoDriver();
    DirectionalFallbackProbe probe;
    Keire::Application application(PipelineSpecification(2U));
    (void)application.PushLayer(std::make_unique<DirectionalFallbackLayer>(probe));
    CHECK(application.Run() == 0);
    REQUIRE(probe.CapturedLights.size() == 2U);
    CHECK(probe.CapturedLights[0] == probe.FirstLight);
    CHECK(probe.CapturedLights[1] == probe.ReloadedLight);
}

TEST_CASE("Ignored SDL GPU failures classify device loss with stable operation codes")
{
    UsePipelineDummyVideoDriver();
    DeviceFailureClassificationProbe probe;
    Keire::Application application(PipelineSpecification(1U));
    (void)application.PushLayer(std::make_unique<DeviceFailureClassificationLayer>(probe));
    CHECK(application.Run() == 0);
    REQUIRE(probe.Diagnostics.size() == 3U);
    CHECK(probe.Diagnostics[0].Operation == "SDL_MapGPUTransferBuffer");
    CHECK(probe.Diagnostics[1].Operation == "SDL_BeginGPURenderPass(occlusion debug)");
    CHECK(probe.Diagnostics[2].Operation == "SDL_AcquireGPUCommandBuffer(surface)");
}

TEST_CASE("Recovery winning the admission barrier returns the unaccepted slot without capture")
{
    UsePipelineDummyVideoDriver();
    AdmissionRecoveryProbe probe;
    Keire::Application application(PipelineSpecification(2U));
    (void)application.PushLayer(std::make_unique<AdmissionRecoveryLayer>(probe));
    CHECK(application.Run() == 0);
    CHECK(probe.EnumerationsBeforeResume == 0U);
    CHECK(probe.SlotsBeforeResume == 2U);
    CHECK(probe.Statistics.AcceptedFrames == 1U);
    CHECK(probe.Statistics.RetiredFrames == 1U);
    CHECK(probe.Statistics.CancelledFrames == 0U);
}

TEST_CASE("Flush affinity recovery signalling completion and pending Close preserve pipeline invariants")
{
    UsePipelineDummyVideoDriver();
    PipelineInvariantProbe probe;
    Keire::Application application(PipelineSpecification(2U));
    (void)application.PushLayer(std::make_unique<PipelineInvariantLayer>(probe));
    CHECK(application.Run() == 0);
    CHECK(probe.WorkerFlushRejected);
    CHECK(probe.WorkerFlushStateUnchanged);
    CHECK(probe.RecoveryFlushSignalled);
    CHECK(probe.RecoveryFlushStateUnchanged);
    CHECK(probe.CompletionExactlyOnce);
    CHECK(probe.RecoveryPendingCloseCompleted);
}
#endif
