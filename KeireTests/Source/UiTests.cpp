#include "Keire/Core.h"

#include "KeireInternal/UiInputInternal.h"
#include "KeireInternal/UiRenderBackendInternal.h"

#include <doctest/doctest.h>

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <limits>
#include <memory>
#include <ranges>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace
{
    void UseDummyVideoDriver() { REQUIRE(SDL_SetHintWithPriority(SDL_HINT_VIDEO_DRIVER, "dummy", SDL_HINT_OVERRIDE)); }

    Keire::ApplicationSpecification UiSpecification(const char* title, const Keire::UiMode mode)
    {
        Keire::ApplicationSpecification specification;
        specification.MainWindow.Title = title;
        specification.MainWindow.Visible = false;
        specification.TargetFrameRate = 0;
        specification.Ui.Mode = mode;
        specification.Ui.LayoutPath.clear();
        return specification;
    }

    Keire::ApplicationSpecification FontUiSpecification()
    {
        auto specification = UiSpecification("ui-headless", Keire::UiMode::Headless);
        const auto root = std::filesystem::current_path() / "KeireHubContent" / "Fonts";
        specification.Ui.Fonts = {{Keire::UiFontRole::Body, root / "Inter-Variable.ttf", 15.0F},
                                  {Keire::UiFontRole::Heading, root / "Inter-Variable.ttf", 20.0F},
                                  {Keire::UiFontRole::Icons, root / "MaterialSymbolsRounded-Subset.ttf", 20.0F}};
        return specification;
    }

    class DisabledUiLayer final : public Keire::Layer
    {
      public:
        explicit DisabledUiLayer(int& calls) : Layer("DisabledUiLayer"), m_Calls(calls) {}

      protected:
        void OnUpdate(const Keire::Time&) override { Owner().RequestExit(); }
        void OnUi(Keire::UiFrame&) override { ++m_Calls; }

      private:
        int& m_Calls;
    };

    class DisabledUiApplication final : public Keire::Application
    {
      public:
        explicit DisabledUiApplication(int& calls)
            : Application(UiSpecification("ui-disabled", Keire::UiMode::Disabled))
        {
            (void)PushLayer(std::make_unique<DisabledUiLayer>(calls));
        }
    };

    class HeadlessUiLayer final : public Keire::Layer
    {
      public:
        HeadlessUiLayer(std::vector<std::string>& order, bool& staleRejected, std::atomic<bool>& threadRejected)
            : Layer("HeadlessUiLayer"), m_Order(order), m_StaleRejected(staleRejected), m_ThreadRejected(threadRejected)
        {
        }

      protected:
        void OnUpdate(const Keire::Time&) override
        {
            m_Order.emplace_back("update");
            if (m_PreviousFrame)
            {
                try
                {
                    m_PreviousFrame->Text("outside frame");
                }
                catch (const std::logic_error&)
                {
                    m_StaleRejected = true;
                }
            }
        }

        void OnUi(Keire::UiFrame& ui) override
        {
            m_Order.emplace_back("ui");
            CHECK(Owner().UiEnabled());
            if (m_UiFrames == 0)
            {
                const auto& io = ImGui::GetIO();
                CHECK((io.ConfigFlags & ImGuiConfigFlags_NavEnableKeyboard) != 0);
                CHECK(io.ConfigDpiScaleFonts);
                const auto iconFont =
                    std::ranges::find_if(io.Fonts->Fonts, [](const ImFont* font)
                                         { return std::string_view(font->GetDebugName()) == "Keire.Icons"; });
                REQUIRE(iconFont != io.Fonts->Fonts.end());
                CHECK((*iconFont)->IsGlyphInFont(0xE3EC));
                CHECK((*iconFont)->IsGlyphInFont(0xE897));
                CHECK((*iconFont)->IsGlyphInFont(0xE89F));
                CHECK(Owner().CurrentUiTheme() == Keire::UiTheme::Dark);
                Owner().SetUiTheme(Keire::UiTheme::Light);
                CHECK(Owner().CurrentUiTheme() == Keire::UiTheme::Light);
            }

            {
                auto menuBar = ui.BeginMainMenuBar();
                if (menuBar)
                {
                    auto menu = ui.BeginMenu("File");
                    if (menu)
                        (void)ui.MenuItem("Exit");
                }
            }
            {
                auto toolbar = ui.BeginMainToolbar();
                if (toolbar)
                {
                    (void)ui.IconButton("TestNew", Keire::UiIcon::Create, false, {28.0F, 24.0F});
                    ui.SameLine();
                    ui.AlignNextItemGroup(0.5F, 98.0F);
                    (void)ui.IconButton("TestPlay", Keire::UiIcon::Play, false, {28.0F, 24.0F});
                    ui.SameLine();
                    (void)ui.IconButton("TestTranslateFallback", Keire::UiIcon::Translate, false, {28.0F, 24.0F});
                    ui.SameLine();
                    (void)ui.IconButton("TestCloseSymbol", Keire::UiIcon::Close, false, {28.0F, 24.0F});
                    ui.SameLine();
                    (void)ui.IconButton("TestCenteredMore", Keire::UiIcon::More, false, {28.0F, 24.0F});
                    const auto button = ui.LastItemRect();
                    const auto* mainMenu = ImGui::FindWindowByName("##MainMenuBar");
                    REQUIRE(mainMenu != nullptr);
                    CHECK(button.Minimum.Y >= mainMenu->Pos.y + mainMenu->Size.y - 1.0F);
                    const auto* toolbarWindow = ImGui::GetCurrentWindowRead();
                    REQUIRE(toolbarWindow != nullptr);
                    CHECK(button.Maximum.Y <= toolbarWindow->Pos.y + toolbarWindow->Size.y + 1.0F);
                }
            }
            {
                auto status = ui.BeginMainStatusBar();
                if (status)
                    ui.Text("Ready");
            }

            ui.SetNextWindowSize({320.0F, 200.0F});
            if (auto window = ui.BeginWindow("Headless UI"); window)
            {
                {
                    const auto heading = ui.PushFont(Keire::UiFontRole::Heading);
                    CHECK(heading);
                    ui.Text("Font roles stay behind the public UI facade");
                }
                const auto originalButton = ImGui::GetStyleColorVec4(ImGuiCol_Button);
                {
                    const auto color = ui.PushStyleColor(Keire::UiStyleColorRole::Button, {0.1F, 0.2F, 0.3F, 0.4F});
                    CHECK(color);
                    CHECK(ImGui::GetStyleColorVec4(ImGuiCol_Button).x == doctest::Approx(0.1F));
                    CHECK(ImGui::GetStyleColorVec4(ImGuiCol_Button).w == doctest::Approx(0.4F));
                }
                CHECK(ImGui::GetStyleColorVec4(ImGuiCol_Button).x == doctest::Approx(originalButton.x));
                const auto originalPadding = ImGui::GetStyle().WindowPadding;
                {
                    const auto padding =
                        ui.PushStyleVariable(Keire::UiStyleVariable::WindowPadding, Keire::UiSize{3.0F, 4.0F});
                    CHECK(padding);
                    CHECK(ImGui::GetStyle().WindowPadding.x == doctest::Approx(3.0F));
                    CHECK(ImGui::GetStyle().WindowPadding.y == doctest::Approx(4.0F));
                }
                CHECK(ImGui::GetStyle().WindowPadding.x == doctest::Approx(originalPadding.x));
                const auto originalWindowRounding = ImGui::GetStyle().WindowRounding;
                {
                    const auto rounding = ui.PushStyleVariable(Keire::UiStyleVariable::WindowRounding, 6.0F);
                    CHECK(rounding);
                    CHECK(ImGui::GetStyle().WindowRounding == doctest::Approx(6.0F));
                }
                CHECK(ImGui::GetStyle().WindowRounding == doctest::Approx(originalWindowRounding));
                CHECK_THROWS_AS((void)ui.PushStyleColor(static_cast<Keire::UiStyleColorRole>(255), {}),
                                std::invalid_argument);
                CHECK_THROWS_AS((void)ui.PushStyleVariable(Keire::UiStyleVariable::WindowPadding, 1.0F),
                                std::invalid_argument);
                CHECK_THROWS_AS(
                    (void)ui.PushStyleVariable(Keire::UiStyleVariable::WindowRounding, Keire::UiSize{1.0F, 1.0F}),
                    std::invalid_argument);
                CHECK_FALSE(ui.PushFont(Keire::UiFontRole::Monospace));
                CHECK_THROWS_AS((void)ui.PushFont(static_cast<Keire::UiFontRole>(255)), std::invalid_argument);
                ui.Text("Public Kéire UI facade");
                ui.TextWrapped("Wrapped public UI text remains inside the available content region.");
                ui.TextColoredWrapped({0.8F, 0.9F, 1.0F, 1.0F}, "Colored wrapped text");
                const auto cursor = ui.CursorPosition();
                const auto screenCursor = ui.CursorScreenPosition();
                ui.SetCursorPosition(cursor);
                ui.SetCursorScreenPosition(screenCursor);
                const std::array cursorMappings{
                    std::pair{Keire::UiCursorShape::TextInput, ImGuiMouseCursor_TextInput},
                    std::pair{Keire::UiCursorShape::Move, ImGuiMouseCursor_ResizeAll},
                    std::pair{Keire::UiCursorShape::ResizeHorizontal, ImGuiMouseCursor_ResizeEW},
                    std::pair{Keire::UiCursorShape::ResizeVertical, ImGuiMouseCursor_ResizeNS},
                    std::pair{Keire::UiCursorShape::ResizeNorthwestSoutheast, ImGuiMouseCursor_ResizeNWSE},
                    std::pair{Keire::UiCursorShape::ResizeNortheastSouthwest, ImGuiMouseCursor_ResizeNESW},
                    std::pair{Keire::UiCursorShape::Hand, ImGuiMouseCursor_Hand},
                    std::pair{Keire::UiCursorShape::NotAllowed, ImGuiMouseCursor_NotAllowed},
                };
                for (const auto& [shape, expected] : cursorMappings)
                {
                    ui.SetCursorShape(shape);
                    CHECK(ImGui::GetMouseCursor() == expected);
                }
                ui.SetCursorShape(Keire::UiCursorShape::Default);
                CHECK(ImGui::GetMouseCursor() == ImGuiMouseCursor_Arrow);
                CHECK_THROWS_AS(ui.SetCursorShape(static_cast<Keire::UiCursorShape>(255)), std::invalid_argument);
                ui.SetNextItemWidth(160.0F);
                ui.DrawLine({10.0F, 10.0F}, {30.0F, 30.0F}, {1.0F, 0.0F, 0.0F, 1.0F}, 2.0F);
                ui.DrawCircle({40.0F, 40.0F}, 8.0F, {0.0F, 1.0F, 0.0F, 1.0F});
                ui.DrawFilledCircle({60.0F, 40.0F}, 6.0F, {0.0F, 0.0F, 1.0F, 0.5F});
                ui.DrawRectangle({{70.0F, 30.0F}, {90.0F, 50.0F}}, {1.0F, 1.0F, 0.0F, 1.0F});
                ui.DrawFilledRectangle({{95.0F, 30.0F}, {115.0F, 50.0F}}, {1.0F, 0.0F, 1.0F, 0.5F});
                ui.DrawTriangle({120.0F, 50.0F}, {130.0F, 30.0F}, {140.0F, 50.0F}, {0.0F, 1.0F, 1.0F, 1.0F});
                ui.DrawFilledTriangle({145.0F, 50.0F}, {155.0F, 30.0F}, {165.0F, 50.0F}, {1.0F, 0.5F, 0.0F, 1.0F});
                ui.DrawOverlayText({170.0F, 30.0F}, {1.0F, 1.0F, 1.0F, 1.0F}, "overlay");
                const std::array<std::byte, 4> imagePixels{std::byte{0x33}, std::byte{0x66}, std::byte{0x99},
                                                           std::byte{0xFF}};
                const auto image = ui.CreateImage(1, 1, imagePixels);
                ui.DrawImage(image, {{170.0F, 52.0F}, {186.0F, 68.0F}});
                CHECK_THROWS_AS(ui.DrawImage(Keire::Ref<Keire::UiImage>{}, {{170.0F, 52.0F}, {186.0F, 68.0F}}),
                                std::invalid_argument);
                CHECK_THROWS_AS(ui.DrawImage(image, {{170.0F, 52.0F}, {170.0F, 68.0F}}), std::invalid_argument);
                {
                    const auto content = ui.ContentRect();
                    const Keire::UiItemRect clipRectangle{{content.Minimum.X + 4.0F, content.Minimum.Y + 4.0F},
                                                          {content.Minimum.X + 28.0F, content.Minimum.Y + 28.0F}};
                    const auto clip = ui.PushClipRect(clipRectangle);
                    const auto& activeClip = ImGui::GetWindowDrawList()->_ClipRectStack.back();
                    CHECK(activeClip.x == doctest::Approx(clipRectangle.Minimum.X));
                    CHECK(activeClip.y == doctest::Approx(clipRectangle.Minimum.Y));
                    CHECK(activeClip.z == doctest::Approx(clipRectangle.Maximum.X));
                    CHECK(activeClip.w == doctest::Approx(clipRectangle.Maximum.Y));
                    ui.DrawLine({0.0F, 0.0F}, {80.0F, 80.0F}, {1.0F, 1.0F, 1.0F, 1.0F});
                }
                CHECK_THROWS_AS((void)ui.PushClipRect({{0.0F, 0.0F}, {0.0F, 1.0F}}), std::invalid_argument);
                CHECK_THROWS_AS((void)ui.PushClipRect({{0.0F, 0.0F}, {std::numeric_limits<float>::infinity(), 1.0F}}),
                                std::invalid_argument);
                CHECK_THROWS_AS(ui.DrawCircle({0.0F, 0.0F}, -1.0F, {}), std::invalid_argument);
                bool checked = false;
                (void)ui.Checkbox("Check", checked);
                float value = 0.5F;
                (void)ui.SliderFloat("Value", value, 0.0F, 1.0F);
                int fixedValue = 14;
                CHECK_FALSE(ui.SliderInt("Fixed Value", fixedValue, 14, 14));
                CHECK(fixedValue == 14);
                CHECK_THROWS_AS((void)ui.SliderInt("Reversed Value", fixedValue, 15, 14), std::invalid_argument);
                Keire::Vector3 vector{1.0F, 2.0F, 3.0F};
                CHECK_FALSE(ui.DragVector3("Vector", vector));
                CHECK_THROWS_AS((void)ui.DragVector3("", vector), std::invalid_argument);
                std::string text = "editable";
                (void)ui.InputText("Text", text);
                (void)ui.InputTextWithHint("Search", "Search items", text);
                const auto item = ui.LastItemState();
                CHECK_FALSE(item.DoubleClicked);
                ui.ProgressBar(0.5F, {120.0F, 4.0F}, "50%");
                if (auto table = ui.BeginTable("ActionBindings", 2); table)
                {
                    ui.TableSetupColumn("Action", Keire::UiTableColumnSizing::Stretch, 1.0F);
                    ui.TableSetupColumn("Binding", Keire::UiTableColumnSizing::Fixed, 100.0F);
                    ui.TableHeaderRow();
                    ui.TableNextRow();
                    CHECK(ui.TableNextColumn());
                    ui.Text("Action");
                    CHECK(ui.TableNextColumn());
                    ui.Text("Binding");
                }
                const Keire::UiTableOptions gridOptions{.Sizing = Keire::UiTableSizing::Equal,
                                                        .Borders = false,
                                                        .Resizable = false,
                                                        .RowBackground = false,
                                                        .PersistSettings = false};
                const std::size_t gridColumns = m_UiFrames == 0 ? 6 : 2;
                if (auto table = ui.BeginTable("ResponsiveGrid", gridColumns, gridOptions); table)
                {
                    ui.TableNextRow();
                    for (std::size_t column = 0; column < gridColumns; ++column)
                    {
                        CHECK(ui.TableNextColumn());
                        ui.Text("Cell");
                    }
                }
                float leading = 100.0F;
                float trailing = 100.0F;
                (void)ui.Splitter(Keire::UiAxis::Horizontal, "EditorSplitter", leading, trailing);
                (void)ui.Shortcut({Keire::UiKey::S, true});
                (void)ui.Shortcut({.Key = Keire::UiKey::Z, .Primary = true, .Global = true});
                if (auto disabled = ui.BeginDisabled(); disabled)
                {
                    (void)ui.Button("Disabled");
                }

                const std::array<std::byte, 1> dragPayload{};
                CHECK_THROWS_AS(ui.SetDragPayload("KEIRE_TEST", dragPayload), std::logic_error);
                std::vector<std::byte> acceptedPayload;
                CHECK_THROWS_AS((void)ui.AcceptDragPayload("KEIRE_TEST", acceptedPayload), std::logic_error);

                (void)ui.Button("Overlay layout anchor", {120.0F, 40.0F});
                const auto anchor = ui.LastItemRect();
                (void)ui.OverlayIconButton("HeadlessOverlay", Keire::UiIcon::Play,
                                           {.Position = anchor.Minimum,
                                            .Size = {28.0F, 28.0F},
                                            .Tooltip = "Overlay without layout mutation",
                                            .Selected = true});
                ui.DrawOverlayIcon(Keire::UiIcon::Notifications, {anchor.Maximum.X - 24.0F, anchor.Minimum.Y + 4.0F},
                                   {0.35F, 0.65F, 1.0F, 1.0F});
            }

            std::thread worker(
                [&ui, this]
                {
                    try
                    {
                        ui.Text("wrong thread");
                    }
                    catch (const std::logic_error&)
                    {
                        m_ThreadRejected.store(true, std::memory_order_release);
                    }
                });
            worker.join();

            m_PreviousFrame = &ui;
            if (++m_UiFrames == 2)
                Owner().RequestExit(7);
        }

      private:
        std::vector<std::string>& m_Order;
        bool& m_StaleRejected;
        std::atomic<bool>& m_ThreadRejected;
        Keire::UiFrame* m_PreviousFrame = nullptr;
        int m_UiFrames = 0;
    };

    class HeadlessUiApplication final : public Keire::Application
    {
      public:
        HeadlessUiApplication(std::vector<std::string>& order, bool& staleRejected, std::atomic<bool>& threadRejected)
            : Application(FontUiSpecification())
        {
            (void)PushLayer(std::make_unique<HeadlessUiLayer>(order, staleRejected, threadRejected));
        }
    };

    class ThrowingUiLayer final : public Keire::Layer
    {
      public:
        ThrowingUiLayer() : Layer("ThrowingUiLayer") {}

      protected:
        void OnUi(Keire::UiFrame& ui) override
        {
            auto window = ui.BeginWindow("Throwing UI");
            if (window)
                ui.Text("scope must unwind");
            throw std::runtime_error("ui failure");
        }
    };

    class ThrowingUiApplication final : public Keire::Application
    {
      public:
        ThrowingUiApplication() : Application(UiSpecification("ui-throw", Keire::UiMode::Headless))
        {
            (void)PushLayer(std::make_unique<ThrowingUiLayer>());
        }
    };

    class ModalScopeUiLayer final : public Keire::Layer
    {
      public:
        explicit ModalScopeUiLayer(bool& bodyInsideModal)
            : Layer("ModalScopeUiLayer"), m_BodyInsideModal(bodyInsideModal)
        {
        }

      protected:
        void OnUi(Keire::UiFrame& ui) override
        {
            auto host = ui.BeginWindow("Modal Host");
            if (host)
            {
                ui.OpenPopup("Lifetime Modal");
                Keire::UiWindowOptions options;
                options.NoTitleBar = true;
                auto dialog = ui.BeginPopupModal("Lifetime Modal", nullptr, options, false);
                if (dialog)
                {
                    const auto* current = ImGui::GetCurrentWindowRead();
                    m_BodyInsideModal = current != nullptr &&
                                        (current->Flags & ImGuiWindowFlags_Modal) == ImGuiWindowFlags_Modal &&
                                        (current->Flags & ImGuiWindowFlags_NoTitleBar) == ImGuiWindowFlags_NoTitleBar &&
                                        (current->Flags & ImGuiWindowFlags_AlwaysAutoResize) == 0;
                    ui.Text("Modal body");
                }
            }
            Owner().RequestExit();
        }

      private:
        bool& m_BodyInsideModal;
    };

    class ModalScopeUiApplication final : public Keire::Application
    {
      public:
        explicit ModalScopeUiApplication(bool& bodyInsideModal)
            : Application(UiSpecification("ui-modal-scope", Keire::UiMode::Headless))
        {
            (void)PushLayer(std::make_unique<ModalScopeUiLayer>(bodyInsideModal));
        }
    };

    class AddedUiLayer final : public Keire::Layer
    {
      public:
        explicit AddedUiLayer(std::vector<std::string>& order) : Layer("AddedUiLayer"), m_Order(order) {}

      protected:
        void OnAttach() override { m_Order.emplace_back("added-attach"); }
        void OnUi(Keire::UiFrame&) override
        {
            m_Order.emplace_back("added-ui");
            Owner().RequestExit();
        }

      private:
        std::vector<std::string>& m_Order;
    };

    class MutatingUiLayer final : public Keire::Layer
    {
      public:
        MutatingUiLayer(Keire::LayerId& self, std::vector<std::string>& order)
            : Layer("MutatingUiLayer"), m_Self(self), m_Order(order)
        {
        }

      protected:
        void OnUi(Keire::UiFrame&) override
        {
            m_Order.emplace_back("mutator-ui");
            (void)Owner().PushOverlay(std::make_unique<AddedUiLayer>(m_Order));
            CHECK(Owner().RemoveLayer(m_Self));
        }
        void OnDetach() noexcept override { m_Order.emplace_back("mutator-detach"); }

      private:
        Keire::LayerId& m_Self;
        std::vector<std::string>& m_Order;
    };

    class MutatingUiApplication final : public Keire::Application
    {
      public:
        explicit MutatingUiApplication(std::vector<std::string>& order)
            : Application(UiSpecification("ui-mutation", Keire::UiMode::Headless))
        {
            m_Mutator = PushLayer(std::make_unique<MutatingUiLayer>(m_Mutator, order));
        }

      private:
        Keire::LayerId m_Mutator;
    };

    class LayoutUiLayer final : public Keire::Layer
    {
      public:
        LayoutUiLayer() : Layer("LayoutUiLayer") {}

      protected:
        void OnUi(Keire::UiFrame& ui) override
        {
            auto window = ui.BeginWindow("Persisted Window");
            if (window)
                ui.Text("Persisted");
            Owner().RequestExit();
        }
    };

    class LayoutUiApplication final : public Keire::Application
    {
      public:
        explicit LayoutUiApplication(const std::filesystem::path& path) : Application(BuildSpecification(path))
        {
            (void)PushLayer(std::make_unique<LayoutUiLayer>());
        }

      private:
        static Keire::ApplicationSpecification BuildSpecification(const std::filesystem::path& path)
        {
            auto specification = UiSpecification("ui-layout", Keire::UiMode::Headless);
            specification.Ui.LayoutPath = path;
            return specification;
        }
    };

    class WorkspaceUiLayer final : public Keire::Layer
    {
      public:
        explicit WorkspaceUiLayer(const bool create) : Layer("WorkspaceUiLayer"), m_Create(create) {}

      protected:
        void OnAttach() override
        {
            m_Panel = Owner().GetUiWorkspace().RegisterPanel({"test.panel", "Test Panel"});
            CHECK_FALSE(m_Panel.Locked());
            m_Panel.SetLocked(true);
            CHECK(m_Panel.Locked());
            m_Panel.SetLocked(false);
            CHECK_FALSE(m_Panel.Locked());
        }

        void OnUi(Keire::UiFrame& ui) override
        {
            auto& workspace = Owner().GetUiWorkspace();
            if (m_Create)
            {
                CHECK(workspace.Layouts().size() == 1);
                CHECK(workspace.Themes().size() == 3);
                workspace.SaveLayoutAs("Focused Work");
                auto theme = workspace.ThemeDefinition(workspace.ActiveTheme());
                theme.Accent = {0.12F, 0.44F, 0.91F, 1.0F};
                const auto id = workspace.SaveThemeAs("Test Theme", theme);
                CHECK(workspace.ActiveTheme() == id);
                m_Panel.SetVisible(false);
            }
            else
            {
                CHECK(workspace.Layouts().size() == 2);
                CHECK(workspace.Themes().size() == 4);
                CHECK(workspace.Layouts().back().Name == "Focused Work");
                CHECK(workspace.Layouts().back().Active);
                CHECK(workspace.Themes().back().Name == "Test Theme");
                CHECK(workspace.Themes().back().Active);
                CHECK_FALSE(m_Panel.Visible());
                m_Panel.RequestFocus();
                CHECK(m_Panel.Visible());
                CHECK_THROWS_AS(workspace.DeleteLayout(Keire::UiLayoutId(1)), std::invalid_argument);
                CHECK_THROWS_AS(workspace.UpdateTheme(Keire::UiThemeId(1), {}), std::invalid_argument);
                CHECK_THROWS_AS(workspace.ShowImportLayoutDialog(), Keire::UiError);

                std::atomic<bool> rejected{false};
                std::thread worker(
                    [&workspace, &rejected]
                    {
                        try
                        {
                            (void)workspace.Layouts();
                        }
                        catch (const std::logic_error&)
                        {
                            rejected.store(true, std::memory_order_release);
                        }
                    });
                worker.join();
                CHECK(rejected.load(std::memory_order_acquire));

                rejected.store(false, std::memory_order_release);
                std::thread focusWorker(
                    [this, &rejected]
                    {
                        try
                        {
                            m_Panel.RequestFocus();
                        }
                        catch (const std::logic_error&)
                        {
                            rejected.store(true, std::memory_order_release);
                        }
                    });
                focusWorker.join();
                CHECK(rejected.load(std::memory_order_acquire));
                CHECK(m_Panel.Visible());

                const auto malformed = std::filesystem::temp_directory_path() / "keire-malformed-theme.keiretheme";
                {
                    std::ofstream output(malformed, std::ios::binary | std::ios::trunc);
                    output
                        << R"({"schemaVersion":1,"kind":"theme","name":"Bad","colors":{},"metrics":{},"unexpected":true})";
                }
                CHECK_THROWS_AS(workspace.ImportTheme(malformed), Keire::UiError);
                std::filesystem::remove(malformed);
            }

            if (auto panel = ui.BeginPanel(m_Panel); panel)
                ui.Text("Workspace panel");
            Owner().RequestExit();
        }

      private:
        Keire::UiPanelRegistration m_Panel;
        bool m_Create = false;
    };

    class WorkspaceUiApplication final : public Keire::Application
    {
      public:
        WorkspaceUiApplication(const std::filesystem::path& directory, const bool create)
            : Application(BuildSpecification(directory))
        {
            (void)PushLayer(std::make_unique<WorkspaceUiLayer>(create));
        }

      private:
        static Keire::ApplicationSpecification BuildSpecification(const std::filesystem::path& directory)
        {
            auto specification = UiSpecification("ui-workspace", Keire::UiMode::Headless);
            specification.Ui.Workspace.Enabled = true;
            specification.Ui.Workspace.DirectoryOverride = directory;
            specification.Ui.Workspace.BuildFactoryLayout = [](Keire::UiLayoutBuilder& layout)
            { layout.Dock("test.panel", layout.Root()); };
            return specification;
        }
    };

    struct ResponsiveDockWidths
    {
        float InitialLeft = 0.0F;
        float InitialCenter = 0.0F;
        float ResizedLeft = 0.0F;
        float ResizedCenter = 0.0F;
        float RestoredLeft = 0.0F;
        float RestoredCenter = 0.0F;
    };

    class ResponsiveWorkspaceLayer final : public Keire::Layer
    {
      public:
        explicit ResponsiveWorkspaceLayer(ResponsiveDockWidths& widths)
            : Layer("ResponsiveWorkspaceLayer"), m_Widths(widths)
        {
        }

      protected:
        void OnAttach() override
        {
            auto& workspace = Owner().GetUiWorkspace();
            m_Left = workspace.RegisterPanel({"responsive.left", "Left"});
            m_Center = workspace.RegisterPanel({"responsive.center", "Center"});
        }

        void OnUi(Keire::UiFrame& ui) override
        {
            const float left = DrawPanel(ui, m_Left);
            const float center = DrawPanel(ui, m_Center);
            if (m_Frame == 1)
            {
                m_Widths.InitialLeft = left;
                m_Widths.InitialCenter = center;
                Owner().MainWindow()->SetSize({640, 360});
            }
            else if (m_Frame == 2)
            {
                m_Widths.ResizedLeft = left;
                m_Widths.ResizedCenter = center;
                Owner().MainWindow()->SetSize({1280, 720});
            }
            else if (m_Frame == 3)
            {
                m_Widths.RestoredLeft = left;
                m_Widths.RestoredCenter = center;
                Owner().RequestExit();
            }
            ++m_Frame;
        }

      private:
        static float DrawPanel(Keire::UiFrame& ui, Keire::UiPanelRegistration& registration)
        {
            if (auto panel = ui.BeginPanel(registration); panel)
            {
                const auto* node = ImGui::DockBuilderGetNode(ImGui::GetWindowDockID());
                return node ? node->Size.x : 0.0F;
            }
            return 0.0F;
        }

        ResponsiveDockWidths& m_Widths;
        Keire::UiPanelRegistration m_Left;
        Keire::UiPanelRegistration m_Center;
        int m_Frame = 0;
    };

    class ResponsiveWorkspaceApplication final : public Keire::Application
    {
      public:
        explicit ResponsiveWorkspaceApplication(ResponsiveDockWidths& widths) : Application(BuildSpecification())
        {
            (void)PushLayer(std::make_unique<ResponsiveWorkspaceLayer>(widths));
        }

      private:
        static Keire::ApplicationSpecification BuildSpecification()
        {
            auto specification = UiSpecification("responsive-ui-workspace", Keire::UiMode::Headless);
            specification.MainWindow.Width = 1280;
            specification.MainWindow.Height = 720;
            specification.Ui.Workspace.Enabled = true;
            specification.Ui.Workspace.Ephemeral = true;
            specification.Ui.Workspace.BuildFactoryLayout = [](Keire::UiLayoutBuilder& layout)
            {
                const auto left = layout.Split(layout.Root(), Keire::UiDockDirection::Left, 0.25F);
                layout.Dock("responsive.left", left.Near);
                layout.Dock("responsive.center", left.Far);
            };
            return specification;
        }
    };
} // namespace

TEST_CASE("Disabled UI preserves the existing application lifecycle")
{
    UseDummyVideoDriver();
    int calls = 0;
    DisabledUiApplication application(calls);
    CHECK(application.Run() == 0);
    CHECK(calls == 0);
    CHECK_FALSE(application.UiEnabled());
    CHECK(application.UiCapture().Pointer == false);
}

TEST_CASE("Headless UI runs after updates and rejects stale and cross-thread use")
{
    UseDummyVideoDriver();
    std::vector<std::string> order;
    bool staleRejected = false;
    std::atomic<bool> threadRejected{false};
    HeadlessUiApplication application(order, staleRejected, threadRejected);

    CHECK(application.Run() == 7);
    REQUIRE(order.size() == 4);
    CHECK(order[0] == "update");
    CHECK(order[1] == "ui");
    CHECK(order[2] == "update");
    CHECK(order[3] == "ui");
    CHECK(staleRejected);
    CHECK(threadRejected.load(std::memory_order_acquire));
}

TEST_CASE("UI scope cleanup preserves callback exceptions and permits a later runtime")
{
    UseDummyVideoDriver();
    ThrowingUiApplication throwing;
    CHECK_THROWS_WITH_AS((void)throwing.Run(), "ui failure", std::runtime_error);

    int calls = 0;
    DisabledUiApplication replacement(calls);
    CHECK(replacement.Run() == 0);
}

TEST_CASE("UI popup scopes keep modal bodies inside the popup window")
{
    UseDummyVideoDriver();
    bool bodyInsideModal = false;
    ModalScopeUiApplication application(bodyInsideModal);

    CHECK(application.Run() == 0);
    CHECK(bodyInsideModal);
}

TEST_CASE("Layer mutations requested during UI traversal remain deferred")
{
    UseDummyVideoDriver();
    std::vector<std::string> order;
    MutatingUiApplication application(order);
    CHECK(application.Run() == 0);
    REQUIRE(order.size() == 4);
    CHECK(order[0] == "mutator-ui");
    CHECK(order[1] == "added-attach");
    CHECK(order[2] == "mutator-detach");
    CHECK(order[3] == "added-ui");
}

TEST_CASE("Kéire owns bounded UI layout persistence")
{
    UseDummyVideoDriver();
    const auto layout = std::filesystem::temp_directory_path() / "keire-ui-layout-test.ini";
    std::filesystem::remove(layout);
    {
        LayoutUiApplication application(layout);
        CHECK(application.Run() == 0);
    }
    CHECK(std::filesystem::exists(layout));
    CHECK(std::filesystem::file_size(layout) <= 1024U * 1024U);

    {
        std::ofstream oversized(layout, std::ios::binary | std::ios::trunc);
        oversized.seekp(std::streamoff{1024} * 1024);
        oversized.put('x');
    }
    LayoutUiApplication oversized(layout);
    CHECK_THROWS_WITH_AS((void)oversized.Run(),
                         "UI operation 'LoadLayout' failed: layout file exceeds the 1 MiB safety limit: " +
                             layout.string(),
                         Keire::UiError);
    std::filesystem::remove(layout);
}

TEST_CASE("UI workspace persists named layouts, themes, and registered panel visibility")
{
    UseDummyVideoDriver();
    const auto directory = std::filesystem::temp_directory_path() / "keire-ui-workspace-test";
    std::filesystem::remove_all(directory);
    {
        WorkspaceUiApplication application(directory, true);
        CHECK(application.Run() == 0);
    }
    CHECK(std::filesystem::exists(directory / "catalog.json"));
    CHECK(std::filesystem::exists(directory / "session.keirelayout"));
    CHECK(std::filesystem::exists(directory / "Layouts" / "100.keirelayout"));
    CHECK(std::filesystem::exists(directory / "Themes" / "100.keiretheme"));
    {
        WorkspaceUiApplication application(directory, false);
        CHECK(application.Run() == 0);
    }
    std::filesystem::remove_all(directory);
}

TEST_CASE("UI workspace preserves dock proportions when its host window is resized")
{
    UseDummyVideoDriver();
    ResponsiveDockWidths widths;
    ResponsiveWorkspaceApplication application(widths);
    CHECK(application.Run() == 0);

    REQUIRE(widths.InitialLeft > 0.0F);
    REQUIRE(widths.InitialCenter > 0.0F);
    REQUIRE(widths.ResizedLeft > 0.0F);
    REQUIRE(widths.ResizedCenter > 0.0F);
    REQUIRE(widths.RestoredLeft > 0.0F);
    REQUIRE(widths.RestoredCenter > 0.0F);
    const float initialRatio = widths.InitialLeft / (widths.InitialLeft + widths.InitialCenter);
    const float resizedRatio = widths.ResizedLeft / (widths.ResizedLeft + widths.ResizedCenter);
    const float restoredRatio = widths.RestoredLeft / (widths.RestoredLeft + widths.RestoredCenter);
    CHECK(initialRatio == doctest::Approx(0.25F).epsilon(0.02));
    CHECK(resizedRatio == doctest::Approx(initialRatio).epsilon(0.02));
    CHECK(restoredRatio == doctest::Approx(initialRatio).epsilon(0.02));
}

TEST_CASE("UI workspace and legacy single-file persistence are mutually exclusive")
{
    UseDummyVideoDriver();
    auto specification = UiSpecification("ui-workspace-conflict", Keire::UiMode::Headless);
    specification.Ui.LayoutPath = "legacy.ini";
    specification.Ui.Workspace.Enabled = true;

    class ConflictingUiApplication final : public Keire::Application
    {
      public:
        explicit ConflictingUiApplication(Keire::ApplicationSpecification value) : Application(std::move(value)) {}
    };

    ConflictingUiApplication application(std::move(specification));
    CHECK_THROWS_WITH_AS((void)application.Run(),
                         "UiSpecification::LayoutPath and Workspace cannot be enabled together.",
                         std::invalid_argument);
}
TEST_CASE("UI item rectangles report bounded screen-space geometry")
{
    const Keire::UiItemRect rect{{10.0F, 20.0F}, {110.0F, 70.0F}};
    CHECK(rect.Size() == (Keire::UiSize{100.0F, 50.0F}));
    CHECK(rect.Contains({10.0F, 20.0F}));
    CHECK(rect.Contains({55.0F, 45.0F}));
    CHECK_FALSE(rect.Contains({9.0F, 45.0F}));
    CHECK_FALSE(rect.Contains({55.0F, 71.0F}));
}

TEST_CASE("external retained text focus requests the Dear ImGui platform text-input lifecycle")
{
    ImGuiContext* previous = ImGui::GetCurrentContext();
    ImGuiContext* context = ImGui::CreateContext();
    REQUIRE(context);
    CHECK_FALSE(context->PlatformImeData.WantTextInput);

    Keire::Detail::UiBackendRequestTextInput();
    CHECK(context->PlatformImeData.WantTextInput);

    ImGui::DestroyContext(context);
    ImGui::SetCurrentContext(previous);
}

TEST_CASE("lost-device UI abandonment preserves CPU textures without releasing invalid GPU handles")
{
    ImGuiContext* context = ImGui::CreateContext();
    ImGui::SetCurrentContext(context);

    ImGuiIO& io = ImGui::GetIO();
    io.BackendRendererUserData = ImGui::MemAlloc(128);
    io.BackendRendererName = "imgui_impl_sdlgpu3";
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_RendererHasTextures |
                       ImGuiBackendFlags_RendererHasViewports;

    auto texture = std::make_unique<ImTextureData>();
    texture->Create(ImTextureFormat_RGBA32, 2, 2);
    std::ranges::fill(std::span(texture->Pixels, 16), static_cast<unsigned char>(0xA5));
    texture->UpdateRect = {1U, 0U, 1U, 2U};
    texture->Updates.push_back(texture->UpdateRect);
    const ImTextureID logicalId = static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(texture.get()));
    texture->SetTexID(logicalId);
    texture->BackendUserData = reinterpret_cast<void*>(static_cast<std::uintptr_t>(0xFEEDBEEF));
    texture->SetStatus(ImTextureStatus_WantUpdates);
    ImGui::RegisterUserTexture(texture.get());
    ImGuiPlatformIO& platform = ImGui::GetPlatformIO();
    platform.Textures.push_back(texture.get());
    ImGuiViewport* mainViewport = ImGui::GetMainViewport();
    REQUIRE(mainViewport);
    mainViewport->RendererUserData = reinterpret_cast<void*>(0xBADF00D);

    Keire::Detail::AbandonLostGpuBackend(context);
    Keire::Detail::AbandonLostGpuBackend(context);

    CHECK(io.BackendRendererUserData == nullptr);
    CHECK(io.BackendRendererName == nullptr);
    CHECK((io.BackendFlags & (ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_RendererHasTextures |
                              ImGuiBackendFlags_RendererHasViewports)) == 0);
    CHECK(texture->GetTexID() == logicalId);
    CHECK(texture->BackendUserData == reinterpret_cast<void*>(static_cast<std::uintptr_t>(0xFEEDBEEF)));
    CHECK(texture->Status == ImTextureStatus_WantUpdates);
    CHECK(texture->UpdateRect.x == 1U);
    CHECK(texture->UpdateRect.y == 0U);
    CHECK(texture->UpdateRect.w == 1U);
    CHECK(texture->UpdateRect.h == 2U);
    REQUIRE(texture->Updates.Size == 1);
    CHECK(std::ranges::all_of(std::span(texture->Pixels, 16), [](const unsigned char value) { return value == 0xA5; }));
    CHECK(mainViewport->RendererUserData == nullptr);
    CHECK(platform.Renderer_CreateWindow == nullptr);

    platform.Textures.clear();
    ImGui::UnregisterUserTexture(texture.get());
    texture.reset();
    ImGui::DestroyContext(context);
}

TEST_CASE("UI context access is recursive for the owner and excludes backend work on another thread")
{
    ImGuiContext* previous = ImGui::GetCurrentContext();
    ImGuiContext* context = ImGui::CreateContext();
    auto access = std::make_shared<Keire::Detail::UiContextAccess>(context);

    auto ownerLock = access->Acquire();
    auto nestedOwnerLock = access->Acquire();
    CHECK(ImGui::GetCurrentContext() == context);

    std::promise<void> workerStarted;
    std::promise<void> workerAcquired;
    auto workerStartedFuture = workerStarted.get_future();
    auto workerAcquiredFuture = workerAcquired.get_future();
    std::jthread worker(
        [&]
        {
            workerStarted.set_value();
            const auto workerLock = access->Acquire();
            workerAcquired.set_value();
        });

    workerStartedFuture.wait();
    CHECK(workerAcquiredFuture.wait_for(std::chrono::milliseconds(25)) == std::future_status::timeout);
    nestedOwnerLock = {};
    ownerLock = {};
    CHECK(workerAcquiredFuture.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
    worker.join();

    const auto cleanupLock = access->Acquire();
    ImGui::DestroyContext(context);
    access->Invalidate(previous);
}

TEST_CASE("UI backend context access fails closed when its live binding is absent")
{
    const std::shared_ptr<Keire::Detail::UiContextAccess> missing;
    CHECK_THROWS_WITH_AS((void)Keire::Detail::AcquireRequiredUiContext(missing, "missing live UI context"),
                         "missing live UI context", std::logic_error);
}

TEST_CASE("distinct UI context access instances serialize the process-global Dear ImGui context")
{
    ImGuiContext* previous = ImGui::GetCurrentContext();
    ImGuiContext* first = ImGui::CreateContext();
    auto firstAccess = std::make_shared<Keire::Detail::UiContextAccess>(first);
    ImGuiContext* second = ImGui::CreateContext();
    auto secondAccess = std::make_shared<Keire::Detail::UiContextAccess>(second);

    auto firstLock = firstAccess->Acquire();
    std::promise<void> workerStarted;
    std::promise<bool> workerAcquiredSecond;
    auto workerStartedFuture = workerStarted.get_future();
    auto workerAcquiredSecondFuture = workerAcquiredSecond.get_future();
    std::jthread worker(
        [&]
        {
            workerStarted.set_value();
            const auto secondLock = secondAccess->Acquire();
            workerAcquiredSecond.set_value(ImGui::GetCurrentContext() == second);
        });

    workerStartedFuture.wait();
    CHECK(workerAcquiredSecondFuture.wait_for(std::chrono::milliseconds(25)) == std::future_status::timeout);
    firstLock = {};
    REQUIRE(workerAcquiredSecondFuture.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
    CHECK(workerAcquiredSecondFuture.get());
    worker.join();

    {
        const auto cleanupLock = firstAccess->Acquire();
        ImGui::DestroyContext(first);
        firstAccess->Invalidate(previous);
    }
    {
        const auto cleanupLock = secondAccess->Acquire();
        ImGui::DestroyContext(second);
        secondAccess->Invalidate(previous);
    }
}
