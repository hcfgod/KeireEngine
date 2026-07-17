#include "Keire/Core.h"

#include <doctest/doctest.h>

#include <SDL3/SDL.h>

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
