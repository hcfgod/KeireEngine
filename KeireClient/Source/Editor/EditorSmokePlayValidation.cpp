#include "KeireClient/Editor/EditorSmokePlayValidation.h"

#include "KeireInternal/FileSystem.h"
#include "KeireInternal/RenderInternal.h"
#include "KeireInternal/WindowInternal.h"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_video.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace KeireEditor
{
    class EditorSmokePlayValidation::Impl final
    {
      public:
        enum class Phase : std::uint8_t
        {
            WaitForPlay,
            WaitForAdditiveLoad,
            WaitForDeviceRecovery,
            ObserveInitialGameView,
            WaitForTopmostInput,
            StartUnload,
            WaitForUnload,
            StartReload,
            WaitForReload,
            ObserveReloadedGameView,
            Complete
        };

        explicit Impl(std::filesystem::path output
#if defined(KEIRE_ENABLE_TEST_HOOKS)
                      ,
                      const bool validateDeviceLoss
#endif
                      )
            : Output(std::move(output))
#if defined(KEIRE_ENABLE_TEST_HOOKS)
              ,
              ValidateDeviceLoss(validateDeviceLoss)
#endif
        {
        }

        [[nodiscard]] static Keire::EntityId AddValidationButton(const Keire::Ref<Keire::SceneRuntimeSession>& session,
                                                                 const std::string& name)
        {
            if (!session || !session->RuntimeScene() || !session->Presentation())
                throw std::runtime_error("Editor Play validation requires a presentation-backed runtime session.");
            auto canvas = session->RuntimeScene()->CreateEntity(name + " Canvas");
            const auto canvasComponent = canvas.AddComponent<Keire::CanvasComponent>();
            if (!canvasComponent)
                throw std::runtime_error("Editor Play validation could not create its Canvas.");
            canvasComponent->SetScaleMode(Keire::CanvasScaleMode::ConstantPixels);
            auto button = session->RuntimeScene()->CreateEntity(name + " Button", canvas);
            const auto rect = button.AddComponent<Keire::RectTransformComponent>();
            if (!rect || !button.AddComponent<Keire::UiButtonComponent>())
                throw std::runtime_error("Editor Play validation could not create its Button.");
            rect->SetAnchorMinimum({});
            rect->SetAnchorMaximum({});
            rect->SetPivot({});
            rect->SetAnchoredPosition({24.0F, 24.0F});
            rect->SetSizeDelta({120.0F, 48.0F});
            session->Presentation()->Synchronize(session->RuntimeScene(), 1280.0F, 720.0F, true);
            return button.Id();
        }

        [[nodiscard]] static std::size_t PresentationCount(const Keire::Ref<Keire::SceneRuntimeWorld>& world)
        {
            return static_cast<std::size_t>(std::ranges::count_if(world->Sessions(), [](const auto& session)
                                                                  { return session && session->Presentation(); }));
        }

        static void RequireOrder(const Keire::Ref<Keire::SceneRuntimeWorld>& world,
                                 const std::vector<Keire::SceneHandle>& expected)
        {
            if (world->LoadedScenes() != expected)
                throw std::runtime_error("Editor Play validation observed a non-deterministic session order.");
        }

        void PushClick(Keire::Application& application, const Keire::UiItemRect viewport)
        {
            const auto native =
                Keire::WindowSystemInternalAccess::NativeWindow(*application.Windows(), application.MainWindow()->Id());
            if (!native)
                throw std::runtime_error("Editor Play validation could not resolve the editor window.");
            const auto windowId = SDL_GetWindowID(native);
            const auto x = viewport.Minimum.X + 40.0F;
            const auto y = viewport.Minimum.Y + 40.0F;
            SDL_Event motion{};
            motion.type = SDL_EVENT_MOUSE_MOTION;
            motion.motion.windowID = windowId;
            motion.motion.x = x;
            motion.motion.y = y;
            if (!SDL_PushEvent(&motion))
                throw std::runtime_error("Editor Play validation could not queue pointer motion.");

            for (const auto type : {SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_EVENT_MOUSE_BUTTON_UP})
            {
                SDL_Event button{};
                button.type = type;
                button.button.windowID = windowId;
                button.button.button = SDL_BUTTON_LEFT;
                button.button.x = x;
                button.button.y = y;
                if (!SDL_PushEvent(&button))
                    throw std::runtime_error("Editor Play validation could not queue a pointer button event.");
            }
            NativeWindowInputQueued = true;
        }

        void Update(Keire::Application& application, const Keire::Ref<Keire::SceneRuntimeWorld>& world,
                    const Keire::PlayerBuildScenes& buildScenes)
        {
            if (Current == Phase::Complete)
                return;
            if (++Frames > 3600U)
                throw std::runtime_error("Editor Play additive validation timed out.");
            if (!world && Current != Phase::WaitForPlay)
                throw std::runtime_error("Editor Play runtime world closed before validation completed.");
            switch (Current)
            {
            case Phase::WaitForPlay:
            {
                if (!world || world->Sessions().size() != 1U || !world->Active())
                    return;
                const auto enabled = Keire::EnabledPlayerBuildScenes(buildScenes);
                const auto firstAsset = world->Asset(world->Active());
                const auto secondAsset = std::ranges::find_if(enabled, [firstAsset](const Keire::AssetId asset)
                                                              { return asset && asset != firstAsset; });
                if (secondAsset == enabled.end())
                    throw std::runtime_error("Editor Play validation requires a second enabled build scene.");
                First = world->Active();
                FirstButton = AddValidationButton(world->Session(First), "Editor validation startup");
                Load = world->Load(*secondAsset, Keire::SceneLoadMode::Additive);
                Current = Phase::WaitForAdditiveLoad;
                break;
            }
            case Phase::WaitForAdditiveLoad:
                if (Load->State() == Keire::SceneLoadState::Failed)
                    throw std::runtime_error("Editor Play additive load failed: " + Load->Diagnostic().Message);
                if (Load->State() == Keire::SceneLoadState::Ready)
                {
                    Second = Load->Result();
                    RequireOrder(world, {First, Second});
                    SecondButton = AddValidationButton(world->Session(Second), "Editor validation additive");
                    if (PresentationCount(world) != 2U || !world->SetActive(Second))
                        throw std::runtime_error("Editor Play validation could not activate two presentation trees.");
#if defined(KEIRE_ENABLE_TEST_HOOKS)
                    if (ValidateDeviceLoss)
                    {
                        Keire::RenderSystemInternalAccess::InjectDeviceLoss(*application.Renderer());
                        DeviceLossInjectedDuringPlay = true;
                        Current = Phase::WaitForDeviceRecovery;
                    }
                    else
#endif
                    {
                        Current = Phase::ObserveInitialGameView;
                    }
                }
                break;
#if defined(KEIRE_ENABLE_TEST_HOOKS)
            case Phase::WaitForDeviceRecovery:
                if (const auto diagnostic = application.Renderer()->LastDeviceLoss();
                    diagnostic && diagnostic->RecoverySucceeded &&
                    application.Renderer()->DeviceState() == Keire::RenderDeviceState::Running)
                {
                    RecoveredDeviceLoss = *diagnostic;
                    Current = Phase::ObserveInitialGameView;
                }
                break;
#else
            case Phase::WaitForDeviceRecovery:
                throw std::logic_error("Device recovery validation is unavailable in this build.");
#endif
            case Phase::StartUnload:
                if (!world->Unload(Second))
                    throw std::runtime_error("Editor Play validation could not queue additive scene unload.");
                Current = Phase::WaitForUnload;
                break;
            case Phase::WaitForUnload:
                if (!world->IsLoaded(Second))
                {
                    RequireOrder(world, {First});
                    Current = Phase::StartReload;
                }
                break;
            case Phase::StartReload:
                Load = world->Load(SecondAsset, Keire::SceneLoadMode::Additive);
                Current = Phase::WaitForReload;
                break;
            case Phase::WaitForReload:
                if (Load->State() == Keire::SceneLoadState::Failed)
                    throw std::runtime_error("Editor Play additive reload failed: " + Load->Diagnostic().Message);
                if (Load->State() == Keire::SceneLoadState::Ready)
                {
                    Second = Load->Result();
                    RequireOrder(world, {First, Second});
                    SecondButton = AddValidationButton(world->Session(Second), "Editor validation reloaded");
                    if (PresentationCount(world) != 2U || !world->SetActive(Second))
                        throw std::runtime_error("Editor Play validation could not activate the reloaded session.");
                    Current = Phase::ObserveReloadedGameView;
                }
                break;
            case Phase::ObserveInitialGameView:
            case Phase::WaitForTopmostInput:
            case Phase::ObserveReloadedGameView:
                break;
            case Phase::Complete:
                break;
            }
            if (Current == Phase::WaitForAdditiveLoad && Load)
                SecondAsset = Load->Asset();
            (void)application;
        }

        void ObserveGameView(Keire::Application& application, const Keire::Ref<Keire::SceneRuntimeWorld>& world,
                             const Keire::Ref<Keire::RenderSurface>& surface, const Keire::UiItemRect viewport,
                             const std::span<const Keire::Ref<Keire::ScenePresentationRuntime>> presentations)
        {
            if (Current == Phase::Complete || !world)
                return;
            if (Current == Phase::WaitForTopmostInput)
            {
                const auto firstClicked = world->Session(First)->Presentation()->ConsumeClick(FirstButton);
                const auto secondClicked = world->Session(Second)->Presentation()->ConsumeClick(SecondButton);
                if (!secondClicked)
                    return;
                if (firstClicked)
                    throw std::runtime_error(
                        "Editor Play input reached a lower presentation beneath the topmost tree.");
                TopmostInputHandled = true;
                Current = Phase::StartUnload;
                return;
            }
            if (Current != Phase::ObserveInitialGameView && Current != Phase::ObserveReloadedGameView)
                return;
            if (!surface || presentations.size() != 2U ||
                Keire::RenderSystemInternalAccess::SceneContributionCount(*application.Renderer(), *surface) != 2U)
            {
                throw std::runtime_error("Editor Play Game view did not submit both ordered sessions and UI trees.");
            }
            RequireOrder(world, {First, Second});
            if (world->Active() != Second)
                throw std::runtime_error("Editor Play Game view did not render the active additive session.");
            ++ObservedRenderedFrames;
            if (Current == Phase::ObserveInitialGameView)
            {
                PushClick(application, viewport);
                Current = Phase::WaitForTopmostInput;
                return;
            }
            if (!TopmostInputHandled || !NativeWindowInputQueued)
                throw std::runtime_error("Editor Play validation completed without routed native-window input.");
            if (!Output.empty())
            {
                const auto build = Keire::GetBuildInfo();
                auto result = nlohmann::json{{"schemaVersion", 1},
                                             {"status", "passed"},
                                             {"build",
                                              {{"gitCommit", std::string(build.GitCommit)},
                                               {"configuration", std::string(build.Configuration)},
                                               {"dirty", build.Dirty}}},
                                             {"renderedWindowLoop", true},
                                             {"twoSceneContributions", 2},
                                             {"twoPresentationTrees", true},
                                             {"activeSessionRendered", true},
                                             {"topmostInputHandled", true},
                                             {"nativeWindowInputQueued", true},
                                             {"unloadReloadOrder", true},
                                             {"observedRenderedFrames", ObservedRenderedFrames}};
#if defined(KEIRE_ENABLE_TEST_HOOKS)
                if (ValidateDeviceLoss)
                {
                    const auto renderer = application.Renderer();
                    const auto timelines = renderer->RecentFrameTimelines();
                    const auto retries =
                        std::ranges::count(timelines, true, &Keire::RenderFrameTimeline::RetriedAfterDeviceLoss);
                    if (!DeviceLossInjectedDuringPlay || !RecoveredDeviceLoss || retries != 1U ||
                        Keire::RenderSystemInternalAccess::LostGenerationGpuCleanupCallCount(*renderer) != 0U ||
                        Keire::RenderSystemInternalAccess::LastRetriedVfxSnapshotCount(*renderer) == 0U)
                    {
                        throw std::runtime_error(
                            "Editor Play device-loss validation did not recover and retry exactly once.");
                    }
                    result["deviceLoss"] = {
                        {"duringPlay", true},
                        {"recoverySucceeded", RecoveredDeviceLoss->RecoverySucceeded},
                        {"operation", RecoveredDeviceLoss->Operation},
                        {"backend", RecoveredDeviceLoss->Backend},
                        {"adapter", RecoveredDeviceLoss->Adapter},
                        {"recoveryAttempt", RecoveredDeviceLoss->RecoveryAttempt},
                        {"oldGeneration", RecoveredDeviceLoss->DeviceGeneration},
                        {"newGeneration", RecoveredDeviceLoss->RecoveredDeviceGeneration},
                        {"retryCount", retries},
                        {"lostGenerationGpuCleanupCalls", 0},
                        {"continuedAfterRecovery", TopmostInputHandled && NativeWindowInputQueued},
                        {"retainedVfxSnapshots",
                         Keire::RenderSystemInternalAccess::LastRetriedVfxSnapshotCount(*renderer)}};
                }
#endif
                Keire::Detail::WriteTextFileAtomically(Output, result.dump(2) + '\n');
            }
            Current = Phase::Complete;
            application.RequestExit();
        }

        std::filesystem::path Output;
        Phase Current = Phase::WaitForPlay;
        Keire::Ref<Keire::SceneRuntimeLoadOperation> Load;
        Keire::SceneHandle First;
        Keire::SceneHandle Second;
        Keire::AssetId SecondAsset;
        Keire::EntityId FirstButton;
        Keire::EntityId SecondButton;
        std::uint32_t Frames = 0;
        std::uint32_t ObservedRenderedFrames = 0;
        bool NativeWindowInputQueued = false;
        bool TopmostInputHandled = false;
#if defined(KEIRE_ENABLE_TEST_HOOKS)
        bool ValidateDeviceLoss = false;
        bool DeviceLossInjectedDuringPlay = false;
        std::optional<Keire::GpuDeviceLossDiagnostic> RecoveredDeviceLoss;
#endif
    };

    EditorSmokePlayValidation::EditorSmokePlayValidation(std::filesystem::path output
#if defined(KEIRE_ENABLE_TEST_HOOKS)
                                                         ,
                                                         const bool validateDeviceLoss
#endif
                                                         )
        : m_Impl(std::make_unique<Impl>(std::move(output)
#if defined(KEIRE_ENABLE_TEST_HOOKS)
                                            ,
                                        validateDeviceLoss
#endif
                                        ))
    {
    }

    EditorSmokePlayValidation::~EditorSmokePlayValidation() = default;

    void EditorSmokePlayValidation::Update(Keire::Application& application,
                                           const Keire::Ref<Keire::SceneRuntimeWorld>& world,
                                           const Keire::PlayerBuildScenes& buildScenes)
    {
        m_Impl->Update(application, world, buildScenes);
    }

    void EditorSmokePlayValidation::ObserveGameView(
        Keire::Application& application, const Keire::Ref<Keire::SceneRuntimeWorld>& world,
        const Keire::Ref<Keire::RenderSurface>& surface, const Keire::UiItemRect viewport,
        const std::span<const Keire::Ref<Keire::ScenePresentationRuntime>> presentations)
    {
        m_Impl->ObserveGameView(application, world, surface, viewport, presentations);
    }
} // namespace KeireEditor
