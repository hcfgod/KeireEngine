#include "KeireClient/Editor/GraphNavigation.h"

#include <SDL3/SDL.h>
#include <doctest/doctest.h>
#include <imgui.h>
#include <imgui_internal.h>

#include <memory>

namespace
{
    class BookmarkLayer final : public Keire::Layer
    {
      public:
        explicit BookmarkLayer(bool& checked) : Layer("Bookmark controls"), m_Checked(checked)
        {
            m_Menu.Name = "Save Current View";
        }

      protected:
        void OnUi(Keire::UiFrame& ui) override
        {
            ui.SetNextWindowSize({640, 480}, false);
            auto window = ui.BeginWindow("Bookmark controls fixture");
            if (m_Frame % 2 == 1)
            {
                Activate(ImGui::GetCurrentWindow()->GetID("Bookmarks"));
            }
            else if (m_Frame >= 2 && m_Frame <= 10)
            {
                auto* context = ImGui::GetCurrentContext();
                REQUIRE(context->OpenPopupStack.Size == 1);
                auto* popup = context->OpenPopupStack.back().Window;
                REQUIRE(popup != nullptr);
                CHECK(popup->Size.x <= 360.0F);
                const char* action = nullptr;
                if (m_Frame == 2)
                {
                    action = "Save Current View";
                }
                else if (m_Frame == 4)
                {
                    m_Menu.Name = "Renamed";
                    action = "Rename Selected";
                }
                else if (m_Frame == 6)
                {
                    m_Canvas.RestoreViewport({{30, 40}, 0.5F});
                    action = "Update Current View";
                }
                else if (m_Frame == 8)
                {
                    m_Canvas.RestoreViewport({{1, 2}, 1});
                    action = "Renamed";
                }
                else
                {
                    action = "Remove Selected";
                }
                const auto id = m_Frame == 8 ? ImHashStr(action, 0, ImHashStr("SavedViews", 0, popup->IDStack.back()))
                                             : popup->GetID(action);
                Activate(id);
            }

            const bool navigated = KeireEditor::DrawGraphBookmarkMenu(ui, m_Bookmarks, m_Canvas, m_Menu);
            CHECK(navigated == (m_Frame == 8));
            if (m_Frame == 2)
            {
                CHECK(m_Bookmarks.Find("Save Current View").has_value());
            }
            if (m_Frame == 4)
            {
                CHECK_FALSE(m_Bookmarks.Find("Save Current View"));
                CHECK(m_Bookmarks.Find("Renamed").has_value());
            }
            if (m_Frame == 6 || m_Frame == 8)
            {
                CHECK(m_Bookmarks.Find("Renamed") == KeireEditor::NodeGraphViewport{{30, 40}, 0.5F});
                CHECK(m_Canvas.Viewport() == KeireEditor::NodeGraphViewport{{30, 40}, 0.5F});
            }
            if (m_Frame == 10)
            {
                CHECK(m_Bookmarks.Bookmarks().empty());
                CHECK(m_Menu.Selected.empty());
            }
            if (++m_Frame == 11)
            {
                m_Checked = true;
                Owner().RequestExit();
            }
        }

      private:
        static void Activate(const ImGuiID id)
        {
            auto* context = ImGui::GetCurrentContext();
            context->NavActivateId = id;
            context->NavActivateDownId = id;
            context->NavActivatePressedId = id;
        }

        bool& m_Checked;
        int m_Frame = 0;
        KeireEditor::GraphBookmarkSet m_Bookmarks;
        KeireEditor::GraphBookmarkMenuState m_Menu;
        KeireEditor::StableNodeGraphCanvas m_Canvas;
    };
} // namespace

TEST_CASE("Graph bookmark menu saves renames updates navigates and removes views")
{
    REQUIRE(SDL_SetHintWithPriority(SDL_HINT_VIDEO_DRIVER, "dummy", SDL_HINT_OVERRIDE));
    bool checked = false;
    Keire::ApplicationSpecification specification;
    specification.MainWindow.Visible = false;
    specification.TargetFrameRate = 0;
    specification.Ui.Mode = Keire::UiMode::Headless;
    specification.Ui.LayoutPath.clear();
    Keire::Application application(specification);
    (void)application.PushLayer(std::make_unique<BookmarkLayer>(checked));
    CHECK(application.Run() == 0);
    CHECK(checked);
}
