#include "KeireRenderTests/RenderedOutputTestSupport.h"

#include "Keire/ECS/Components/MeshRendererComponent.h"
#include "Keire/Scenes/Scene.h"
#include "KeireInternal/RenderInternal.h"
#include "KeireInternal/Rendering/RenderSurfaceStateInternal.h"

#include <doctest/doctest.h>

#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

namespace
{
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
        bool Resized = false;
        bool Minimized = false;
        bool Restored = false;
    };

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
            if (m_Frame == 0)
            {
                m_Results->InitialGeneration = m_Surface->Generation();
#if defined(KEIRE_ENABLE_TEST_HOOKS)
                m_Results->QueueHighWaterMark =
                    Keire::RenderSystemInternalAccess::SaturateRendererQueue(*Owner().Renderer());
#endif
                m_Surface->RequestSize(128, 80);
            }
            else if (m_Frame == 1)
            {
                m_Results->ResizedGeneration = m_Surface->Generation();
                m_Results->Resized = m_Surface->Available() && m_Surface->Width() == 128 && m_Surface->Height() == 80;
                Keire::RenderSystemInternalAccess::RequestSurfaceSize(*m_Surface, 0, 0);
            }
            else if (m_Frame == 2)
            {
                m_Results->MinimizedGeneration = m_Surface->Generation();
                m_Results->Minimized = !m_Surface->Available() && m_Surface->Width() == 0 && m_Surface->Height() == 0;
                m_Results->MinimizedOcclusion = m_Surface->OcclusionDiagnostics();
                Keire::RenderSystemInternalAccess::RequestSurfaceSize(*m_Surface, 96, 48);
            }
            else
            {
                m_Results->RestoredGeneration = m_Surface->Generation();
                m_Results->Restored = m_Surface->Available() && m_Surface->Width() == 96 && m_Surface->Height() == 48;
                m_Results->RestoredOcclusion = m_Surface->OcclusionDiagnostics();
                Owner().RequestExit();
                return;
            }
            ++m_Frame;
        }

      private:
        std::shared_ptr<RendererLifecycleResults> m_Results;
        Keire::Ref<Keire::RenderSurface> m_Surface;
        std::uint32_t m_Frame = 0;
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

    struct PostSubmitFailureResults final
    {
        Keire::RenderStatistics Statistics;
        std::vector<Keire::RenderFrameTimeline> Timelines;
        std::size_t AvailableSlots = 0;
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
#endif
} // namespace

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
    CHECK(results->QueueHighWaterMark == 2);
#endif
    CHECK(results->Resized);
    CHECK(results->Minimized);
    CHECK(results->Restored);
    CHECK(results->ResizedGeneration > results->InitialGeneration);
    CHECK(results->MinimizedGeneration > results->ResizedGeneration);
    CHECK(results->RestoredGeneration > results->MinimizedGeneration);
    CHECK_FALSE(results->MinimizedOcclusion.PyramidValid);
    CHECK_FALSE(results->MinimizedOcclusion.ReadbackValid);
    CHECK(results->MinimizedOcclusion.Candidates == 0);
    CHECK_FALSE(results->RestoredOcclusion.PyramidValid);
    CHECK_FALSE(results->RestoredOcclusion.ReadbackValid);
}

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

#if defined(KEIRE_ENABLE_TEST_HOOKS)
TEST_CASE("injected GPU device loss propagates and renderer shutdown remains safe")
{
    Keire::Application application(RenderTestSpecification());
    (void)application.PushLayer(std::make_unique<DeviceLossLayer>());
    CHECK_THROWS_AS((void)application.Run(), Keire::GpuDeviceLostError);
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
    REQUIRE(results.Timelines.size() == 1U);
    CHECK(results.Timelines.front().Cancelled);
}
#endif
