#include "Keire/Core.h"

#include <SDL3/SDL.h>
#include <doctest/doctest.h>
#include <imgui.h>

#include <memory>

TEST_CASE("Hierarchy edge scrolling follows a held pointer and stops on release")
{
    REQUIRE(SDL_SetHintWithPriority(SDL_HINT_VIDEO_DRIVER, "dummy", SDL_HINT_OVERRIDE));
    bool checked = false;
    class ScrollLayer final : public Keire::Layer
    {
      public:
        explicit ScrollLayer(bool& checked) : Layer("Hierarchy scrolling"), m_Checked(checked) {}

      protected:
        void OnUi(Keire::UiFrame& ui) override
        {
            ui.SetNextWindowSize({420, 300}, false);
            // Match the docked hierarchy: dragging its content must not move the fixture window.
            auto window = ui.BeginWindow("Hierarchy scroll fixture", nullptr, {.NoMove = true});
            auto child = ui.BeginChild("Entities", {300, 180}, true);
            const auto position = ImGui::GetWindowPos();
            const auto size = ImGui::GetWindowSize();
            auto& input = ImGui::GetIO();
            input.DeltaTime = 1.0F / 60.0F;
            ui.AutoScrollVerticalNearPointer();
            for (int row = 0; row < 100; ++row)
            {
                ui.Text("Entity");
            }
            if (m_Frame == 2)
            {
                input.AddMousePosEvent(position.x + size.x * 0.5F, position.y + size.y - 10.0F);
                input.AddMouseButtonEvent(0, true);
            }
            if (m_Frame == 15)
            {
                CHECK(ImGui::GetScrollY() > 20.0F);
                input.AddMousePosEvent(position.x + size.x * 0.5F, position.y + 10.0F);
                m_BottomScroll = ImGui::GetScrollY();
            }
            if (m_Frame == 25)
            {
                INFO("pointer=" << input.MousePos.y << " window=" << position.y << " height=" << size.y);
                CHECK(ImGui::GetScrollY() < m_BottomScroll);
                input.AddMouseButtonEvent(0, false);
            }
            if (m_Frame == 28)
            {
                m_ReleasedScroll = ImGui::GetScrollY();
            }
            if (m_Frame == 35)
            {
                CHECK(ImGui::GetScrollY() == doctest::Approx(m_ReleasedScroll));
                m_Checked = true;
                Owner().RequestExit();
            }
            ++m_Frame;
        }

      private:
        bool& m_Checked;
        int m_Frame = 0;
        float m_BottomScroll = 0.0F;
        float m_ReleasedScroll = 0.0F;
    };
    Keire::ApplicationSpecification specification;
    specification.MainWindow.Visible = false;
    specification.TargetFrameRate = 0;
    specification.Ui.Mode = Keire::UiMode::Headless;
    specification.Ui.LayoutPath.clear();
    Keire::Application application(specification);
    (void)application.PushLayer(std::make_unique<ScrollLayer>(checked));
    CHECK(application.Run() == 0);
    CHECK(checked);
}

TEST_CASE("Tree reveal is one-shot and leaf rows do not expose expansion arrows")
{
    REQUIRE(SDL_SetHintWithPriority(SDL_HINT_VIDEO_DRIVER, "dummy", SDL_HINT_OVERRIDE));
    bool checked = false;
    class RevealLayer final : public Keire::Layer
    {
      public:
        explicit RevealLayer(bool& checked) : Layer("Tree reveal"), m_Checked(checked) {}

      protected:
        void OnUi(Keire::UiFrame& ui) override
        {
            ui.SetNextWindowSize({420, 240}, false);
            auto window = ui.BeginWindow("Tree reveal fixture");
            CHECK_THROWS_AS(ui.AutoScrollVerticalNearPointer(0.0F), std::invalid_argument);
            CHECK_THROWS_AS(ui.AutoScrollVerticalNearPointer(32.0F, -1.0F), std::invalid_argument);
            {
                auto parent = ui.BeginTreeNode("Parent", false, false, m_Frame == 1);
                if (m_Frame == 0 || m_Frame >= 3)
                {
                    CHECK_FALSE(static_cast<bool>(parent));
                }
                else
                {
                    CHECK(static_cast<bool>(parent));
                }
                if (parent)
                {
                    auto leaf = ui.BeginTreeNode("Selected child", true, true, false);
                    CHECK(static_cast<bool>(leaf));
                }
            }
            // Simulate the user's deliberate collapse after reveal. The next frame must respect it.
            if (m_Frame == 2)
            {
                ImGui::GetStateStorage()->SetInt(ImGui::GetID("Parent"), 0);
            }
            if (++m_Frame == 5)
            {
                m_Checked = true;
                Owner().RequestExit();
            }
        }

      private:
        bool& m_Checked;
        int m_Frame = 0;
    };
    Keire::ApplicationSpecification specification;
    specification.MainWindow.Visible = false;
    specification.TargetFrameRate = 0;
    specification.Ui.Mode = Keire::UiMode::Headless;
    specification.Ui.LayoutPath.clear();
    Keire::Application application(specification);
    (void)application.PushLayer(std::make_unique<RevealLayer>(checked));
    CHECK(application.Run() == 0);
    CHECK(checked);
}

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
