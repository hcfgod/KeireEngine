#include "Keire/Application.h"

#include "UiInternal.h"

#include <atomic>
#include <chrono>
#include <exception>
#include <limits>
#include <stdexcept>
#include <string>
#include <thread>
#include <variant>

namespace Keire
{
    class Application::Impl final
    {
      public:
        enum class State : std::uint8_t
        {
            Constructed,
            Running,
            Stopped
        };

        explicit Impl(ApplicationSpecification value)
            : Specification(std::move(value)), OwnerThread(std::this_thread::get_id())
        {
            if (Specification.TargetFrameRate > 1000)
            {
                throw std::invalid_argument("Target frame rate must be 0 or in the range 1..1000.");
            }
            if (Specification.MinimizedPumpRate == 0 || Specification.MinimizedPumpRate > 1000)
            {
                throw std::invalid_argument("Minimized pump rate must be in the range 1..1000.");
            }
        }

        static constexpr int NoExitRequested = std::numeric_limits<int>::min();

        ApplicationSpecification Specification;
        std::thread::id OwnerThread;
        State RuntimeState = State::Constructed;
        std::atomic<int> ExitCode{NoExitRequested};
        Ref<EventBus> EventSystem;
        std::unique_ptr<Time> Clock;
        Ref<WindowSystem> Windowing;
        Ref<Window> PrimaryWindow;
        EventSubscription LayerListener;
        std::unique_ptr<LayerStack> LayerSystem;
        std::unique_ptr<UiSystem> UserInterface;
    };

    Application::Application(ApplicationSpecification specification)
        : m_Impl(std::make_unique<Impl>(std::move(specification)))
    {
        m_Impl->LayerSystem = std::unique_ptr<LayerStack>(new LayerStack(*this));
    }

    Application::~Application()
    {
        if (m_Impl->RuntimeState == Impl::State::Running)
        {
            std::terminate();
        }
    }

    int Application::Run()
    {
        RequireOwnerThread("Run");
        if (m_Impl->RuntimeState != Impl::State::Constructed)
        {
            throw std::logic_error("Application::Run may be called exactly once.");
        }

        m_Impl->RuntimeState = Impl::State::Running;
        m_Impl->ExitCode.store(Impl::NoExitRequested, std::memory_order_release);
        bool initialized = false;
        std::exception_ptr failure;

        try
        {
            if (m_Impl->Specification.ManageLogging)
            {
                Log::Initialize(m_Impl->Specification.Logging);
            }

            m_Impl->EventSystem = CreateRef<EventBus>(m_Impl->Specification.Events);
            m_Impl->Clock = std::make_unique<Time>(m_Impl->Specification.Timing);
            m_Impl->Windowing = CreateRef<WindowSystem>();
            m_Impl->PrimaryWindow = m_Impl->Windowing->CreateWindow(m_Impl->Specification.MainWindow);
            if (m_Impl->Specification.Ui.Mode != UiMode::Disabled)
            {
                m_Impl->UserInterface =
                    std::make_unique<UiSystem>(m_Impl->Specification.Ui, *m_Impl->Windowing, *m_Impl->PrimaryWindow);
            }
            m_Impl->LayerListener = m_Impl->EventSystem->SubscribeAny([this](const EventView& event)
                                                                      { return m_Impl->LayerSystem->Dispatch(event); },
                                                                      EventPriorities::Normal);

            m_Impl->LayerSystem->Activate();
            OnInitialize();
            m_Impl->LayerSystem->ApplyPending();
            initialized = true;

            auto previousFrame = std::chrono::steady_clock::now();
            while (!ExitRequested() && m_Impl->PrimaryWindow->IsOpen())
            {
                const auto frameStart = std::chrono::steady_clock::now();
                const bool suspended =
                    m_Impl->Specification.SuspendWhenMainWindowMinimized && m_Impl->PrimaryWindow->Minimized();
                const auto rawDelta = TimeStep::FromChrono(frameStart - previousFrame);
                previousFrame = frameStart;

                m_Impl->Clock->AdvanceFrame(rawDelta, suspended);
                m_Impl->LayerSystem->ApplyPending();

                while (const auto event = m_Impl->Windowing->PollEvent())
                {
                    (void)DispatchWindowEvent(*event);
                    if (ExitRequested())
                    {
                        break;
                    }
                }

                if (!ExitRequested())
                {
                    (void)m_Impl->EventSystem->DispatchQueued();
                }

                const bool nowSuspended =
                    m_Impl->Specification.SuspendWhenMainWindowMinimized && m_Impl->PrimaryWindow->Minimized();

                if (!ExitRequested() && !nowSuspended)
                {
                    while (m_Impl->Clock->ConsumeFixedStep())
                    {
                        m_Impl->LayerSystem->FixedUpdate(*m_Impl->Clock);
                        if (ExitRequested())
                        {
                            break;
                        }
                    }
                    if (!ExitRequested())
                    {
                        m_Impl->LayerSystem->Update(*m_Impl->Clock);
                    }

                    if (!ExitRequested() && m_Impl->UserInterface)
                    {
                        m_Impl->UserInterface->BeginFrame(m_Impl->Clock->UnscaledDeltaTime(),
                                                          m_Impl->PrimaryWindow->LogicalSize());
                        m_Impl->LayerSystem->Ui(m_Impl->UserInterface->Frame());
                        m_Impl->UserInterface->EndFrame();
                    }
                }

                m_Impl->LayerSystem->ApplyPending();

                if (ExitRequested())
                {
                    break;
                }

                const auto targetRate =
                    nowSuspended ? m_Impl->Specification.MinimizedPumpRate : m_Impl->Specification.TargetFrameRate;
                if (targetRate > 0)
                {
                    const auto frameDuration = std::chrono::duration<double>(1.0 / static_cast<double>(targetRate));
                    std::this_thread::sleep_until(
                        frameStart + std::chrono::duration_cast<std::chrono::steady_clock::duration>(frameDuration));
                }
            }
        }
        catch (...)
        {
            failure = std::current_exception();
        }

        ShutdownRuntime(initialized);
        m_Impl->RuntimeState = Impl::State::Stopped;

        if (failure)
        {
            std::rethrow_exception(failure);
        }

        const auto exitCode = m_Impl->ExitCode.load(std::memory_order_acquire);
        return exitCode == Impl::NoExitRequested ? 0 : exitCode;
    }

    void Application::RequestExit(const int exitCode) noexcept
    {
        const int safeExitCode = exitCode == Impl::NoExitRequested ? 1 : exitCode;
        int expected = Impl::NoExitRequested;
        (void)m_Impl->ExitCode.compare_exchange_strong(expected, safeExitCode, std::memory_order_release,
                                                       std::memory_order_relaxed);
    }

    bool Application::ExitRequested() const noexcept
    {
        return m_Impl->ExitCode.load(std::memory_order_acquire) != Impl::NoExitRequested;
    }

    bool Application::IsRunning() const noexcept { return m_Impl->RuntimeState == Impl::State::Running; }

    LayerId Application::PushLayer(std::unique_ptr<Layer> layer)
    {
        return m_Impl->LayerSystem->PushLayer(std::move(layer));
    }

    LayerId Application::PushOverlay(std::unique_ptr<Layer> overlay)
    {
        return m_Impl->LayerSystem->PushOverlay(std::move(overlay));
    }

    bool Application::RemoveLayer(const LayerId id) { return m_Impl->LayerSystem->Remove(id); }

    LayerStack& Application::Layers() noexcept { return *m_Impl->LayerSystem; }

    const LayerStack& Application::Layers() const noexcept { return *m_Impl->LayerSystem; }

    Ref<EventBus> Application::Events() const noexcept { return m_Impl->EventSystem; }

    Time& Application::GetTime()
    {
        if (!m_Impl->Clock)
        {
            throw std::logic_error("Application time is not available before Run initializes services.");
        }

        return *m_Impl->Clock;
    }

    const Time& Application::GetTime() const
    {
        if (!m_Impl->Clock)
        {
            throw std::logic_error("Application time is not available before Run initializes services.");
        }

        return *m_Impl->Clock;
    }

    Ref<WindowSystem> Application::Windows() const noexcept { return m_Impl->Windowing; }

    Ref<Window> Application::MainWindow() const noexcept { return m_Impl->PrimaryWindow; }

    const ApplicationSpecification& Application::Specification() const noexcept { return m_Impl->Specification; }

    bool Application::UiEnabled() const noexcept { return m_Impl->UserInterface != nullptr; }

    UiCaptureState Application::UiCapture() const noexcept
    {
        return m_Impl->UserInterface ? m_Impl->UserInterface->Capture() : UiCaptureState{};
    }

    UiWorkspace& Application::GetUiWorkspace()
    {
        if (!m_Impl->UserInterface || !m_Impl->UserInterface->Workspace())
            throw std::logic_error("The UI workspace is not enabled for this application.");
        return *m_Impl->UserInterface->Workspace();
    }

    const UiWorkspace& Application::GetUiWorkspace() const
    {
        if (!m_Impl->UserInterface || !m_Impl->UserInterface->Workspace())
            throw std::logic_error("The UI workspace is not enabled for this application.");
        return *m_Impl->UserInterface->Workspace();
    }

    void Application::RequireOwnerThread(const char* operation) const
    {
        if (std::this_thread::get_id() != m_Impl->OwnerThread)
        {
            throw std::logic_error(std::string("Application::") + operation +
                                   " must be called on the application construction thread.");
        }
    }

    bool Application::CanModifyLayers() const noexcept { return m_Impl->RuntimeState != Impl::State::Stopped; }

    bool Application::DispatchWindowEvent(const WindowEvent& event)
    {
        return std::visit(
            [this](const auto& typedEvent)
            {
                const bool handled = m_Impl->EventSystem->Dispatch(typedEvent);
                using Event = std::decay_t<decltype(typedEvent)>;
                if (!handled)
                {
                    if constexpr (std::same_as<Event, QuitEvent>)
                    {
                        RequestExit();
                    }
                    else if constexpr (std::same_as<Event, WindowCloseRequestedEvent>)
                    {
                        if (typedEvent.Header.Window == m_Impl->PrimaryWindow->Id())
                        {
                            RequestExit();
                        }
                    }
                }
                return handled;
            },
            event);
    }

    void Application::ShutdownRuntime(const bool initialized) noexcept
    {
        m_Impl->LayerSystem->Deactivate();
        if (initialized)
        {
            OnShutdown();
        }

        m_Impl->LayerListener.Disconnect();

        if (m_Impl->UserInterface)
        {
            m_Impl->UserInterface->Shutdown();
            m_Impl->UserInterface.reset();
        }

        if (m_Impl->EventSystem && m_Impl->EventSystem->IsOpen())
        {
            try
            {
                m_Impl->EventSystem->Close();
            }
            catch (...)
            {
            }
        }

        m_Impl->PrimaryWindow.Reset();

        if (m_Impl->Windowing && m_Impl->Windowing->IsActive())
        {
            try
            {
                m_Impl->Windowing->Shutdown();
            }
            catch (...)
            {
            }
        }

        m_Impl->Windowing.Reset();
        m_Impl->Clock.reset();
        m_Impl->EventSystem.Reset();

        if (m_Impl->Specification.ManageLogging)
        {
            Log::Shutdown();
        }
    }
} // namespace Keire
