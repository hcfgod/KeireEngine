#include "Keire/Core.h"

#include <SDL3/SDL.h>
#include <doctest/doctest.h>
#include <imgui.h>

#include <memory>

TEST_CASE("Settings sections toggle on label and row clicks without changing hierarchy selection")
{
    REQUIRE(SDL_SetHintWithPriority(SDL_HINT_VIDEO_DRIVER, "dummy", SDL_HINT_OVERRIDE));
    for (int variant = 0; variant < 3; ++variant)
    {
        INFO("Input variant: " << variant);
        bool checked = false;
        class SectionInputLayer final : public Keire::Layer
        {
          public:
            SectionInputLayer(const int variant, bool& checked)
                : Layer("Section input"), m_Variant(variant), m_Checked(checked)
            {
            }

          protected:
            void OnUi(Keire::UiFrame& ui) override
            {
                ui.SetNextWindowSize({420, 240}, false);
                auto window = ui.BeginWindow("Settings section input fixture");
                bool expanded = false;
                Keire::UiItemRect heading;
                {
                    auto section =
                        m_Variant == 2 ? ui.BeginTreeNode("Cloud settings") : ui.BeginSection("Cloud settings");
                    heading = ui.LastItemRect();
                    expanded = static_cast<bool>(section);
                    if (section)
                    {
                        ui.Text("Section content");
                    }
                }
                auto& io = ImGui::GetIO();
                if (m_Frame == 0)
                {
                    const float clickX = m_Variant == 1 ? heading.Maximum.X - 8.0F : heading.Minimum.X + 65.0F;
                    io.AddMousePosEvent(clickX, (heading.Minimum.Y + heading.Maximum.Y) * 0.5F);
                }
                if (m_Frame == 2 || (m_Frame == 8 && m_Variant != 2))
                {
                    io.AddMouseButtonEvent(0, true);
                }
                if (m_Frame == 3 || (m_Frame == 9 && m_Variant != 2))
                {
                    io.AddMouseButtonEvent(0, false);
                }
                if (m_Frame == 6)
                {
                    CHECK(expanded == (m_Variant != 2));
                }
                if (++m_Frame == 13)
                {
                    CHECK_FALSE(expanded);
                    m_Checked = true;
                    Owner().RequestExit();
                }
            }

          private:
            int m_Variant;
            bool& m_Checked;
            int m_Frame = 0;
        };

        Keire::ApplicationSpecification specification;
        specification.MainWindow.Visible = false;
        specification.TargetFrameRate = 0;
        specification.Ui.Mode = Keire::UiMode::Headless;
        specification.Ui.LayoutPath.clear();
        Keire::Application application(specification);
        (void)application.PushLayer(std::make_unique<SectionInputLayer>(variant, checked));
        CHECK(application.Run() == 0);
        CHECK(checked);
    }
}
