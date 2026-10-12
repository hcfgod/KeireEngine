#include "Keire/Core.h"
#include "KeireInternal/RenderInternal.h"
#include "KeireInternal/Rendering/GpuVfxPipelineBatchInternal.h"

#include <SDL3/SDL.h>
#include <doctest/doctest.h>

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <stop_token>
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

namespace
{
    struct PipelineBatchProbe final
    {
        std::array<int, Keire::RenderBackend::GpuVfxPipelineBatch::Count> Storage{};
        std::array<unsigned, Keire::RenderBackend::GpuVfxPipelineBatch::Count> Releases{};

        SDL_GPUDevice* Device() noexcept { return reinterpret_cast<SDL_GPUDevice*>(this); }
        SDL_GPUComputePipeline* Handle(std::size_t index) noexcept
        {
            return reinterpret_cast<SDL_GPUComputePipeline*>(&Storage[index]);
        }
        static void Release(SDL_GPUDevice* device, SDL_GPUComputePipeline* pipeline) noexcept
        {
            auto& probe = *reinterpret_cast<PipelineBatchProbe*>(device);
            for (std::size_t index = 0; index < probe.Storage.size(); ++index)
            {
                if (probe.Handle(index) == pipeline)
                    ++probe.Releases[index];
            }
        }
    };
} // namespace

TEST_CASE("GPU VFX pipeline batch releases partial compilation while preserving the original failure")
{
    PipelineBatchProbe probe;
    const auto compile = [&]
    {
        Keire::RenderBackend::GpuVfxPipelineBatch batch(probe.Device(), PipelineBatchProbe::Release);
        batch.Pipelines[0] = probe.Handle(0);
        batch.Pipelines[3] = probe.Handle(3);
        throw std::runtime_error("pipeline compilation failed");
    };
    CHECK_THROWS_WITH_AS(compile(), "pipeline compilation failed", std::runtime_error);
    for (std::size_t index = 0; index < probe.Releases.size(); ++index)
        CHECK(probe.Releases[index] == ((index == 0 || index == 3) ? 1U : 0U));
}

TEST_CASE("GPU VFX pipeline batch transfers complete ownership without premature release")
{
    PipelineBatchProbe probe;
    Keire::RenderBackend::GpuVfxPipelineBatch::Handles published{};
    {
        Keire::RenderBackend::GpuVfxPipelineBatch batch(probe.Device(), PipelineBatchProbe::Release);
        for (std::size_t index = 0; index < batch.Pipelines.size(); ++index)
            batch.Pipelines[index] = probe.Handle(index);
        published = batch.Release();
        CHECK(batch.Release() == Keire::RenderBackend::GpuVfxPipelineBatch::Handles{});
    }
    for (std::size_t index = 0; index < published.size(); ++index)
    {
        CHECK(published[index] == probe.Handle(index));
        CHECK(probe.Releases[index] == 0);
        PipelineBatchProbe::Release(probe.Device(), published[index]);
        CHECK(probe.Releases[index] == 1);
    }
}

TEST_CASE("GPU VFX pipeline batch abandons lost-device handles without invoking their release callback")
{
    PipelineBatchProbe probe;
    {
        Keire::RenderBackend::GpuVfxPipelineBatch batch(probe.Device(), PipelineBatchProbe::Release);
        batch.Pipelines[0] = probe.Handle(0);
        batch.Pipelines[15] = probe.Handle(15);
        batch.Abandon();
        batch.Abandon();
    }
    for (const auto releases : probe.Releases)
        CHECK(releases == 0);
}

#if defined(KEIRE_ENABLE_TEST_HOOKS)
namespace
{
    struct BackgroundWarmupProbe final
    {
        std::mutex Mutex;
        std::condition_variable Changed;
        bool Started = false;
        bool Release = false;
        bool TimedOut = false;
        bool Fail = false;
        bool RejectScheduling = false;
        bool ExpireGeneration = false;
        std::uint64_t Retired = 0;
        bool CancelOnClose = false;
        bool SawCancellation = false;
        std::uint64_t RetiredWhileCompiling = 0;
        bool Ready = false;
    };

    class BackgroundWarmupLayer final : public Keire::Layer
    {
      public:
        explicit BackgroundWarmupLayer(BackgroundWarmupProbe& probe) : Layer("background-warmup"), m_Probe(probe) {}

      protected:
        void OnAttach() override
        {
            auto renderer = Owner().Renderer();
            REQUIRE(Keire::RenderSystemInternalAccess::StartThreadedHeadlessForTest(*renderer));
            m_Scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(),
                                                     Keire::SceneAsset::EmptyDefinition("background-warmup"));
            m_View = renderer->CreateView({.Name = "background-warmup", .Width = 32, .Height = 32});
            Keire::RenderSystemInternalAccess::StartGpuVfxWarmupForTest(
                *renderer,
                [probe = &m_Probe](Keire::JobContext& context)
                {
                    std::unique_lock lock(probe->Mutex);
                    probe->Started = true;
                    probe->Changed.notify_all();
                    std::stop_callback stopped(context.StopToken(), [probe] { probe->Changed.notify_all(); });
                    probe->TimedOut = !probe->Changed.wait_for(lock, std::chrono::seconds(3), [probe, &context]
                                                               { return probe->Release || context.StopRequested(); });
                    probe->SawCancellation = context.StopRequested();
                    if (probe->Fail)
                        throw std::runtime_error("Injected background shader compilation failure.");
                },
                m_Probe.RejectScheduling);
            if (!m_Probe.RejectScheduling)
            {
                std::unique_lock lock(m_Probe.Mutex);
                REQUIRE(m_Probe.Changed.wait_for(lock, std::chrono::seconds(3), [this] { return m_Probe.Started; }));
            }
            m_Started = std::chrono::steady_clock::now();
        }
        void OnUpdate(const Keire::Time&) override
        {
            auto renderer = Owner().Renderer();
            const auto statistics = renderer->Statistics();
            m_Probe.Retired = statistics.RetiredFrames;
            if (statistics.VfxPipelineWarmupPending)
            {
                m_Probe.RetiredWhileCompiling = statistics.RetiredFrames;
                if (statistics.RetiredFrames >= 3)
                {
                    if (m_Probe.ExpireGeneration && !m_GenerationExpired)
                    {
                        Keire::RenderSystemInternalAccess::ExpireGpuVfxWarmupForTest(*renderer);
                        m_GenerationExpired = true;
                    }
                    if (m_Probe.CancelOnClose)
                        Owner().RequestExit();
                    else
                        ReleaseCompile();
                }
            }
            if (statistics.VfxPipelinesReady ||
                (!statistics.VfxPipelineWarmupPending && statistics.RetiredFrames >= 3) ||
                std::chrono::steady_clock::now() - m_Started > std::chrono::seconds(4))
            {
                m_Probe.Ready = statistics.VfxPipelinesReady;
                ReleaseCompile();
                Owner().RequestExit();
            }
            renderer->Submit({m_Scene, m_View});
        }
        void OnDetach() noexcept override
        {
            if (!m_Probe.CancelOnClose)
                ReleaseCompile();
            try
            {
                Owner().Renderer()->Flush();
            }
            catch (...)
            {
            }
            if (m_Scene)
                m_Scene->Close();
        }

      private:
        void ReleaseCompile() noexcept
        {
            std::scoped_lock lock(m_Probe.Mutex);
            m_Probe.Release = true;
            m_Probe.Changed.notify_all();
        }
        BackgroundWarmupProbe& m_Probe;
        bool m_GenerationExpired = false;
        Keire::Ref<Keire::Scene> m_Scene;
        Keire::Ref<Keire::RenderView> m_View;
        std::chrono::steady_clock::time_point m_Started;
    };
} // namespace

TEST_CASE("VFX background compilation keeps retiring frames before publishing readiness")
{
#if defined(_WIN32)
    REQUIRE(_putenv_s("SDL_VIDEODRIVER", "dummy") == 0);
#else
    REQUIRE(setenv("SDL_VIDEODRIVER", "dummy", 1) == 0);
#endif
    BackgroundWarmupProbe probe;
    SUBCASE("publishes successfully") {}
    SUBCASE("ordinary failure preserves frame retirement") { probe.Fail = true; }
    SUBCASE("close cancels and drains active preparation") { probe.CancelOnClose = true; }
    SUBCASE("rejected scheduling leaves ordinary rendering usable") { probe.RejectScheduling = true; }
    SUBCASE("expired generation never publishes ready pipelines") { probe.ExpireGeneration = true; }
    {
        Keire::ApplicationSpecification specification;
        specification.MainWindow.Title = "background-warmup";
        specification.MainWindow.Visible = false;
        specification.SuspendWhenMainWindowMinimized = false;
        specification.ManageLogging = false;
        specification.Render.Mode = Keire::RenderMode::Headless;
        specification.Render.MaximumFramesInFlight = 1U;
        specification.Ui.Mode = Keire::UiMode::Disabled;
        Keire::Application application(specification);
        (void)application.PushLayer(std::make_unique<BackgroundWarmupLayer>(probe));
        CHECK(application.Run() == 0);
    }
    CHECK_FALSE(probe.TimedOut);
    CHECK(probe.Retired >= 3);
    CHECK(probe.Started == !probe.RejectScheduling);
    if (!probe.RejectScheduling)
        CHECK(probe.RetiredWhileCompiling >= 3);
    CHECK(probe.Ready == (!probe.Fail && !probe.CancelOnClose && !probe.RejectScheduling && !probe.ExpireGeneration));
    CHECK(probe.SawCancellation == probe.CancelOnClose);
}
#endif
