#include "Keire/Core.h"

#include <SDL3/SDL.h>
#include <doctest/doctest.h>
#include <imgui.h>

#include <memory>
#include <string>

namespace
{
    class RevealScrollLayer final : public Keire::Layer
    {
      public:
        explicit RevealScrollLayer(const bool grid) : Layer("RevealScroll"), m_Grid(grid) {}

      protected:
        void OnUi(Keire::UiFrame& ui) override
        {
            ui.SetNextWindowPosition({20.0F, 20.0F}, false);
            ui.SetNextWindowSize({540.0F, 280.0F}, false);
            if (auto window = ui.BeginWindow("Reveal scrolling"); window)
            {
                ui.Text("Breadcrumbs remain before the scrolling items");
                if (auto child = ui.BeginChild("Asset content", {480.0F, 150.0F}); child)
                {
                    if (m_Frame == 1)
                        CHECK(ImGui::GetScrollY() > 500.0F);
                    if (m_Frame == 3)
                        CHECK(ImGui::GetScrollY() == 0.0F);
                    const auto drawItem = [&](const int index)
                    {
                        auto id = ui.PushId(std::to_string(index));
                        (void)ui.InvisibleButton(index < 3 ? "Folder" : "Asset", {100.0F, index == 6 ? 180.0F : 80.0F});
                        if (index != 26)
                            return;
                        if (m_Frame == 0)
                        {
                            CHECK_FALSE(ImGui::IsItemVisible());
                            ui.ScrollLastItemIntoView();
                        }
                        if (m_Frame == 1)
                            CHECK(ImGui::IsItemVisible());
                    };
                    if (m_Grid)
                    {
                        const Keire::UiTableOptions options{.Sizing = Keire::UiTableSizing::Equal,
                                                            .Borders = false,
                                                            .Resizable = false,
                                                            .RowBackground = false,
                                                            .PersistSettings = false};
                        if (auto table = ui.BeginTable("Grid", 3, options); table)
                            for (int index = 0; index < 33; ++index)
                            {
                                if (index % 3 == 0)
                                    ui.TableNextRow();
                                (void)ui.TableNextColumn();
                                drawItem(index);
                            }
                    }
                    else
                        for (int index = 0; index < 33; ++index)
                            drawItem(index);
                    if (m_Frame == 2)
                        ImGui::SetScrollY(0.0F);
                }
            }
            if (++m_Frame == 4)
                Owner().RequestExit();
        }

      private:
        bool m_Grid;
        int m_Frame = 0;
    };
} // namespace

TEST_CASE("UI reveal scroll reaches an offscreen item after folders and variable-height rows")
{
    REQUIRE(SDL_SetHintWithPriority(SDL_HINT_VIDEO_DRIVER, "dummy", SDL_HINT_OVERRIDE));
    bool grid = false;
    SUBCASE("List") {}
    SUBCASE("Three-column grid") { grid = true; }
    Keire::ApplicationSpecification specification;
    specification.MainWindow.Visible = false;
    specification.TargetFrameRate = 0;
    specification.Ui.Mode = Keire::UiMode::Headless;
    specification.Ui.LayoutPath.clear();
    Keire::Application application(specification);
    (void)application.PushLayer(std::make_unique<RevealScrollLayer>(grid));
    CHECK(application.Run() == 0);
}
