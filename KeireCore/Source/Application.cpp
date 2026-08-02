#include "Keire/Application.h"

#include "Keire/Animation/AnimationSystem.h"
#include "Keire/Assets/BuiltinAssetRegistry.h"
#include "Keire/Assets/RenderingAssets.h"
#include "Keire/Audio/AudioAssets.h"
#include "Keire/Scenes/PrefabAsset.h"
#include "Keire/Scripting/ManagedAssemblyAsset.h"

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
        Ref<Profiler> ProfilerService;
        Ref<UndoService> UndoHistory;
        Ref<Project> ProjectService;
        Ref<AssetSystem> Assets;
        Ref<ScriptSystem> ScriptService;
        Ref<PhysicsSystem> PhysicsService;
        Ref<AudioSystem> AudioService;
        Ref<NavigationSystem> NavigationService;
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
            if (m_Impl->Specification.Profiling.Mode == ProfilerMode::Enabled)
                m_Impl->ProfilerService = CreateRef<Profiler>(m_Impl->Specification.Profiling);
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
                AppendMissingBuiltinAssetDecoders(m_Impl->Specification.Assets.Decoders);
            if (m_Impl->Specification.Assets.Mode != AssetMode::Disabled)
            {
                m_Impl->Assets = CreateRef<AssetSystem>(m_Impl->Specification.Assets, m_Impl->EventSystem);
            }
            if (m_Impl->Specification.Scripting.Mode == ScriptMode::Enabled)
            {
                if (!m_Impl->Assets)
                    throw std::invalid_argument("Enabled scripting requires enabled assets.");
                m_Impl->ScriptService = CreateRef<ScriptSystem>(m_Impl->Specification.Scripting);
                m_Impl->ScriptService->SetAssetSystem(m_Impl->Assets);
            }
            if (m_Impl->Specification.Physics.Mode == PhysicsMode::Enabled)
                m_Impl->PhysicsService = CreateRef<PhysicsSystem>(m_Impl->Specification.Physics);
            if (m_Impl->Specification.Navigation.Mode == NavigationMode::Enabled)
            {
                if (!m_Impl->Assets || !m_Impl->PhysicsService)
                    throw std::invalid_argument("Enabled navigation requires enabled assets and physics.");
                m_Impl->NavigationService = CreateRef<NavigationSystem>(m_Impl->Specification.Navigation);
            }
            if (m_Impl->Specification.Audio.Mode != AudioMode::Disabled)
            {
                if (!m_Impl->Assets)
                    throw std::invalid_argument("Enabled audio requires enabled assets.");
                m_Impl->AudioService = CreateRef<AudioSystem>(m_Impl->Specification.Audio);
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
                if (m_Impl->ProfilerService)
                    m_Impl->ProfilerService->BeginFrame();
                {
                    ProfileScope services(m_Impl->ProfilerService, ProfileCategory::Application, "Frame services");
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
                        if (m_Impl->ScriptService)
                            m_Impl->ScriptService->PumpManagedAssets();
                        if (m_Impl->SceneService)
                            m_Impl->SceneService->AdvanceFrame();
                    }
                }

                const bool nowSuspended =
                    m_Impl->Specification.SuspendWhenMainWindowMinimized && m_Impl->PrimaryWindow->Minimized();

                if (!ExitRequested() && m_Impl->InputService)
                {
                    ProfileScope input(m_Impl->ProfilerService, ProfileCategory::Application, "Input");
                    m_Impl->InputService->AdvanceFrame(m_Impl->Clock->UnscaledDeltaTime(), UiCapture(), nowSuspended);
                }
                if (!ExitRequested() && m_Impl->AudioService)
                {
                    ProfileScope audio(m_Impl->ProfilerService, ProfileCategory::Audio, "Audio update");
                    m_Impl->AudioService->Update(
                        std::chrono::duration<float>(static_cast<float>(m_Impl->Clock->UnscaledDeltaTime().Seconds())));
                }

                bool renderFrame = false;
                if (!ExitRequested() && !nowSuspended && m_Impl->Renderer)
                {
                    ProfileScope beginRender(m_Impl->ProfilerService, ProfileCategory::Rendering, "Render begin");
                    RenderSystemInternalAccess::BeginFrame(*m_Impl->Renderer);
                    renderFrame = true;
                }

                try
                {
                    // Suspension is sampled before advancing Time. A minimize event can arrive later in this frame,
                    // but every fixed step produced by AdvanceFrame must still be consumed before the next frame.
                    if (!ExitRequested() && !suspended)
                    {
                        {
                            ProfileScope fixedUpdate(m_Impl->ProfilerService, ProfileCategory::Application,
                                                     "Fixed update");
                            while (m_Impl->Clock->ConsumeFixedStep())
                            {
                                m_Impl->LayerSystem->FixedUpdate(*m_Impl->Clock);
                                if (ExitRequested())
                                {
                                    break;
                                }
                            }
                        }
                        if (!ExitRequested())
                        {
                            ProfileScope update(m_Impl->ProfilerService, ProfileCategory::Application, "Update");
                            m_Impl->LayerSystem->Update(*m_Impl->Clock);
                        }
                    }

                    if (!ExitRequested() && !nowSuspended && m_Impl->UserInterface)
                    {
                        {
                            ProfileScope editorUi(m_Impl->ProfilerService, ProfileCategory::Application, "Editor UI");
                            m_Impl->UserInterface->BeginFrame(m_Impl->Clock->UnscaledDeltaTime(),
                                                              m_Impl->PrimaryWindow->LogicalSize());
                            m_Impl->LayerSystem->Ui(m_Impl->UserInterface->Frame());
                        }
                        {
                            ProfileScope present(m_Impl->ProfilerService, ProfileCategory::Rendering, "Present");
                            m_Impl->UserInterface->EndFrame();
                        }
                        renderFrame = false;
                    }

                    // A frame begun before a layer requests exit must still be completed. Leaving the renderer in an
                    // active-frame state makes shutdown race GPU work in optimized builds.
                    if (renderFrame)
                    {
                        ProfileScope present(m_Impl->ProfilerService, ProfileCategory::Rendering, "Present");
                        RenderSystemInternalAccess::EndFrame(*m_Impl->Renderer, nullptr);
                        renderFrame = false;
                    }
                }
                catch (...)
                {
                    if (renderFrame)
                        RenderSystemInternalAccess::CancelFrame(*m_Impl->Renderer);
                    throw;
                }

                m_Impl->LayerSystem->ApplyPending();

                if (m_Impl->ProfilerService)
                {
                    m_Impl->ProfilerService->SetCounter(ProfileCategory::Application, "Frame",
                                                        static_cast<double>(m_Impl->Clock->FrameCount()));
                    const auto unscaledDeltaMilliseconds = m_Impl->Clock->UnscaledDeltaTime().Seconds() * 1000.0;
                    m_Impl->ProfilerService->SetCounter(ProfileCategory::Application, "Frame time (ms)",
                                                        unscaledDeltaMilliseconds);
                    m_Impl->ProfilerService->SetCounter(
                        ProfileCategory::Application, "FPS",
                        unscaledDeltaMilliseconds > 0.0 ? 1000.0 / unscaledDeltaMilliseconds : 0.0);
                    m_Impl->ProfilerService->SetCounter(ProfileCategory::Application, "Simulation delta (ms)",
                                                        m_Impl->Clock->DeltaTime().Seconds() * 1000.0);
                    m_Impl->ProfilerService->SetCounter(ProfileCategory::Application, "Fixed delta (ms)",
                                                        m_Impl->Clock->FixedDeltaTime().Seconds() * 1000.0);
                    if (m_Impl->Renderer)
                    {
                        const auto statistics = m_Impl->Renderer->Statistics();
                        m_Impl->ProfilerService->SetCounter(ProfileCategory::Rendering, "Passes", statistics.Passes);
                        m_Impl->ProfilerService->SetCounter(ProfileCategory::Rendering, "Surfaces",
                                                            statistics.Surfaces);
                        m_Impl->ProfilerService->SetCounter(ProfileCategory::Rendering, "Draw calls",
                                                            statistics.DrawCalls);
                        m_Impl->ProfilerService->SetCounter(ProfileCategory::Rendering, "Triangles",
                                                            statistics.Triangles);
                        m_Impl->ProfilerService->SetCounter(ProfileCategory::Rendering, "Visible submeshes",
                                                            statistics.VisibleSubmeshes);
                        m_Impl->ProfilerService->SetCounter(ProfileCategory::Rendering, "Culled submeshes",
                                                            statistics.CulledSubmeshes);
                        m_Impl->ProfilerService->SetCounter(ProfileCategory::Rendering, "Instance batches",
                                                            statistics.InstanceBatches);
                        m_Impl->ProfilerService->SetCounter(ProfileCategory::Rendering, "Visible local lights",
                                                            statistics.VisibleLocalLights);
                        m_Impl->ProfilerService->SetCounter(ProfileCategory::Rendering, "CPU preparation (ms)",
                                                            statistics.CpuPreparationMilliseconds);
                        m_Impl->ProfilerService->SetCounter(ProfileCategory::Rendering, "Command recording (ms)",
                                                            statistics.CommandRecordingMilliseconds);
                        m_Impl->ProfilerService->SetCounter(ProfileCategory::Rendering, "Skinning preparation (ms)",
                                                            statistics.SkinningPreparationMilliseconds);
                        m_Impl->ProfilerService->SetCounter(ProfileCategory::Rendering, "VFX preparation (ms)",
                                                            statistics.VfxPreparationMilliseconds);
                        m_Impl->ProfilerService->SetCounter(ProfileCategory::Rendering, "Draw preparation (ms)",
                                                            statistics.DrawPreparationMilliseconds);
                        m_Impl->ProfilerService->SetCounter(ProfileCategory::Rendering, "Shadow recording (ms)",
                                                            statistics.ShadowRecordingMilliseconds);
                        m_Impl->ProfilerService->SetCounter(ProfileCategory::Rendering, "Forward+ culling (ms)",
                                                            statistics.ForwardPlusCullingMilliseconds);
                        m_Impl->ProfilerService->SetCounter(ProfileCategory::Rendering, "Scene pass (ms)",
                                                            statistics.ScenePassMilliseconds);
                        m_Impl->ProfilerService->SetCounter(ProfileCategory::Rendering, "Depth pass (ms)",
                                                            statistics.DepthPassMilliseconds);
                        m_Impl->ProfilerService->SetCounter(ProfileCategory::Rendering, "Tone mapping (ms)",
                                                            statistics.ToneMapMilliseconds);
                        m_Impl->ProfilerService->SetCounter(ProfileCategory::Rendering, "Command unattributed (ms)",
                                                            statistics.CommandRecordingUnattributedMilliseconds);
                        m_Impl->ProfilerService->SetCounter(ProfileCategory::Rendering, "Frame uploads (ms)",
                                                            statistics.FrameUploadMilliseconds);
                        m_Impl->ProfilerService->SetCounter(ProfileCategory::Rendering, "Frame upload submissions",
                                                            static_cast<double>(statistics.FrameUploadSubmissions));
                        m_Impl->ProfilerService->SetCounter(
                            ProfileCategory::Rendering, "Forward+ buffer reallocations",
                            static_cast<double>(statistics.ForwardPlusBufferReallocations));
                        m_Impl->ProfilerService->SetCounter(ProfileCategory::Rendering, "Forward+ cache hits",
                                                            static_cast<double>(statistics.ForwardPlusCacheHits));
                        m_Impl->ProfilerService->SetCounter(ProfileCategory::Rendering, "Forward+ upload bytes",
                                                            static_cast<double>(statistics.ForwardPlusUploadBytes));
                        m_Impl->ProfilerService->SetCounter(ProfileCategory::Rendering, "VFX GPU worlds",
                                                            static_cast<double>(statistics.VfxGpuWorlds));
                        m_Impl->ProfilerService->SetCounter(ProfileCategory::Rendering, "VFX GPU buffer bytes",
                                                            static_cast<double>(statistics.VfxGpuBufferBytes));
                        m_Impl->ProfilerService->SetCounter(ProfileCategory::Rendering, "VFX GPU particle capacity",
                                                            static_cast<double>(statistics.VfxGpuParticleCapacity));
                        m_Impl->ProfilerService->SetCounter(ProfileCategory::Rendering, "VFX compute dispatches",
                                                            static_cast<double>(statistics.VfxComputeDispatches));
                        m_Impl->ProfilerService->SetCounter(ProfileCategory::Rendering, "VFX compute thread groups",
                                                            static_cast<double>(statistics.VfxComputeThreadGroups));
                        m_Impl->ProfilerService->SetCounter(ProfileCategory::Rendering, "VFX indirect draws",
                                                            static_cast<double>(statistics.VfxIndirectDraws));
                        m_Impl->ProfilerService->SetCounter(ProfileCategory::Rendering, "VFX pipeline warmup pending",
                                                            statistics.VfxPipelineWarmupPending ? 1.0 : 0.0);
                        m_Impl->ProfilerService->SetCounter(ProfileCategory::Rendering, "VFX pipelines ready",
                                                            statistics.VfxPipelinesReady ? 1.0 : 0.0);
                        m_Impl->ProfilerService->SetCounter(ProfileCategory::Rendering, "VFX pipeline warmup (ms)",
                                                            statistics.VfxPipelineWarmupMilliseconds);
                        m_Impl->ProfilerService->SetCounter(ProfileCategory::Rendering, "GPU fence wait (ms)",
                                                            statistics.GpuFenceWaitMilliseconds);
                        m_Impl->ProfilerService->SetCounter(ProfileCategory::Rendering, "Swapchain wait (ms)",
                                                            statistics.SwapchainWaitMilliseconds);
                        m_Impl->ProfilerService->SetCounter(ProfileCategory::Rendering, "Allowed frames in flight",
                                                            statistics.AllowedFramesInFlight);
                        m_Impl->ProfilerService->SetCounter(ProfileCategory::Rendering, "UI recording (ms)",
                                                            statistics.UiRecordingMilliseconds);
                        m_Impl->ProfilerService->SetCounter(ProfileCategory::Rendering, "GPU submission CPU (ms)",
                                                            statistics.GpuSubmissionMilliseconds);
                        m_Impl->ProfilerService->SetCounter(ProfileCategory::Rendering, "GPU timing supported",
                                                            statistics.GpuTimingSupported ? 1.0 : 0.0);
                        if (statistics.GpuTimingSupported)
                            m_Impl->ProfilerService->SetCounter(ProfileCategory::Rendering, "GPU frame (ms)",
                                                                statistics.GpuFrameMilliseconds);
                        m_Impl->ProfilerService->SetCounter(ProfileCategory::Rendering, "Renderer latency (ms)",
                                                            statistics.RendererLatencyMilliseconds);
                    }
                    if (m_Impl->Assets)
                    {
                        const auto statistics = m_Impl->Assets->Statistics();
                        m_Impl->ProfilerService->SetCounter(ProfileCategory::Assets, "Known assets",
                                                            static_cast<double>(statistics.KnownAssets));
                        m_Impl->ProfilerService->SetCounter(ProfileCategory::Assets, "Queued assets",
                                                            static_cast<double>(statistics.QueuedAssets));
                        m_Impl->ProfilerService->SetCounter(ProfileCategory::Assets, "Loading assets",
                                                            static_cast<double>(statistics.LoadingAssets));
                        m_Impl->ProfilerService->SetCounter(ProfileCategory::Assets, "Resident bytes",
                                                            static_cast<double>(statistics.ResidentBytes));
                        m_Impl->ProfilerService->SetCounter(ProfileCategory::Assets, "Failed loads",
                                                            static_cast<double>(statistics.FailedLoads));
                        m_Impl->ProfilerService->SetCounter(ProfileCategory::Assets, "Evictions",
                                                            static_cast<double>(statistics.Evictions));
                    }
                    if (m_Impl->AudioService)
                    {
                        const auto statistics = m_Impl->AudioService->Statistics();
                        m_Impl->ProfilerService->SetCounter(ProfileCategory::Audio, "Voices",
                                                            static_cast<double>(statistics.Voices));
                        m_Impl->ProfilerService->SetCounter(ProfileCategory::Audio, "Audible voices",
                                                            static_cast<double>(statistics.AudibleVoices));
                        m_Impl->ProfilerService->SetCounter(ProfileCategory::Audio, "Virtual voices",
                                                            static_cast<double>(statistics.VirtualVoices));
                        m_Impl->ProfilerService->SetCounter(ProfileCategory::Audio, "Underruns",
                                                            static_cast<double>(statistics.Underruns));
                    }
                    if (m_Impl->ScriptService)
                    {
                        const auto metrics = m_Impl->ScriptService->Metrics();
                        m_Impl->ProfilerService->SetCounter(ProfileCategory::Scripting, "Generation",
                                                            static_cast<double>(metrics.Generation));
                        m_Impl->ProfilerService->SetCounter(ProfileCategory::Scripting, "Active instances",
                                                            static_cast<double>(metrics.ActiveInstances));
                        m_Impl->ProfilerService->SetCounter(ProfileCategory::Scripting, "Faulted instances",
                                                            static_cast<double>(metrics.FaultedInstances));
                        m_Impl->ProfilerService->SetCounter(ProfileCategory::Scripting, "Diagnostics",
                                                            static_cast<double>(metrics.Diagnostics));
                        m_Impl->ProfilerService->SetCounter(ProfileCategory::Scripting, "Callback invocations",
                                                            static_cast<double>(metrics.CallbackInvocations));
                        m_Impl->ProfilerService->SetCounter(ProfileCategory::Scripting, "Skipped callbacks",
                                                            static_cast<double>(metrics.SkippedCallbacks));
                        m_Impl->ProfilerService->SetCounter(ProfileCategory::Scripting, "Managed interop calls",
                                                            static_cast<double>(metrics.ManagedInteropCalls));
                        m_Impl->ProfilerService->SetCounter(ProfileCategory::Scripting, "Callback total (ms)",
                                                            metrics.CallbackMilliseconds);
                        m_Impl->ProfilerService->SetCounter(ProfileCategory::Scripting, "Callback maximum (ms)",
                                                            metrics.MaximumCallbackMilliseconds);
                    }
                    m_Impl->ProfilerService->EndFrame();
                }

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

    Ref<Profiler> Application::GetProfiler() const noexcept { return m_Impl->ProfilerService; }

    Ref<AssetSystem> Application::Assets() const noexcept { return m_Impl->Assets; }

    Ref<ScriptSystem> Application::Scripts() const noexcept { return m_Impl->ScriptService; }

    Ref<PhysicsSystem> Application::Physics() const noexcept { return m_Impl->PhysicsService; }

    Ref<AudioSystem> Application::Audio() const noexcept { return m_Impl->AudioService; }

    Ref<NavigationSystem> Application::Navigation() const noexcept { return m_Impl->NavigationService; }

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

        if (m_Impl->AudioService)
        {
            m_Impl->AudioService->Close();
            m_Impl->AudioService.Reset();
        }

        if (m_Impl->NavigationService)
        {
            m_Impl->NavigationService->Close();
            m_Impl->NavigationService.Reset();
        }

        if (m_Impl->PhysicsService)
        {
            m_Impl->PhysicsService->Close();
            m_Impl->PhysicsService.Reset();
        }

        if (m_Impl->ScriptService)
        {
            m_Impl->ScriptService->Close();
            m_Impl->ScriptService.Reset();
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

        if (m_Impl->ProfilerService)
        {
            m_Impl->ProfilerService->Close();
            m_Impl->ProfilerService.Reset();
        }

        m_Impl->EventSystem.Reset();

        if (m_Impl->Specification.ManageLogging)
        {
            Log::Shutdown();
        }
    }
} // namespace Keire
