#include "KeireClient/Editor/EditorSmokePlayValidation.h"

#include "KeireInternal/FileSystem.h"
#include "KeireInternal/RenderInternal.h"
#include "KeireInternal/WindowInternal.h"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_video.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
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
            WaitForPointerPress,
            WaitForTopmostInput,
            StartUnload,
            WaitForUnload,
            StartReload,
            WaitForReload,
            ObserveReloadedGameView,
            Complete
        };

        [[nodiscard]] static constexpr std::string_view PhaseName(const Phase phase) noexcept
        {
            switch (phase)
            {
            case Phase::WaitForPlay:
                return "wait-for-play";
            case Phase::WaitForAdditiveLoad:
                return "wait-for-additive-load";
            case Phase::WaitForDeviceRecovery:
                return "wait-for-device-recovery";
            case Phase::ObserveInitialGameView:
                return "observe-initial-game-view";
            case Phase::WaitForPointerPress:
                return "wait-for-pointer-press";
            case Phase::WaitForTopmostInput:
                return "wait-for-topmost-input";
            case Phase::StartUnload:
                return "start-unload";
            case Phase::WaitForUnload:
                return "wait-for-unload";
            case Phase::StartReload:
                return "start-reload";
            case Phase::WaitForReload:
                return "wait-for-reload";
            case Phase::ObserveReloadedGameView:
                return "observe-reloaded-game-view";
            case Phase::Complete:
                return "complete";
            }
            return "unknown";
        }

        [[nodiscard]] static constexpr std::string_view DeviceStateName(const Keire::RenderDeviceState state) noexcept
        {
            switch (state)
            {
            case Keire::RenderDeviceState::Running:
                return "running";
            case Keire::RenderDeviceState::RecoveryPending:
                return "recovery-pending";
            case Keire::RenderDeviceState::Recovering:
                return "recovering";
            case Keire::RenderDeviceState::Failed:
                return "failed";
            case Keire::RenderDeviceState::Closing:
                return "closing";
            case Keire::RenderDeviceState::Closed:
                return "closed";
            }
            return "unknown";
        }

        [[nodiscard]] static constexpr std::string_view CursorModeName(const Keire::CursorMode mode) noexcept
        {
            switch (mode)
            {
            case Keire::CursorMode::Normal:
                return "normal";
            case Keire::CursorMode::Hidden:
                return "hidden";
            case Keire::CursorMode::Confined:
                return "confined";
            case Keire::CursorMode::RelativeLocked:
                return "relative-locked";
            }
            return "unknown";
        }

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
                                                                 const std::string& name,
                                                                 Keire::RuntimeUiElementId& element)
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
            if (!session->Presentation()->SetFocus(button.Id()))
                throw std::runtime_error("Editor Play validation could not resolve its Button UI node.");
            element = session->Presentation()->Ui()->Focus();
            if (!element)
                throw std::runtime_error("Editor Play validation resolved an invalid Button UI node.");
            return button.Id();
        }

        [[nodiscard]] static std::size_t PresentationCount(const Keire::Ref<Keire::SceneRuntimeWorld>& world)
        {
            return static_cast<std::size_t>(std::ranges::count_if(world->Sessions(), [](const auto& session)
                                                                  { return session && session->Presentation(); }));
        }

        void TransitionTo(const Phase phase) noexcept
        {
            Current = phase;
            PhaseStartedAt = std::chrono::steady_clock::now();
            ++PhaseTransitions;
        }

        [[noreturn]] void Fail(Keire::Application& application, const Keire::Ref<Keire::SceneRuntimeWorld>& world,
                               std::string reason)
        {
            const auto now = std::chrono::steady_clock::now();
            const auto renderer = application.Renderer();
            const auto statistics = renderer ? renderer->Statistics() : Keire::RenderStatistics{};
            const auto deviceState = renderer ? renderer->DeviceState() : Keire::RenderDeviceState::Closed;
            const auto windows = application.Windows();
            const auto mainWindow = application.MainWindow();
            const auto cursorMode =
                windows && mainWindow ? windows->GetCursorMode(mainWindow->Id()) : Keire::CursorMode::Normal;
            const auto totalMilliseconds = std::chrono::duration<float, std::milli>(now - ValidationStartedAt).count();
            const auto phaseMilliseconds = std::chrono::duration<float, std::milli>(now - PhaseStartedAt).count();
            const auto uiStateDiagnostic = [](const std::optional<Keire::RuntimeUiElementState>& state)
            {
                if (!state)
                    return nlohmann::json{{"present", false}};
                return nlohmann::json{{"present", true},
                                      {"visible", state->Visible},
                                      {"enabled", state->Enabled},
                                      {"interactable", state->Interactable},
                                      {"focused", state->Focused},
                                      {"hovered", state->Hovered},
                                      {"pressed", state->Pressed},
                                      {"rect",
                                       {{"x", state->Rect.X},
                                        {"y", state->Rect.Y},
                                        {"width", state->Rect.Width},
                                        {"height", state->Rect.Height}}},
                                      {"clipRect",
                                       {{"x", state->ClipRect.X},
                                        {"y", state->ClipRect.Y},
                                        {"width", state->ClipRect.Width},
                                        {"height", state->ClipRect.Height}}}};
            };
            nlohmann::json failure{{"schemaVersion", 1},
                                   {"status", "failed"},
                                   {"reason", reason},
                                   {"phase", PhaseName(Current)},
                                   {"totalMilliseconds", totalMilliseconds},
                                   {"phaseMilliseconds", phaseMilliseconds},
                                   {"updateCalls", UpdateCalls},
                                   {"gameViewObservationCalls", GameViewObservationCalls},
                                   {"phaseTransitions", PhaseTransitions},
                                   {"worldSessions", world ? world->Sessions().size() : 0U},
                                   {"lastPresentationCount", LastPresentationCount},
                                   {"lastContributionCount", LastContributionCount},
                                   {"lastSurfacePresent", LastSurfacePresent},
                                   {"lastSurfaceAvailable", LastSurfaceAvailable},
                                   {"lastViewportWidth", LastViewportWidth},
                                   {"lastViewportHeight", LastViewportHeight},
                                   {"lastViewportMinimumX", LastViewportMinimumX},
                                   {"lastViewportMinimumY", LastViewportMinimumY},
                                   {"nativeWindowInputQueued", NativeWindowInputQueued},
                                   {"nativePointerPressQueued", NativePointerPressQueued},
                                   {"mainWindowFocused", mainWindow && mainWindow->Focused()},
                                   {"gameViewportInputActive", GameViewportInputActive},
                                   {"gamePanelFocused", GamePanelFocused},
                                   {"cursorMode", CursorModeName(cursorMode)},
                                   {"nativeCursorNormalized", NativeCursorNormalized},
                                   {"clickX", ClickX},
                                   {"clickY", ClickY},
                                   {"lastPointerX", LastPointer.Position.X},
                                   {"lastPointerY", LastPointer.Position.Y},
                                   {"lastPointerLocalX", LastPointerLocalX},
                                   {"lastPointerLocalY", LastPointerLocalY},
                                   {"lastPointerLeftDown", LastPointer.LeftDown},
                                   {"lastPointerLeftPressed", LastPointer.LeftPressed},
                                   {"lastPointerLeftReleased", LastPointer.LeftReleased},
                                   {"pointerPressObserved", PointerPressObserved},
                                   {"pointerReleaseObserved", PointerReleaseObserved},
                                   {"topmostPointerHovered", TopmostPointerHovered},
                                   {"topmostPointerCaptured", TopmostPointerCaptured},
                                   {"topmostHoverObserved", TopmostHoverObserved},
                                   {"topmostCaptureObserved", TopmostCaptureObserved},
                                   {"topmostDirectHit", TopmostDirectHit},
                                   {"topmostDirectHitIsButton", TopmostDirectHitIsButton},
                                   {"topmostCanonicalHit", TopmostCanonicalHit},
                                   {"topmostCanonicalHitIsButton", TopmostCanonicalHitIsButton},
                                   {"firstButton", uiStateDiagnostic(FirstButtonState)},
                                   {"secondButton", uiStateDiagnostic(SecondButtonState)},
                                   {"firstUi",
                                    {{"elements", FirstUiStatistics.Elements},
                                     {"interactableElements", FirstUiStatistics.InteractableElements},
                                     {"drawCommands", FirstUiStatistics.DrawCommands},
                                     {"scale", FirstUiStatistics.Scale}}},
                                   {"secondUi",
                                    {{"elements", SecondUiStatistics.Elements},
                                     {"interactableElements", SecondUiStatistics.InteractableElements},
                                     {"drawCommands", SecondUiStatistics.DrawCommands},
                                     {"scale", SecondUiStatistics.Scale},
                                     {"pendingEvents", LastSecondPendingEvents},
                                     {"pendingPointerDownEvents", LastSecondPointerDownEvents},
                                     {"pendingPointerUpEvents", LastSecondPointerUpEvents},
                                     {"pendingClickEvents", LastSecondClickEvents},
                                     {"secondButtonPointerUpPending", SecondButtonPointerUpPending},
                                     {"secondButtonClickPending", SecondButtonClickPending}}},
                                   {"topmostInputHandled", TopmostInputHandled},
                                   {"observedRenderedFrames", ObservedRenderedFrames},
                                   {"renderer",
                                    {{"deviceState", DeviceStateName(deviceState)},
                                     {"acceptedFrames", statistics.AcceptedFrames},
                                     {"presentedFrames", statistics.PresentedFrames},
                                     {"retiredFrames", statistics.RetiredFrames},
                                     {"outstandingFrames", statistics.OutstandingFrames},
                                     {"lastAcceptedFrame", statistics.LastAcceptedFrame},
                                     {"lastPresentedFrame", statistics.LastPresentedFrame},
                                     {"lastRetiredFrame", statistics.LastRetiredFrame}}}};
            if (renderer)
            {
                if (const auto diagnostic = renderer->LastDeviceLoss())
                {
                    failure["renderer"]["deviceLoss"] = {{"operation", diagnostic->Operation},
                                                         {"recoverySucceeded", diagnostic->RecoverySucceeded},
                                                         {"oldGeneration", diagnostic->DeviceGeneration},
                                                         {"newGeneration", diagnostic->RecoveredDeviceGeneration},
                                                         {"recoveryAttempt", diagnostic->RecoveryAttempt}};
                }
            }
            if (!Output.empty())
                Keire::Detail::WriteTextFileAtomically(Output, failure.dump(2) + '\n');

            std::ostringstream message;
            message << reason << " (phase=" << PhaseName(Current) << ", phaseMs=" << phaseMilliseconds
                    << ", updates=" << UpdateCalls << ", gameViewObservations=" << GameViewObservationCalls
                    << ", presentations=" << LastPresentationCount << ", contributions=" << LastContributionCount
                    << ", surface=" << LastSurfacePresent << '/' << LastSurfaceAvailable
                    << ", viewport=" << LastViewportMinimumX << ',' << LastViewportMinimumY << '+' << LastViewportWidth
                    << 'x' << LastViewportHeight << ", cursor=" << CursorModeName(cursorMode) << '/'
                    << NativeCursorNormalized << ", pointer=" << LastPointer.Position.X << ',' << LastPointer.Position.Y
                    << '/' << LastPointerLocalX << ',' << LastPointerLocalY << '/' << LastPointer.LeftDown
                    << LastPointer.LeftPressed << LastPointer.LeftReleased << ", click=" << ClickX << ',' << ClickY
                    << ", press/releaseSeen=" << PointerPressObserved << '/' << PointerReleaseObserved
                    << ", focus=" << (mainWindow && mainWindow->Focused()) << '/' << GamePanelFocused << '/'
                    << GameViewportInputActive << ", topHover/capture=" << TopmostPointerHovered << '/'
                    << TopmostPointerCaptured << '/' << TopmostHoverObserved << '/' << TopmostCaptureObserved
                    << ", directHit/button=" << TopmostDirectHit << '/' << TopmostDirectHitIsButton
                    << ", canonicalHit/button=" << TopmostCanonicalHit << '/' << TopmostCanonicalHitIsButton
                    << ", secondButton=" << static_cast<bool>(SecondButtonState)
                    << (SecondButtonState ? '/' + std::to_string(SecondButtonState->Rect.X) + ',' +
                                                std::to_string(SecondButtonState->Rect.Y) + '+' +
                                                std::to_string(SecondButtonState->Rect.Width) + 'x' +
                                                std::to_string(SecondButtonState->Rect.Height)
                                          : std::string{})
                    << ", pending2=" << LastSecondPendingEvents << '/' << LastSecondPointerDownEvents << '/'
                    << LastSecondPointerUpEvents << '/' << LastSecondClickEvents << '/' << SecondButtonPointerUpPending
                    << '/' << SecondButtonClickPending << ", device=" << DeviceStateName(deviceState)
                    << ", accepted/presented/retired=" << statistics.AcceptedFrames << '/' << statistics.PresentedFrames
                    << '/' << statistics.RetiredFrames << ", clickQueued/handled=" << NativeWindowInputQueued << '/'
                    << TopmostInputHandled << ").";
            throw std::runtime_error(message.str());
        }

        void CheckDeadline(Keire::Application& application, const Keire::Ref<Keire::SceneRuntimeWorld>& world)
        {
            const auto now = std::chrono::steady_clock::now();
            if (now - ValidationStartedAt > std::chrono::minutes(3))
                Fail(application, world, "Editor Play additive validation exceeded its total deadline");

            const auto phaseDeadline = Current == Phase::WaitForPlay ? std::chrono::seconds(180)
                                       : Current == Phase::WaitForAdditiveLoad || Current == Phase::WaitForReload
                                           ? std::chrono::seconds(60)
                                           : std::chrono::seconds(15);
            if (now - PhaseStartedAt > phaseDeadline)
                Fail(application, world, "Editor Play additive validation stopped making phase progress");
        }

        static void RequireOrder(const Keire::Ref<Keire::SceneRuntimeWorld>& world,
                                 const std::vector<Keire::SceneHandle>& expected)
        {
            if (world->LoadedScenes() != expected)
                throw std::runtime_error("Editor Play validation observed a non-deterministic session order.");
        }

        void PushClickPress(Keire::Application& application, const Keire::UiItemRect viewport,
                            const Keire::RuntimeUiRect target)
        {
            const auto windows = application.Windows();
            const auto mainWindow = application.MainWindow();
            if (!windows || !mainWindow)
                throw std::runtime_error("Editor Play validation could not resolve the editor window service.");
            const auto native = Keire::WindowSystemInternalAccess::NativeWindow(*windows, mainWindow->Id());
            if (!native)
                throw std::runtime_error("Editor Play validation could not resolve the editor window.");
            const auto windowId = SDL_GetWindowID(native);
            const auto hitRect = target.Intersect(SecondButtonState ? SecondButtonState->ClipRect : target);
            if (hitRect.Empty())
                throw std::runtime_error("Editor Play validation Button has no visible hit-test area.");
            const auto x = viewport.Minimum.X + hitRect.X + hitRect.Width * 0.5F;
            const auto y = viewport.Minimum.Y + hitRect.Y + hitRect.Height * 0.5F;

            // The sample's first-person controller requests relative cursor capture during Play. Runtime UI in the
            // Editor is routed from ImGui's absolute pointer state, so synthetic button coordinates cannot hit a UI
            // element while the window remains relative-locked. Release only the validation window's capture through
            // the WindowSystem boundary, then move the real SDL cursor before queuing the native button events.
            windows->SetCursorMode(mainWindow->Id(), Keire::CursorMode::Normal);
            windows->WarpCursor(mainWindow->Id(), {static_cast<std::int32_t>(x), static_cast<std::int32_t>(y)});
            NativeCursorNormalized = windows->GetCursorMode(mainWindow->Id()) == Keire::CursorMode::Normal;
            if (!NativeCursorNormalized)
                throw std::runtime_error("Editor Play validation could not release relative cursor capture.");
            ClickX = x;
            ClickY = y;

            SDL_Event motion{};
            motion.type = SDL_EVENT_MOUSE_MOTION;
            motion.motion.windowID = windowId;
            motion.motion.x = x;
            motion.motion.y = y;
            if (!SDL_PushEvent(&motion))
                throw std::runtime_error("Editor Play validation could not queue pointer motion.");

            SDL_Event button{};
            button.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
            button.button.windowID = windowId;
            button.button.button = SDL_BUTTON_LEFT;
            button.button.x = x;
            button.button.y = y;
            if (!SDL_PushEvent(&button))
                throw std::runtime_error("Editor Play validation could not queue a pointer button press.");
            NativePointerPressQueued = true;
        }

        void PushClickRelease(Keire::Application& application)
        {
            const auto windows = application.Windows();
            const auto mainWindow = application.MainWindow();
            if (!windows || !mainWindow)
                throw std::runtime_error("Editor Play validation could not resolve the editor window service.");
            const auto native = Keire::WindowSystemInternalAccess::NativeWindow(*windows, mainWindow->Id());
            if (!native)
                throw std::runtime_error("Editor Play validation could not resolve the editor window.");

            SDL_Event button{};
            button.type = SDL_EVENT_MOUSE_BUTTON_UP;
            button.button.windowID = SDL_GetWindowID(native);
            button.button.button = SDL_BUTTON_LEFT;
            button.button.x = ClickX;
            button.button.y = ClickY;
            if (!SDL_PushEvent(&button))
                throw std::runtime_error("Editor Play validation could not queue a pointer button release.");
            NativeWindowInputQueued = true;
        }

        void Update(Keire::Application& application, const Keire::Ref<Keire::SceneRuntimeWorld>& world,
                    const Keire::PlayerBuildScenes& buildScenes)
        {
            if (Current == Phase::Complete)
                return;
            ++UpdateCalls;
            CheckDeadline(application, world);
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
                FirstButton =
                    AddValidationButton(world->Session(First), "Editor validation startup", FirstButtonElement);
                Load = world->Load(*secondAsset, Keire::SceneLoadMode::Additive);
                TransitionTo(Phase::WaitForAdditiveLoad);
                break;
            }
            case Phase::WaitForAdditiveLoad:
                if (Load->State() == Keire::SceneLoadState::Failed)
                    throw std::runtime_error("Editor Play additive load failed: " + Load->Diagnostic().Message);
                if (Load->State() == Keire::SceneLoadState::Ready)
                {
                    Second = Load->Result();
                    RequireOrder(world, {First, Second});
                    SecondButton =
                        AddValidationButton(world->Session(Second), "Editor validation additive", SecondButtonElement);
                    if (PresentationCount(world) != 2U || !world->SetActive(Second))
                        throw std::runtime_error("Editor Play validation could not activate two presentation trees.");
#if defined(KEIRE_ENABLE_TEST_HOOKS)
                    if (ValidateDeviceLoss)
                    {
                        Keire::RenderSystemInternalAccess::InjectDeviceLoss(*application.Renderer());
                        DeviceLossInjectedDuringPlay = true;
                        TransitionTo(Phase::WaitForDeviceRecovery);
                    }
                    else
#endif
                    {
                        TransitionTo(Phase::ObserveInitialGameView);
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
                    TransitionTo(Phase::ObserveInitialGameView);
                }
                break;
#else
            case Phase::WaitForDeviceRecovery:
                throw std::logic_error("Device recovery validation is unavailable in this build.");
#endif
            case Phase::StartUnload:
                if (!world->Unload(Second))
                    throw std::runtime_error("Editor Play validation could not queue additive scene unload.");
                TransitionTo(Phase::WaitForUnload);
                break;
            case Phase::WaitForUnload:
                if (!world->IsLoaded(Second))
                {
                    RequireOrder(world, {First});
                    TransitionTo(Phase::StartReload);
                }
                break;
            case Phase::StartReload:
                Load = world->Load(SecondAsset, Keire::SceneLoadMode::Additive);
                TransitionTo(Phase::WaitForReload);
                break;
            case Phase::WaitForReload:
                if (Load->State() == Keire::SceneLoadState::Failed)
                    throw std::runtime_error("Editor Play additive reload failed: " + Load->Diagnostic().Message);
                if (Load->State() == Keire::SceneLoadState::Ready)
                {
                    Second = Load->Result();
                    RequireOrder(world, {First, Second});
                    SecondButton =
                        AddValidationButton(world->Session(Second), "Editor validation reloaded", SecondButtonElement);
                    if (PresentationCount(world) != 2U || !world->SetActive(Second))
                        throw std::runtime_error("Editor Play validation could not activate the reloaded session.");
                    TransitionTo(Phase::ObserveReloadedGameView);
                }
                break;
            case Phase::ObserveInitialGameView:
            case Phase::WaitForPointerPress:
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
                             const std::span<const Keire::Ref<Keire::ScenePresentationRuntime>> presentations,
                             const Keire::UiPointerState pointer,
                             const Keire::Ref<Keire::ScenePresentationRuntime>& hoveredPresentation,
                             const Keire::Ref<Keire::ScenePresentationRuntime>& primaryPointerCapture,
                             const bool gameViewportInputActive, const bool gamePanelFocused)
        {
            ++GameViewObservationCalls;
            LastPresentationCount = presentations.size();
            LastSurfacePresent = static_cast<bool>(surface);
            LastSurfaceAvailable = surface && surface->Available();
            LastViewportMinimumX = viewport.Minimum.X;
            LastViewportMinimumY = viewport.Minimum.Y;
            LastViewportWidth = viewport.Maximum.X - viewport.Minimum.X;
            LastViewportHeight = viewport.Maximum.Y - viewport.Minimum.Y;
            LastPointer = pointer;
            LastPointerLocalX = pointer.Position.X - viewport.Minimum.X;
            LastPointerLocalY = pointer.Position.Y - viewport.Minimum.Y;
            PointerPressObserved = PointerPressObserved || pointer.LeftPressed;
            PointerReleaseObserved = PointerReleaseObserved || pointer.LeftReleased;
            GameViewportInputActive = gameViewportInputActive;
            GamePanelFocused = gamePanelFocused;
            const auto topmost =
                presentations.empty() ? Keire::Ref<Keire::ScenePresentationRuntime>{} : presentations.back();
            TopmostPointerHovered = topmost && hoveredPresentation == topmost;
            TopmostPointerCaptured = topmost && primaryPointerCapture == topmost;
            TopmostHoverObserved = TopmostHoverObserved || TopmostPointerHovered;
            TopmostCaptureObserved = TopmostCaptureObserved || TopmostPointerCaptured;
            const auto firstPresentation = world && world->Session(First)
                                               ? world->Session(First)->Presentation()
                                               : Keire::Ref<Keire::ScenePresentationRuntime>{};
            const auto secondPresentation = world && world->Session(Second)
                                                ? world->Session(Second)->Presentation()
                                                : Keire::Ref<Keire::ScenePresentationRuntime>{};
            const auto firstTree = firstPresentation ? firstPresentation->Ui() : Keire::Ref<Keire::RuntimeUiTree>{};
            const auto secondTree = secondPresentation ? secondPresentation->Ui() : Keire::Ref<Keire::RuntimeUiTree>{};
            FirstButtonState = firstTree ? firstTree->State(FirstButtonElement) : std::nullopt;
            SecondButtonState = secondTree ? secondTree->State(SecondButtonElement) : std::nullopt;
            FirstUiStatistics = firstTree ? firstTree->Statistics() : Keire::RuntimeUiStatistics{};
            SecondUiStatistics = secondTree ? secondTree->Statistics() : Keire::RuntimeUiStatistics{};
            LastSecondPendingEvents = 0;
            LastSecondPointerDownEvents = 0;
            LastSecondPointerUpEvents = 0;
            LastSecondClickEvents = 0;
            SecondButtonPointerUpPending = false;
            SecondButtonClickPending = false;
            if (secondPresentation)
            {
                const auto checkpoint = secondPresentation->CaptureCheckpoint();
                LastSecondPendingEvents = checkpoint.PendingUiEvents.size();
                for (const auto& event : checkpoint.PendingUiEvents)
                {
                    if (event.Type == Keire::RuntimeUiEventType::PointerDown)
                        ++LastSecondPointerDownEvents;
                    else if (event.Type == Keire::RuntimeUiEventType::PointerUp)
                        ++LastSecondPointerUpEvents;
                    else if (event.Type == Keire::RuntimeUiEventType::Click)
                        ++LastSecondClickEvents;
                    if (event.Target == SecondButton && event.Type == Keire::RuntimeUiEventType::PointerUp)
                        SecondButtonPointerUpPending = true;
                    if (event.Target == SecondButton && event.Type == Keire::RuntimeUiEventType::Click)
                        SecondButtonClickPending = true;
                }
            }
            const auto directHit =
                secondTree ? secondTree->HitTest(LastPointerLocalX, LastPointerLocalY) : std::nullopt;
            TopmostDirectHit = directHit.has_value();
            TopmostDirectHitIsButton = directHit && *directHit == SecondButtonElement;
            const auto canonicalHit = secondTree ? secondTree->HitTest(40.0F, 40.0F) : std::nullopt;
            TopmostCanonicalHit = canonicalHit.has_value();
            TopmostCanonicalHitIsButton = canonicalHit && *canonicalHit == SecondButtonElement;
            LastContributionCount =
                surface ? Keire::RenderSystemInternalAccess::SceneContributionCount(*application.Renderer(), *surface)
                        : 0U;
            if (Current == Phase::Complete || !world)
                return;
            if (Current == Phase::WaitForPointerPress)
            {
                if (!TopmostPointerCaptured)
                    return;
                PushClickRelease(application);
                TransitionTo(Phase::WaitForTopmostInput);
                return;
            }
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
                TransitionTo(Phase::StartUnload);
                return;
            }
            if (Current != Phase::ObserveInitialGameView && Current != Phase::ObserveReloadedGameView)
                return;
            if (!surface || presentations.size() != 2U || LastContributionCount != 2U)
            {
                throw std::runtime_error("Editor Play Game view did not submit both ordered sessions and UI trees.");
            }
            RequireOrder(world, {First, Second});
            if (world->Active() != Second)
                throw std::runtime_error("Editor Play Game view did not render the active additive session.");
            ++ObservedRenderedFrames;
            if (Current == Phase::ObserveInitialGameView)
            {
                if (!GameViewportInputActive)
                    return;
                if (!SecondButtonState || !SecondButtonState->Visible || !SecondButtonState->Enabled ||
                    !SecondButtonState->Interactable)
                {
                    Fail(application, world,
                         "Editor Play topmost validation Button is not available for native-window input");
                }
                PushClickPress(application, viewport, SecondButtonState->Rect);
                TransitionTo(Phase::WaitForPointerPress);
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
                    const auto retries = Keire::RenderSystemInternalAccess::RecoveryAttemptCountForTest(*renderer);
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
            TransitionTo(Phase::Complete);
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
        Keire::RuntimeUiElementId FirstButtonElement;
        Keire::RuntimeUiElementId SecondButtonElement;
        std::chrono::steady_clock::time_point ValidationStartedAt = std::chrono::steady_clock::now();
        std::chrono::steady_clock::time_point PhaseStartedAt = ValidationStartedAt;
        std::uint64_t UpdateCalls = 0;
        std::uint64_t GameViewObservationCalls = 0;
        std::uint32_t PhaseTransitions = 0;
        std::uint32_t ObservedRenderedFrames = 0;
        std::size_t LastPresentationCount = 0;
        std::size_t LastContributionCount = 0;
        float LastViewportWidth = 0.0F;
        float LastViewportHeight = 0.0F;
        float LastViewportMinimumX = 0.0F;
        float LastViewportMinimumY = 0.0F;
        float LastPointerLocalX = 0.0F;
        float LastPointerLocalY = 0.0F;
        float ClickX = 0.0F;
        float ClickY = 0.0F;
        Keire::UiPointerState LastPointer;
        std::optional<Keire::RuntimeUiElementState> FirstButtonState;
        std::optional<Keire::RuntimeUiElementState> SecondButtonState;
        Keire::RuntimeUiStatistics FirstUiStatistics;
        Keire::RuntimeUiStatistics SecondUiStatistics;
        bool LastSurfacePresent = false;
        bool LastSurfaceAvailable = false;
        bool NativeWindowInputQueued = false;
        bool NativePointerPressQueued = false;
        bool NativeCursorNormalized = false;
        bool PointerPressObserved = false;
        bool PointerReleaseObserved = false;
        bool GameViewportInputActive = false;
        bool GamePanelFocused = false;
        bool TopmostPointerHovered = false;
        bool TopmostPointerCaptured = false;
        bool TopmostHoverObserved = false;
        bool TopmostCaptureObserved = false;
        bool TopmostDirectHit = false;
        bool TopmostDirectHitIsButton = false;
        bool TopmostCanonicalHit = false;
        bool TopmostCanonicalHitIsButton = false;
        std::size_t LastSecondPendingEvents = 0;
        std::size_t LastSecondPointerDownEvents = 0;
        std::size_t LastSecondPointerUpEvents = 0;
        std::size_t LastSecondClickEvents = 0;
        bool SecondButtonPointerUpPending = false;
        bool SecondButtonClickPending = false;
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
        const std::span<const Keire::Ref<Keire::ScenePresentationRuntime>> presentations,
        const Keire::UiPointerState pointer, const Keire::Ref<Keire::ScenePresentationRuntime>& hoveredPresentation,
        const Keire::Ref<Keire::ScenePresentationRuntime>& primaryPointerCapture, const bool gameViewportInputActive,
        const bool gamePanelFocused)
    {
        m_Impl->ObserveGameView(application, world, surface, viewport, presentations, pointer, hoveredPresentation,
                                primaryPointerCapture, gameViewportInputActive, gamePanelFocused);
    }
} // namespace KeireEditor
