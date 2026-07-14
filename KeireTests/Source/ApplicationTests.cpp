#include "Keire/Application.h"

#include <SDL3/SDL.h>
#include <doctest/doctest.h>

#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <string>
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

TEST_CASE("Application defers layer mutation requested during event traversal")
{
    UseApplicationDummyVideoDriver();
    std::vector<std::string> order;
    DeferredMutationApplication application(order);
    CHECK(application.Run() == 0);
    CHECK(order == std::vector<std::string>{"attach:mutator", "initialize", "event:mutator", "attach:added",
                                            "detach:mutator", "update:added", "detach:added", "shutdown"});
}

TEST_CASE("Application cleans up services before rethrowing callback failures")
{
    UseApplicationDummyVideoDriver();
    ThrowApplication throwing;
    CHECK_THROWS_WITH_AS((void)throwing.Run(), "layer failure", std::runtime_error);

    ThreadExitApplication replacement;
    CHECK(replacement.Run() == 4);
}
