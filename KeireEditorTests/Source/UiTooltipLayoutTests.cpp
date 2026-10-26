#include "Keire/Core.h"

#include <SDL3/SDL.h>
#include <doctest/doctest.h>
#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <memory>
#include <string_view>

namespace
{
    class TooltipLayoutLayer final : public Keire::Layer
    {
      public:
        explicit TooltipLayoutLayer(bool& checked) : Layer("Tooltip layout"), m_Checked(checked) {}

      protected:
        void OnUi(Keire::UiFrame& ui) override
        {
            ui.SetNextWindowPosition({10.0F, 10.0F}, false);
            ui.SetNextWindowSize({280.0F, 160.0F}, false);
            auto window = ui.BeginWindow("Tooltip fixture");
            if (!window)
            {
                Owner().RequestExit();
                return;
            }
            ImGui::PushFont(nullptr, m_Frame < 2 ? 16.0F : 26.0F);
            (void)ui.Button("Help");
            auto* context = ImGui::GetCurrentContext();
            context->HoveredWindow = ImGui::GetCurrentWindow();
            context->LastItemData.StatusFlags |= ImGuiItemStatusFlags_HoveredRect;
            REQUIRE(ImGui::IsItemHovered());
            constexpr std::string_view description =
                "GPU visibility is camera-local. Bounds describe the selected camera's submitted objects, not every "
                "object in the scene. Use the Game camera to inspect runtime visibility and the Scene camera for "
                "authoring. Camera cuts and streamed content can change these results between frames.";
            ui.SetTooltip(description);
            ImGuiWindow* tooltip = nullptr;
            for (auto* candidate : context->Windows)
            {
                if (candidate->Active && (candidate->Flags & ImGuiWindowFlags_Tooltip) != 0)
                {
                    tooltip = candidate;
                }
            }
            REQUIRE(tooltip != nullptr);
            const auto& style = ImGui::GetStyle();
            const float available =
                tooltip->Viewport->WorkSize.x - 2.0F * (style.WindowPadding.x + style.DisplaySafeAreaPadding.x);
            const float expectedWidth = std::min(ImGui::GetFontSize() * 35.0F, available);
            CHECK(tooltip->DC.CursorMaxPos.x - tooltip->DC.CursorStartPos.x <= expectedWidth + 1.0F);
            CHECK(tooltip->DC.CursorMaxPos.y - tooltip->DC.CursorStartPos.y > ImGui::GetFontSize());
            CHECK(tooltip->DC.TextWrapPosStack.empty());
            ImGui::PopFont();
            if (++m_Frame == 4)
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

TEST_CASE("Long tooltips wrap within viewport bounds at normal and large font sizes")
{
    REQUIRE(SDL_SetHintWithPriority(SDL_HINT_VIDEO_DRIVER, "dummy", SDL_HINT_OVERRIDE));
    int viewportWidth = 360;
    SUBCASE("narrow viewport") {}
    SUBCASE("wide viewport") { viewportWidth = 1100; }
    bool checked = false;
    Keire::ApplicationSpecification specification;
    specification.MainWindow.Visible = false;
    specification.MainWindow.Width = viewportWidth;
    specification.MainWindow.Height = 700;
    specification.TargetFrameRate = 0;
    specification.Ui.Mode = Keire::UiMode::Headless;
    specification.Ui.LayoutPath.clear();
    Keire::Application application(specification);
    (void)application.PushLayer(std::make_unique<TooltipLayoutLayer>(checked));
    CHECK(application.Run() == 0);
    CHECK(checked);
}
