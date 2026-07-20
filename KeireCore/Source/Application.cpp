#include "Keire/Application.h"

#include "Keire/Assets/RenderingAssets.h"

#include "KeireInternal/RenderInternal.h"
#include "KeireInternal/UiInternal.h"

#include <algorithm>
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
        Ref<UndoService> UndoHistory;
        Ref<Project> ProjectService;
        Ref<AssetSystem> Assets;
        Ref<SceneSystem> SceneService;
        Ref<InputSystem> InputService;
        std::unique_ptr<Time> Clock;
        Ref<WindowSystem> Windowing;
        Ref<Window> PrimaryWindow;
        Ref<RenderSystem> Renderer;
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
            if (m_Impl->Specification.Projects.Mode == ProjectMode::Editor)
            {
                if (m_Impl->Specification.Assets.Mode != AssetMode::Development)
                    throw std::invalid_argument("Editor projects require Development assets.");
                m_Impl->ProjectService = Project::Open(m_Impl->Specification.Projects.Root, ProjectOpenMode::Exclusive);
                m_Impl->Specification.Assets.DevelopmentCatalog = m_Impl->ProjectService->AssetCatalog();
                if (m_Impl->Specification.Input.BindingOverrideDirectory.empty())
                {
                    m_Impl->Specification.Input.BindingOverrideDirectory =
                        m_Impl->ProjectService->InputOverridesDirectory();
                }
                if (m_Impl->Specification.Ui.Workspace.DirectoryOverride.empty())
                {
                    m_Impl->Specification.Ui.Workspace.DirectoryOverride = m_Impl->ProjectService->WorkspaceDirectory();
                }
                if (m_Impl->Specification.Logging.LogDirectory == "Logs")
                    m_Impl->Specification.Logging.LogDirectory = (m_Impl->ProjectService->Root() / "Logs").string();
            }
            if (m_Impl->Specification.ManageLogging)
            {
                Log::Initialize(m_Impl->Specification.Logging);
            }

            m_Impl->EventSystem = CreateRef<EventBus>(m_Impl->Specification.Events);
            m_Impl->UndoHistory = CreateRef<UndoService>(m_Impl->Specification.Undo);
            if (m_Impl->Specification.Input.Mode == InputMode::Enabled &&
                m_Impl->Specification.Assets.Mode == AssetMode::Disabled)
            {
                throw std::invalid_argument("Enabled input requires enabled assets.");
            }
            if (m_Impl->Specification.Scenes.Mode == SceneMode::Enabled &&
                m_Impl->Specification.Assets.Mode == AssetMode::Disabled)
            {
                throw std::invalid_argument("Enabled scenes require enabled assets.");
            }
            if (m_Impl->Specification.Input.Mode == InputMode::Enabled)
            {
                const auto inputType = InputActionAsset::StaticType();
                const auto decoder = std::ranges::find(m_Impl->Specification.Assets.Decoders, inputType,
                                                       &AssetDecoderRegistration::Type);
                if (decoder == m_Impl->Specification.Assets.Decoders.end())
                    m_Impl->Specification.Assets.Decoders.push_back(CreateInputActionAssetDecoder());
            }
            if (m_Impl->Specification.Scenes.Mode == SceneMode::Enabled)
            {
                const auto sceneType = SceneAsset::StaticType();
                const auto decoder = std::ranges::find(m_Impl->Specification.Assets.Decoders, sceneType,
                                                       &AssetDecoderRegistration::Type);
                if (decoder == m_Impl->Specification.Assets.Decoders.end())
                    m_Impl->Specification.Assets.Decoders.push_back(CreateSceneAssetDecoder());
            }
            if (m_Impl->Specification.Assets.Mode != AssetMode::Disabled)
            {
                const auto addDecoder = [this](AssetDecoderRegistration registration)
                {
                    const auto found = std::ranges::find(m_Impl->Specification.Assets.Decoders, registration.Type,
                                                         &AssetDecoderRegistration::Type);
                    if (found == m_Impl->Specification.Assets.Decoders.end())
                        m_Impl->Specification.Assets.Decoders.push_back(std::move(registration));
                };
                addDecoder(CreateShaderAssetDecoder());
                addDecoder(CreateMaterialAssetDecoder());
                addDecoder(CreateMeshAssetDecoder());
                addDecoder(CreateTexture2DAssetDecoder());
            }
            if (m_Impl->Specification.Assets.Mode != AssetMode::Disabled)
            {
                m_Impl->Assets = CreateRef<AssetSystem>(m_Impl->Specification.Assets, m_Impl->EventSystem);
            }
            if (m_Impl->Specification.Scenes.Mode == SceneMode::Enabled)
            {
                m_Impl->SceneService =
                    CreateRef<SceneSystem>(m_Impl->Specification.Scenes, m_Impl->Assets, m_Impl->EventSystem);
            }
            m_Impl->Clock = std::make_unique<Time>(m_Impl->Specification.Timing);
            m_Impl->Windowing = CreateRef<WindowSystem>();
            m_Impl->PrimaryWindow = m_Impl->Windowing->CreateWindow(m_Impl->Specification.MainWindow);

            auto renderSpecification = m_Impl->Specification.Render;
            if (renderSpecification.Mode == RenderMode::Automatic)
            {
                if (m_Impl->Specification.Ui.Mode == UiMode::Rendered)
                    renderSpecification.Mode = RenderMode::Rendered;
                else if (m_Impl->Specification.Ui.Mode == UiMode::Headless)
                    renderSpecification.Mode = RenderMode::Headless;
                else
                    renderSpecification.Mode = RenderMode::Disabled;
            }
            if (m_Impl->Specification.Ui.Mode != UiMode::Disabled && renderSpecification.Mode == RenderMode::Disabled)
                throw std::invalid_argument("An enabled UI requires an enabled application renderer.");
            if (m_Impl->Specification.Ui.Mode == UiMode::Rendered && renderSpecification.Mode != RenderMode::Rendered)
                throw std::invalid_argument("Rendered UI mode requires rendered application renderer mode.");
            if (m_Impl->Specification.Ui.Mode == UiMode::Headless && renderSpecification.Mode != RenderMode::Headless)
                throw std::invalid_argument("Headless UI mode requires headless application renderer mode.");

            if (m_Impl->Specification.Ui.Mode == UiMode::Rendered)
            {
                renderSpecification.PresentMode = static_cast<RenderPresentMode>(m_Impl->Specification.Ui.PresentMode);
                renderSpecification.SwapchainClearColor = {
                    m_Impl->Specification.Ui.ClearColor.Red, m_Impl->Specification.Ui.ClearColor.Green,
                    m_Impl->Specification.Ui.ClearColor.Blue, m_Impl->Specification.Ui.ClearColor.Alpha};
                renderSpecification.EnableGpuValidation |= m_Impl->Specification.Ui.EnableGpuValidation;
            }
            m_Impl->Specification.Render = renderSpecification;
            if (renderSpecification.Mode != RenderMode::Disabled)
            {
                m_Impl->Renderer = CreateRef<RenderSystem>(renderSpecification, m_Impl->Windowing,
                                                           m_Impl->PrimaryWindow, m_Impl->Assets);
            }
            if (m_Impl->Specification.Input.Mode == InputMode::Enabled)
            {
                m_Impl->InputService = CreateRef<InputSystem>(m_Impl->Specification.Input, m_Impl->Windowing,
                                                              m_Impl->Assets, m_Impl->EventSystem);
            }
            if (m_Impl->Specification.Ui.Mode != UiMode::Disabled)
            {
                m_Impl->UserInterface = std::make_unique<UiSystem>(m_Impl->Specification.Ui, *m_Impl->Windowing,
                                                                   *m_Impl->PrimaryWindow, *m_Impl->Renderer);
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
                    if (m_Impl->Assets)
                    {
                        (void)m_Impl->Assets->PumpCompletions();
                    }
                    if (m_Impl->SceneService)
                        m_Impl->SceneService->AdvanceFrame();
                }

                const bool nowSuspended =
                    m_Impl->Specification.SuspendWhenMainWindowMinimized && m_Impl->PrimaryWindow->Minimized();

                if (!ExitRequested() && m_Impl->InputService)
                {
                    m_Impl->InputService->AdvanceFrame(m_Impl->Clock->UnscaledDeltaTime(), UiCapture(), nowSuspended);
                }

                bool renderFrame = false;
                if (!ExitRequested() && !nowSuspended && m_Impl->Renderer)
                {
                    RenderSystemInternalAccess::BeginFrame(*m_Impl->Renderer);
                    renderFrame = true;
                }

                // Suspension is sampled before advancing Time. A minimize event can arrive later in this frame, but
                // every fixed step produced by AdvanceFrame must still be consumed before the next frame begins.
                if (!ExitRequested() && !suspended)
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
                }

                if (!ExitRequested() && !nowSuspended && m_Impl->UserInterface)
                {
                    m_Impl->UserInterface->BeginFrame(m_Impl->Clock->UnscaledDeltaTime(),
                                                      m_Impl->PrimaryWindow->LogicalSize());
                    m_Impl->LayerSystem->Ui(m_Impl->UserInterface->Frame());
                    m_Impl->UserInterface->EndFrame();
                    renderFrame = false;
                }

                if (!ExitRequested() && renderFrame)
                    RenderSystemInternalAccess::EndFrame(*m_Impl->Renderer, nullptr);

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

    Ref<AssetSystem> Application::Assets() const noexcept { return m_Impl->Assets; }

    Ref<Project> Application::GetProject() const noexcept { return m_Impl->ProjectService; }

    Ref<SceneSystem> Application::Scenes() const noexcept { return m_Impl->SceneService; }

    Ref<InputSystem> Application::Input() const noexcept { return m_Impl->InputService; }

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

    Ref<RenderSystem> Application::Renderer() const noexcept { return m_Impl->Renderer; }

    Ref<UndoService> Application::Undo() const noexcept { return m_Impl->UndoHistory; }

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

        if (m_Impl->UndoHistory)
        {
            m_Impl->UndoHistory->Close();
            m_Impl->UndoHistory.Reset();
        }

        if (m_Impl->UserInterface)
        {
            m_Impl->UserInterface->Shutdown();
            m_Impl->UserInterface.reset();
        }

        if (m_Impl->Renderer)
        {
            m_Impl->Renderer->Close();
            m_Impl->Renderer.Reset();
        }

        if (m_Impl->InputService)
        {
            m_Impl->InputService->Close();
            m_Impl->InputService.Reset();
        }

        if (m_Impl->SceneService)
        {
            m_Impl->SceneService->Close();
            m_Impl->SceneService.Reset();
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

        if (m_Impl->Assets)
        {
            m_Impl->Assets->Close();
            m_Impl->Assets.Reset();
        }

        m_Impl->ProjectService.Reset();

        m_Impl->EventSystem.Reset();

        if (m_Impl->Specification.ManageLogging)
        {
            Log::Shutdown();
        }
    }
} // namespace Keire
