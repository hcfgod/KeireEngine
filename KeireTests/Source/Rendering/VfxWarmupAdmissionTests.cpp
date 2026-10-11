#include "Keire/Core.h"
#include "KeireInternal/RenderInternal.h"

#include <SDL3/SDL.h>
#include <doctest/doctest.h>

#include <atomic>
#include <cstdlib>
#include <memory>
#include <thread>

#if defined(KEIRE_ENABLE_TEST_HOOKS)
namespace
{
    struct WarmupAdmissionProbe final
    {
        std::atomic<bool> WaiterObserved{false};
        std::uint64_t CapturesAfterDeferral = 0;
        std::uint64_t AcceptedAfterDeferral = 0;
        std::size_t SlotsAfterDeferral = 0;
        Keire::RenderStatistics Final;
    };

    class WarmupAdmissionLayer final : public Keire::Layer
    {
      public:
        explicit WarmupAdmissionLayer(WarmupAdmissionProbe& probe) : Layer("warmup-admission"), m_Probe(probe) {}

      protected:
        void OnAttach() override
        {
            REQUIRE(Keire::RenderSystemInternalAccess::StartThreadedHeadlessForTest(*Owner().Renderer()));
            m_Scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(),
                                                     Keire::SceneAsset::EmptyDefinition("warmup-admission"));
            m_View = Owner().Renderer()->CreateView({.Name = "warmup-admission", .Width = 32, .Height = 32});
        }

        void OnUpdate(const Keire::Time&) override
        {
            const auto renderer = Owner().Renderer();
            if (m_Frame++ == 0U)
            {
                Keire::RenderSystemInternalAccess::BlockNextAcceptedFrame(*renderer);
                renderer->Submit({m_Scene, m_View});
                return;
            }
            if (m_Frame == 2U)
            {
                REQUIRE(Keire::RenderSystemInternalAccess::WaitForAcceptedFrameBlock(*renderer));
                m_Warmup = std::jthread(
                    [renderer, &probe = m_Probe]
                    {
                        const bool waiting = Keire::RenderSystemInternalAccess::WaitForFrameAdmissionWaiter(*renderer);
                        probe.WaiterObserved.store(waiting, std::memory_order_release);
                        Keire::RenderSystemInternalAccess::SetGpuVfxWarmupPendingForTest(*renderer, true);
                    });
                renderer->Submit({m_Scene, m_View});
                return;
            }
            m_Warmup.join();
            m_Probe.CapturesAfterDeferral = Keire::RenderSystemInternalAccess::SceneCaptureEnumerationCount(*renderer);
            m_Probe.AcceptedAfterDeferral = renderer->Statistics().AcceptedFrames;
            m_Probe.SlotsAfterDeferral = Keire::RenderSystemInternalAccess::AvailableFrameSlotCount(*renderer);
            Keire::RenderSystemInternalAccess::SetGpuVfxWarmupPendingForTest(*renderer, false);
            Keire::RenderSystemInternalAccess::ReleaseAcceptedFrameBlock(*renderer);
            renderer->Submit({m_Scene, m_View});
            Owner().RequestExit();
        }

        void OnDetach() noexcept override
        {
            const auto renderer = Owner().Renderer();
            if (m_Warmup.joinable())
                m_Warmup.join();
            Keire::RenderSystemInternalAccess::SetGpuVfxWarmupPendingForTest(*renderer, false);
            Keire::RenderSystemInternalAccess::ReleaseAcceptedFrameBlock(*renderer);
            try
            {
                renderer->Flush();
                m_Probe.Final = renderer->Statistics();
            }
            catch (...)
            {
            }
            if (m_Scene)
                m_Scene->Close();
        }

      private:
        WarmupAdmissionProbe& m_Probe;
        Keire::Ref<Keire::Scene> m_Scene;
        Keire::Ref<Keire::RenderView> m_View;
        std::jthread m_Warmup;
        std::uint32_t m_Frame = 0;
    };
} // namespace

TEST_CASE("VFX warmup starting after frame admission waits defers capture and resumes without losing slots")
{
#if defined(_WIN32)
    REQUIRE(_putenv_s("SDL_VIDEODRIVER", "dummy") == 0);
#else
    REQUIRE(setenv("SDL_VIDEODRIVER", "dummy", 1) == 0);
#endif
    Keire::ApplicationSpecification specification;
    specification.MainWindow.Title = "warmup-admission";
    specification.MainWindow.Visible = false;
    specification.SuspendWhenMainWindowMinimized = false;
    specification.ManageLogging = false;
    specification.Render.Mode = Keire::RenderMode::Headless;
    specification.Render.MaximumFramesInFlight = 1U;
    specification.Ui.Mode = Keire::UiMode::Disabled;
    WarmupAdmissionProbe probe;
    Keire::Application application(specification);
    (void)application.PushLayer(std::make_unique<WarmupAdmissionLayer>(probe));
    CHECK(application.Run() == 0);
    CHECK(probe.WaiterObserved.load(std::memory_order_acquire));
    CHECK(probe.CapturesAfterDeferral == 1U);
    CHECK(probe.AcceptedAfterDeferral == 1U);
    CHECK(probe.SlotsAfterDeferral == 0U);
    CHECK(probe.Final.AcceptedFrames == 2U);
    CHECK(probe.Final.RetiredFrames == 2U);
    CHECK(probe.Final.OutstandingFrames == 0U);
    CHECK(probe.Final.CancelledFrames == 0U);
}
#endif
