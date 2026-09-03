#include "Keire/Application.h"
#include "Keire/Ui/RuntimeUi.h"

#include <doctest/doctest.h>

#include <cstdint>
#include <memory>
#include <utility>

namespace
{
    class NativeRuntimeUiLayer final : public Keire::Layer
    {
      public:
        NativeRuntimeUiLayer(bool& rendered, bool& uploadPoolReused)
            : Layer("Native Runtime UI"), m_Rendered(rendered), m_UploadPoolReused(uploadPoolReused)
        {
        }

      protected:
        void OnAttach() override
        {
            Owner().MainWindow()->SetPosition({-10'000, -10'000});
            Owner().MainWindow()->SetVisible(true);
        }

        void OnUpdate(const Keire::Time&) override
        {
            auto tree = Keire::CreateRef<Keire::RuntimeUiTree>();
            const auto panel = tree->Create(Keire::RuntimeUiElementType::Panel);
            Keire::RuntimeUiStyle style;
            style.Background = {0.1F, 0.6F, 0.9F, 0.8F};
            style.Border = {1.0F, 1.0F, 1.0F, 1.0F};
            style.BorderWidth = 2.0F;
            REQUIRE(tree->SetStyle(panel, style));
            Keire::RuntimeUiContent content;
            content.Text = "Native Game UI";
            REQUIRE(tree->SetContent(panel, std::move(content)));
            tree->Layout(128.0F, 72.0F);
            Owner().Renderer()->SubmitRuntimeUi(tree);
            if (++m_Frames == 4)
                Owner().RequestExit();
        }

        void OnDetach() noexcept override
        {
            const auto renderer = Owner().Renderer();
            m_Rendered = renderer && renderer->Statistics().DrawCalls > 0;
            m_UploadPoolReused = renderer && renderer->Statistics().RuntimeUiRenderer.UploadBufferPoolSize == 1U &&
                                 renderer->Statistics().RuntimeUiRenderer.UploadBufferReallocations == 0U;
            Owner().MainWindow()->SetVisible(false);
        }

      private:
        bool& m_Rendered;
        bool& m_UploadPoolReused;
        std::uint32_t m_Frames = 0;
    };

    [[nodiscard]] Keire::ApplicationSpecification NativeRuntimeUiSpecification()
    {
        Keire::ApplicationSpecification specification;
        specification.MainWindow.Title = "Native Runtime UI Test";
        specification.MainWindow.Width = 128;
        specification.MainWindow.Height = 72;
        specification.MainWindow.Visible = false;
        specification.SuspendWhenMainWindowMinimized = false;
        specification.ManageLogging = false;
        specification.Render.Mode = Keire::RenderMode::Rendered;
        specification.Render.PreferredSampleCount = Keire::RenderSampleCount::One;
        specification.Render.MaximumFramesInFlight = 1;
        specification.Ui.Mode = Keire::UiMode::Disabled;
        return specification;
    }
} // namespace

TEST_CASE("runtime Game UI renders through SDL GPU without editor UI")
{
    bool rendered = false;
    bool uploadPoolReused = false;
    Keire::Application application(NativeRuntimeUiSpecification());
    (void)application.PushLayer(std::make_unique<NativeRuntimeUiLayer>(rendered, uploadPoolReused));
    CHECK(application.Run() == 0);
    CHECK(rendered);
    CHECK(uploadPoolReused);
    CHECK_FALSE(application.UiEnabled());
}
