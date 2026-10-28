#include "Keire/Core.h"

#include <SDL3/SDL.h>
#include <doctest/doctest.h>
#include <imgui.h>

#include <memory>

TEST_CASE("Environment-style tabs switch child content after one click")
{
    REQUIRE(SDL_SetHintWithPriority(SDL_HINT_VIDEO_DRIVER, "dummy", SDL_HINT_OVERRIDE));
    for (const bool batchedClick : {false, true})
    {
        INFO("Batched click: " << batchedClick);
        bool checked = false;
        class TabInputLayer final : public Keire::Layer
        {
          public:
            TabInputLayer(bool& checked, const bool batchedClick)
                : Layer("Tab input"), m_Checked(checked), m_BatchedClick(batchedClick)
            {
            }

          protected:
            void OnUi(Keire::UiFrame& ui) override
            {
                ui.SetNextWindowSize({720, 800}, false);
                auto window = ui.BeginWindow("Environment tab input fixture");
                ui.Text("Project-wide environment");
                auto tabs = ui.BeginTabBar("EnvironmentSections");
                REQUIRE(static_cast<bool>(tabs));
                bool lightingVisible = false;
                bool cloudsVisible = false;
                {
                    auto tab = ui.BeginTabItem("Lighting");
                    if (tab)
                    {
                        auto content = ui.BeginChild("LightingContent", {0, 0});
                        lightingVisible = static_cast<bool>(content);
                        ui.Text("Lighting timeline");
                    }
                }
                Keire::UiItemRect cloudTab;
                {
                    auto tab = ui.BeginTabItem("Clouds");
                    cloudTab = ui.LastItemRect();
                    if (tab)
                    {
                        auto content = ui.BeginChild("CloudContent", {0, 0});
                        cloudsVisible = static_cast<bool>(content);
                        ui.Text("Cloud controls");
                    }
                }
                auto& io = ImGui::GetIO();
                if (m_Frame == 2)
                {
                    CHECK(lightingVisible);
                    CHECK_FALSE(cloudsVisible);
                    m_CloudTab = cloudTab;
                    if (!m_BatchedClick)
                    {
                        MoveToCloudTab(io);
                    }
                }
                if (m_Frame == 4)
                {
                    if (m_BatchedClick)
                    {
                        MoveToCloudTab(io);
                    }
                    io.AddMouseButtonEvent(0, true);
                    if (m_BatchedClick)
                    {
                        io.AddMouseButtonEvent(0, false);
                    }
                }
                if (m_Frame == 5 && !m_BatchedClick)
                {
                    io.AddMouseButtonEvent(0, false);
                }
                if (++m_Frame == 10)
                {
                    CHECK(cloudsVisible);
                    CHECK_FALSE(lightingVisible);
                    m_Checked = true;
                    Owner().RequestExit();
                }
            }

          private:
            void MoveToCloudTab(ImGuiIO& io) const
            {
                io.AddMousePosEvent((m_CloudTab.Minimum.X + m_CloudTab.Maximum.X) * 0.5F,
                                    (m_CloudTab.Minimum.Y + m_CloudTab.Maximum.Y) * 0.5F);
            }

            bool& m_Checked;
            bool m_BatchedClick;
            Keire::UiItemRect m_CloudTab;
            int m_Frame = 0;
        };

        Keire::ApplicationSpecification specification;
        specification.MainWindow.Visible = false;
        specification.TargetFrameRate = 0;
        specification.Ui.Mode = Keire::UiMode::Headless;
        specification.Ui.LayoutPath.clear();
        Keire::Application application(specification);
        (void)application.PushLayer(std::make_unique<TabInputLayer>(checked, batchedClick));
        CHECK(application.Run() == 0);
        CHECK(checked);
    }
}
