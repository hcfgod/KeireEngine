#include "Keire/Core.h"
#include "KeireInternal/NumericExpression.h"
#include "KeireInternal/UiInputInternal.h"
#include "KeireInternal/UiNumericExpression.h"
#include "KeireInternal/UiStickySection.h"

#include <SDL3/SDL.h>
#include <doctest/doctest.h>
#include <imgui.h>
#include <imgui_internal.h>

#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>

TEST_CASE("Overlay actions receive pointer clicks above an overlapping viewport canvas")
{
    REQUIRE(SDL_SetHintWithPriority(SDL_HINT_VIDEO_DRIVER, "dummy", SDL_HINT_OVERRIDE));
    for (const bool fastClick : {false, true})
    {
        INFO("Fast click: " << fastClick);
        bool checked = false;
        class OverlayInputLayer final : public Keire::Layer
        {
          public:
            OverlayInputLayer(const bool fast, bool& checked) : Layer("Overlay input"), m_Fast(fast), m_Checked(checked)
            {
            }

          protected:
            void OnUi(Keire::UiFrame& ui) override
            {
                ui.SetNextWindowSize({420, 320}, false);
                auto window = ui.BeginWindow("Overlay pointer fixture");
                if (ui.InvisibleButton("Canvas", {360, 240}, true))
                    ++m_CanvasClicks;
                const auto canvas = ui.LastItemRect();
                const Keire::UiPosition buttonPosition{canvas.Minimum.X + 20, canvas.Minimum.Y + 20};
                if (ui.OverlayIconButton("Create", Keire::UiIcon::Create,
                                         {.Position = buttonPosition, .Size = {32, 28}}))
                    ++m_OverlayClicks;
                auto& io = ImGui::GetIO();
                if (m_Frame == 0)
                    io.AddMousePosEvent(buttonPosition.X + 16, buttonPosition.Y + 14);
                if (m_Frame == 2 || m_Frame == 8)
                {
                    io.AddMouseButtonEvent(0, true);
                    if (m_Fast)
                        io.AddMouseButtonEvent(0, false);
                }
                if (m_Frame == 3 || m_Frame == 9)
                    io.AddMouseButtonEvent(0, false);
                if (m_Frame == 6)
                    io.AddMousePosEvent(canvas.Maximum.X - 20, canvas.Maximum.Y - 20);
                if (++m_Frame == 13)
                {
                    CHECK(m_OverlayClicks == 1);
                    CHECK(m_CanvasClicks == 1);
                    m_Checked = true;
                    Owner().RequestExit();
                }
            }

          private:
            bool m_Fast;
            bool& m_Checked;
            int m_Frame = 0;
            int m_CanvasClicks = 0;
            int m_OverlayClicks = 0;
        };
        Keire::ApplicationSpecification specification;
        specification.MainWindow.Visible = false;
        specification.TargetFrameRate = 0;
        specification.Ui.Mode = Keire::UiMode::Headless;
        specification.Ui.LayoutPath.clear();
        Keire::Application application(specification);
        (void)application.PushLayer(std::make_unique<OverlayInputLayer>(fastClick, checked));
        CHECK(application.Run() == 0);
        CHECK(checked);
    }
}

TEST_CASE("numeric field expressions evaluate complete finite arithmetic")
{
    CHECK(Keire::Detail::EvaluateNumericExpression("0.35 / 2").value() == doctest::Approx(0.175));
    CHECK(Keire::Detail::EvaluateNumericExpression("2 + 3 * 4").value() == doctest::Approx(14));
    CHECK(Keire::Detail::EvaluateNumericExpression("-(2 + 3) * 4").value() == doctest::Approx(-20));
    CHECK(Keire::Detail::EvaluateNumericExpression("1e-3 * 2").value() == doctest::Approx(0.002));
    for (const auto text : {"", "0.35 /", "1 / 0", "1garbage", "nan", "inf", "1e309", "(2 + 3", "2 ** 3"})
        CHECK_FALSE(Keire::Detail::EvaluateNumericExpression(text));
    CHECK_FALSE(Keire::Detail::EvaluateNumericExpression(std::string(257, '1')));
    CHECK_FALSE(Keire::Detail::EvaluateNumericExpression(std::string(34, '-') + "1"));
    CHECK(Keire::Detail::EvaluateNumericField<std::int64_t>("9223372036854775807").value() ==
          std::numeric_limits<std::int64_t>::max());
    CHECK_FALSE(Keire::Detail::EvaluateNumericField<std::int64_t>("9223372036854775808"));
    CHECK_FALSE(Keire::Detail::EvaluateNumericField<int>("3/2"));
    CHECK(Keire::Detail::EvaluateNumericField<int>("6/2").value() == 3);
}

TEST_CASE("sticky component titles pin and yield to the following section")
{
    CHECK_FALSE(Keire::Detail::StickySectionTop(120, 200, 100, 24));
    CHECK_FALSE(Keire::Detail::StickySectionTop(20, 80, 100, 24));
    CHECK(Keire::Detail::StickySectionTop(20, 200, 100, 24).value() == doctest::Approx(100));
    CHECK(Keire::Detail::StickySectionTop(20, 110, 100, 24).value() == doctest::Approx(86));
    CHECK_FALSE(Keire::Detail::StickySectionTop(20, 200, 100, 0));
}

TEST_CASE("numeric controls evaluate pasted expressions without publishing invalid prefixes")
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
    io.DisplaySize = {640, 480};
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    double value = 0.8;
    const double minimum = 0;
    const double maximum = 1;
    ImGuiID field = 0;
    const auto frame = [&](const bool activate)
    {
        Keire::Detail::UiBackendNewFrame();
        ImGui::SetNextWindowSize({400, 200});
        ImGui::Begin("Expression fixture");
        field = ImGui::GetID("Value");
        if (activate)
        {
            context->NavActivateId = field;
            context->NavActivateFlags = ImGuiActivateFlags_PreferInput;
        }
        (void)Keire::Detail::DrawNumericExpression(
            "Value", value, ImGuiDataType_Double, "%.6g", &minimum, &maximum, [&](auto& candidate)
            { return ImGui::DragScalar("Value", ImGuiDataType_Double, &candidate, 0.1F, &minimum, &maximum, "%.6g"); });
        ImGui::End();
        ImGui::EndFrame();
    };
    frame(false);
    frame(true);
    REQUIRE(ImGui::GetInputTextState(field));
    const auto paste = [&](const char* expression)
    {
        ImGui::GetInputTextState(field)->SelectAll();
        io.AddInputCharactersUTF8(expression);
        frame(false);
    };
    paste("0.35 / 2");
    CHECK(value == doctest::Approx(0.175));
    paste("0.9 / ");
    CHECK(value == doctest::Approx(0.175));
    paste("1/0");
    CHECK(value == doctest::Approx(0.175));
    paste("(0.35 + 0.05) / 2");
    CHECK(value == doctest::Approx(0.2));
    paste("4");
    CHECK(value == doctest::Approx(0.2));
}

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

TEST_CASE("sticky component titles survive scrolling inside an Inspector card")
{
    REQUIRE(SDL_SetHintWithPriority(SDL_HINT_VIDEO_DRIVER, "dummy", SDL_HINT_OVERRIDE));
    bool checked = false;
    class StickyCardLayer final : public Keire::Layer
    {
      public:
        explicit StickyCardLayer(bool& checked) : Layer("Sticky card"), m_Checked(checked) {}

      protected:
        void OnUi(Keire::UiFrame& ui) override
        {
            ImGui::SetNextWindowPos({0, 0});
            ui.SetNextWindowSize({400, 300});
            auto window = ui.BeginWindow("Inspector fixture");
            const auto start = ui.CursorScreenPosition().Y;
            ImGuiWindow* child = nullptr;
            {
                auto card = ui.BeginChild("Component card", {280, 140}, true);
                child = ImGui::GetCurrentWindow();
                ui.Text("Component");
                ImGui::Dummy({200, 600});
                ImGui::SetScrollY(60);
            }
            const auto before = child->DrawList->VtxBuffer.Size;
            ui.StickySectionTitle("Component", start, ui.CursorScreenPosition().Y);
            if (++m_Frame >= 3)
            {
                CHECK(ImGui::GetScrollY() == 0);
                CHECK(child->Scroll.y > 0);
                REQUIRE(child->DrawList->VtxBuffer.Size > before);
                CHECK(child->DrawList->VtxBuffer[before].pos.y == doctest::Approx(child->InnerClipRect.Min.y));
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
    (void)application.PushLayer(std::make_unique<StickyCardLayer>(checked));
    CHECK(application.Run() == 0);
    CHECK(checked);
}

TEST_CASE("interactive sticky headers preserve layout and route clicks above their component body")
{
    REQUIRE(SDL_SetHintWithPriority(SDL_HINT_VIDEO_DRIVER, "dummy", SDL_HINT_OVERRIDE));
    for (const bool scrollCard : {false, true})
    {
        INFO("Card scroll instead of Inspector scroll: " << scrollCard);
        bool checked = false;
        class HeaderLayer final : public Keire::Layer
        {
          public:
            HeaderLayer(bool& checked, bool scrollCard)
                : Layer("Pinned header input"), m_Checked(checked), m_ScrollCard(scrollCard)
            {
            }

          protected:
            void OnUi(Keire::UiFrame& ui) override
            {
                ImGui::SetNextWindowPos({0, 0});
                ui.SetNextWindowSize({400, 300});
                auto window = ui.BeginWindow("Pinned input fixture");
                const auto start = ui.CursorScreenPosition().Y;
                {
                    auto card = ui.BeginChild("Card", {280, m_ScrollCard ? 140.0F : 450.0F}, true);
                    ui.Text("Original header");
                    if (ui.Button("Covered body", {240, 180}))
                        ++m_BodyClicks;
                    ImGui::Dummy({200, 600});
                    if (m_ScrollCard)
                        ImGui::SetScrollY(60);
                }
                const auto cursor = ui.CursorScreenPosition();
                const auto maximum = ImGui::GetCurrentWindow()->DC.CursorMaxPos;
                ui.StickySectionHeader("Pinned", start, cursor.Y,
                                       [&]
                                       {
                                           if (ui.Button("Actions", {150, 22}))
                                               ++m_HeaderClicks;
                                           const auto minimum = ImGui::GetItemRectMin();
                                           m_Click = {minimum.x + 30, minimum.y + 10};
                                           ++m_HeaderFrames;
                                       });
                CHECK(ui.CursorScreenPosition().X == cursor.X);
                CHECK(ui.CursorScreenPosition().Y == cursor.Y);
                CHECK(ImGui::GetCurrentWindow()->DC.CursorMaxPos.x == maximum.x);
                CHECK(ImGui::GetCurrentWindow()->DC.CursorMaxPos.y == maximum.y);
                if (!m_ScrollCard)
                    ImGui::SetScrollY(60);
                auto& io = ImGui::GetIO();
                if (m_Frame == 3)
                {
                    REQUIRE(m_HeaderFrames > 0);
                    io.AddMousePosEvent(m_Click.x, m_Click.y);
                }
                if (m_Frame == 4)
                    io.AddMouseButtonEvent(0, true);
                if (m_Frame == 5)
                    io.AddMouseButtonEvent(0, false);
                if (m_Frame == 7)
                {
                    CHECK(m_HeaderClicks == 1);
                    CHECK(m_BodyClicks == 0);
                    CHECK_THROWS_AS(ui.StickySectionHeader("Throwing", start, cursor.Y,
                                                           [] { throw std::runtime_error("header callback"); }),
                                    std::runtime_error);
                    CHECK(ui.CursorScreenPosition().Y == cursor.Y);
                    ui.Text("Parent remains usable after callback failure");
                    m_Checked = true;
                    Owner().RequestExit();
                }
                ++m_Frame;
            }

          private:
            bool& m_Checked;
            bool m_ScrollCard;
            int m_Frame = 0, m_HeaderFrames = 0, m_HeaderClicks = 0, m_BodyClicks = 0;
            ImVec2 m_Click{};
        };
        Keire::ApplicationSpecification specification;
        specification.MainWindow.Visible = false;
        specification.TargetFrameRate = 0;
        specification.Ui.Mode = Keire::UiMode::Headless;
        specification.Ui.LayoutPath.clear();
        Keire::Application application(specification);
        (void)application.PushLayer(std::make_unique<HeaderLayer>(checked, scrollCard));
        CHECK(application.Run() == 0);
        CHECK(checked);
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
