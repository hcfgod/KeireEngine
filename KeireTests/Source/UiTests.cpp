#include "Keire/Core.h"

#include <doctest/doctest.h>

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_internal.h>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <memory>
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

            ui.SetNextWindowSize({320.0F, 200.0F});
            if (auto window = ui.BeginWindow("Headless UI"); window)
            {
                ui.Text("Public Kéire UI facade");
                bool checked = false;
                (void)ui.Checkbox("Check", checked);
                float value = 0.5F;
                (void)ui.SliderFloat("Value", value, 0.0F, 1.0F);
                std::string text = "editable";
                (void)ui.InputText("Text", text);
                const auto item = ui.LastItemState();
                CHECK_FALSE(item.DoubleClicked);
                ui.ProgressBar(0.5F, {120.0F, 4.0F}, "50%");
                if (auto table = ui.BeginTable("ActionBindings", 2); table)
                {
                    ui.TableNextRow();
                    CHECK(ui.TableNextColumn());
                    ui.Text("Action");
                    CHECK(ui.TableNextColumn());
                    ui.Text("Binding");
                }
                float leading = 100.0F;
                float trailing = 100.0F;
                (void)ui.Splitter(Keire::UiAxis::Horizontal, "EditorSplitter", leading, trailing);
                (void)ui.Shortcut({Keire::UiKey::S, true});
                if (auto disabled = ui.BeginDisabled(); disabled)
                {
                    (void)ui.Button("Disabled");
                }
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
            : Application(UiSpecification("ui-headless", Keire::UiMode::Headless))
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
        void OnAttach() override { m_Panel = Owner().GetUiWorkspace().RegisterPanel({"test.panel", "Test Panel"}); }

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
        oversized.seekp(1024U * 1024U);
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
