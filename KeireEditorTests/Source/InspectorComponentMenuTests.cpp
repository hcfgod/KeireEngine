#include "Keire/Core.h"
#include "KeireClient/Editor/InspectorComponentUtilities.h"

#include <SDL3/SDL.h>
#include <doctest/doctest.h>
#include <imgui.h>
#include <imgui_internal.h>

#include <memory>
#include <optional>

TEST_CASE("multi-selection component popup has stable first-visible-frame sizing")
{
    REQUIRE(SDL_SetHintWithPriority(SDL_HINT_VIDEO_DRIVER, "dummy", SDL_HINT_OVERRIDE));
    for (const bool removable : {false, true})
    {
        bool checked = false;
        class MenuLayer final : public Keire::Layer
        {
          public:
            MenuLayer(bool& checked, const bool removable)
                : Layer("Component menu"), m_Checked(checked), m_Removable(removable)
            {
            }

          protected:
            void OnUi(Keire::UiFrame& ui) override
            {
                ui.SetNextWindowSize({400, 400});
                auto window = ui.BeginWindow("Inspector menu fixture");
                auto card = ui.BeginChild("Component", {300, 300}, true);
                if (m_Frame == 0)
                    ui.OpenPopup("ComponentMenu");
                if (auto popup = ui.BeginPopup("ComponentMenu"); popup)
                {
                    CHECK_FALSE(KeireEditor::DrawSelectionComponentRemovalMenu(ui, m_Removable));
                    const auto* current = ImGui::GetCurrentWindow();
                    if (!current->Hidden)
                    {
                        CHECK(current->Size.y < 100);
                        if (m_PreviousSize)
                        {
                            CHECK(current->Size.x == doctest::Approx(m_PreviousSize->x));
                            CHECK(current->Size.y == doctest::Approx(m_PreviousSize->y));
                        }
                        m_PreviousSize = current->Size;
                        ++m_VisibleFrames;
                    }
                }
                if (++m_Frame == 8)
                {
                    CHECK(m_VisibleFrames >= 6);
                    m_Checked = true;
                    Owner().RequestExit();
                }
            }

          private:
            bool& m_Checked;
            bool m_Removable;
            int m_Frame = 0;
            int m_VisibleFrames = 0;
            std::optional<ImVec2> m_PreviousSize;
        };
        Keire::ApplicationSpecification specification;
        specification.MainWindow.Visible = false;
        specification.TargetFrameRate = 0;
        specification.Ui.Mode = Keire::UiMode::Headless;
        specification.Ui.LayoutPath.clear();
        Keire::Application application(specification);
        (void)application.PushLayer(std::make_unique<MenuLayer>(checked, removable));
        CHECK(application.Run() == 0);
        CHECK(checked);
    }
}
