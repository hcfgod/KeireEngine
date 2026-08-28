#include "KeireRenderTests/RenderedOutputTestSupport.h"

#include "Keire/Assets/AssetSystem.h"
#include "Keire/Assets/RenderingAssets.h"
#include "Keire/BuiltinUnlitShaders.h"
#include "Keire/ECS/Components/CameraComponent.h"
#include "Keire/ECS/Components/DirectionalLightComponent.h"
#include "Keire/ECS/Components/MeshRendererComponent.h"
#include "Keire/ECS/Components/TransformComponent.h"
#include "Keire/Scenes/Scene.h"
#include "Keire/Scenes/SceneRuntimeWorld.h"
#include "KeireInternal/RenderInternal.h"
#include "KeireInternal/Rendering/RenderSurfaceStateInternal.h"
#include "KeireInternal/Scenes/SceneRuntimeRenderingInternal.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace
{
    [[nodiscard]] Keire::RenderBackend::RenderSurfacePropertySnapshot
    CurrentSurfaceProperties(const Keire::RenderSurface& surface)
    {
        const auto state = std::static_pointer_cast<Keire::RenderBackend::RenderSurfaceState>(
            Keire::RenderSystemInternalAccess::SurfaceLease(surface));
        if (!state)
            throw std::logic_error("A render surface lost its state while reading published properties.");
        return state->SurfacePropertiesSnapshot();
    }

    class CloseAfterSubmitLayer final : public Keire::Layer
    {
      public:
        CloseAfterSubmitLayer() : Layer("Close scene after submit") {}

      protected:
        void OnAttach() override
        {
            m_Scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Parse("711ace00-0000-4000-8000-000000000010"),
                                                     Keire::SceneAsset::EmptyDefinition("Frame-local scene packet"),
                                                     Keire::ComponentRegistry::CreateDefault());
            auto object = m_Scene->CreateEntity("Closing cube");
            (void)object.AddComponent<Keire::MeshRendererComponent>();
            Keire::RenderSurfaceSpecification surface;
            surface.Name = "Frame-local scene packet";
            surface.Width = SurfaceSize;
            surface.Height = SurfaceSize;
            surface.SampleCount = Keire::RenderSampleCount::One;
            m_View = Owner().Renderer()->CreateView(surface);
            Keire::RenderCamera camera;
            camera.View = Keire::Math::LookAt({0.0F, 0.0F, 2.5F}, {}, {0.0F, 1.0F, 0.0F});
            camera.Projection = Keire::Math::Perspective(55.0F, 1.0F, 0.1F, 100.0F);
            m_View->SetCamera(camera);
        }

        void OnDetach() noexcept override
        {
            m_View.Reset();
            m_Scene.Reset();
        }

        void OnUpdate(const Keire::Time&) override
        {
            Owner().Renderer()->Submit({m_Scene, m_View});
            m_Scene->Close();
            Owner().RequestExit();
        }

      private:
        Keire::Ref<Keire::Scene> m_Scene;
        Keire::Ref<Keire::RenderView> m_View;
    };

    struct RendererLifecycleResults final
    {
        std::uint32_t QueueHighWaterMark = 0;
        std::uint64_t InitialGeneration = 0;
        std::uint64_t ResizedGeneration = 0;
        std::uint64_t MinimizedGeneration = 0;
        std::uint64_t RestoredGeneration = 0;
        Keire::GpuOcclusionSurfaceDiagnostics MinimizedOcclusion;
        Keire::GpuOcclusionSurfaceDiagnostics RestoredOcclusion;
        Keire::RenderBackend::RenderSurfacePropertySnapshot ResizedProperties;
        Keire::RenderBackend::RenderSurfacePropertySnapshot MinimizedProperties;
        Keire::RenderBackend::RenderSurfacePropertySnapshot RestoredProperties;
        std::vector<Keire::RenderBackend::RenderSurfacePropertySnapshot> ObservedProperties;
        bool Resized = false;
        bool Minimized = false;
        bool Restored = false;
    };

#if defined(KEIRE_ENABLE_TEST_HOOKS)
    class QueueSaturationLayer final : public Keire::Layer
    {
      public:
        explicit QueueSaturationLayer(std::uint32_t& highWaterMark)
            : Layer("Renderer queue saturation"), m_HighWaterMark(highWaterMark)
        {
        }

      protected:
        void OnUpdate(const Keire::Time&) override
        {
            m_HighWaterMark = Keire::RenderSystemInternalAccess::SaturateRendererQueue(*Owner().Renderer());
            Owner().RequestExit();
        }

      private:
        std::uint32_t& m_HighWaterMark;
    };
#endif

    struct SurfaceEpochLeaseResults final
    {
        std::size_t Worksets = 0;
        std::size_t FinalOutputs = 0;
        bool ResizeEpochAliveWhileLeased = false;
        bool ResizeEpochRetiredAfterRelease = false;
        bool MinimizedEpochAliveWhileLeased = false;
        bool MinimizedEpochRetiredAfterRelease = false;
        bool Restored = false;
        std::vector<Keire::RenderFrameTimeline> Timelines;
    };

    struct PresentationOrderResults final
    {
        Keire::RenderStatistics Statistics;
        std::vector<Keire::RenderFrameTimeline> Timelines;
    };

#if defined(KEIRE_ENABLE_TEST_HOOKS)
    struct FlushPublicationBarrierResults final
    {
        Keire::RenderStatistics Statistics;
        std::vector<Keire::RenderFrameTimeline> Timelines;
        bool DistinctRenderThread = false;
        bool FlushCompleted = false;
    };

    class FlushPublicationBarrierLayer final : public Keire::Layer
    {
      public:
        explicit FlushPublicationBarrierLayer(FlushPublicationBarrierResults& results)
            : Layer("Flush publication barrier"), m_Results(results)
        {
        }

      protected:
        void OnAttach() override
        {
            m_Results.DistinctRenderThread =
                Keire::RenderSystemInternalAccess::StartThreadedHeadlessForTest(*Owner().Renderer());
            m_Scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(),
                                                     Keire::SceneAsset::EmptyDefinition("Flush publication barrier"));
            m_View = Owner().Renderer()->CreateView({.Name = "Flush publication barrier", .Width = 32, .Height = 32});
        }

        void OnUpdate(const Keire::Time&) override
        {
            Owner().Renderer()->Submit({m_Scene, m_View});
            if (++m_Submitted == 16U)
                Owner().RequestExit();
        }

        void OnDetach() noexcept override
        {
            try
            {
                const auto renderer = Owner().Renderer();
                renderer->Flush();
                m_Results.FlushCompleted = true;
                m_Results.Statistics = renderer->Statistics();
                m_Results.Timelines = renderer->RecentFrameTimelines();
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
        FlushPublicationBarrierResults& m_Results;
        Keire::Ref<Keire::Scene> m_Scene;
        Keire::Ref<Keire::RenderView> m_View;
        std::uint32_t m_Submitted = 0U;
    };
#endif

    class PresentationOrderLayer final : public Keire::Layer
    {
      public:
        explicit PresentationOrderLayer(PresentationOrderResults& results)
            : Layer("Monotonic rendered presentation"), m_Results(results)
        {
        }

      protected:
        void OnAttach() override
        {
            m_Scene = Keire::CreateRef<Keire::Scene>(
                Keire::AssetId::Generate(), Keire::SceneAsset::EmptyDefinition("Monotonic rendered presentation"));
            auto object = m_Scene->CreateEntity("Presented cube");
            (void)object.AddComponent<Keire::MeshRendererComponent>();
            m_View = Owner().Renderer()->CreateView(
                {.Name = "Monotonic rendered presentation", .Width = 48U, .Height = 48U});
            Keire::RenderCamera camera;
            camera.View = Keire::Math::LookAt({0.0F, 0.0F, 2.5F}, {}, {0.0F, 1.0F, 0.0F});
            camera.Projection = Keire::Math::Perspective(55.0F, 1.0F, 0.1F, 100.0F);
            m_View->SetCamera(camera);
            Keire::RenderSystemInternalAccess::SetPresentationSurface(*Owner().Renderer(), m_View->Surface());
        }

        void OnUpdate(const Keire::Time&) override
        {
            constexpr std::uint32_t frameCount = 12U;
            const auto renderer = Owner().Renderer();
            if (m_Submitted < frameCount)
            {
                renderer->Submit({m_Scene, m_View});
                ++m_Submitted;
                return;
            }
            renderer->Flush();
            m_Results.Statistics = renderer->Statistics();
            m_Results.Timelines = renderer->RecentFrameTimelines();
            Owner().RequestExit();
        }

        void OnDetach() noexcept override
        {
            if (m_Scene)
                m_Scene->Close();
            m_View.Reset();
            m_Scene.Reset();
        }

      private:
        PresentationOrderResults& m_Results;
        Keire::Ref<Keire::Scene> m_Scene;
        Keire::Ref<Keire::RenderView> m_View;
        std::uint32_t m_Submitted = 0U;
    };

    class SurfaceEpochLeaseLayer final : public Keire::Layer
    {
      public:
        explicit SurfaceEpochLeaseLayer(SurfaceEpochLeaseResults& results)
            : Layer("Surface epoch output lease"), m_Results(results)
        {
        }

      protected:
        void OnAttach() override
        {
            m_Scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(),
                                                     Keire::SceneAsset::EmptyDefinition("Surface epoch output lease"));
            m_View = Owner().Renderer()->CreateView({.Name = "Surface epoch output lease", .Width = 32, .Height = 32});
        }

        void OnUpdate(const Keire::Time&) override
        {
            const auto renderer = Owner().Renderer();
            const auto surface = m_View->Surface();
            REQUIRE(renderer);
            REQUIRE(surface);
            switch (m_Phase++)
            {
            case 0U:
                renderer->Submit({m_Scene, m_View});
                break;
            case 1U:
            {
                renderer->Flush();
                const auto state = std::static_pointer_cast<Keire::RenderBackend::RenderSurfaceState>(
                    Keire::RenderSystemInternalAccess::SurfaceLease(*surface));
                REQUIRE(state);
                REQUIRE(state->Lifetime);
                m_Results.Worksets = state->Resources.Worksets.size();
                m_Results.FinalOutputs = state->Resources.FinalOutputs.size();
                m_ResizeEpochLease = state->Lifetime;
                m_ResizeEpochLifetime = state->Lifetime;
                surface->RequestSize(48U, 40U);
                renderer->Submit({m_Scene, m_View});
                break;
            }
            case 2U:
            {
                renderer->Flush();
                m_Results.ResizeEpochAliveWhileLeased = !m_ResizeEpochLifetime.expired();
                m_ResizeEpochLease.reset();
                const auto state = std::static_pointer_cast<Keire::RenderBackend::RenderSurfaceState>(
                    Keire::RenderSystemInternalAccess::SurfaceLease(*surface));
                REQUIRE(state);
                REQUIRE(state->Lifetime);
                m_MinimizedEpochLease = state->Lifetime;
                m_MinimizedEpochLifetime = state->Lifetime;
                Keire::RenderSystemInternalAccess::RequestSurfaceSize(*surface, 0U, 0U);
                break;
            }
            case 3U:
                renderer->Flush();
                m_Results.ResizeEpochRetiredAfterRelease = m_ResizeEpochLifetime.expired();
                m_Results.MinimizedEpochAliveWhileLeased = !m_MinimizedEpochLifetime.expired();
                m_MinimizedEpochLease.reset();
                Keire::RenderSystemInternalAccess::RequestSurfaceSize(*surface, 64U, 36U);
                renderer->Submit({m_Scene, m_View});
                break;
            case 4U:
                renderer->Flush();
                renderer->Submit({m_Scene, m_View});
                break;
            default:
                renderer->Flush();
                m_Results.MinimizedEpochRetiredAfterRelease = m_MinimizedEpochLifetime.expired();
                m_Results.Restored = surface->Available() && surface->Width() == 64U && surface->Height() == 36U;
                m_Results.Timelines = renderer->RecentFrameTimelines();
                Owner().RequestExit();
                break;
            }
        }

        void OnDetach() noexcept override
        {
            m_ResizeEpochLease.reset();
            m_MinimizedEpochLease.reset();
            if (m_Scene)
                m_Scene->Close();
            m_View.Reset();
            m_Scene.Reset();
        }

      private:
        SurfaceEpochLeaseResults& m_Results;
        Keire::Ref<Keire::Scene> m_Scene;
        Keire::Ref<Keire::RenderView> m_View;
        std::shared_ptr<const Keire::RenderBackend::RenderSurfaceEpochLease> m_ResizeEpochLease;
        std::weak_ptr<const Keire::RenderBackend::RenderSurfaceEpochLease> m_ResizeEpochLifetime;
        std::shared_ptr<const Keire::RenderBackend::RenderSurfaceEpochLease> m_MinimizedEpochLease;
        std::weak_ptr<const Keire::RenderBackend::RenderSurfaceEpochLease> m_MinimizedEpochLifetime;
        std::uint32_t m_Phase = 0;
    };

    class RendererLifecycleLayer final : public Keire::Layer
    {
      public:
        explicit RendererLifecycleLayer(std::shared_ptr<RendererLifecycleResults> results)
            : Layer("Renderer lifecycle"), m_Results(std::move(results))
        {
        }

      protected:
        void OnAttach() override
        {
            Keire::RenderSurfaceSpecification surface;
            surface.Name = "Renderer lifecycle";
            surface.Width = 64;
            surface.Height = 64;
            m_Surface = Owner().Renderer()->CreateSurface(surface);
        }

        void OnDetach() noexcept override { m_Surface.Reset(); }

        void OnUpdate(const Keire::Time&) override
        {
            constexpr std::uint32_t maximumUpdateCount = 120U;
            if (++m_UpdateCount > maximumUpdateCount)
                throw std::runtime_error(
                    "Renderer resize/minimize/restore lifecycle did not settle within 120 updates.");
            m_Results->ObservedProperties.push_back(CurrentSurfaceProperties(*m_Surface));

            if (m_Phase == 0U)
            {
                m_Results->InitialGeneration = m_Surface->Generation();
#if defined(KEIRE_ENABLE_TEST_HOOKS)
                m_Results->QueueHighWaterMark =
                    Keire::RenderSystemInternalAccess::SaturateRendererQueue(*Owner().Renderer());
#endif
                m_Surface->RequestSize(128, 80);
                ++m_Phase;
            }
            else if (m_Phase == 1U)
            {
                if (!m_Surface->Available() || m_Surface->Width() != 128U || m_Surface->Height() != 80U)
                    return;
                m_Results->ResizedGeneration = m_Surface->Generation();
                m_Results->Resized = true;
                m_Results->ResizedProperties = {m_Surface->Width(), m_Surface->Height(), m_Surface->SampleCount()};
                Keire::RenderSystemInternalAccess::RequestSurfaceSize(*m_Surface, 0, 0);
                ++m_Phase;
            }
            else if (m_Phase == 2U)
            {
                if (m_Surface->Available() || m_Surface->Width() != 0U || m_Surface->Height() != 0U)
                    return;
                m_Results->MinimizedGeneration = m_Surface->Generation();
                m_Results->Minimized = true;
                m_Results->MinimizedProperties = {m_Surface->Width(), m_Surface->Height(), m_Surface->SampleCount()};
                m_Results->MinimizedOcclusion = m_Surface->OcclusionDiagnostics();
                Keire::RenderSystemInternalAccess::RequestSurfaceSize(*m_Surface, 96, 48);
                ++m_Phase;
            }
            else
            {
                if (!m_Surface->Available() || m_Surface->Width() != 96U || m_Surface->Height() != 48U)
                    return;
                m_Results->RestoredGeneration = m_Surface->Generation();
                m_Results->Restored = true;
                m_Results->RestoredProperties = {m_Surface->Width(), m_Surface->Height(), m_Surface->SampleCount()};
                m_Results->RestoredOcclusion = m_Surface->OcclusionDiagnostics();
                Owner().RequestExit();
            }
        }

      private:
        std::shared_ptr<RendererLifecycleResults> m_Results;
        Keire::Ref<Keire::RenderSurface> m_Surface;
        std::uint32_t m_Phase = 0U;
        std::uint32_t m_UpdateCount = 0U;
    };

#if defined(KEIRE_ENABLE_TEST_HOOKS)
    class DeviceLossLayer final : public Keire::Layer
    {
      public:
        DeviceLossLayer() : Layer("Device loss") {}

      protected:
        void OnUpdate(const Keire::Time&) override
        {
            Keire::RenderSystemInternalAccess::InjectDeviceLoss(*Owner().Renderer());
        }
    };

    struct ShutdownDeviceLossResults final
    {
        std::optional<Keire::GpuDeviceLossDiagnostic> RecoveredDiagnostic;
        std::optional<Keire::GpuDeviceLossDiagnostic> Diagnostic;
        Keire::RenderStatistics Statistics;
        Keire::RenderBackend::RenderSurfacePropertySnapshot RecoveredProperties;
        Keire::RenderBackend::RenderSurfacePropertySnapshot AfterFirstCloseProperties;
        Keire::RenderBackend::RenderSurfacePropertySnapshot AfterSecondCloseProperties;
        std::uint32_t AttemptsBeforeClose = 0;
        std::uint32_t AttemptsAfterFirstClose = 0;
        std::uint32_t AttemptsAfterSecondClose = 0;
        bool AcceptedFrameBlocked = false;
        bool ClosedAfterFirstClose = false;
        bool ClosedAfterSecondClose = false;
        bool TerminalFailure = false;
        bool CaptureFailed = false;
        bool SurfacePropertiesCoherent = true;
    };

    class ShutdownDeviceLossLayer final : public Keire::Layer
    {
      public:
        explicit ShutdownDeviceLossLayer(ShutdownDeviceLossResults& results)
            : Layer("Shutdown device loss"), m_Results(results)
        {
        }

      protected:
        void OnAttach() override
        {
            m_Surface =
                Owner().Renderer()->CreateSurface({.Name = "Shutdown recovery surface", .Width = 72U, .Height = 40U});
        }

        void OnUpdate(const Keire::Time&) override
        {
            constexpr std::uint32_t maximumUpdateCount = 120U;
            if (++m_UpdateCount > maximumUpdateCount)
                throw std::runtime_error("Renderer did not recover before the shutdown device-loss test deadline.");

            const auto renderer = Owner().Renderer();
            const auto properties = CurrentSurfaceProperties(*m_Surface);
            m_Results.SurfacePropertiesCoherent =
                m_Results.SurfacePropertiesCoherent && ((properties.Width == 0U && properties.Height == 0U) ||
                                                        (properties.Width == 72U && properties.Height == 40U));
            if (!m_RecoveryInjected)
            {
                Keire::RenderSystemInternalAccess::InjectDeviceLoss(*renderer);
                m_RecoveryInjected = true;
                return;
            }
            if (m_ShutdownLossArmed)
                return;
            const auto diagnostic = renderer->LastDeviceLoss();
            if (!diagnostic || !diagnostic->RecoverySucceeded ||
                renderer->DeviceState() != Keire::RenderDeviceState::Running)
            {
                return;
            }
            if (!m_Surface->Available() || properties.Width != 72U || properties.Height != 40U)
                return;

            renderer->Flush();
            m_Results.RecoveredDiagnostic = diagnostic;
            m_Results.RecoveredProperties = properties;
            m_Results.AttemptsBeforeClose = Keire::RenderSystemInternalAccess::RecoveryAttemptCountForTest(*renderer);
            Keire::RenderSystemInternalAccess::BlockNextAcceptedFrame(*renderer);
            Keire::RenderSystemInternalAccess::InjectDeviceLoss(*renderer);
            m_ShutdownLossArmed = true;
            Owner().RequestExit();
        }

        void OnDetach() noexcept override
        {
            try
            {
                const auto renderer = Owner().Renderer();
                m_Results.AcceptedFrameBlocked =
                    Keire::RenderSystemInternalAccess::WaitForAcceptedFrameBlock(*renderer);
                m_Results.AttemptsBeforeClose =
                    Keire::RenderSystemInternalAccess::RecoveryAttemptCountForTest(*renderer);
                renderer->Close();
                m_Results.ClosedAfterFirstClose = renderer->DeviceState() == Keire::RenderDeviceState::Closed;
                m_Results.AfterFirstCloseProperties = {m_Surface->Width(), m_Surface->Height(),
                                                       m_Surface->SampleCount()};
                m_Results.AttemptsAfterFirstClose =
                    Keire::RenderSystemInternalAccess::RecoveryAttemptCountForTest(*renderer);
                renderer->Close();
                m_Results.ClosedAfterSecondClose = renderer->DeviceState() == Keire::RenderDeviceState::Closed;
                m_Results.AfterSecondCloseProperties = {m_Surface->Width(), m_Surface->Height(),
                                                        m_Surface->SampleCount()};
                m_Results.AttemptsAfterSecondClose =
                    Keire::RenderSystemInternalAccess::RecoveryAttemptCountForTest(*renderer);
                m_Results.Diagnostic = renderer->LastDeviceLoss();
                m_Results.Statistics = renderer->Statistics();
                m_Results.TerminalFailure =
                    static_cast<bool>(Keire::RenderSystemInternalAccess::TerminalFailure(*renderer));
            }
            catch (...)
            {
                m_Results.CaptureFailed = true;
            }
        }

      private:
        ShutdownDeviceLossResults& m_Results;
        std::uint32_t m_UpdateCount = 0;
        bool m_RecoveryInjected = false;
        bool m_ShutdownLossArmed = false;
        Keire::Ref<Keire::RenderSurface> m_Surface;
    };

    struct PostSubmitFailureResults final
    {
        Keire::RenderStatistics Statistics;
        std::vector<Keire::RenderFrameTimeline> Timelines;
        std::size_t AvailableSlots = 0;
        std::uint64_t AbandonedHandles = 0;
        std::uint64_t LostGenerationGpuCleanupCalls = 0;
        bool Closed = false;
    };

    struct ActiveResourceDeviceLossResults final
    {
        Keire::RenderStatistics Statistics;
        std::vector<Keire::RenderFrameTimeline> Timelines;
        std::uint64_t AbandonedHandles = 0;
        std::uint64_t LostGenerationGpuCleanupCalls = 0;
    };

    class ActiveResourceDeviceLossLayer final : public Keire::Layer
    {
      public:
        explicit ActiveResourceDeviceLossLayer(ActiveResourceDeviceLossResults& results)
            : Layer("Active-resource device loss"), m_Results(results)
        {
        }

      protected:
        void OnAttach() override
        {
            m_Scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(),
                                                     Keire::SceneAsset::EmptyDefinition("Active-resource loss"));
            m_View = Owner().Renderer()->CreateView({.Name = "Active-resource loss", .Width = 32, .Height = 32});
        }

        void OnUpdate(const Keire::Time&) override
        {
            Keire::RenderSystemInternalAccess::InjectDeviceLossWithActiveResources(*Owner().Renderer());
            Owner().Renderer()->Submit({m_Scene, m_View});
        }

        void OnDetach() noexcept override
        {
            try
            {
                const auto renderer = Owner().Renderer();
                renderer->Close();
                m_Results.Statistics = renderer->Statistics();
                m_Results.Timelines = renderer->RecentFrameTimelines();
                m_Results.AbandonedHandles =
                    Keire::RenderSystemInternalAccess::LostGenerationAbandonedHandleCount(*renderer);
                m_Results.LostGenerationGpuCleanupCalls =
                    Keire::RenderSystemInternalAccess::LostGenerationGpuCleanupCallCount(*renderer);
            }
            catch (...)
            {
            }
            if (m_Scene)
                m_Scene->Close();
        }

      private:
        ActiveResourceDeviceLossResults& m_Results;
        Keire::Ref<Keire::Scene> m_Scene;
        Keire::Ref<Keire::RenderView> m_View;
    };

    class PostSubmitFailureLayer final : public Keire::Layer
    {
      public:
        explicit PostSubmitFailureLayer(PostSubmitFailureResults& results)
            : Layer("Post-submit failure"), m_Results(results)
        {
        }

      protected:
        void OnAttach() override
        {
            m_Scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(),
                                                     Keire::SceneAsset::EmptyDefinition("Post-submit failure"));
            m_View = Owner().Renderer()->CreateView({.Name = "Post-submit failure", .Width = 32, .Height = 32});
        }

        void OnUpdate(const Keire::Time&) override
        {
            Keire::RenderSystemInternalAccess::InjectPostSubmitFailure(*Owner().Renderer());
            Owner().Renderer()->Submit({m_Scene, m_View});
        }

        void OnDetach() noexcept override
        {
            try
            {
                const auto renderer = Owner().Renderer();
                renderer->Close();
                m_Results.Statistics = renderer->Statistics();
                m_Results.Timelines = renderer->RecentFrameTimelines();
                m_Results.AvailableSlots = Keire::RenderSystemInternalAccess::AvailableFrameSlotCount(*renderer);
                m_Results.AbandonedHandles =
                    Keire::RenderSystemInternalAccess::LostGenerationAbandonedHandleCount(*renderer);
                m_Results.LostGenerationGpuCleanupCalls =
                    Keire::RenderSystemInternalAccess::LostGenerationGpuCleanupCallCount(*renderer);
                m_Results.Closed = renderer->DeviceState() == Keire::RenderDeviceState::Closed;
            }
            catch (...)
            {
            }
            if (m_Scene)
                m_Scene->Close();
        }

      private:
        PostSubmitFailureResults& m_Results;
        Keire::Ref<Keire::Scene> m_Scene;
        Keire::Ref<Keire::RenderView> m_View;
    };

    struct UiSurfaceAdmissionRaceResults final
    {
        Keire::RenderStatistics Statistics;
        std::vector<Keire::RenderFrameTimeline> Timelines;
        std::uintptr_t TextureAtUiRecord = 0U;
        std::uintptr_t TextureBeforeRelease = 0U;
        std::uintptr_t TextureAfterRotation = 0U;
        bool EarlierFrameBlocked = false;
        bool AdmissionWaiterObserved = false;
        bool TargetFrameBlocked = false;
        bool EpochAliveWhileTargetBlocked = false;
        bool EpochRetiredAfterCompletion = false;
    };

    class UiSurfaceAdmissionRaceLayer final : public Keire::Layer
    {
      public:
        explicit UiSurfaceAdmissionRaceLayer(UiSurfaceAdmissionRaceResults& results)
            : Layer("UI surface admission race"), m_Results(results)
        {
        }

      protected:
        void OnAttach() override
        {
            m_Scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(),
                                                     Keire::SceneAsset::EmptyDefinition("UI surface admission race"));
            m_View = Owner().Renderer()->CreateView({.Name = "UI surface admission race", .Width = 32, .Height = 32});
        }

        void OnUpdate(const Keire::Time&) override
        {
            const auto renderer = Owner().Renderer();
            const auto surface = m_View->Surface();
            REQUIRE(renderer);
            REQUIRE(surface);

            switch (m_Phase++)
            {
            case 0U:
                renderer->Submit({m_Scene, m_View});
                break;
            case 1U:
            {
                renderer->Flush();
                const auto state = CurrentSurfaceState(*surface);
                REQUIRE(state->PublishedTexture.load(std::memory_order_acquire) != nullptr);
                Keire::RenderSystemInternalAccess::BlockNextAcceptedFrame(*renderer);
                renderer->Submit({m_Scene, m_View});
                break;
            }
            case 2U:
            {
                m_Results.EarlierFrameBlocked = Keire::RenderSystemInternalAccess::WaitForAcceptedFrameBlock(*renderer);
                REQUIRE(m_Results.EarlierFrameBlocked);
                const auto state = CurrentSurfaceState(*surface);
                auto* const textureAtRecord = state->PublishedTexture.load(std::memory_order_acquire);
                REQUIRE(textureAtRecord);
                m_CapturedEpochLifetime = state->Lifetime;
                Keire::RenderSystemInternalAccess::BlockNextAcceptedFrame(*renderer);
                m_DrawSurface = true;
                m_Observer = std::jthread(
                    [renderer, state = std::weak_ptr<Keire::RenderBackend::RenderSurfaceState>(state), textureAtRecord,
                     &results = m_Results]
                    {
                        results.AdmissionWaiterObserved =
                            Keire::RenderSystemInternalAccess::WaitForFrameAdmissionWaiter(*renderer);
                        if (const auto locked = state.lock())
                        {
                            results.TextureBeforeRelease = reinterpret_cast<std::uintptr_t>(
                                locked->PublishedTexture.load(std::memory_order_acquire));
                        }
                        Keire::RenderSystemInternalAccess::ReleaseAcceptedFrameBlock(*renderer);

                        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
                        while (std::chrono::steady_clock::now() < deadline)
                        {
                            if (const auto locked = state.lock())
                            {
                                auto* const published = locked->PublishedTexture.load(std::memory_order_acquire);
                                if (published && published != textureAtRecord)
                                {
                                    results.TextureAfterRotation = reinterpret_cast<std::uintptr_t>(published);
                                    break;
                                }
                            }
                            std::this_thread::sleep_for(std::chrono::milliseconds(1));
                        }
                    });
                break;
            }
            case 3U:
                m_Results.TargetFrameBlocked = Keire::RenderSystemInternalAccess::WaitForAcceptedFrameBlock(*renderer);
                REQUIRE(m_Results.TargetFrameBlocked);
                if (m_Observer.joinable())
                    m_Observer.join();
                REQUIRE(m_Results.AdmissionWaiterObserved);
                REQUIRE(m_Results.TextureAfterRotation != 0U);
                surface->RequestSize(48U, 40U);
                m_Results.EpochAliveWhileTargetBlocked = !m_CapturedEpochLifetime.expired();
                Keire::RenderSystemInternalAccess::ReleaseAcceptedFrameBlock(*renderer);
                break;
            default:
                renderer->Flush();
                m_Results.EpochRetiredAfterCompletion = m_CapturedEpochLifetime.expired();
                m_Results.Statistics = renderer->Statistics();
                m_Results.Timelines = renderer->RecentFrameTimelines();
                Owner().RequestExit();
                break;
            }
        }

        void OnUi(Keire::UiFrame& ui) override
        {
            if (!m_DrawSurface)
                return;
            m_DrawSurface = false;
            const auto surface = m_View->Surface();
            const auto state = CurrentSurfaceState(*surface);
            m_Results.TextureAtUiRecord =
                reinterpret_cast<std::uintptr_t>(state->PublishedTexture.load(std::memory_order_acquire));
            auto window = ui.BeginWindow("UI surface admission race");
            REQUIRE(window);
            ui.Image(surface, {32.0F, 32.0F});
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
        [[nodiscard]] static std::shared_ptr<Keire::RenderBackend::RenderSurfaceState>
        CurrentSurfaceState(const Keire::RenderSurface& surface)
        {
            auto state = std::static_pointer_cast<Keire::RenderBackend::RenderSurfaceState>(
                Keire::RenderSystemInternalAccess::SurfaceLease(surface));
            REQUIRE(state);
            REQUIRE(state->Lifetime);
            return state;
        }

        UiSurfaceAdmissionRaceResults& m_Results;
        Keire::Ref<Keire::Scene> m_Scene;
        Keire::Ref<Keire::RenderView> m_View;
        std::weak_ptr<const Keire::RenderBackend::RenderSurfaceEpochLease> m_CapturedEpochLifetime;
        std::jthread m_Observer;
        std::uint32_t m_Phase = 0U;
        bool m_DrawSurface = false;
    };

    template <std::size_t Size>
    [[nodiscard]] std::vector<std::byte> CopyShaderBytes(const unsigned char (&source)[Size])
    {
        const auto bytes = std::as_bytes(std::span(source));
        return {bytes.begin(), bytes.end()};
    }

    [[nodiscard]] Keire::AssetId PublishTransparentTestMaterial(Keire::Application& application)
    {
        const auto assets = application.Assets();
        REQUIRE(assets);
        const auto shaderId = Keire::AssetId::Parse("711ace00-0000-4000-8000-000000000020");
        const auto materialId = Keire::AssetId::Parse("711ace00-0000-4000-8000-000000000021");
        Keire::ShaderPropertyDefinition baseColor;
        baseColor.Name = "BaseColorTexture";
        baseColor.Type = Keire::ShaderPropertyType::Texture2D;
        baseColor.TextureSemantic = Keire::ShaderTextureSemantic::BaseColor;
        Keire::ShaderPropertyDefinition normal;
        normal.Name = "NormalTexture";
        normal.Type = Keire::ShaderPropertyType::Texture2D;
        normal.TextureSemantic = Keire::ShaderTextureSemantic::Normal;
        Keire::ShaderAssetDefinition shader;
        shader.Source = "Test/AdditiveCaptureUnlit.hlsl";
        shader.Properties = {std::move(baseColor), std::move(normal)};
        shader.Variants = {{Keire::ShaderBinaryFormat::Dxil, CopyShaderBytes(Keire::Detail::BuiltinUnlitVertexDxil),
                            CopyShaderBytes(Keire::Detail::BuiltinUnlitFragmentDxil)},
                           {Keire::ShaderBinaryFormat::SpirV, CopyShaderBytes(Keire::Detail::BuiltinUnlitVertexSpirV),
                            CopyShaderBytes(Keire::Detail::BuiltinUnlitFragmentSpirV)},
                           {Keire::ShaderBinaryFormat::Msl, CopyShaderBytes(Keire::Detail::BuiltinUnlitVertexMsl),
                            CopyShaderBytes(Keire::Detail::BuiltinUnlitFragmentMsl)}};
        REQUIRE(assets->PublishDevelopmentAsset(shaderId, Keire::CreateRef<Keire::ShaderAsset>(std::move(shader))));
        Keire::MaterialAssetDefinition material;
        material.Shader = shaderId;
        material.Surface.AlphaMode = Keire::MaterialAlphaMode::Blend;
        REQUIRE(
            assets->PublishDevelopmentAsset(materialId, Keire::CreateRef<Keire::MaterialAsset>(std::move(material))));
        return materialId;
    }

    struct AdditiveSessionFixture final
    {
        Keire::Ref<Keire::Scene> EditScene;
        Keire::Ref<Keire::SceneRuntimeSession> Session;
        Keire::EntityId Camera;
        std::vector<Keire::EntityId> OpaqueEntities;
        std::vector<Keire::EntityId> TransparentEntities;
        std::array<Keire::EntityId, 2> DirectionalLights;
    };

    [[nodiscard]] AdditiveSessionFixture CreateAdditiveSession(const Keire::Ref<Keire::AssetSystem>& assets,
                                                               const Keire::AssetId sceneId, const std::string& name,
                                                               const Keire::AssetId transparentMaterial,
                                                               const Keire::Vector3 cameraPosition,
                                                               const Keire::CameraClearMode clearMode,
                                                               const Keire::Color clearColor)
    {
        AdditiveSessionFixture result;
        result.EditScene = Keire::CreateRef<Keire::Scene>(sceneId, Keire::SceneAsset::EmptyDefinition(name),
                                                          Keire::ComponentRegistry::CreateDefault());
        const auto addRenderer = [&](const std::string& entityName, const Keire::AssetId material)
        {
            auto entity = result.EditScene->CreateEntity(entityName);
            const auto renderer = entity.AddComponent<Keire::MeshRendererComponent>();
            REQUIRE(renderer);
            renderer->SetMesh(Keire::MeshAsset::CubeId());
            renderer->SetMaterial(material);
            renderer->SetAlwaysVisible(true);
            return entity.Id();
        };
        result.OpaqueEntities = {addRenderer(name + " opaque A", {}), addRenderer(name + " opaque B", {})};
        result.TransparentEntities = {addRenderer(name + " transparent A", transparentMaterial),
                                      addRenderer(name + " transparent B", transparentMaterial)};
        auto cameraEntity = result.EditScene->CreateEntity(name + " camera");
        const auto camera = cameraEntity.AddComponent<Keire::CameraComponent>();
        REQUIRE(camera);
        camera->SetClearMode(clearMode);
        camera->SetClearColor(clearColor);
        camera->SetClipPlanes(0.25F, 250.0F);
        const auto cameraTransform = cameraEntity.GetComponent<Keire::TransformComponent>();
        REQUIRE(cameraTransform);
        cameraTransform->SetLocalPosition(cameraPosition);
        result.Camera = cameraEntity.Id();
        for (std::size_t index = 0; index < result.DirectionalLights.size(); ++index)
        {
            auto light = result.EditScene->CreateEntity(name + " directional " + std::to_string(index));
            REQUIRE(light.AddComponent<Keire::DirectionalLightComponent>());
            result.DirectionalLights[index] = light.Id();
        }
        result.Session = Keire::CreateRef<Keire::SceneRuntimeSession>(result.EditScene, assets);
        result.Session->Play();
        REQUIRE(result.Session->State() == Keire::ScenePlayState::Playing);
        return result;
    }

    struct AdditiveCaptureResults final
    {
        std::vector<Keire::AdditiveSceneCaptureSummary> Captures;
        std::vector<std::uint64_t> DirectionalLights;
        std::vector<Keire::RenderCamera> ExpectedCameras;
        std::vector<Keire::RenderEnvironmentSettings> ExpectedEnvironments;
        std::vector<Keire::EntityId> ExpectedOpaqueEntities;
        std::vector<Keire::EntityId> ExpectedTransparentEntities;
        Keire::AssetId ActiveScene;
        Keire::AssetId FallbackScene;
        std::uint64_t ActiveDirectionalLight = 0;
        std::uint64_t FallbackDirectionalLight = 0;
    };

    class AdditiveCaptureLayer final : public Keire::Layer
    {
      public:
        explicit AdditiveCaptureLayer(AdditiveCaptureResults& results) : Layer("Additive capture"), m_Results(results)
        {
        }

      protected:
        void OnAttach() override
        {
            const auto transparentMaterial = PublishTransparentTestMaterial(Owner());
            m_Results.FallbackScene = Keire::AssetId::Parse("711ace00-0000-4000-8000-000000000022");
            m_Results.ActiveScene = Keire::AssetId::Parse("711ace00-0000-4000-8000-000000000023");
            m_Fallback =
                CreateAdditiveSession(Owner().Assets(), m_Results.FallbackScene, "Fallback", transparentMaterial,
                                      {0.0F, 0.0F, 4.0F}, Keire::CameraClearMode::Skybox, {0.05F, 0.10F, 0.30F, 1.0F});
            m_Active = CreateAdditiveSession(Owner().Assets(), m_Results.ActiveScene, "Active", transparentMaterial,
                                             {1.0F, 0.0F, 3.0F}, Keire::CameraClearMode::SolidColor,
                                             {0.35F, 0.04F, 0.02F, 1.0F});
            m_World = Keire::CreateRef<Keire::SceneRuntimeWorld>(
                Keire::SceneRuntimeWorldSpecification{.Scenes = Owner().Scenes(), .Assets = Owner().Assets()});
            const auto fallback = m_World->Adopt(m_Fallback.Session);
            const auto active = m_World->Adopt(m_Active.Session);
            REQUIRE(fallback);
            REQUIRE(active);
            REQUIRE(m_World->SetActive(active));
            m_World->Process();
            REQUIRE(m_World->Active() == active);
            m_View = Owner().Renderer()->CreateView({.Name = "Additive capture", .Width = 64U, .Height = 64U});
            m_Environment.AmbientColor = {0.12F, 0.24F, 0.36F, 1.0F};
            m_Environment.AmbientIntensity = 1.25F;
            m_Environment.Exposure = 0.75F;
            m_Environment.EnvironmentRotationDegrees = 37.0F;
            m_Environment.SkyVisible = true;

            auto appendSorted = [](std::vector<Keire::EntityId> source, std::vector<Keire::EntityId>& destination)
            {
                std::ranges::sort(source);
                destination.insert(destination.end(), source.begin(), source.end());
            };
            appendSorted(m_Fallback.OpaqueEntities, m_Results.ExpectedOpaqueEntities);
            appendSorted(m_Active.OpaqueEntities, m_Results.ExpectedOpaqueEntities);
            appendSorted(m_Fallback.TransparentEntities, m_Results.ExpectedTransparentEntities);
            appendSorted(m_Active.TransparentEntities, m_Results.ExpectedTransparentEntities);
            m_Results.ActiveDirectionalLight = std::ranges::min(m_Active.DirectionalLights).Value().Low();
            m_Results.FallbackDirectionalLight = std::ranges::min(m_Fallback.DirectionalLights).Value().Low();
        }

        void OnUpdate(const Keire::Time&) override
        {
            switch (m_Phase++)
            {
            case 0U:
                SubmitSelectedFrame();
                break;
            case 1U:
                CaptureCompletedFrame();
                SetActiveCameraEnabled(false);
                SubmitSelectedFrame();
                break;
            case 2U:
                CaptureCompletedFrame();
                SetActiveDirectionalLightsEnabled(false);
                SubmitSelectedFrame();
                break;
            default:
                CaptureCompletedFrame();
                Owner().RequestExit();
                break;
            }
        }

        void OnDetach() noexcept override
        {
            if (m_World)
                m_World->Close();
            for (const auto& scene : {m_Active.EditScene, m_Fallback.EditScene})
                if (scene)
                    scene->Close();
            m_View.Reset();
            m_World.Reset();
            m_Active = {};
            m_Fallback = {};
        }

      private:
        void SubmitSelectedFrame()
        {
            const auto selected = Keire::Internal::SelectRuntimeRenderSession(m_World);
            REQUIRE(selected);
            REQUIRE(selected.Session == m_Active.Session);
            REQUIRE(selected.Camera);
            Keire::RenderCamera camera;
            camera.View = Keire::Math::Inverse(selected.Camera->Transform->WorldMatrix());
            camera.Projection = selected.Camera->Camera->ProjectionMatrix(1.0F);
            camera.ClearColor = selected.Camera->Camera->ClearColor();
            camera.NearPlane = selected.Camera->Camera->NearPlane();
            camera.FarPlane = selected.Camera->Camera->FarPlane();
            m_View->SetCamera(camera);
            auto environment = m_Environment;
            environment.SkyVisible =
                environment.SkyVisible && selected.Camera->Camera->ClearMode() == Keire::CameraClearMode::Skybox;
            auto request = Keire::Internal::CaptureRuntimeSceneRenderRequest(m_World, selected.Session, m_View,
                                                                             environment, {}, true);
            REQUIRE(request);
            REQUIRE(request->PrimaryContributionIndex == 1U);
            request->FrameIndex = m_Phase;
            m_Results.ExpectedCameras.push_back(camera);
            m_Results.ExpectedEnvironments.push_back(environment);
            Owner().Renderer()->Submit(std::move(*request));
        }

        void CaptureCompletedFrame()
        {
            const auto renderer = Owner().Renderer();
            renderer->Flush();
            m_Results.Captures.push_back(Keire::RenderSystemInternalAccess::LastCapturedAdditiveScene(*renderer));
            m_Results.DirectionalLights.push_back(
                Keire::RenderSystemInternalAccess::LastCapturedDirectionalLightEntity(*renderer));
        }

        void SetActiveCameraEnabled(const bool enabled)
        {
            const auto camera =
                m_Active.Session->RuntimeScene()->FindEntity(m_Active.Camera).GetComponent<Keire::CameraComponent>();
            REQUIRE(camera);
            camera->SetEnabled(enabled);
        }

        void SetActiveDirectionalLightsEnabled(const bool enabled)
        {
            for (const auto entity : m_Active.DirectionalLights)
            {
                const auto light = m_Active.Session->RuntimeScene()
                                       ->FindEntity(entity)
                                       .GetComponent<Keire::DirectionalLightComponent>();
                REQUIRE(light);
                light->SetEnabled(enabled);
            }
        }

        AdditiveCaptureResults& m_Results;
        AdditiveSessionFixture m_Fallback;
        AdditiveSessionFixture m_Active;
        Keire::Ref<Keire::SceneRuntimeWorld> m_World;
        Keire::Ref<Keire::RenderView> m_View;
        Keire::RenderEnvironmentSettings m_Environment;
        std::uint32_t m_Phase = 0;
    };
#endif
} // namespace

TEST_CASE("surface properties publish only complete extent and sample snapshots")
{
    Keire::RenderBackend::RenderSurfaceState state;
    const Keire::RenderBackend::RenderSurfacePropertySnapshot first{64U, 36U, Keire::RenderSampleCount::Two};
    const Keire::RenderBackend::RenderSurfacePropertySnapshot second{192U, 108U, Keire::RenderSampleCount::Four};
    state.Width = first.Width;
    state.Height = first.Height;
    state.ActualSamples = first.SampleCount;
    state.PublishSurfacePropertiesSnapshot();

    std::atomic<std::uint32_t> phase{0U};
    std::jthread writer(
        [&]
        {
            state.Width = second.Width;
            phase.store(1U, std::memory_order_release);
            phase.notify_all();
            phase.wait(1U, std::memory_order_acquire);
            state.Height = second.Height;
            state.ActualSamples = second.SampleCount;
            state.PublishSurfacePropertiesSnapshot();
            phase.store(3U, std::memory_order_release);
            phase.notify_all();
        });

    auto observedPhase = phase.load(std::memory_order_acquire);
    while (observedPhase != 1U)
    {
        phase.wait(observedPhase, std::memory_order_acquire);
        observedPhase = phase.load(std::memory_order_acquire);
    }
    CHECK(state.SurfacePropertiesSnapshot() == first);
    phase.store(2U, std::memory_order_release);
    phase.notify_all();

    observedPhase = phase.load(std::memory_order_acquire);
    while (observedPhase != 3U)
    {
        phase.wait(observedPhase, std::memory_order_acquire);
        observedPhase = phase.load(std::memory_order_acquire);
    }
    CHECK(state.SurfacePropertiesSnapshot() == second);
}

TEST_CASE("submitted scene data remains valid when the scene closes before end frame")
{
    Keire::Application application(RenderTestSpecification());
    (void)application.PushLayer(std::make_unique<CloseAfterSubmitLayer>());
    CHECK(application.Run() == 0);
}

TEST_CASE("renderer thread handles resize minimize restore and bounded queue saturation")
{
    const auto results = std::make_shared<RendererLifecycleResults>();
    {
        Keire::Application application(RenderTestSpecification());
        (void)application.PushLayer(std::make_unique<RendererLifecycleLayer>(results));
        REQUIRE(application.Run() == 0);
    }

#if defined(KEIRE_ENABLE_TEST_HOOKS)
    CHECK(results->QueueHighWaterMark == 1U);
#endif
    CHECK(results->Resized);
    CHECK(results->Minimized);
    CHECK(results->Restored);
    CHECK(results->ResizedGeneration > results->InitialGeneration);
    CHECK(results->MinimizedGeneration > results->ResizedGeneration);
    CHECK(results->RestoredGeneration > results->MinimizedGeneration);
    CHECK(results->ResizedProperties.Width == 128U);
    CHECK(results->ResizedProperties.Height == 80U);
    CHECK(results->MinimizedProperties.Width == 0U);
    CHECK(results->MinimizedProperties.Height == 0U);
    CHECK(results->RestoredProperties.Width == 96U);
    CHECK(results->RestoredProperties.Height == 48U);
    CHECK(results->MinimizedProperties.SampleCount == results->ResizedProperties.SampleCount);
    CHECK(results->RestoredProperties.SampleCount == results->ResizedProperties.SampleCount);
    for (const auto& properties : results->ObservedProperties)
    {
        const bool knownExtent = (properties.Width == 0U && properties.Height == 0U) ||
                                 (properties.Width == 64U && properties.Height == 64U) ||
                                 (properties.Width == 128U && properties.Height == 80U) ||
                                 (properties.Width == 96U && properties.Height == 48U);
        CHECK(knownExtent);
        CHECK((properties.SampleCount == Keire::RenderSampleCount::One ||
               properties.SampleCount == Keire::RenderSampleCount::Two ||
               properties.SampleCount == Keire::RenderSampleCount::Four ||
               properties.SampleCount == Keire::RenderSampleCount::Eight));
    }
    CHECK_FALSE(results->MinimizedOcclusion.PyramidValid);
    CHECK_FALSE(results->MinimizedOcclusion.ReadbackValid);
    CHECK(results->MinimizedOcclusion.Candidates == 0);
    CHECK_FALSE(results->RestoredOcclusion.PyramidValid);
    CHECK_FALSE(results->RestoredOcclusion.ReadbackValid);
}

#if defined(KEIRE_ENABLE_TEST_HOOKS)
TEST_CASE("renderer dispatch queue honors configured capacities and blocks overflow admission")
{
    for (const auto depth : {1U, 2U, 3U})
    {
        CAPTURE(depth);
        std::uint32_t highWaterMark = 0U;
        auto specification = RenderTestSpecification();
        specification.Render.MaximumFramesInFlight = depth;
        Keire::Application application(specification);
        (void)application.PushLayer(std::make_unique<QueueSaturationLayer>(highWaterMark));
        REQUIRE(application.Run() == 0);
        CHECK(highWaterMark == depth);
    }
}

TEST_CASE("threaded headless Flush publishes the last retired frame before returning")
{
    FlushPublicationBarrierResults results;
    auto specification = RenderTestSpecification();
    specification.Render.Mode = Keire::RenderMode::Headless;
    specification.Render.MaximumFramesInFlight = 3U;
    Keire::Application application(specification);
    (void)application.PushLayer(std::make_unique<FlushPublicationBarrierLayer>(results));
    CHECK(application.Run() == 0);
    CHECK(results.DistinctRenderThread);
    CHECK(results.FlushCompleted);
    CHECK(results.Statistics.OutstandingFrames == 0U);
    CHECK(results.Statistics.AcceptedFrames == 16U);
    CHECK(results.Statistics.RetiredFrames == 16U);
    REQUIRE(results.Statistics.LastRetiredFrame == 16U);
    REQUIRE(results.Timelines.size() == 16U);
    CHECK(std::ranges::any_of(results.Timelines, [&results](const Keire::RenderFrameTimeline& timeline)
                              { return timeline.Frame == results.Statistics.LastRetiredFrame; }));
}
#endif

TEST_CASE("published surface epochs retain N worksets and N plus one outputs until logical leases retire")
{
    for (const auto depth : {1U, 2U, 3U})
    {
        CAPTURE(depth);
        SurfaceEpochLeaseResults results;
        auto specification = RenderTestSpecification();
        specification.Render.MaximumFramesInFlight = depth;
        Keire::Application application(specification);
        (void)application.PushLayer(std::make_unique<SurfaceEpochLeaseLayer>(results));
        CHECK(application.Run() == 0);
        CHECK(results.Worksets == depth);
        CHECK(results.FinalOutputs == depth + 1U);
        CHECK(results.ResizeEpochAliveWhileLeased);
        CHECK(results.ResizeEpochRetiredAfterRelease);
        CHECK(results.MinimizedEpochAliveWhileLeased);
        CHECK(results.MinimizedEpochRetiredAfterRelease);
        CHECK(results.Restored);
        REQUIRE(results.Timelines.size() >= 5U);
        for (std::size_t index = 1; index < results.Timelines.size(); ++index)
            CHECK(results.Timelines[index - 1U].Frame < results.Timelines[index].Frame);
    }
}

TEST_CASE("rendered depths one through three publish every accepted frame in monotonic presentation order")
{
    constexpr std::uint64_t frameCount = 12U;
    for (const auto depth : {1U, 2U, 3U})
    {
        CAPTURE(depth);
        PresentationOrderResults results;
        auto specification = RenderTestSpecification();
        specification.Render.MaximumFramesInFlight = depth;
        Keire::Application application(specification);
        (void)application.PushLayer(std::make_unique<PresentationOrderLayer>(results));
        CHECK(application.Run() == 0);

        CHECK(results.Statistics.AllowedFramesInFlight == depth);
        CHECK(results.Statistics.AcceptedFrames == frameCount);
        CHECK(results.Statistics.PresentedFrames == frameCount);
        CHECK(results.Statistics.RetiredFrames == frameCount);
        CHECK(results.Statistics.CancelledFrames == 0U);
        CHECK(results.Statistics.LastAcceptedFrame == frameCount);
        CHECK(results.Statistics.LastPresentedFrame == frameCount);
        CHECK(results.Statistics.LastRetiredFrame == frameCount);
        CHECK(results.Statistics.OutstandingFrames == 0U);
        CHECK(results.Statistics.FramesInFlightHighWaterMark <= depth);
        REQUIRE(results.Timelines.size() == frameCount);
        for (std::size_t index = 0; index < results.Timelines.size(); ++index)
        {
            CHECK(results.Timelines[index].Frame == index + 1U);
            CHECK(results.Timelines[index].Presented);
            CHECK_FALSE(results.Timelines[index].Cancelled);
            CHECK(results.Timelines[index].SubmitToPresentMilliseconds >= 0.0F);
        }
    }
}

#if defined(KEIRE_ENABLE_TEST_HOOKS)
TEST_CASE("rendered UI captures a logical surface lease before admission rotates the published output")
{
    UiSurfaceAdmissionRaceResults results;
    auto specification = RenderTestSpecification();
    specification.Render.MaximumFramesInFlight = 1U;
    specification.Ui.Mode = Keire::UiMode::Rendered;
    {
        Keire::Application application(specification);
        (void)application.PushLayer(std::make_unique<UiSurfaceAdmissionRaceLayer>(results));
        CHECK(application.Run() == 0);
    }

    CHECK(results.EarlierFrameBlocked);
    CHECK(results.AdmissionWaiterObserved);
    CHECK(results.TargetFrameBlocked);
    CHECK(results.TextureAtUiRecord != 0U);
    CHECK(results.TextureBeforeRelease == results.TextureAtUiRecord);
    CHECK(results.TextureAfterRotation != 0U);
    CHECK(results.TextureAfterRotation != results.TextureAtUiRecord);
    CHECK(results.EpochAliveWhileTargetBlocked);
    CHECK(results.EpochRetiredAfterCompletion);
    CHECK(results.Statistics.AllowedFramesInFlight == 1U);
    CHECK(results.Statistics.AcceptedFrames == results.Statistics.RetiredFrames);
    CHECK(results.Statistics.OutstandingFrames == 0U);
    REQUIRE(results.Timelines.size() >= 4U);
    CHECK(results.Timelines[2U].AdmissionWaitMilliseconds > 0.0F);
}

TEST_CASE("additive capture preserves active ownership fallback camera and global deterministic draw order")
{
    AdditiveCaptureResults results;
    auto specification = RenderTestSpecification();
    specification.Assets.Mode = Keire::AssetMode::Development;
    specification.Assets.DevelopmentCatalog.clear();
    specification.Scenes.Mode = Keire::SceneMode::Enabled;
    Keire::Application application(specification);
    (void)application.PushLayer(std::make_unique<AdditiveCaptureLayer>(results));
    CHECK(application.Run() == 0);

    REQUIRE(results.Captures.size() == 3U);
    REQUIRE(results.DirectionalLights.size() == results.Captures.size());
    REQUIRE(results.ExpectedCameras.size() == results.Captures.size());
    REQUIRE(results.ExpectedEnvironments.size() == results.Captures.size());
    const std::vector<std::uint32_t> expectedContributionOrder{0U, 0U, 1U, 1U};
    const std::vector<Keire::AssetId> expectedSpatialScenes{results.FallbackScene, results.ActiveScene};
    for (std::size_t index = 0; index < results.Captures.size(); ++index)
    {
        CAPTURE(index);
        const auto& capture = results.Captures[index];
        const auto& camera = results.ExpectedCameras[index];
        CHECK(capture.PrimaryScene == results.ActiveScene);
        CHECK(capture.Camera.View == camera.View);
        CHECK(capture.Camera.Projection == camera.Projection);
        CHECK(capture.Camera.ClearColor == camera.ClearColor);
        CHECK(capture.Camera.NearPlane == camera.NearPlane);
        CHECK(capture.Camera.FarPlane == camera.FarPlane);
        CHECK(capture.ClearColor == camera.ClearColor);
        CHECK(capture.Environment == results.ExpectedEnvironments[index]);
        CHECK(capture.SpatialScenes == expectedSpatialScenes);
        CHECK(capture.PreparedOpaqueContributionOrder == expectedContributionOrder);
        CHECK(capture.PreparedOpaqueEntities == results.ExpectedOpaqueEntities);
        CHECK(capture.PreparedTransparentContributionOrder == expectedContributionOrder);
        CHECK(capture.PreparedTransparentEntities == results.ExpectedTransparentEntities);
    }
    CHECK_FALSE(results.Captures[0].Environment.SkyVisible);
    CHECK(results.Captures[1].Environment.SkyVisible);
    CHECK(results.Captures[2].Environment.SkyVisible);
    CHECK(results.Captures[0].Camera.View != results.Captures[1].Camera.View);
    CHECK(results.Captures[1].Camera.View == results.Captures[2].Camera.View);
    CHECK(results.Captures[1].Camera.Projection == results.Captures[2].Camera.Projection);
    CHECK(results.Captures[1].Camera.ClearColor == results.Captures[2].Camera.ClearColor);
    CHECK(results.Captures[1].Camera.NearPlane == results.Captures[2].Camera.NearPlane);
    CHECK(results.Captures[1].Camera.FarPlane == results.Captures[2].Camera.FarPlane);
    CHECK(results.DirectionalLights[0] == results.ActiveDirectionalLight);
    CHECK(results.DirectionalLights[1] == results.ActiveDirectionalLight);
    CHECK(results.DirectionalLights[2] == results.FallbackDirectionalLight);
}

TEST_CASE("injected GPU device loss propagates and renderer shutdown remains safe")
{
    Keire::Application application(RenderTestSpecification());
    (void)application.PushLayer(std::make_unique<DeviceLossLayer>());
    CHECK_THROWS_AS((void)application.Run(), Keire::GpuDeviceLostError);
}

TEST_CASE("shutdown device loss preserves recovery-attempt publication across idempotent Close")
{
    ShutdownDeviceLossResults results;
    Keire::Application application(RenderTestSpecification());
    (void)application.PushLayer(std::make_unique<ShutdownDeviceLossLayer>(results));
    REQUIRE(application.Run() == 0);

    REQUIRE_FALSE(results.CaptureFailed);
    CHECK(results.AcceptedFrameBlocked);
    CHECK(results.ClosedAfterFirstClose);
    CHECK(results.ClosedAfterSecondClose);
    REQUIRE(results.RecoveredDiagnostic);
    CHECK(results.RecoveredDiagnostic->RecoverySucceeded);
    CHECK(results.RecoveredDiagnostic->RecoveredDeviceGeneration > results.RecoveredDiagnostic->DeviceGeneration);
    CHECK(results.SurfacePropertiesCoherent);
    CHECK(results.RecoveredProperties.Width == 72U);
    CHECK(results.RecoveredProperties.Height == 40U);
    CHECK(results.AfterFirstCloseProperties.Width == 0U);
    CHECK(results.AfterFirstCloseProperties.Height == 0U);
    CHECK(results.AfterFirstCloseProperties.SampleCount == results.RecoveredProperties.SampleCount);
    CHECK(results.AfterSecondCloseProperties == results.AfterFirstCloseProperties);
    CHECK(results.AttemptsBeforeClose == 1U);
    CHECK(results.AttemptsAfterFirstClose == 1U);
    CHECK(results.AttemptsAfterSecondClose == 1U);
    REQUIRE(results.Diagnostic);
    CHECK(results.Diagnostic->Operation == "test frame injection");
    CHECK_FALSE(results.Diagnostic->RecoverySucceeded);
    CHECK(results.Diagnostic->RecoveryAttempt == 0U);
    CHECK(results.Diagnostic->RecoveredDeviceGeneration == 0U);
    CHECK(results.Diagnostic->DeviceGeneration == results.RecoveredDiagnostic->RecoveredDeviceGeneration);
    CHECK_FALSE(results.TerminalFailure);
    CHECK(results.Statistics.AcceptedFrames == 2U);
    CHECK(results.Statistics.CancelledFrames == 1U);
    CHECK(results.Statistics.OutstandingFrames == 0U);
}

TEST_CASE("device loss with active upload resources abandons handles without lost-generation GPU calls")
{
    ActiveResourceDeviceLossResults results;
    auto specification = RenderTestSpecification();
    specification.Render.DeviceLossRecoveryAttempts = 0;
    Keire::Application application(specification);
    (void)application.PushLayer(std::make_unique<ActiveResourceDeviceLossLayer>(results));
    CHECK_THROWS_AS((void)application.Run(), Keire::GpuDeviceLostError);
    CHECK(results.AbandonedHandles >= 4U);
    CHECK(results.LostGenerationGpuCleanupCalls == 0U);
    CHECK(results.Statistics.AcceptedFrames == 1U);
    CHECK(results.Statistics.CancelledFrames == 1U);
    CHECK(results.Statistics.OutstandingFrames == 0U);
    REQUIRE(results.Timelines.size() == 1U);
    CHECK(results.Timelines.front().Cancelled);
}

TEST_CASE("post-submit failure retains the accepted slot until exactly-once shutdown retirement")
{
    PostSubmitFailureResults results;
    auto specification = RenderTestSpecification();
    specification.Render.DeviceLossRecoveryAttempts = 0;
    Keire::Application application(specification);
    (void)application.PushLayer(std::make_unique<PostSubmitFailureLayer>(results));
    CHECK_THROWS_WITH((void)application.Run(), "Injected post-submit publication failure.");
    CHECK(results.Closed);
    CHECK(results.Statistics.AcceptedFrames == 1U);
    CHECK(results.Statistics.RetiredFrames == 0U);
    CHECK(results.Statistics.CancelledFrames == 1U);
    CHECK(results.Statistics.OutstandingFrames == 0U);
    CHECK(results.AvailableSlots == 1U);
    CHECK(results.AbandonedHandles >= 1U);
    CHECK(results.LostGenerationGpuCleanupCalls == 0U);
    REQUIRE(results.Timelines.size() == 1U);
    CHECK(results.Timelines.front().Cancelled);
}
#endif
