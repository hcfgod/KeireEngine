#include "Keire/Core.h"

#include <SDL3/SDL.h>
#include <doctest/doctest.h>

#include <memory>

namespace
{
    class TabSelectionLayer final : public Keire::Layer
    {
      public:
        explicit TabSelectionLayer(bool& checked) : Layer("Requested tab selection"), m_Checked(checked) {}

      protected:
        void OnUi(Keire::UiFrame& ui) override
        {
            ui.SetNextWindowSize({640, 480}, false);
            auto window = ui.BeginWindow("Tab selection fixture");
            bool graphVisible = false;
            bool modulesVisible = false;
            if (auto tabs = ui.BeginTabBar("AuthoringTabs"); tabs)
            {
                // The legacy overload and the explicit false overload must not steal selection.
                {
                    auto graph = m_Frame == 5 ? ui.BeginTabItem("Graph", nullptr, true) : ui.BeginTabItem("Graph");
                    graphVisible = static_cast<bool>(graph);
                    if (graph)
                    {
                        ui.Text("Graph controls");
                    }
                }
                {
                    auto modules = ui.BeginTabItem("Modules", nullptr, m_Frame == 2);
                    modulesVisible = static_cast<bool>(modules);
                    if (modules)
                    {
                        ui.Text("Module controls");
                    }
                }
            }
            if (m_Frame >= 3 && m_Frame <= 4)
            {
                CHECK_FALSE(graphVisible);
                CHECK(modulesVisible);
            }
            if (m_Frame >= 6)
            {
                CHECK(graphVisible);
                CHECK_FALSE(modulesVisible);
            }
            if (++m_Frame == 8)
            {
                m_Checked = true;
                Owner().RequestExit();
            }
        }

      private:
        bool& m_Checked;
        int m_Frame = 0;
    };
} // namespace

TEST_CASE("UI requested tab selection reveals the target once without locking user navigation")
{
    REQUIRE(SDL_SetHintWithPriority(SDL_HINT_VIDEO_DRIVER, "dummy", SDL_HINT_OVERRIDE));
    bool checked = false;
    Keire::ApplicationSpecification specification;
    specification.MainWindow.Visible = false;
    specification.TargetFrameRate = 0;
    specification.Ui.Mode = Keire::UiMode::Headless;
    specification.Ui.LayoutPath.clear();
    Keire::Application application(specification);
    (void)application.PushLayer(std::make_unique<TabSelectionLayer>(checked));
    CHECK(application.Run() == 0);
    CHECK(checked);
}
