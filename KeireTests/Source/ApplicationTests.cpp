#include "Keire/Application.h"
#include "Keire/Ui/RuntimeUi.h"
#include "KeireInternal/RenderInternal.h"

#include <SDL3/SDL.h>
#include <doctest/doctest.h>

#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace
{
    void UseApplicationDummyVideoDriver()
    {
#if defined(_WIN32)
        REQUIRE(_putenv_s("SDL_VIDEODRIVER", "dummy") == 0);
#else
        REQUIRE(setenv("SDL_VIDEODRIVER", "dummy", 1) == 0);
#endif
        REQUIRE(SDL_SetHintWithPriority(SDL_HINT_VIDEO_DRIVER, "dummy", SDL_HINT_OVERRIDE));
    }

    Keire::ApplicationSpecification HiddenApplicationSpecification(const char* title)
    {
        Keire::ApplicationSpecification specification;
        specification.MainWindow.Title = title;
        specification.MainWindow.Visible = false;
        specification.SuspendWhenMainWindowMinimized = false;
        specification.ManageLogging = false;
        return specification;
    }

    struct LifecycleEvent
    {
    };

    struct NestedOuterEvent
    {
    };

    struct NestedInnerEvent
    {
    };

    struct TeardownEvent
    {
    };

    class LifecycleLayer final : public Keire::Layer
    {
      public:
        LifecycleLayer(std::string name, std::vector<std::string>& order, const bool exitOnUpdate = false)
            : Layer(name), m_Order(order), m_ExitOnUpdate(exitOnUpdate)
        {
        }

      protected:
        void OnAttach() override { m_Order.push_back("attach:" + Name()); }
        void OnDetach() noexcept override { m_Order.push_back("detach:" + Name()); }
        void OnUpdate(const Keire::Time&) override
        {
            m_Order.push_back("update:" + Name());
            if (m_ExitOnUpdate)
            {
                Owner().RequestExit();
            }
        }
        Keire::EventFlow OnEvent(const Keire::EventView& event) override
        {
            if (event.Is<LifecycleEvent>())
            {
                m_Order.push_back("event:" + Name());
            }
            return Keire::EventFlow::Continue;
        }

      private:
        std::vector<std::string>& m_Order;
        bool m_ExitOnUpdate = false;
    };

    class LifecycleApplication final : public Keire::Application
    {
      public:
        explicit LifecycleApplication(std::vector<std::string>& order)
            : Application(HiddenApplicationSpecification("lifecycle")), m_Order(order)
        {
            (void)Layers().PushLayer(std::make_unique<LifecycleLayer>("base", order));
            (void)Layers().PushLayer(std::make_unique<LifecycleLayer>("top", order));
            (void)Layers().PushOverlay(std::make_unique<LifecycleLayer>("overlay", order, true));
        }

      protected:
        void OnInitialize() override
        {
            m_Order.push_back("initialize");
            CHECK_FALSE(Events()->Dispatch(LifecycleEvent{}));
        }
        void OnShutdown() noexcept override { m_Order.push_back("shutdown"); }

      private:
        std::vector<std::string>& m_Order;
    };

    class QuitLayer final : public Keire::Layer
    {
      public:
        QuitLayer(const bool handle, int& updates) : Layer("QuitLayer"), m_Handle(handle), m_Updates(updates) {}

      protected:
        Keire::EventFlow OnEvent(const Keire::EventView& event) override
        {
            if (m_Handle && event.Is<Keire::QuitEvent>())
            {
                return Keire::EventFlow::Handled;
            }
            return Keire::EventFlow::Continue;
        }
        void OnUpdate(const Keire::Time&) override
        {
            ++m_Updates;
            Owner().RequestExit(7);
        }

      private:
        bool m_Handle = false;
        int& m_Updates;
    };

    class QuitApplication final : public Keire::Application
    {
      public:
        QuitApplication(const bool handle, int& updates)
            : Application(HiddenApplicationSpecification(handle ? "quit-veto" : "quit-default"))
        {
            (void)PushLayer(std::make_unique<QuitLayer>(handle, updates));
        }

      protected:
        void OnInitialize() override
        {
            SDL_Event event{};
            event.type = SDL_EVENT_QUIT;
            REQUIRE(SDL_PushEvent(&event));
        }
    };

    class ExitLayer final : public Keire::Layer
    {
      public:
        explicit ExitLayer(const int code) : Layer("ExitLayer"), m_Code(code) {}

      protected:
        void OnUpdate(const Keire::Time&) override { Owner().RequestExit(m_Code); }

      private:
        int m_Code;
    };

    class SecondaryCloseApplication final : public Keire::Application
    {
      public:
        SecondaryCloseApplication() : Application(HiddenApplicationSpecification("primary"))
        {
            (void)PushLayer(std::make_unique<ExitLayer>(5));
        }

      protected:
        void OnInitialize() override
        {
            Keire::WindowSpecification specification;
            specification.Title = "secondary";
            specification.Visible = false;
            m_Secondary = Windows()->CreateWindow(specification);

            int count = 0;
            SDL_Window** windows = SDL_GetWindows(&count);
            REQUIRE(windows != nullptr);
            SDL_WindowID secondaryId = 0;
            for (int index = 0; index < count; ++index)
            {
                if (std::string(SDL_GetWindowTitle(windows[index])) == "secondary")
                {
                    secondaryId = SDL_GetWindowID(windows[index]);
                }
            }
            SDL_free(windows);
            REQUIRE(secondaryId != 0);

            SDL_Event close{};
            close.type = SDL_EVENT_WINDOW_CLOSE_REQUESTED;
            close.window.windowID = secondaryId;
            REQUIRE(SDL_PushEvent(&close));
        }

      private:
        Keire::Ref<Keire::Window> m_Secondary;
    };

    class ThrowLayer final : public Keire::Layer
    {
      public:
        ThrowLayer() : Layer("ThrowLayer") {}

      protected:
        void OnUpdate(const Keire::Time&) override { throw std::runtime_error("layer failure"); }
    };

    class ThrowApplication final : public Keire::Application
    {
      public:
        ThrowApplication() : Application(HiddenApplicationSpecification("throwing"))
        {
            (void)PushLayer(std::make_unique<ThrowLayer>());
        }
    };

    class AttachProbeLayer final : public Keire::Layer
    {
      public:
        explicit AttachProbeLayer(int& attachments) : Layer("AttachProbe"), m_Attachments(attachments) {}

      protected:
        void OnAttach() override { ++m_Attachments; }

      private:
        int& m_Attachments;
    };

    class FailingAttachLayer final : public Keire::Layer
    {
      public:
        explicit FailingAttachLayer(int& nestedAttachments)
            : Layer("FailingAttach"), m_NestedAttachments(nestedAttachments)
        {
        }

      protected:
        void OnAttach() override
        {
            (void)Owner().PushOverlay(std::make_unique<AttachProbeLayer>(m_NestedAttachments));
            throw std::runtime_error("attach failure");
        }

      private:
        int& m_NestedAttachments;
    };

    class AttachFailureApplication final : public Keire::Application
    {
      public:
        AttachFailureApplication(const bool overlay, bool& caught, std::size_t& retainedLayers, int& nestedAttachments)
            : Application(HiddenApplicationSpecification("attach-failure")), m_Overlay(overlay), m_Caught(caught),
              m_RetainedLayers(retainedLayers), m_NestedAttachments(nestedAttachments)
        {
        }

      protected:
        void OnInitialize() override
        {
            try
            {
                if (m_Overlay)
                    (void)PushOverlay(std::make_unique<FailingAttachLayer>(m_NestedAttachments));
                else
                    (void)PushLayer(std::make_unique<FailingAttachLayer>(m_NestedAttachments));
            }
            catch (const std::runtime_error& exception)
            {
                m_Caught = std::string_view(exception.what()) == "attach failure";
            }
            m_RetainedLayers = Layers().Size();
            RequestExit();
        }

      private:
        bool m_Overlay = false;
        bool& m_Caught;
        std::size_t& m_RetainedLayers;
        int& m_NestedAttachments;
    };

    class UndoLifecycleLayer final : public Keire::Layer
    {
      public:
        UndoLifecycleLayer(Keire::Ref<Keire::UndoContext>& retained, int& value)
            : Layer("UndoLifecycle"), m_Retained(retained), m_Value(value)
        {
        }

      protected:
        void OnAttach() override
        {
            REQUIRE(Owner().Undo());
            m_Retained = Owner().Undo()->CreateContext({.Name = "Application Lifecycle"});
        }
        void OnUpdate(const Keire::Time&) override
        {
            m_Retained->Execute(
                Keire::CreateUndoCommand("Set Value", [this] { m_Value = 1; }, [this] { m_Value = 0; }));
            Owner().RequestExit();
        }
        void OnDetach() noexcept override { CHECK(m_Retained->IsOpen()); }

      private:
        Keire::Ref<Keire::UndoContext>& m_Retained;
        int& m_Value;
    };

    class UndoLifecycleApplication final : public Keire::Application
    {
      public:
        UndoLifecycleApplication(Keire::Ref<Keire::UndoContext>& retained, int& value)
            : Application(HiddenApplicationSpecification("undo-lifecycle"))
        {
            (void)PushLayer(std::make_unique<UndoLifecycleLayer>(retained, value));
        }
    };

    class ThreadExitApplication final : public Keire::Application
    {
      public:
        ThreadExitApplication() : Application(HiddenApplicationSpecification("thread-exit")) {}

      protected:
        void OnInitialize() override
        {
            std::thread worker([this] { RequestExit(4); });
            worker.join();
        }
    };

    class RuntimeUiOnlyLayer final : public Keire::Layer
    {
      public:
        RuntimeUiOnlyLayer(bool& updated, bool& editorUiCalled)
            : Layer("RuntimeUiOnly"), m_Updated(updated), m_EditorUiCalled(editorUiCalled)
        {
        }

      protected:
        void OnAttach() override
        {
            const auto tree = Keire::CreateRef<Keire::RuntimeUiTree>();
            CHECK_THROWS_AS(Owner().Renderer()->SubmitRuntimeUi(tree), std::logic_error);
        }

        void OnUpdate(const Keire::Time&) override
        {
            auto tree = Keire::CreateRef<Keire::RuntimeUiTree>();
            const auto panel = tree->Create(Keire::RuntimeUiElementType::Panel);
            Keire::RuntimeUiStyle style;
            style.Background = {0.2F, 0.4F, 0.8F, 1.0F};
            REQUIRE(tree->SetStyle(panel, style));
            tree->Layout(320.0F, 180.0F);
            Owner().Renderer()->SubmitRuntimeUi(tree);
            CHECK(Keire::RenderSystemInternalAccess::RuntimeUiCommandCount(*Owner().Renderer()) > 0);
            m_Updated = true;
            Owner().RequestExit();
        }

        void OnUi(Keire::UiFrame&) override { m_EditorUiCalled = true; }

      private:
        bool& m_Updated;
        bool& m_EditorUiCalled;
    };

    class RuntimeUiOnlyApplication final : public Keire::Application
    {
      public:
        RuntimeUiOnlyApplication(bool& updated, bool& editorUiCalled)
            : Application(Specification()), m_Updated(updated), m_EditorUiCalled(editorUiCalled)
        {
            (void)PushLayer(std::make_unique<RuntimeUiOnlyLayer>(m_Updated, m_EditorUiCalled));
        }

      private:
        [[nodiscard]] static Keire::ApplicationSpecification Specification()
        {
            auto specification = HiddenApplicationSpecification("runtime-ui-only");
            specification.Render.Mode = Keire::RenderMode::Headless;
            specification.Ui.Mode = Keire::UiMode::Disabled;
            return specification;
        }

        bool& m_Updated;
        bool& m_EditorUiCalled;
    };

    class DeferredMutationLayer final : public Keire::Layer
    {
      public:
        DeferredMutationLayer(Keire::LayerId& self, std::vector<std::string>& order)
            : Layer("mutator"), m_Self(self), m_Order(order)
        {
        }

      protected:
        void OnAttach() override { m_Order.push_back("attach:mutator"); }
        void OnDetach() noexcept override { m_Order.push_back("detach:mutator"); }
        Keire::EventFlow OnEvent(const Keire::EventView& event) override
        {
            if (event.Is<LifecycleEvent>())
            {
                m_Order.push_back("event:mutator");
                (void)Owner().PushOverlay(std::make_unique<LifecycleLayer>("added", m_Order, true));
                CHECK(Owner().RemoveLayer(m_Self));
            }
            return Keire::EventFlow::Continue;
        }

      private:
        Keire::LayerId& m_Self;
        std::vector<std::string>& m_Order;
    };

    class DeferredMutationApplication final : public Keire::Application
    {
      public:
        explicit DeferredMutationApplication(std::vector<std::string>& order)
            : Application(HiddenApplicationSpecification("deferred-mutation")), m_Order(order)
        {
            m_Mutator = PushLayer(std::make_unique<DeferredMutationLayer>(m_Mutator, order));
        }

      protected:
        void OnInitialize() override
        {
            m_Order.push_back("initialize");
            (void)Events()->Dispatch(LifecycleEvent{});
        }
        void OnShutdown() noexcept override { m_Order.push_back("shutdown"); }

      private:
        std::vector<std::string>& m_Order;
        Keire::LayerId m_Mutator;
    };

    class NestedMutationLayer final : public Keire::Layer
    {
      public:
        NestedMutationLayer(Keire::LayerId& self, std::vector<std::string>& order)
            : Layer("nested-mutator"), m_Self(self), m_Order(order)
        {
        }

      protected:
        void OnAttach() override { m_Order.push_back("attach:nested-mutator"); }
        void OnDetach() noexcept override { m_Order.push_back("detach:nested-mutator"); }
        Keire::EventFlow OnEvent(const Keire::EventView& event) override
        {
            if (event.Is<NestedInnerEvent>())
            {
                m_Order.push_back("event:inner");
            }
            else if (event.Is<NestedOuterEvent>())
            {
                m_Order.push_back("event:outer-begin");
                (void)Owner().Events()->Dispatch(NestedInnerEvent{});
                m_Order.push_back("event:outer-resume");
                (void)Owner().PushOverlay(std::make_unique<LifecycleLayer>("nested-added", m_Order, true));
                CHECK(Owner().RemoveLayer(m_Self));
            }
            return Keire::EventFlow::Continue;
        }

      private:
        Keire::LayerId& m_Self;
        std::vector<std::string>& m_Order;
    };

    class NestedMutationApplication final : public Keire::Application
    {
      public:
        explicit NestedMutationApplication(std::vector<std::string>& order)
            : Application(HiddenApplicationSpecification("nested-mutation")), m_Order(order)
        {
            m_Mutator = PushLayer(std::make_unique<NestedMutationLayer>(m_Mutator, order));
        }

      protected:
        void OnInitialize() override
        {
            m_Order.push_back("initialize");
            (void)Events()->Dispatch(NestedOuterEvent{});
        }

      private:
        std::vector<std::string>& m_Order;
        Keire::LayerId m_Mutator;
    };

    class TeardownSubscriberLayer final : public Keire::Layer
    {
      public:
        explicit TeardownSubscriberLayer(int& calls) : Layer("teardown-subscriber"), m_Calls(calls) {}

      protected:
        void OnDetach() noexcept override
        {
            Listen<TeardownEvent>(
                [this](const TeardownEvent&)
                {
                    ++m_Calls;
                    return Keire::EventFlow::Continue;
                });
        }

      private:
        int& m_Calls;
    };

    class TeardownDispatcherLayer final : public Keire::Layer
    {
      public:
        TeardownDispatcherLayer() : Layer("teardown-dispatcher") {}

      protected:
        void OnDetach() noexcept override { (void)Owner().Events()->Dispatch(TeardownEvent{}); }
    };

    class TeardownSubscriptionApplication final : public Keire::Application
    {
      public:
        explicit TeardownSubscriptionApplication(int& calls)
            : Application(HiddenApplicationSpecification("teardown-subscription"))
        {
            (void)PushLayer(std::make_unique<TeardownDispatcherLayer>());
            (void)PushLayer(std::make_unique<TeardownSubscriberLayer>(calls));
        }

      protected:
        void OnInitialize() override { RequestExit(); }
    };

    class OwnerThreadApplication final : public Keire::Application
    {
      public:
        OwnerThreadApplication() : Application(HiddenApplicationSpecification("owner-thread")) {}

      protected:
        void OnInitialize() override { RequestExit(); }
    };

    class PreRunExitApplication final : public Keire::Application
    {
      public:
        PreRunExitApplication(bool& initialized, bool& shutdown)
            : Application(HiddenApplicationSpecification("pre-run-exit")), m_Initialized(initialized),
              m_Shutdown(shutdown)
        {
            RequestExit(9);
        }

      protected:
        void OnInitialize() override { m_Initialized = true; }
        void OnShutdown() noexcept override { m_Shutdown = true; }

      private:
        bool& m_Initialized;
        bool& m_Shutdown;
    };

    class MinimizeTransitionLayer final : public Keire::Layer
    {
      public:
        explicit MinimizeTransitionLayer(int& fixedUpdates) : Layer("minimize-transition"), m_FixedUpdates(fixedUpdates)
        {
        }

      protected:
        void OnFixedUpdate(const Keire::Time&) override
        {
            ++m_FixedUpdates;
            Owner().RequestExit(6);
        }

      private:
        int& m_FixedUpdates;
    };

    class MinimizeTransitionApplication final : public Keire::Application
    {
      public:
        explicit MinimizeTransitionApplication(int& fixedUpdates) : Application(BuildSpecification())
        {
            (void)PushLayer(std::make_unique<MinimizeTransitionLayer>(fixedUpdates));
        }

      protected:
        void OnInitialize() override
        {
            int count = 0;
            SDL_Window** windows = SDL_GetWindows(&count);
            REQUIRE(windows != nullptr);
            SDL_WindowID primaryId = 0;
            for (int index = 0; index < count; ++index)
                if (std::string(SDL_GetWindowTitle(windows[index])) == "minimize-transition")
                    primaryId = SDL_GetWindowID(windows[index]);
            SDL_free(windows);
            REQUIRE(primaryId != 0);

            SDL_Event minimize{};
            minimize.type = SDL_EVENT_WINDOW_MINIMIZED;
            minimize.window.windowID = primaryId;
            REQUIRE(SDL_PushEvent(&minimize));
        }

      private:
        static Keire::ApplicationSpecification BuildSpecification()
        {
            auto specification = HiddenApplicationSpecification("minimize-transition");
            specification.SuspendWhenMainWindowMinimized = true;
            specification.MinimizedPumpRate = 1000;
            specification.Timing.FixedDeltaTime = Keire::TimeStep::FromSeconds(0.000000001);
            specification.Timing.MaximumFixedStepsPerFrame = 1;
            return specification;
        }
    };
} // namespace

TEST_CASE("LayerStack owns deterministic layer lifecycle and traversal order")
{
    UseApplicationDummyVideoDriver();
    std::vector<std::string> order;
    LifecycleApplication application(order);
    CHECK(application.Run() == 0);
    CHECK(application.Layers().Empty());
    CHECK_FALSE(application.Layers().Active());

    CHECK(order == std::vector<std::string>{"attach:base", "attach:top", "attach:overlay", "initialize",
                                            "event:overlay", "event:top", "event:base", "update:base", "update:top",
                                            "update:overlay", "detach:overlay", "detach:top", "detach:base",
                                            "shutdown"});
}

TEST_CASE("Application default quit is cancelable through handled propagation")
{
    UseApplicationDummyVideoDriver();
    int defaultUpdates = 0;
    QuitApplication defaultExit(false, defaultUpdates);
    CHECK(defaultExit.Run() == 0);
    CHECK(defaultUpdates == 0);

    int vetoUpdates = 0;
    QuitApplication vetoedExit(true, vetoUpdates);
    CHECK(vetoedExit.Run() == 7);
    CHECK(vetoUpdates == 1);
}

TEST_CASE("Application ignores secondary window close for process exit")
{
    UseApplicationDummyVideoDriver();
    SecondaryCloseApplication application;
    CHECK(application.Run() == 5);
}

TEST_CASE("Application accepts cross-thread exit and runs only once")
{
    UseApplicationDummyVideoDriver();
    ThreadExitApplication application;
    CHECK(application.Run() == 4);
    CHECK_THROWS_AS((void)application.Run(), std::logic_error);
}

TEST_CASE("Application requested to exit before Run does not initialize runtime services")
{
    bool initialized = false;
    bool shutdown = false;
    PreRunExitApplication application(initialized, shutdown);

    CHECK(application.ExitRequested());
    CHECK(application.Run() == 9);
    CHECK_FALSE(initialized);
    CHECK_FALSE(shutdown);
    CHECK_FALSE(application.IsRunning());
    CHECK_FALSE(application.Windows());
    CHECK_FALSE(application.MainWindow());
    CHECK_THROWS_AS((void)application.Run(), std::logic_error);
}

TEST_CASE("Runtime UI submits without initializing or invoking editor UI")
{
    UseApplicationDummyVideoDriver();
    bool updated = false;
    bool editorUiCalled = false;
    RuntimeUiOnlyApplication application(updated, editorUiCalled);
    CHECK(application.Run() == 0);
    CHECK(updated);
    CHECK_FALSE(editorUiCalled);
    CHECK_FALSE(application.UiEnabled());
}

TEST_CASE("Application defers layer mutation requested during event traversal")
{
    UseApplicationDummyVideoDriver();
    std::vector<std::string> order;
    DeferredMutationApplication application(order);
    CHECK(application.Run() == 0);
    CHECK(order == std::vector<std::string>{"attach:mutator", "initialize", "event:mutator", "attach:added",
                                            "detach:mutator", "update:added", "detach:added", "shutdown"});
}

TEST_CASE("Application defers layer mutation across nested event traversal")
{
    UseApplicationDummyVideoDriver();
    std::vector<std::string> order;
    NestedMutationApplication application(order);
    CHECK(application.Run() == 0);
    CHECK(order == std::vector<std::string>{"attach:nested-mutator", "initialize", "event:outer-begin", "event:inner",
                                            "event:outer-resume", "attach:nested-added", "detach:nested-mutator",
                                            "update:nested-added", "detach:nested-added"});
}

TEST_CASE("Layer automatic subscriptions cannot be created during detachment")
{
    UseApplicationDummyVideoDriver();
    int calls = 0;
    TeardownSubscriptionApplication application(calls);
    CHECK(application.Run() == 0);
    CHECK(calls == 0);
}

TEST_CASE("Application run and layer mutation are construction-thread-affine")
{
    UseApplicationDummyVideoDriver();
    OwnerThreadApplication application;
    bool runRejected = false;
    std::thread runWorker(
        [&]
        {
            try
            {
                (void)application.Run();
            }
            catch (const std::logic_error&)
            {
                runRejected = true;
            }
        });
    runWorker.join();
    CHECK(runRejected);
    CHECK_FALSE(application.IsRunning());

    bool mutationRejected = false;
    std::thread mutationWorker(
        [&]
        {
            try
            {
                (void)application.PushLayer(std::make_unique<ExitLayer>(1));
            }
            catch (const std::logic_error&)
            {
                mutationRejected = true;
            }
        });
    mutationWorker.join();
    CHECK(mutationRejected);
    CHECK(application.Layers().Empty());
    CHECK(application.Run() == 0);
}

TEST_CASE("Application cleans up services before rethrowing callback failures")
{
    UseApplicationDummyVideoDriver();
    ThrowApplication throwing;
    CHECK_THROWS_WITH_AS((void)throwing.Run(), "layer failure", std::runtime_error);

    ThreadExitApplication replacement;
    CHECK(replacement.Run() == 4);
}

TEST_CASE("Failed active layer attachment rolls back the layer and nested mutations")
{
    UseApplicationDummyVideoDriver();
    for (const bool overlay : {false, true})
    {
        CAPTURE(overlay);
        bool caught = false;
        std::size_t retainedLayers = 1;
        int nestedAttachments = 0;
        AttachFailureApplication application(overlay, caught, retainedLayers, nestedAttachments);
        CHECK(application.Run() == 0);
        CHECK(caught);
        CHECK(retainedLayers == 0);
        CHECK(nestedAttachments == 0);
    }
}

TEST_CASE("Application owns undo before layers and closes it after layer teardown")
{
    UseApplicationDummyVideoDriver();
    Keire::Ref<Keire::UndoContext> retained;
    int value = 0;
    UndoLifecycleApplication application(retained, value);
    CHECK(application.Run() == 0);
    CHECK(value == 1);
    REQUIRE(retained);
    CHECK_FALSE(retained->IsOpen());
    CHECK_FALSE(retained->CanUndo());
}

TEST_CASE("Minimizing during a frame consumes the fixed work produced at that frame boundary")
{
    UseApplicationDummyVideoDriver();
    int fixedUpdates = 0;
    MinimizeTransitionApplication application(fixedUpdates);
    CHECK(application.Run() == 6);
    CHECK(fixedUpdates == 1);
}
