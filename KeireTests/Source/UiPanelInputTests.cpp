#include "Keire/Core.h"
#include "KeireInternal/UiInputInternal.h"

#include <SDL3/SDL.h>
#include <doctest/doctest.h>
#include <imgui.h>
#include <imgui_internal.h>

#include <memory>
#include <string>

namespace
{
    class PanelInputLayer final : public Keire::Layer
    {
      public:
        explicit PanelInputLayer(const bool fastClick) : Layer("Panel input"), m_FastClick(fastClick) {}

      protected:
        void OnAttach() override
        {
            m_Panel = Owner().GetUiWorkspace().RegisterPanel({"test.floating-input", "Floating Input"});
        }

        void OnUi(Keire::UiFrame& ui) override
        {
            ui.SetNextWindowSize({400.0F, 240.0F});
            auto panel = ui.BeginPanel(m_Panel);
            if (m_Frame == 13)
            {
                CHECK_FALSE(m_Panel.Visible());
                Owner().RequestExit();
                return;
            }
            auto* window = ImGui::GetCurrentWindow();
            REQUIRE(window);
            const auto& style = ImGui::GetStyle();
            const float inset = style.FramePadding.x + ImGui::GetFontSize() * 0.5F;
            const float y = window->Pos.y + style.FramePadding.y + ImGui::GetFontSize() * 0.5F;
            auto& io = ImGui::GetIO();
            if (m_Frame == 1 || m_Frame == 4)
            {
                if (m_Frame == 4)
                {
                    CHECK(window->Collapsed);
                    CHECK(m_Panel.Visible());
                }
                io.AddMousePosEvent(window->Pos.x + inset, y);
                io.AddMouseButtonEvent(0, true);
                if (m_FastClick)
                    io.AddMouseButtonEvent(0, false);
            }
            if (m_Frame == 2 || m_Frame == 5 || m_Frame == 9 || m_Frame == 12)
                io.AddMouseButtonEvent(0, false);
            if (m_Frame == 7)
            {
                CHECK_FALSE(window->Collapsed);
                m_OriginalX = window->Pos.x;
                m_DragX = window->Pos.x + 160.0F;
                m_DragY = y;
                io.AddMousePosEvent(m_DragX, m_DragY);
                io.AddMouseButtonEvent(0, true);
                if (m_FastClick)
                {
                    io.AddMousePosEvent(m_DragX + 80.0F, m_DragY + 40.0F);
                    io.AddMouseButtonEvent(0, false);
                }
            }
            if (m_Frame == 8 && !m_FastClick)
                io.AddMousePosEvent(m_DragX + 80.0F, m_DragY + 40.0F);
            if (m_Frame == 11)
            {
                CHECK(window->Pos.x > m_OriginalX + 40.0F);
                io.AddMousePosEvent(window->Pos.x + window->Size.x - inset, y);
                io.AddMouseButtonEvent(0, true);
                if (m_FastClick)
                    io.AddMouseButtonEvent(0, false);
            }
            if (panel)
                ui.Text("Title controls and dragging must receive queued pointer events.");
            ++m_Frame;
        }

      private:
        Keire::UiPanelRegistration m_Panel;
        bool m_FastClick = false;
        int m_Frame = 0;
        float m_OriginalX = 0.0F;
        float m_DragX = 0.0F;
        float m_DragY = 0.0F;
    };

    class PanelInputApplication final : public Keire::Application
    {
      public:
        explicit PanelInputApplication(const bool fastClick) : Application(Specification())
        {
            (void)PushLayer(std::make_unique<PanelInputLayer>(fastClick));
        }

      private:
        static Keire::ApplicationSpecification Specification()
        {
            Keire::ApplicationSpecification specification;
            specification.MainWindow.Visible = false;
            specification.TargetFrameRate = 0;
            specification.Ui.Mode = Keire::UiMode::Headless;
            specification.Ui.LayoutPath.clear();
            specification.Ui.Workspace.Enabled = true;
            specification.Ui.Workspace.Ephemeral = true;
            return specification;
        }
    };

    class ComboWheelLayer final : public Keire::Layer
    {
      public:
        explicit ComboWheelLayer(const bool combinedMotion, const int selected = 0)
            : Layer("Combo wheel"), m_CombinedMotion(combinedMotion), m_Selected(selected)
        {
        }

      protected:
        void OnUi(Keire::UiFrame& ui) override
        {
            ImGui::SetNextWindowPos({20.0F, 20.0F});
            ImGui::SetNextWindowSize({500.0F, 350.0F});
            auto panel = ui.BeginWindow("Bone mapping panel");
            auto* parent = ImGui::GetCurrentWindow();
            auto& io = ImGui::GetIO();
            for (int row = 0; row < 30; ++row)
            {
                auto id = ui.PushId(std::to_string(row));
                auto combo = ui.BeginCombo("Bone", "Automatic");
                if (row == 0 && m_Frame == 1)
                {
                    const auto minimum = ImGui::GetItemRectMin();
                    io.AddMousePosEvent(minimum.x + 20.0F, minimum.y + 10.0F);
                    io.AddMouseButtonEvent(0, true);
                    io.AddMouseButtonEvent(0, false);
                }
                if (combo)
                {
                    auto* popup = ImGui::GetCurrentWindow();
                    (void)ui.InputText("Find target bone", m_Filter);
                    for (int bone = 0; bone < 30; ++bone)
                    {
                        (void)ui.Selectable("Joint " + std::to_string(bone), bone == m_Selected);
                        if (bone == m_Selected && m_Frame == 5)
                        {
                            CHECK(ImGui::GetItemRectMin().y >= popup->InnerRect.Min.y);
                            CHECK(ImGui::GetItemRectMax().y <= popup->InnerRect.Max.y);
                        }
                    }
                    if (m_Frame == 5)
                    {
                        REQUIRE(popup->ScrollMax.y > 0.0F);
                        if (!m_CombinedMotion)
                            io.AddMousePosEvent(popup->Pos.x + 80.0F, popup->Pos.y + 80.0F);
                    }
                    if (m_Frame == 7)
                    {
                        if (m_CombinedMotion)
                            io.AddMousePosEvent(popup->Pos.x + 80.0F, popup->Pos.y + 80.0F);
                        else
                            CHECK(ImGui::GetCurrentContext()->HoveredWindow == popup);
                        m_ParentScroll = parent->Scroll.y;
                        m_PopupScroll = popup->Scroll.y;
                        io.AddMouseWheelEvent(0.0F, -1.0F);
                    }
                    if (m_Frame == 8)
                    {
                        CHECK(popup->Scroll.y > m_PopupScroll);
                        CHECK(parent->Scroll.y == m_ParentScroll);
                        m_Verified = true;
                    }
                }
            }
            if (++m_Frame == 12)
            {
                CHECK(m_Verified);
                Owner().RequestExit();
            }
        }

      private:
        int m_Frame = 0;
        float m_ParentScroll = 0.0F;
        float m_PopupScroll = 0.0F;
        bool m_Verified = false;
        bool m_CombinedMotion = false;
        int m_Selected = 0;
        std::string m_Filter;
    };

    class DropFocusLayer final : public Keire::Layer
    {
      public:
        DropFocusLayer() : Layer("Drop focus") {}

      protected:
        void OnAttach() override
        {
            m_Target = Owner().GetUiWorkspace().RegisterPanel({"test.drop-target", "Scene target"});
            m_Source = Owner().GetUiWorkspace().RegisterPanel({"test.drop-source", "Project source"});
            m_Source.RequestFocus();
        }
        void OnUi(Keire::UiFrame& ui) override
        {
            {
                auto panel = ui.BeginPanel(m_Target);
                if (m_Frame == 1)
                {
                    CHECK_FALSE(ui.WindowFocused());
                    m_Target.RequestFocus();
                }
                if (m_Frame == 2)
                    CHECK(ui.WindowFocused());
            }
            {
                auto panel = ui.BeginPanel(m_Source);
                if (m_Frame == 1)
                    CHECK(ui.WindowFocused());
                if (m_Frame == 2)
                    CHECK_FALSE(ui.WindowFocused());
            }
            if (++m_Frame == 3)
                Owner().RequestExit();
        }

      private:
        Keire::UiPanelRegistration m_Target;
        Keire::UiPanelRegistration m_Source;
        int m_Frame = 0;
    };

    class FloatingScrollRoutingLayer final : public Keire::Layer
    {
      public:
        FloatingScrollRoutingLayer() : Layer("Floating scroll routing") {}

      protected:
        void OnAttach() override
        {
            m_Background = Owner().GetUiWorkspace().RegisterPanel({"test.scroll-background", "Scroll Background"});
            m_Foreground = Owner().GetUiWorkspace().RegisterPanel({"test.scroll-foreground", "Scroll Foreground"});
        }

        void OnUi(Keire::UiFrame& ui) override
        {
            const auto* viewport = ImGui::GetMainViewport();
            REQUIRE(viewport);
            auto& io = ImGui::GetIO();

            ImGui::SetNextWindowPos(viewport->WorkPos, ImGuiCond_Always);
            ImGui::SetNextWindowSize(viewport->WorkSize, ImGuiCond_Always);
            if (auto background = ui.BeginPanel(m_Background, {.NoSavedSettings = true}); background)
            {
                if (auto scroll = ui.BeginChild("BackgroundScroll", {0.0F, 0.0F}, false); scroll)
                {
                    m_BackgroundWindow = ImGui::GetCurrentWindow();
                    for (int row = 0; row < 80; ++row)
                        ui.Text("Background row " + std::to_string(row));
                }
            }

            if (m_Frame == 0)
            {
                ImGui::SetNextWindowPos({viewport->WorkPos.x + 80.0F, viewport->WorkPos.y + 60.0F}, ImGuiCond_Always);
                ImGui::SetNextWindowSize({320.0F, 240.0F}, ImGuiCond_Always);
            }
            if (auto foreground = ui.BeginPanel(m_Foreground, {.NoSavedSettings = true}); foreground)
            {
                auto* foregroundWindow = ImGui::GetCurrentWindow();
                m_ForegroundWindow = foregroundWindow;
                if (m_Frame == 1)
                {
                    CHECK(foregroundWindow->Pos.x >= viewport->WorkPos.x);
                    CHECK(foregroundWindow->Pos.y >= viewport->WorkPos.y);
                    CHECK(foregroundWindow->Pos.x + foregroundWindow->Size.x <=
                          viewport->WorkPos.x + viewport->WorkSize.x);
                    CHECK(foregroundWindow->Pos.y + foregroundWindow->Size.y <=
                          viewport->WorkPos.y + viewport->WorkSize.y);
                }

                if (auto scroll = ui.BeginChild("ForegroundScroll", {0.0F, 130.0F}, true); scroll)
                {
                    m_ForegroundScrollWindow = ImGui::GetCurrentWindow();
                    for (int row = 0; row < 40; ++row)
                        ui.Text("Foreground row " + std::to_string(row));
                }

                if (m_Frame == 0)
                {
                    ImGui::SetWindowSize(foregroundWindow, {viewport->WorkSize.x * 2.0F, viewport->WorkSize.y * 2.0F},
                                         ImGuiCond_Always);
                    ImGui::SetWindowPos(foregroundWindow,
                                        {viewport->WorkPos.x + viewport->WorkSize.x - 20.0F,
                                         viewport->WorkPos.y + viewport->WorkSize.y - 20.0F},
                                        ImGuiCond_Always);
                }
            }

            if (m_Frame == 1)
            {
                REQUIRE(m_ForegroundScrollWindow);
                const auto center = m_ForegroundScrollWindow->InnerRect.GetCenter();
                io.AddMousePosEvent(center.x, center.y);
            }
            if (m_Frame == 3)
            {
                REQUIRE(m_BackgroundWindow);
                REQUIRE(m_ForegroundScrollWindow);
                REQUIRE(ImGui::GetCurrentContext()->HoveredWindow == m_ForegroundScrollWindow);
                REQUIRE(m_ForegroundScrollWindow->ScrollMax.y > 0.0F);
                m_BackgroundScroll = m_BackgroundWindow->Scroll.y;
                m_ForegroundScroll = m_ForegroundScrollWindow->Scroll.y;
                io.AddMouseWheelEvent(0.0F, -1.0F);
                io.AddMouseWheelEvent(0.0F, -1.0F);
                io.AddMouseWheelEvent(0.0F, -1.0F);
            }
            if (m_Frame == 6)
            {
                REQUIRE(m_BackgroundWindow);
                REQUIRE(m_ForegroundScrollWindow);
                CHECK(m_ForegroundScrollWindow->Scroll.y > m_ForegroundScroll);
                CHECK(m_BackgroundWindow->Scroll.y == m_BackgroundScroll);
                m_BackgroundScroll = m_BackgroundWindow->Scroll.y;
                m_ForegroundScroll = m_ForegroundScrollWindow->Scroll.y;
                REQUIRE(m_ForegroundWindow);
                ImGui::SetWindowSize(m_ForegroundWindow, {320.0F, 240.0F}, ImGuiCond_Always);
                ImGui::SetWindowPos(m_ForegroundWindow, {viewport->WorkPos.x + 80.0F, viewport->WorkPos.y + 60.0F},
                                    ImGuiCond_Always);
                const auto backgroundPoint = m_BackgroundWindow->InnerRect.GetCenter();
                io.AddMousePosEvent(m_BackgroundWindow->InnerRect.Max.x - 40.0F, backgroundPoint.y);
            }
            if (m_Frame == 8)
            {
                REQUIRE(m_BackgroundWindow);
                REQUIRE(m_ForegroundScrollWindow);
                REQUIRE(ImGui::GetCurrentContext()->HoveredWindow == m_BackgroundWindow);
                const auto center = m_ForegroundScrollWindow->InnerRect.GetCenter();
                io.AddMouseWheelEvent(0.0F, -1.0F);
                io.AddMousePosEvent(center.x, center.y);
            }
            if (m_Frame == 10)
            {
                REQUIRE(m_BackgroundWindow);
                REQUIRE(m_ForegroundScrollWindow);
                CHECK(m_ForegroundScrollWindow->Scroll.y > m_ForegroundScroll);
                CHECK(m_BackgroundWindow->Scroll.y == m_BackgroundScroll);
                Owner().RequestExit();
            }
            ++m_Frame;
        }

      private:
        Keire::UiPanelRegistration m_Background;
        Keire::UiPanelRegistration m_Foreground;
        ImGuiWindow* m_BackgroundWindow = nullptr;
        ImGuiWindow* m_ForegroundWindow = nullptr;
        ImGuiWindow* m_ForegroundScrollWindow = nullptr;
        float m_BackgroundScroll = 0.0F;
        float m_ForegroundScroll = 0.0F;
        int m_Frame = 0;
    };

} // namespace

TEST_CASE("UI drop target focus displaces a source panel submitted later")
{
    REQUIRE(SDL_SetHintWithPriority(SDL_HINT_VIDEO_DRIVER, "dummy", SDL_HINT_OVERRIDE));
    Keire::ApplicationSpecification specification;
    specification.MainWindow.Visible = false;
    specification.TargetFrameRate = 0;
    specification.Ui.Mode = Keire::UiMode::Headless;
    specification.Ui.LayoutPath.clear();
    specification.Ui.Workspace.Enabled = true;
    specification.Ui.Workspace.Ephemeral = true;
    Keire::Application application(specification);
    (void)application.PushLayer(std::make_unique<DropFocusLayer>());
    CHECK(application.Run() == 0);
}

TEST_CASE("UI bone combo wheel scrolls the popup instead of its parent")
{
    REQUIRE(SDL_SetHintWithPriority(SDL_HINT_VIDEO_DRIVER, "dummy", SDL_HINT_OVERRIDE));
    Keire::ApplicationSpecification specification;
    specification.MainWindow.Visible = false;
    specification.TargetFrameRate = 0;
    specification.Ui.Mode = Keire::UiMode::Headless;
    specification.Ui.LayoutPath.clear();
    for (const bool combinedMotion : {false, true})
        for (const int selected : {0, 20})
        {
            INFO("Motion and wheel queued together: " << combinedMotion);
            Keire::Application application(specification);
            (void)application.PushLayer(std::make_unique<ComboWheelLayer>(combinedMotion, selected));
            CHECK(application.Run() == 0);
        }
}

TEST_CASE("floating UI panels accept queued collapse drag and close input")
{
    REQUIRE(SDL_SetHintWithPriority(SDL_HINT_VIDEO_DRIVER, "dummy", SDL_HINT_OVERRIDE));
    for (const bool fastClick : {false, true})
    {
        INFO("Press and release queued in one frame: " << fastClick);
        PanelInputApplication application(fastClick);
        CHECK(application.Run() == 0);
    }
}

TEST_CASE("constrained floating panel routes continuous wheel input to its scroll child")
{
    REQUIRE(SDL_SetHintWithPriority(SDL_HINT_VIDEO_DRIVER, "dummy", SDL_HINT_OVERRIDE));
    Keire::ApplicationSpecification specification;
    specification.MainWindow.Visible = false;
    specification.TargetFrameRate = 0;
    specification.Ui.Mode = Keire::UiMode::Headless;
    specification.Ui.LayoutPath.clear();
    specification.Ui.Workspace.Enabled = true;
    specification.Ui.Workspace.Ephemeral = true;
    Keire::Application application(specification);
    (void)application.PushLayer(std::make_unique<FloatingScrollRoutingLayer>());
    CHECK(application.Run() == 0);
}

TEST_CASE("UI input queue preserves pointer action order and cancellation")
{
    const auto previous = ImGui::GetCurrentContext();
    const auto context = ImGui::CreateContext();
    struct Cleanup
    {
        ImGuiContext* Context;
        ImGuiContext* Previous;
        ~Cleanup()
        {
            ImGui::DestroyContext(Context);
            ImGui::SetCurrentContext(Previous);
        }
    } cleanup{context, previous};
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = {640.0F, 480.0F};
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    const auto frame = []()
    {
        Keire::Detail::UiBackendNewFrame();
        ImGui::EndFrame();
    };
    io.AddMousePosEvent(100.0F, 100.0F);
    io.AddMouseButtonEvent(0, true);
    frame();
    REQUIRE(io.MouseDown[0]);

    SUBCASE("pointer movement and wheel share one frame while later input remains ordered")
    {
        io.AddMousePosEvent(200.0F, 150.0F);
        io.AddMouseWheelEvent(0.0F, -1.0F);
        io.AddMouseButtonEvent(0, false);
        frame();
        CHECK(io.MousePos.x == 200.0F);
        CHECK(io.MousePos.y == 150.0F);
        CHECK(io.MouseDown[0]);
        CHECK(io.ConfigInputTrickleEventQueue);
        REQUIRE(context->InputEventsQueue.size() == 1);
        CHECK(context->InputEventsQueue.front().Type == ImGuiInputEventType_MouseButton);
        REQUIRE(context->InputEventsTrail.size() == 2);
        CHECK(context->InputEventsTrail[0].Type == ImGuiInputEventType_MousePos);
        CHECK(context->InputEventsTrail[1].Type == ImGuiInputEventType_MouseWheel);
        frame();
        CHECK_FALSE(io.MouseDown[0]);
        CHECK(context->InputEventsQueue.empty());
    }
    SUBCASE("wheel before pointer movement shares one frame while later input remains ordered")
    {
        io.AddMouseWheelEvent(0.0F, -1.0F);
        io.AddMousePosEvent(200.0F, 150.0F);
        io.AddMouseButtonEvent(0, false);
        frame();
        CHECK(io.MousePos.x == 200.0F);
        CHECK(io.MousePos.y == 150.0F);
        CHECK(io.MouseDown[0]);
        CHECK(io.ConfigInputTrickleEventQueue);
        REQUIRE(context->InputEventsQueue.size() == 1);
        CHECK(context->InputEventsQueue.front().Type == ImGuiInputEventType_MouseButton);
        REQUIRE(context->InputEventsTrail.size() == 2);
        CHECK(context->InputEventsTrail[0].Type == ImGuiInputEventType_MouseWheel);
        CHECK(context->InputEventsTrail[1].Type == ImGuiInputEventType_MousePos);
        frame();
        CHECK_FALSE(io.MouseDown[0]);
        CHECK(context->InputEventsQueue.empty());
    }
    SUBCASE("movement gets a held frame then releases before the next press")
    {
        io.AddMousePosEvent(200.0F, 150.0F);
        io.AddMouseButtonEvent(0, false);
        io.AddMouseButtonEvent(1, true);
        frame();
        CHECK(io.MouseDown[0]);
        CHECK_FALSE(io.MouseDown[1]);
        CHECK(io.MousePos.x == 200.0F);
        CHECK(ImGui::IsMouseDragging(0));
        frame();
        CHECK_FALSE(io.MouseDown[0]);
        CHECK(io.MouseDown[1]);
        CHECK(context->InputEventsQueue.empty());
    }
    SUBCASE("subthreshold clicks release immediately")
    {
        io.AddMousePosEvent(101.0F, 100.0F);
        io.AddMouseButtonEvent(0, false);
        frame();
        CHECK_FALSE(io.MouseDown[0]);
        CHECK(context->InputEventsQueue.empty());
    }
    SUBCASE("focus loss cancels the drag without deferring cancellation")
    {
        io.AddMousePosEvent(200.0F, 150.0F);
        io.AddMouseButtonEvent(0, false);
        io.AddFocusEvent(false);
        frame();
        CHECK_FALSE(io.MouseDown[0]);
        CHECK(context->InputEventsQueue.empty());
    }
    SUBCASE("disabled trickling preserves immediate release")
    {
        io.ConfigInputTrickleEventQueue = false;
        io.AddMousePosEvent(200.0F, 150.0F);
        io.AddMouseButtonEvent(0, false);
        frame();
        CHECK_FALSE(io.MouseDown[0]);
        CHECK(context->InputEventsQueue.empty());
    }
}
