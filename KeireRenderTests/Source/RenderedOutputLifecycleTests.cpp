#include "KeireRenderTests/RenderedOutputTestSupport.h"

#include "Keire/ECS/Components/MeshRendererComponent.h"
#include "Keire/Scenes/Scene.h"
#include "KeireInternal/RenderInternal.h"

#include <doctest/doctest.h>

#include <cstdint>
#include <memory>
#include <utility>

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
        bool Resized = false;
        bool Minimized = false;
        bool Restored = false;
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
                m_Results->QueueHighWaterMark =
                    Keire::RenderSystemInternalAccess::SaturateRendererQueue(*Owner().Renderer());
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
                Keire::RenderSystemInternalAccess::RequestSurfaceSize(*m_Surface, 96, 48);
            }
            else
            {
                m_Results->RestoredGeneration = m_Surface->Generation();
                m_Results->Restored = m_Surface->Available() && m_Surface->Width() == 96 && m_Surface->Height() == 48;
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

    CHECK(results->QueueHighWaterMark == 2);
    CHECK(results->Resized);
    CHECK(results->Minimized);
    CHECK(results->Restored);
    CHECK(results->ResizedGeneration > results->InitialGeneration);
    CHECK(results->MinimizedGeneration > results->ResizedGeneration);
    CHECK(results->RestoredGeneration > results->MinimizedGeneration);
}

TEST_CASE("injected GPU device loss propagates and renderer shutdown remains safe")
{
    Keire::Application application(RenderTestSpecification());
    (void)application.PushLayer(std::make_unique<DeviceLossLayer>());
    CHECK_THROWS_WITH((void)application.Run(), "Injected GPU device loss.");
}
