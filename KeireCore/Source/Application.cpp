#include "Keire/Application.h"

#include "Keire/Animation/AnimationSystem.h"
#include "Keire/Assets/BuiltinAssetRegistry.h"
#include "Keire/Assets/RenderingAssets.h"
#include "Keire/Audio/AudioAssets.h"
#include "Keire/Scenes/PrefabAsset.h"
#include "Keire/Scripting/ManagedAssemblyAsset.h"

#include "KeireInternal/Diagnostics/TelemetryInternal.h"
#include "KeireInternal/RenderInternal.h"
#include "KeireInternal/UiInternal.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <exception>
#include <limits>
#include <map>
#include <set>
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
        Ref<DiagnosticCatalog> DiagnosticDefinitionService;
        Ref<DiagnosticSink> DiagnosticReportService;
        Ref<MemorySystem> MemoryService;
        MemoryDomain AssetMemoryDomain;
        MemoryDomain RendererMemoryDomain;
        MemoryDomain RetiredGpuMemoryDomain;
        MemoryDomain PhysicsMemoryDomain;
        MemoryDomain AudioMemoryDomain;
        MemoryDomain AnimationMemoryDomain;
        MemoryDomain ScriptingMemoryDomain;
        MemoryDomain NavigationMemoryDomain;
        MemoryDomain EditorMemoryDomain;
        std::unique_ptr<TrackedMemoryResource> JobMemoryResource;
        Ref<StringInterner> StringService;
        Ref<JobSystem> JobService;
        Ref<ModuleRegistry> ModuleService;
        Ref<UndoService> UndoHistory;
        Ref<Project> ProjectService;
        Ref<AssetSystem> Assets;
        Ref<StreamingSystem> StreamingService;
        Ref<ReplaySystem> ReplayService;
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
        std::uint64_t FixedTick = 0;
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

        Internal::TelemetrySetThreadName("Application owner");

        const auto requestedExitCode = m_Impl->ExitCode.load(std::memory_order_acquire);
        if (requestedExitCode != Impl::NoExitRequested)
        {
            m_Impl->RuntimeState = Impl::State::Stopped;
            return requestedExitCode;
        }

        m_Impl->RuntimeState = Impl::State::Running;
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
            m_Impl->DiagnosticDefinitionService = CreateRef<DiagnosticCatalog>(m_Impl->Specification.Diagnostics);
            m_Impl->DiagnosticReportService =
                CreateRef<DiagnosticSink>(m_Impl->Specification.Diagnostics.MaximumRetainedDiagnostics);
            m_Impl->DiagnosticDefinitionService->Register(
                {DiagnosticId("KEIRE-AUDIO-0001"), "Audio streaming underrun",
                 "The audio callback emitted silence because a decoded streaming page was unavailable.",
                 "KEIRE-AUDIO-0001.md"});
            m_Impl->DiagnosticDefinitionService->Register(
                {DiagnosticId("KEIRE-REPLAY-0001"), "Replay divergence",
                 "Canonical deterministic state differs from the recorded state for a fixed tick.",
                 "KEIRE-REPLAY-0001.md"});
            m_Impl->DiagnosticDefinitionService->Register(
                {DiagnosticId("KEIRE-REPLAY-0002"), "Replay session failure",
                 "Replay recording, decoding, restoration, or finalization failed and the session was stopped.",
                 "KEIRE-REPLAY-0002.md"});
            m_Impl->MemoryService = CreateRef<MemorySystem>(m_Impl->Specification.Memory);
            m_Impl->StringService = CreateRef<StringInterner>();
            const auto jobMemoryDomain = m_Impl->MemoryService->RegisterDomain("Jobs");
            m_Impl->JobMemoryResource = m_Impl->MemoryService->CreateTrackedResource(jobMemoryDomain);
            m_Impl->AssetMemoryDomain = m_Impl->MemoryService->RegisterDomain("Assets");
            m_Impl->RendererMemoryDomain = m_Impl->MemoryService->RegisterDomain("Renderer");
            m_Impl->RetiredGpuMemoryDomain =
                m_Impl->MemoryService->RegisterDomain("Retired GPU Resources", m_Impl->RendererMemoryDomain);
            m_Impl->PhysicsMemoryDomain = m_Impl->MemoryService->RegisterDomain("Physics");
            m_Impl->AudioMemoryDomain = m_Impl->MemoryService->RegisterDomain("Audio");
            m_Impl->AnimationMemoryDomain = m_Impl->MemoryService->RegisterDomain("Animation");
            m_Impl->ScriptingMemoryDomain = m_Impl->MemoryService->RegisterDomain("Scripting");
            m_Impl->NavigationMemoryDomain = m_Impl->MemoryService->RegisterDomain("Navigation");
            m_Impl->EditorMemoryDomain = m_Impl->MemoryService->RegisterDomain("Editor");
            m_Impl->JobService = CreateRef<JobSystem>(m_Impl->Specification.Jobs, m_Impl->JobMemoryResource.get());
            m_Impl->ModuleService = CreateRef<ModuleRegistry>(m_Impl->Specification.Modules);
            if (m_Impl->ProjectService)
                m_Impl->ModuleService->ValidateRequired(m_Impl->ProjectService->Descriptor().RequiredModules);
            for (const auto& definition : m_Impl->ModuleService->Diagnostics())
                m_Impl->DiagnosticDefinitionService->Register(definition);
            const auto moduleDomainRegistrations = m_Impl->ModuleService->MemoryDomains();
            std::set<std::string, std::less<>> moduleDomainNames;
            for (const auto& domain : moduleDomainRegistrations)
            {
                if (domain.Name.empty())
                    throw std::invalid_argument("Module memory domain names cannot be empty.");
                if (!moduleDomainNames.emplace(domain.Name).second)
                    throw std::invalid_argument("Duplicate module memory domain: " + domain.Name);
            }
            for (const auto& domain : moduleDomainRegistrations)
            {
                if (!domain.Parent.empty() && !moduleDomainNames.contains(domain.Parent))
                    throw std::invalid_argument("Unknown parent '" + domain.Parent + "' for module memory domain '" +
                                                domain.Name + "'.");
            }
            std::map<std::string, MemoryDomain, std::less<>> moduleMemoryDomains;
            auto pendingModuleDomains = moduleDomainRegistrations;
            while (!pendingModuleDomains.empty())
            {
                const auto previousCount = pendingModuleDomains.size();
                std::erase_if(pendingModuleDomains,
                              [&](const ModuleMemoryDomainRegistration& domain)
                              {
                                  if (!domain.Parent.empty() && !moduleMemoryDomains.contains(domain.Parent))
                                      return false;
                                  const auto parent = domain.Parent.empty() ? m_Impl->MemoryService->RootDomain()
                                                                            : moduleMemoryDomains.at(domain.Parent);
                                  moduleMemoryDomains.emplace(
                                      domain.Name, m_Impl->MemoryService->RegisterDomain(domain.Name, parent));
                                  return true;
                              });
                if (pendingModuleDomains.size() == previousCount)
                    throw std::invalid_argument("Module memory domains contain a parent cycle involving '" +
                                                pendingModuleDomains.front().Name + "'.");
            }
            m_Impl->DiagnosticDefinitionService->Freeze();
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
                for (auto& decoder : m_Impl->ModuleService->Decoders())
                {
                    const auto duplicate = std::ranges::find(m_Impl->Specification.Assets.Decoders, decoder.Type,
                                                             &AssetDecoderRegistration::Type);
                    if (duplicate != m_Impl->Specification.Assets.Decoders.end())
                        throw std::invalid_argument("A module asset decoder duplicates an existing decoder.");
                    m_Impl->Specification.Assets.Decoders.push_back(std::move(decoder));
                }
                AppendMissingBuiltinAssetDecoders(m_Impl->Specification.Assets.Decoders);
            }
            if (m_Impl->Specification.Assets.Mode != AssetMode::Disabled)
            {
                m_Impl->Assets =
                    CreateRef<AssetSystem>(m_Impl->Specification.Assets, m_Impl->EventSystem, m_Impl->JobService);
                m_Impl->StreamingService =
                    CreateRef<StreamingSystem>(m_Impl->Specification.Streaming, m_Impl->Assets,
                                               m_Impl->DiagnosticReportService, m_Impl->MemoryService);
            }
            if (m_Impl->Specification.Scripting.Mode == ScriptMode::Enabled)
            {
                if (!m_Impl->Assets)
                    throw std::invalid_argument("Enabled scripting requires enabled assets.");
                m_Impl->ScriptService = CreateRef<ScriptSystem>(m_Impl->Specification.Scripting, m_Impl->JobService);
                m_Impl->ScriptService->SetAssetSystem(m_Impl->Assets);
            }
            if (m_Impl->Specification.Physics.Mode == PhysicsMode::Enabled)
                m_Impl->PhysicsService = CreateRef<PhysicsSystem>(m_Impl->Specification.Physics, m_Impl->JobService);
            if (m_Impl->Specification.Navigation.Mode == NavigationMode::Enabled)
            {
                if (!m_Impl->Assets || !m_Impl->PhysicsService)
                    throw std::invalid_argument("Enabled navigation requires enabled assets and physics.");
                m_Impl->NavigationService =
                    CreateRef<NavigationSystem>(m_Impl->Specification.Navigation, m_Impl->JobService);
            }
            if (m_Impl->Specification.Audio.Mode != AudioMode::Disabled)
            {
                if (!m_Impl->Assets)
                    throw std::invalid_argument("Enabled audio requires enabled assets.");
                m_Impl->AudioService = CreateRef<AudioSystem>(m_Impl->Specification.Audio);
            }
            if (m_Impl->Specification.Scenes.Mode == SceneMode::Enabled)
            {
                if (!m_Impl->Specification.Scenes.Components)
                    m_Impl->Specification.Scenes.Components = ComponentRegistry::CreateDefault();
                m_Impl->Specification.Scenes.Components->ReplaceBatch({}, m_Impl->ModuleService->Components());
                m_Impl->SceneService =
                    CreateRef<SceneSystem>(m_Impl->Specification.Scenes, m_Impl->Assets, m_Impl->EventSystem);
            }
            m_Impl->Clock = std::make_unique<Time>(m_Impl->Specification.Timing);
            m_Impl->Windowing = CreateRef<WindowSystem>(m_Impl->Specification.Windowing);
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
                m_Impl->Renderer =
                    CreateRef<RenderSystem>(renderSpecification, m_Impl->Windowing, m_Impl->PrimaryWindow,
                                            m_Impl->Assets, m_Impl->JobService, m_Impl->StreamingService);
            }
            if (m_Impl->Specification.Input.Mode == InputMode::Enabled)
            {
                m_Impl->InputService = CreateRef<InputSystem>(m_Impl->Specification.Input, m_Impl->Windowing,
                                                              m_Impl->Assets, m_Impl->EventSystem);
            }
            m_Impl->ReplayService = CreateRef<ReplaySystem>(m_Impl->Specification.Replay,
                                                            m_Impl->DiagnosticReportService, m_Impl->MemoryService);
            for (auto& serializer : m_Impl->ModuleService->ReplaySerializers())
                m_Impl->ReplayService->RegisterSerializer(std::move(serializer));
            if (m_Impl->Specification.Ui.Mode != UiMode::Disabled)
            {
                m_Impl->UserInterface = std::make_unique<UiSystem>(m_Impl->Specification.Ui, *m_Impl->Windowing,
                                                                   *m_Impl->PrimaryWindow, *m_Impl->Renderer);
            }
            m_Impl->LayerListener = m_Impl->EventSystem->SubscribeAny([this](const EventView& event)
                                                                      { return m_Impl->LayerSystem->Dispatch(event); },
                                                                      EventPriorities::Normal);

            m_Impl->LayerSystem->Activate();
            m_Impl->ModuleService->Start(*this);
            OnInitialize();
            initialized = true;

            auto previousFrame = std::chrono::steady_clock::now();
            while (!ExitRequested() && m_Impl->PrimaryWindow->IsOpen())
            {
                KEIRE_TELEMETRY_ZONE_SCOPED("Application frame");
                m_Impl->MemoryService->ResetFrameArena();
                const auto frameStart = std::chrono::steady_clock::now();
                const bool suspended =
                    m_Impl->Specification.SuspendWhenMainWindowMinimized && m_Impl->PrimaryWindow->Minimized();
                const auto rawDelta = TimeStep::FromChrono(frameStart - previousFrame);
                const auto pumpRecoveryEvents = [this]
                {
                    while (const auto event = m_Impl->Windowing->PollEvent())
                    {
                        (void)DispatchWindowEvent(*event);
                        if (ExitRequested())
                            break;
                    }
                    return !ExitRequested();
                };

                bool recoveryPaused = false;
                if (!ExitRequested() && m_Impl->Renderer)
                {
                    recoveryPaused =
                        RenderSystemInternalAccess::WaitForDeviceRecovery(*m_Impl->Renderer, pumpRecoveryEvents);
                }
                if (recoveryPaused)
                {
                    // The recovery boundary is a window/exit-only frame. Do not charge the recovery wait to Time or
                    // run layer attachment, managed, simulation, input, audio, rendering, or UI work with the stale
                    // pre-wait delta. The next frame starts from a fresh wall-clock sample.
                    previousFrame = std::chrono::steady_clock::now();
                    continue;
                }
                previousFrame = frameStart;

                // Preserve the healthy-frame contract: pending layer changes become visible before this frame's
                // window events. Recovery is checked above so attach/detach callbacks never run while the GPU owner
                // is waiting for its safe-boundary handshake.
                m_Impl->LayerSystem->ApplyPending();
                while (const auto event = m_Impl->Windowing->PollEvent())
                {
                    (void)DispatchWindowEvent(*event);
                    if (ExitRequested())
                        break;
                }

                m_Impl->Clock->AdvanceFrame(rawDelta, suspended);
                if (m_Impl->ProfilerService)
                    m_Impl->ProfilerService->BeginFrame();
                {
                    ProfileScope services(m_Impl->ProfilerService, ProfileCategory::Application, "Frame services");
                    if (!ExitRequested())
                    {
                        (void)m_Impl->EventSystem->DispatchQueued();
                        if (m_Impl->Assets)
                        {
                            (void)m_Impl->Assets->PumpCompletions();
                        }
                        if (m_Impl->StreamingService)
                            (void)m_Impl->StreamingService->Pump();
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
                    m_Impl->InputService->SetGameplayPlayback(m_Impl->ReplayService &&
                                                              m_Impl->ReplayService->ReplacesGameplayInput());
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

                bool recoveryBoundaryRequired = false;
                try
                {
                    // Suspension is sampled before advancing Time. A minimize event can arrive later in this frame,
                    // but every fixed step produced by AdvanceFrame must still be consumed before the next frame.
                    if (!ExitRequested() && !suspended)
                    {
                        if (m_Impl->JobService && m_Impl->ReplayService)
                            m_Impl->JobService->SetDeterministicSimulation(
                                m_Impl->ReplayService->UsesStrictScheduling());
                        {
                            ProfileScope fixedUpdate(m_Impl->ProfilerService, ProfileCategory::Application,
                                                     "Fixed update");
                            while (m_Impl->Clock->PendingFixedSteps() != 0)
                            {
                                if (m_Impl->ReplayService && !m_Impl->ReplayService->ShouldAdvanceFixedTick())
                                {
                                    (void)m_Impl->Clock->DiscardFixedSteps();
                                    break;
                                }
                                if (!m_Impl->Clock->ConsumeFixedStep())
                                    break;
                                ++m_Impl->FixedTick;
                                FixedTickInputSnapshot fixedInput;
                                fixedInput.Tick = m_Impl->FixedTick;
                                if (m_Impl->InputService)
                                    fixedInput = m_Impl->InputService->CaptureFixedTick(m_Impl->FixedTick);
                                if (m_Impl->ReplayService)
                                    fixedInput = m_Impl->ReplayService->BeginFixedTick(fixedInput);
                                if (m_Impl->InputService && m_Impl->ReplayService &&
                                    m_Impl->ReplayService->ReplacesGameplayInput())
                                    m_Impl->InputService->ApplyFixedTick(fixedInput);
                                m_Impl->LayerSystem->FixedUpdate(*m_Impl->Clock);
                                if (m_Impl->ReplayService)
                                    m_Impl->ReplayService->EndFixedTick(m_Impl->FixedTick);
                                if (ExitRequested())
                                {
                                    break;
                                }
                            }
                        }
                    }
                    if (!ExitRequested() && (!suspended || m_Impl->Specification.UpdateLayersWhenMainWindowMinimized))
                    {
                        ProfileScope update(m_Impl->ProfilerService, ProfileCategory::Application, "Update");
                        m_Impl->LayerSystem->Update(*m_Impl->Clock);
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
                catch (const RenderRecoveryBoundaryRequired&)
                {
                    if (renderFrame)
                        RenderSystemInternalAccess::CancelFrame(*m_Impl->Renderer);
                    renderFrame = false;
                    recoveryBoundaryRequired = true;
                }
                catch (...)
                {
                    if (renderFrame)
                        RenderSystemInternalAccess::CancelFrame(*m_Impl->Renderer);
                    throw;
                }

                if (recoveryBoundaryRequired)
                {
                    // This owner frame is intentionally abandoned before the recovery wait, but its profiler frame
                    // still needs a deterministic terminal boundary. Otherwise the resumed application attempts to
                    // begin a second recording over the interrupted one.
                    if (m_Impl->ProfilerService)
                        m_Impl->ProfilerService->EndFrame();
                    (void)RenderSystemInternalAccess::WaitForDeviceRecovery(*m_Impl->Renderer, pumpRecoveryEvents);
                    previousFrame = std::chrono::steady_clock::now();
                    continue;
                }

                m_Impl->LayerSystem->ApplyPending();

                if (m_Impl->MemoryService)
                {
                    if (m_Impl->Assets)
                        m_Impl->MemoryService->ReportExternal(m_Impl->AssetMemoryDomain,
                                                              m_Impl->Assets->Statistics().ResidentBytes);
                    if (m_Impl->Renderer)
                    {
                        const auto statistics = m_Impl->Renderer->Statistics();
                        const auto activeBytes =
                            statistics.ActiveTransientBytes >
                                    std::numeric_limits<std::size_t>::max() - statistics.VfxGpuBufferBytes
                                ? std::numeric_limits<std::size_t>::max()
                                : static_cast<std::size_t>(statistics.ActiveTransientBytes +
                                                           statistics.VfxGpuBufferBytes);
                        m_Impl->MemoryService->ReportExternal(m_Impl->RendererMemoryDomain, activeBytes);
                        m_Impl->MemoryService->ReportExternal(
                            m_Impl->RetiredGpuMemoryDomain,
                            static_cast<std::size_t>(std::min<std::uint64_t>(statistics.FenceRetiredBytes,
                                                                             std::numeric_limits<std::size_t>::max())));
                    }
                }

                if (m_Impl->Renderer)
                {
                    const auto statistics = m_Impl->Renderer->Statistics();
                    Internal::TelemetryPlot("GPU occlusion candidates",
                                            static_cast<double>(statistics.GpuOcclusionCandidates));
                    Internal::TelemetryPlot("GPU occlusion visible",
                                            static_cast<double>(statistics.GpuOcclusionVisible));
                    Internal::TelemetryPlot("GPU occlusion culled", static_cast<double>(statistics.GpuOcclusionCulled));
                    Internal::TelemetryPlot("GPU occlusion recording (ms)",
                                            static_cast<double>(statistics.GpuOcclusionDepthPassMilliseconds +
                                                                statistics.GpuOcclusionPyramidRecordingMilliseconds +
                                                                statistics.GpuOcclusionCullingRecordingMilliseconds));
                }

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
                    if (m_Impl->JobService)
                    {
                        const auto statistics = m_Impl->JobService->Statistics();
                        m_Impl->ProfilerService->SetCounter(ProfileCategory::Application, "Jobs queued",
                                                            static_cast<double>(statistics.QueuedJobs));
                        m_Impl->ProfilerService->SetCounter(ProfileCategory::Application, "Jobs running",
                                                            static_cast<double>(statistics.RunningJobs));
                        m_Impl->ProfilerService->SetCounter(ProfileCategory::Application, "Jobs stolen",
                                                            static_cast<double>(statistics.StolenJobs));
                    }
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
                        m_Impl->ProfilerService->SetCounter(
                            ProfileCategory::Rendering, "Dynamic upload buffer reallocations",
                            static_cast<double>(statistics.DynamicUploadBufferReallocations));
                        m_Impl->ProfilerService->SetCounter(ProfileCategory::Rendering, "Dynamic upload bytes",
                                                            static_cast<double>(statistics.DynamicUploadBytes));
                        m_Impl->ProfilerService->SetCounter(ProfileCategory::Rendering, "Forward+ cache hits",
                                                            static_cast<double>(statistics.ForwardPlusCacheHits));
                        m_Impl->ProfilerService->SetCounter(ProfileCategory::Rendering, "Depth draw calls",
                                                            static_cast<double>(statistics.DepthDrawCalls));
                        m_Impl->ProfilerService->SetCounter(ProfileCategory::Rendering, "Depth triangles",
                                                            static_cast<double>(statistics.DepthTriangles));
                        m_Impl->ProfilerService->SetCounter(ProfileCategory::Rendering, "Shadow draw calls",
                                                            static_cast<double>(statistics.ShadowDrawCalls));
                        m_Impl->ProfilerService->SetCounter(ProfileCategory::Rendering, "Shadow triangles",
                                                            static_cast<double>(statistics.ShadowTriangles));
                        m_Impl->ProfilerService->SetCounter(ProfileCategory::Rendering, "Culled shadow submeshes",
                                                            static_cast<double>(statistics.CulledShadowSubmeshes));
                        m_Impl->ProfilerService->SetCounter(ProfileCategory::Rendering, "Culled local lights",
                                                            static_cast<double>(statistics.CulledLocalLights));
                        m_Impl->ProfilerService->SetCounter(ProfileCategory::Rendering, "Forward+ upload bytes",
                                                            static_cast<double>(statistics.ForwardPlusUploadBytes));
                        m_Impl->ProfilerService->SetCounter(ProfileCategory::Rendering, "VFX GPU worlds",
                                                            static_cast<double>(statistics.VfxGpuWorlds));
                        m_Impl->ProfilerService->SetCounter(ProfileCategory::Rendering, "Culled CPU VFX particles",
                                                            static_cast<double>(statistics.CulledCpuVfxParticles));
                        m_Impl->ProfilerService->SetCounter(ProfileCategory::Rendering, "Dropped VFX particles",
                                                            static_cast<double>(statistics.DroppedVfxParticles));
                        m_Impl->ProfilerService->SetCounter(ProfileCategory::Rendering, "VFX GPU buffer bytes",
                                                            static_cast<double>(statistics.VfxGpuBufferBytes));
                        m_Impl->ProfilerService->SetCounter(ProfileCategory::Rendering, "Fence-retired bytes",
                                                            static_cast<double>(statistics.FenceRetiredBytes));
                        m_Impl->ProfilerService->SetCounter(ProfileCategory::Rendering, "Active transient bytes",
                                                            static_cast<double>(statistics.ActiveTransientBytes));
                        m_Impl->ProfilerService->SetCounter(ProfileCategory::Rendering, "Aliasing saved bytes",
                                                            static_cast<double>(statistics.SavedAliasingBytes));
                        m_Impl->ProfilerService->SetCounter(ProfileCategory::Rendering, "VFX GPU particle capacity",
                                                            static_cast<double>(statistics.VfxGpuParticleCapacity));
                        m_Impl->ProfilerService->SetCounter(ProfileCategory::Rendering, "VFX compute dispatches",
                                                            static_cast<double>(statistics.VfxComputeDispatches));
                        m_Impl->ProfilerService->SetCounter(ProfileCategory::Rendering, "VFX compute thread groups",
                                                            static_cast<double>(statistics.VfxComputeThreadGroups));
                        m_Impl->ProfilerService->SetCounter(ProfileCategory::Rendering, "VFX indirect draws",
                                                            static_cast<double>(statistics.VfxIndirectDraws));
                        m_Impl->ProfilerService->SetCounter(ProfileCategory::Rendering, "CPU VFX draw batches",
                                                            static_cast<double>(statistics.CpuVfxDrawBatches));
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
                        m_Impl->ProfilerService->SetCounter(ProfileCategory::Rendering, "GPU completion latency (ms)",
                                                            statistics.GpuCompletionLatencyMilliseconds);
                        m_Impl->ProfilerService->SetCounter(ProfileCategory::Rendering,
                                                            "VFX GPU completion latency (ms)",
                                                            statistics.VfxGpuCompletionLatencyMilliseconds);
                        m_Impl->ProfilerService->SetCounter(ProfileCategory::Rendering, "GPU timing supported",
                                                            statistics.GpuTimingSupported ? 1.0 : 0.0);
                        if (statistics.GpuTimingSupported)
                            m_Impl->ProfilerService->SetCounter(ProfileCategory::Rendering, "GPU frame (ms)",
                                                                statistics.GpuFrameMilliseconds);
                        m_Impl->ProfilerService->SetCounter(ProfileCategory::Rendering, "Renderer latency (ms)",
                                                            statistics.RendererLatencyMilliseconds);
                        m_Impl->ProfilerService->SetCounter(ProfileCategory::Rendering,
                                                            "Frame admission wait (ms)",
                                                            statistics.FrameAdmissionWaitMilliseconds);
                        m_Impl->ProfilerService->SetCounter(ProfileCategory::Rendering,
                                                            "Renderer queue delay (ms)",
                                                            statistics.RendererQueueDelayMilliseconds);
                        m_Impl->ProfilerService->SetCounter(ProfileCategory::Rendering, "Outstanding frames",
                                                            static_cast<double>(statistics.OutstandingFrames));
                        m_Impl->ProfilerService->SetCounter(
                            ProfileCategory::Rendering, "Frames in flight high-water",
                            static_cast<double>(statistics.FramesInFlightHighWaterMark));
                        m_Impl->ProfilerService->SetCounter(
                            ProfileCategory::Rendering, "Renderer queue high-water",
                            static_cast<double>(statistics.RendererQueueHighWaterMark));
                        m_Impl->ProfilerService->SetCounter(ProfileCategory::Rendering, "GPU occlusion candidates",
                                                            static_cast<double>(statistics.GpuOcclusionCandidates));
                        m_Impl->ProfilerService->SetCounter(ProfileCategory::Rendering, "GPU occlusion visible",
                                                            static_cast<double>(statistics.GpuOcclusionVisible));
                        m_Impl->ProfilerService->SetCounter(ProfileCategory::Rendering, "GPU occlusion culled",
                                                            static_cast<double>(statistics.GpuOcclusionCulled));
                        m_Impl->ProfilerService->SetCounter(ProfileCategory::Rendering, "GPU occlusion dispatches",
                                                            static_cast<double>(statistics.GpuOcclusionDispatches));
                        m_Impl->ProfilerService->SetCounter(ProfileCategory::Rendering, "GPU occlusion indirect draws",
                                                            static_cast<double>(statistics.GpuOcclusionIndirectDraws));
                        m_Impl->ProfilerService->SetCounter(ProfileCategory::Rendering, "GPU occlusion active surfaces",
                                                            static_cast<double>(statistics.GpuOcclusionActiveSurfaces));
                        m_Impl->ProfilerService->SetCounter(
                            ProfileCategory::Rendering, "GPU occlusion fallback surfaces",
                            static_cast<double>(statistics.GpuOcclusionFallbackSurfaces));
                        m_Impl->ProfilerService->SetCounter(
                            ProfileCategory::Rendering, "GPU occlusion partial fallback surfaces",
                            static_cast<double>(statistics.GpuOcclusionPartialFallbackSurfaces));
                        m_Impl->ProfilerService->SetCounter(
                            ProfileCategory::Rendering, "GPU occlusion recording (ms)",
                            static_cast<double>(statistics.GpuOcclusionDepthPassMilliseconds +
                                                statistics.GpuOcclusionPyramidRecordingMilliseconds +
                                                statistics.GpuOcclusionCullingRecordingMilliseconds));
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
                    if (m_Impl->StreamingService)
                    {
                        double requestedBytes = 0.0;
                        double inFlightBytes = 0.0;
                        double residentCpuBytes = 0.0;
                        double residentGpuBytes = 0.0;
                        double retiredBytes = 0.0;
                        double requests = 0.0;
                        double completed = 0.0;
                        double cancelled = 0.0;
                        double failures = 0.0;
                        double evictions = 0.0;
                        double successfulLatencyTotal = 0.0;
                        for (const auto& statistics : m_Impl->StreamingService->Statistics())
                        {
                            requestedBytes += static_cast<double>(statistics.RequestedBytes);
                            inFlightBytes += static_cast<double>(statistics.InFlightBytes);
                            residentCpuBytes += static_cast<double>(statistics.ResidentCpuBytes);
                            residentGpuBytes += static_cast<double>(statistics.ResidentGpuBytes);
                            retiredBytes += static_cast<double>(statistics.RetiredCpuBytes);
                            retiredBytes += static_cast<double>(statistics.RetiredGpuBytes);
                            requests += static_cast<double>(statistics.Requests);
                            completed += static_cast<double>(statistics.CompletedRequests);
                            cancelled += static_cast<double>(statistics.CancelledRequests);
                            failures += static_cast<double>(statistics.Failures);
                            evictions += static_cast<double>(statistics.Evictions);
                            successfulLatencyTotal += statistics.AverageLatencyMilliseconds *
                                                      static_cast<double>(statistics.CompletedRequests);
                        }
                        m_Impl->ProfilerService->SetCounter(ProfileCategory::Assets, "Streaming requested bytes",
                                                            requestedBytes);
                        m_Impl->ProfilerService->SetCounter(ProfileCategory::Assets, "Streaming in-flight bytes",
                                                            inFlightBytes);
                        m_Impl->ProfilerService->SetCounter(ProfileCategory::Assets, "Streaming resident CPU bytes",
                                                            residentCpuBytes);
                        m_Impl->ProfilerService->SetCounter(ProfileCategory::Assets, "Streaming resident GPU bytes",
                                                            residentGpuBytes);
                        m_Impl->ProfilerService->SetCounter(ProfileCategory::Assets, "Streaming retired bytes",
                                                            retiredBytes);
                        m_Impl->ProfilerService->SetCounter(ProfileCategory::Assets, "Streaming requests", requests);
                        m_Impl->ProfilerService->SetCounter(ProfileCategory::Assets, "Streaming completed", completed);
                        m_Impl->ProfilerService->SetCounter(ProfileCategory::Assets, "Streaming cancelled", cancelled);
                        m_Impl->ProfilerService->SetCounter(ProfileCategory::Assets, "Streaming failures", failures);
                        m_Impl->ProfilerService->SetCounter(ProfileCategory::Assets, "Streaming evictions", evictions);
                        m_Impl->ProfilerService->SetCounter(ProfileCategory::Assets, "Streaming successful latency ms",
                                                            completed == 0.0 ? 0.0
                                                                             : successfulLatencyTotal / completed);
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
                    Internal::TelemetryFrameMark();
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
                Internal::TelemetryFrameMark();
            }
        }
        catch (...)
        {
            failure = std::current_exception();
        }

        // Renderer close joins its owner thread, so a failure racing an exit request may become observable only
        // during shutdown. Keep the closed renderer alive long enough to preserve that first terminal failure while
        // leaving an ordinary user-requested Close noexcept.
        const auto rendererAtShutdown = m_Impl->Renderer;
        ShutdownRuntime(initialized);
        m_Impl->RuntimeState = Impl::State::Stopped;

        if (!failure && rendererAtShutdown)
            failure = RenderSystemInternalAccess::TerminalFailure(*rendererAtShutdown);

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

    Ref<DiagnosticCatalog> Application::DiagnosticDefinitions() const noexcept
    {
        return m_Impl->DiagnosticDefinitionService;
    }

    Ref<DiagnosticSink> Application::DiagnosticReports() const noexcept { return m_Impl->DiagnosticReportService; }

    Ref<MemorySystem> Application::Memory() const noexcept { return m_Impl->MemoryService; }

    Ref<StringInterner> Application::Strings() const noexcept { return m_Impl->StringService; }

    Ref<JobSystem> Application::Jobs() const noexcept { return m_Impl->JobService; }

    Ref<ModuleRegistry> Application::Modules() const noexcept { return m_Impl->ModuleService; }

    Ref<AssetSystem> Application::Assets() const noexcept { return m_Impl->Assets; }

    Ref<StreamingSystem> Application::Streaming() const noexcept { return m_Impl->StreamingService; }

    Ref<ReplaySystem> Application::Replay() const noexcept { return m_Impl->ReplayService; }

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

    void Application::SetUiTheme(const UiTheme theme)
    {
        RequireOwnerThread("SetUiTheme");
        if (!m_Impl->UserInterface)
            throw std::logic_error("The UI is not enabled for this application.");
        m_Impl->UserInterface->SetTheme(theme);
        m_Impl->Specification.Ui.Theme = theme;
    }

    UiTheme Application::CurrentUiTheme() const noexcept
    {
        return m_Impl->UserInterface ? m_Impl->UserInterface->Theme() : m_Impl->Specification.Ui.Theme;
    }

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
        const auto closeService = []<typename T>(Ref<T>& service) noexcept
        {
            if (!service)
            {
                return;
            }
            try
            {
                service->Close();
            }
            catch (...)
            {
            }
            service.Reset();
        };

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

        if (m_Impl->ReplayService)
        {
            m_Impl->ReplayService->Close();
            m_Impl->ReplayService.Reset();
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

        closeService(m_Impl->AudioService);
        closeService(m_Impl->NavigationService);
        closeService(m_Impl->PhysicsService);
        closeService(m_Impl->ScriptService);

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

        if (m_Impl->StreamingService)
        {
            m_Impl->StreamingService->Close();
            m_Impl->StreamingService.Reset();
        }

        if (m_Impl->Assets)
        {
            m_Impl->Assets->Close();
            m_Impl->Assets.Reset();
        }

        if (m_Impl->ModuleService)
        {
            m_Impl->ModuleService->Close();
            m_Impl->ModuleService.Reset();
        }

        m_Impl->ProjectService.Reset();

        if (m_Impl->JobService)
        {
            m_Impl->JobService->Close();
            m_Impl->JobService.Reset();
        }

        m_Impl->StringService.Reset();
        m_Impl->JobMemoryResource.reset();
        m_Impl->MemoryService.Reset();
        m_Impl->DiagnosticReportService.Reset();
        m_Impl->DiagnosticDefinitionService.Reset();

        closeService(m_Impl->ProfilerService);

        m_Impl->EventSystem.Reset();

        if (m_Impl->Specification.ManageLogging)
        {
            Log::Shutdown();
        }
    }
} // namespace Keire
