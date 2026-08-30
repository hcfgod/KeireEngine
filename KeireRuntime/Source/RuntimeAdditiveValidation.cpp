#include "KeireRuntimeInternal/RuntimeAdditiveValidation.h"

#include "Keire/Core.h"

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
#include <stdexcept>
#include <string>
#include <utility>

namespace KeireRuntime
{
    void ParseRuntimeAdditiveValidationOption(const Keire::ApplicationCommandLineArguments& arguments,
                                              std::size_t& index, std::filesystem::path& output)
    {
        if (++index >= arguments.Size())
            throw Keire::CommandLineError("--validate-additive-runtime requires an output path.");
        output = std::filesystem::absolute(Keire::Detail::PathFromUtf8(arguments[index])).lexically_normal();
    }

    std::vector<Keire::AssetId> ParseRuntimeValidationScenes(const nlohmann::json& manifest,
                                                             const Keire::AssetId startupScene)
    {
        const auto scenes = manifest.find("buildScenes");
        if (scenes == manifest.end())
            return {startupScene};
        if (!scenes->is_array() || scenes->empty() || scenes->size() > 1024U)
            throw Keire::CommandLineError("Runtime manifest buildScenes must be a non-empty bounded array.");
        std::vector<Keire::AssetId> result;
        result.reserve(scenes->size());
        for (const auto& encoded : *scenes)
        {
            const auto scene = Keire::AssetId::Parse(encoded.get<std::string>());
            if (std::ranges::find(result, scene) != result.end())
                throw Keire::CommandLineError("Runtime manifest buildScenes contains a duplicate scene.");
            result.push_back(scene);
        }
        if (result.front() != startupScene)
            throw Keire::CommandLineError("Runtime manifest startupScene must be the first enabled build scene.");
        return result;
    }

    std::vector<Keire::AssetId> SelectRuntimeValidationScenes(const std::filesystem::path& output,
                                                              const std::span<const Keire::AssetId> buildScenes)
    {
        if (output.empty())
            return {};
        if (buildScenes.size() < 4U)
        {
            throw Keire::CommandLineError(
                "Additive runtime validation requires four enabled cooked build scenes, including the GPU occlusion "
                "stress scene.");
        }
        return {buildScenes.begin(), buildScenes.end()};
    }

    class RuntimeAdditiveValidation::Impl final
    {
      public:
        enum class Phase : std::uint8_t
        {
            Disabled,
            StartAdditiveLoad,
            WaitForAdditiveLoad,
            ObserveTwoScenes,
            StartNoPresentationLoad,
            WaitForNoPresentationLoad,
            ObserveThreeScenes,
            StartUnload,
            WaitForUnload,
            ObserveUnload,
            StartReload,
            WaitForReload,
            WaitForActiveReload,
            WaitForInput,
            StartFailedLoad,
            WaitForFailedLoad,
            StartOcclusionLoad,
            WaitForOcclusionLoad,
            ObserveOcclusionFrame,
            ObserveFinalFrame,
            AwaitShutdown,
            Complete
        };

        Impl(std::filesystem::path output, std::vector<Keire::AssetId> buildScenes
#if defined(KEIRE_ENABLE_TEST_HOOKS)
             ,
             const bool validateDeviceLoss
#endif
             )
            : Output(std::move(output)), BuildScenes(std::move(buildScenes)),
              Current(Output.empty() ? Phase::Disabled : Phase::StartAdditiveLoad)
#if defined(KEIRE_ENABLE_TEST_HOOKS)
              ,
              ValidateDeviceLoss(validateDeviceLoss)
#endif
        {
            if (Current != Phase::Disabled && BuildScenes.size() < 4U)
            {
                throw std::invalid_argument(
                    "Additive runtime validation requires at least four cooked build scenes, including the GPU "
                    "occlusion stress scene.");
            }
        }

        [[nodiscard]] static bool
        HasExactOcclusionEvidence(const Keire::RenderCapabilities& capabilities,
                                  const Keire::RenderStatistics& statistics, const Keire::RenderDeviceIdentity& device,
                                  const Keire::RenderSurface& surface,
                                  const Keire::GpuOcclusionSurfaceDiagnostics& diagnostics) noexcept
        {
            const auto classifiedLocalLights = static_cast<std::uint64_t>(diagnostics.LocalLightVisible) +
                                               static_cast<std::uint64_t>(diagnostics.LocalLightCulled);
            return capabilities.GpuOcclusionCulling && capabilities.GpuOcclusionSkinnedMeshes &&
                   capabilities.GpuOcclusionVfxVisibilityMasks && capabilities.GpuOcclusionLocalLightMasks &&
                   statistics.GpuOcclusionEnabled && statistics.AllowedFramesInFlight != 0U &&
                   diagnostics.SourceFrame != 0U && diagnostics.SourceFrame <= statistics.LastRetiredFrame &&
                   diagnostics.SourceSurfaceEpoch == surface.Generation() &&
                   diagnostics.SourceFrameSlot < statistics.AllowedFramesInFlight && device.Available &&
                   diagnostics.SourceDeviceGeneration == device.DeviceGeneration &&
                   diagnostics.RequestedMode == Keire::GpuOcclusionMode::Automatic &&
                   diagnostics.EffectiveMode == Keire::GpuOcclusionMode::Automatic &&
                   diagnostics.State == Keire::GpuOcclusionSurfaceState::Active && diagnostics.PyramidValid &&
                   diagnostics.ReadbackValid && diagnostics.Culled != 0U && diagnostics.SafeOccluders != 0U &&
                   diagnostics.LocalLightCandidates != 0U &&
                   classifiedLocalLights == diagnostics.LocalLightCandidates && diagnostics.LocalLightMaskConsumed &&
                   diagnostics.FreshPoseSkinnedCandidates != 0U && diagnostics.FreshPoseSkinnedDepthDraws != 0U &&
                   diagnostics.VfxMaskEntries != 0U && diagnostics.VfxMaskedDraws != 0U && diagnostics.VfxMaskConsumed;
        }

        struct ValidationButton final
        {
            Keire::EntityId Document;
            Keire::AssetId StableId;
            std::uint64_t DocumentGeneration = 0;
            std::uint64_t Element = 0;
        };

        [[nodiscard]] static ValidationButton
        BeginValidationButton(const Keire::Ref<Keire::SceneRuntimeSession>& session, const std::string& name)
        {
            if (!session || !session->RuntimeScene() || !session->Presentation())
                throw std::runtime_error("Additive runtime validation requires a presentation-backed session.");
            const auto visualTree = Keire::AssetId::Parse("6a100001-1111-4000-8000-000000000002");
            const auto panelSettings = Keire::AssetId::Parse("6a100001-1111-4000-8000-000000000003");
            const auto buttonStableId = Keire::AssetId::Parse("6a110001-1111-4000-8000-000000000005");
            auto documentEntity = session->RuntimeScene()->CreateEntity(name + " UI Document");
            const auto document = documentEntity.AddComponent<Keire::UiDocumentComponent>();
            if (!document)
                throw std::runtime_error("Additive runtime validation could not create its UI Document.");
            document->SetVisualTree(visualTree);
            document->SetPanelSettings(panelSettings);
            return {.Document = documentEntity.Id(), .StableId = buttonStableId};
        }

        [[nodiscard]] static bool ResolveValidationButton(const Keire::Ref<Keire::SceneRuntimeSession>& session,
                                                          ValidationButton& result, const float width,
                                                          const float height)
        {
            if (!session || !session->RuntimeScene() || !session->Presentation() || !result.Document ||
                !result.StableId)
                throw std::runtime_error("Additive runtime validation has an invalid pending UI Document Button.");
            session->Presentation()->Synchronize(session->RuntimeScene(), width, height, true);
            const auto button = session->Presentation()->FindUiDocumentElement(result.Document, result.StableId);
            if (!button)
                return false;
            if (button->Type != Keire::RuntimeUiElementType::Button)
                throw std::runtime_error(
                    "Additive runtime validation resolved an unexpected UI Document element type.");
            result.StableId = button->StableId;
            result.DocumentGeneration = button->DocumentGeneration;
            result.Element = button->Element;
            return true;
        }

        [[nodiscard]] static std::optional<Keire::ScenePresentationUiDocumentDebugState>
        ButtonState(const Keire::Ref<Keire::ScenePresentationRuntime>& presentation, const ValidationButton& button)
        {
            const auto snapshot = presentation ? presentation->UiDocumentDebugSnapshot(button.Document) : std::nullopt;
            if (!snapshot || snapshot->DocumentGeneration != button.DocumentGeneration)
                return std::nullopt;
            const auto element = std::ranges::find(snapshot->Elements, button.StableId,
                                                   &Keire::ScenePresentationUiDocumentDebugElement::StableId);
            return element == snapshot->Elements.end()
                       ? std::nullopt
                       : std::optional<Keire::ScenePresentationUiDocumentDebugState>(element->State);
        }

        [[nodiscard]] static std::size_t PresentationCount(const Keire::Ref<Keire::SceneRuntimeWorld>& world)
        {
            return static_cast<std::size_t>(std::ranges::count_if(world->Sessions(), [](const auto& session)
                                                                  { return session && session->Presentation(); }));
        }

        static void AddOccludedValidationLight(const Keire::Ref<Keire::SceneRuntimeSession>& session)
        {
            if (!session || !session->RuntimeScene())
                throw std::runtime_error("GPU occlusion validation requires a loaded runtime scene.");
            auto entity = session->RuntimeScene()->CreateEntity("Validation occluded point light");
            const auto transform = entity.GetComponent<Keire::TransformComponent>();
            const auto light = entity.AddComponent<Keire::PointLightComponent>();
            if (!transform || !light)
                throw std::runtime_error("GPU occlusion validation could not create its point light.");
            transform->SetLocalPosition({0.0F, 0.0F, 4.0F});
            light->SetIntensity(8.0F);
            light->SetRange(0.75F);
            light->SetShadows(Keire::ShadowQuality::Disabled);
        }

        static void PrepareFreshPoseOcclusionFixture(const Keire::Ref<Keire::SceneRuntimeSession>& session)
        {
            if (!session || !session->RuntimeScene())
                throw std::runtime_error("GPU occlusion validation requires the reloaded skinned runtime scene.");
            const auto compatibleMaterial = Keire::AssetId::Parse("77c1e51e-6397-5983-b80b-e82587b2edaa");
            for (const auto& entity : session->RuntimeScene()->Query<Keire::AnimatorComponent>())
            {
                const auto animator = entity.GetComponent<Keire::AnimatorComponent>();
                const auto renderer = entity.GetComponent<Keire::MeshRendererComponent>();
                if (!animator || !animator->SkinnedMesh() || !renderer)
                    continue;
                renderer->SetMaterial(compatibleMaterial);
                renderer->SetAlwaysVisible(false);
                return;
            }
            throw std::runtime_error("GPU occlusion validation could not find the SampleScene skinned renderer.");
        }

        static void RequireOrder(const Keire::Ref<Keire::SceneRuntimeWorld>& world,
                                 const std::vector<Keire::SceneHandle>& expected)
        {
            if (world->LoadedScenes() != expected)
                throw std::runtime_error("Additive runtime validation observed a non-deterministic session order.");
        }

        void PushClick(Keire::Application& application, const float pixelX, const float pixelY)
        {
            const auto logical = application.MainWindow()->LogicalSize();
            const auto pixels = application.MainWindow()->PixelSize();
            const float scaleX = logical.Width == 0U ? 1.0F : static_cast<float>(pixels.Width) / logical.Width;
            const float scaleY = logical.Height == 0U ? 1.0F : static_cast<float>(pixels.Height) / logical.Height;
            const auto native =
                Keire::WindowSystemInternalAccess::NativeWindow(*application.Windows(), application.MainWindow()->Id());
            if (!native)
                throw std::runtime_error("Additive runtime validation could not resolve the runtime window.");
            const auto windowId = SDL_GetWindowID(native);
            const auto x = pixelX / scaleX;
            const auto y = pixelY / scaleY;
            SDL_Event motion{};
            motion.type = SDL_EVENT_MOUSE_MOTION;
            motion.motion.windowID = windowId;
            motion.motion.x = x;
            motion.motion.y = y;
            if (!SDL_PushEvent(&motion))
                throw std::runtime_error("Additive runtime validation could not queue pointer motion.");

            for (const auto type : {SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_EVENT_MOUSE_BUTTON_UP})
            {
                SDL_Event button{};
                button.type = type;
                button.button.windowID = windowId;
                button.button.button = SDL_BUTTON_LEFT;
                button.button.x = x;
                button.button.y = y;
                if (!SDL_PushEvent(&button))
                    throw std::runtime_error("Additive runtime validation could not queue a pointer button event.");
            }
        }

        void Update(Keire::Application& application, const Keire::Ref<Keire::SceneRuntimeWorld>& world,
                    const float width, const float height)
        {
            if (Current == Phase::Disabled || Current == Phase::Complete)
                return;
            if (std::chrono::steady_clock::now() - ValidationStartedAt > std::chrono::minutes(5))
                throw std::runtime_error("Additive runtime validation timed out.");
            const auto sessions = world->Sessions();
            switch (Current)
            {
            case Phase::StartAdditiveLoad:
                if (sessions.size() != 1U)
                    throw std::runtime_error("Additive runtime validation expected exactly one startup session.");
                if (!FirstButton.Document)
                {
                    First = world->Active();
                    FirstButton = BeginValidationButton(sessions.front(), "Validation startup");
                }
                if (!ResolveValidationButton(sessions.front(), FirstButton, width, height))
                    return;
                AdditiveLoad = world->Load(BuildScenes[1], Keire::SceneLoadMode::Additive);
#if defined(KEIRE_ENABLE_TEST_HOOKS)
                if (ValidateDeviceLoss)
                {
                    Keire::RenderSystemInternalAccess::InjectDeviceLoss(*application.Renderer());
                    DeviceLossInjectedDuringLoading = true;
                }
#endif
                Current = Phase::WaitForAdditiveLoad;
                break;
            case Phase::WaitForAdditiveLoad:
                if (AdditiveLoad->State() == Keire::SceneLoadState::Failed)
                    throw std::runtime_error("Additive cooked-scene load failed: " +
                                             AdditiveLoad->Diagnostic().Message);
                if (AdditiveLoad->State() == Keire::SceneLoadState::Ready)
                {
#if defined(KEIRE_ENABLE_TEST_HOOKS)
                    if (ValidateDeviceLoss)
                    {
                        const auto diagnostic = application.Renderer()->LastDeviceLoss();
                        if (!diagnostic || !diagnostic->RecoverySucceeded)
                            return;
                        RecoveredDeviceLoss = *diagnostic;
                    }
#endif
                    if (!SecondButton.Document)
                    {
                        Second = AdditiveLoad->Result();
                        RequireOrder(world, {First, Second});
                        SecondButton = BeginValidationButton(world->Session(Second), "Validation additive");
                    }
                    if (!ResolveValidationButton(world->Session(Second), SecondButton, width, height))
                        return;
                    if (PresentationCount(world) != 2U)
                        throw std::runtime_error("Additive runtime validation did not create two presentation trees.");
                    Current = Phase::ObserveTwoScenes;
                }
                break;
            case Phase::StartNoPresentationLoad:
                NoPresentationLoad = application.Scenes()->Load(BuildScenes[2], Keire::SceneLoadMode::Additive);
                Current = Phase::WaitForNoPresentationLoad;
                break;
            case Phase::WaitForNoPresentationLoad:
                if (NoPresentationLoad->State() == Keire::SceneLoadState::Failed)
                    throw std::runtime_error("No-presentation cooked-scene load failed: " +
                                             NoPresentationLoad->Diagnostic().Message);
                if (NoPresentationLoad->State() == Keire::SceneLoadState::Ready)
                {
                    NoPresentationSession = Keire::CreateRef<Keire::SceneRuntimeSession>(NoPresentationLoad->Result());
                    NoPresentationSession->Play();
                    if (NoPresentationSession->State() != Keire::ScenePlayState::Playing ||
                        NoPresentationSession->Presentation())
                    {
                        throw std::runtime_error("No-presentation runtime session did not preserve its contract.");
                    }
                    Third = world->Adopt(NoPresentationSession);
                    RequireOrder(world, {First, Second, Third});
                    Current = Phase::ObserveThreeScenes;
                }
                break;
            case Phase::StartUnload:
                if (!world->Unload(Second))
                    throw std::runtime_error("Additive runtime validation could not queue scene unload.");
                Current = Phase::WaitForUnload;
                break;
            case Phase::WaitForUnload:
                if (!world->IsLoaded(Second))
                {
                    RequireOrder(world, {First, Third});
                    Current = Phase::ObserveUnload;
                }
                break;
            case Phase::StartReload:
                SecondButton = {};
                Reload = world->Load(BuildScenes[1], Keire::SceneLoadMode::Additive);
                Current = Phase::WaitForReload;
                break;
            case Phase::WaitForReload:
                if (Reload->State() == Keire::SceneLoadState::Failed)
                    throw std::runtime_error("Additive cooked-scene reload failed: " + Reload->Diagnostic().Message);
                if (Reload->State() == Keire::SceneLoadState::Ready)
                {
                    if (!SecondButton.Document)
                    {
                        Second = Reload->Result();
                        RequireOrder(world, {First, Third, Second});
                        SecondButton = BeginValidationButton(world->Session(Second), "Validation reloaded");
                    }
                    if (!ResolveValidationButton(world->Session(Second), SecondButton, width, height))
                        return;
                    if (!world->SetActive(Second))
                        throw std::runtime_error("Additive runtime validation could not select the reloaded session.");
                    Current = Phase::WaitForActiveReload;
                }
                break;
            case Phase::WaitForActiveReload:
                if (world->Active() == Second)
                {
                    const auto state = ButtonState(world->Session(Second)->Presentation(), SecondButton);
                    if (!state)
                        throw std::runtime_error("Additive runtime validation Button state is unavailable.");
                    PushClick(application, state->Rect.X + state->Rect.Width * 0.5F,
                              state->Rect.Y + state->Rect.Height * 0.5F);
                    Current = Phase::WaitForInput;
                }
                break;
            case Phase::WaitForInput:
                if (world->Session(Second)->Presentation()->ConsumeUiDocumentElementEvent(
                        SecondButton.Document, SecondButton.DocumentGeneration, SecondButton.Element,
                        Keire::RuntimeUiEventType::Click))
                {
                    InputHandled = true;
                    Current = Phase::StartFailedLoad;
                }
                break;
            case Phase::StartFailedLoad:
                BeforeFailedLoad = world->LoadedScenes();
                FailedLoad = world->Load(Keire::AssetId::Parse("11111111-1111-4111-8111-111111111111"),
                                         Keire::SceneLoadMode::Additive);
                Current = Phase::WaitForFailedLoad;
                break;
            case Phase::WaitForFailedLoad:
                if (FailedLoad->State() == Keire::SceneLoadState::Ready)
                    throw std::runtime_error("Missing cooked scene unexpectedly loaded during rollback validation.");
                if (FailedLoad->State() == Keire::SceneLoadState::Failed)
                {
                    FailedLoadPreserved = world->LoadedScenes() == BeforeFailedLoad;
                    if (!FailedLoadPreserved)
                        throw std::runtime_error("Failed additive load changed the previous valid runtime world.");
                    Current = Phase::StartOcclusionLoad;
                }
                break;
            case Phase::StartOcclusionLoad:
                OcclusionLoad = world->Load(BuildScenes[3], Keire::SceneLoadMode::Additive);
                Current = Phase::WaitForOcclusionLoad;
                break;
            case Phase::WaitForOcclusionLoad:
                if (OcclusionLoad->State() == Keire::SceneLoadState::Failed)
                    throw std::runtime_error("GPU occlusion stress scene load failed: " +
                                             OcclusionLoad->Diagnostic().Message);
                if (OcclusionLoad->State() == Keire::SceneLoadState::Ready)
                {
                    if (!OcclusionButton.Document)
                    {
                        Occlusion = OcclusionLoad->Result();
                        RequireOrder(world, {First, Third, Second, Occlusion});
                        AddOccludedValidationLight(world->Session(Occlusion));
                        PrepareFreshPoseOcclusionFixture(world->Session(Second));
                        OcclusionButton = BeginValidationButton(world->Session(Occlusion), "Validation occlusion");
                    }
                    if (!ResolveValidationButton(world->Session(Occlusion), OcclusionButton, width, height))
                        return;
                    if (!world->SetActive(Occlusion))
                        throw std::runtime_error("GPU occlusion validation could not activate its stress scene.");
                    Current = Phase::ObserveOcclusionFrame;
                }
                break;
            case Phase::ObserveTwoScenes:
            case Phase::ObserveThreeScenes:
            case Phase::ObserveUnload:
            case Phase::ObserveOcclusionFrame:
            case Phase::ObserveFinalFrame:
            case Phase::AwaitShutdown:
            case Phase::Disabled:
            case Phase::Complete:
                break;
            }
        }

        void ObserveSubmission(Keire::Application& application, const Keire::Ref<Keire::RenderSystem>& renderer,
                               const Keire::Ref<Keire::RenderSurface>& surface)
        {
            if (!renderer || !surface)
                throw std::runtime_error("Additive runtime validation cannot observe an unavailable renderer surface.");
            const auto contributions = Keire::RenderSystemInternalAccess::SceneContributionCount(*renderer, *surface);
            const auto uiCommands = Keire::RenderSystemInternalAccess::RuntimeUiCommandCount(*renderer);
            const auto requireSubmission =
                [&](const std::size_t expectedContributions, const std::size_t expectedPresentations)
            {
                if (contributions != expectedContributions || uiCommands < expectedPresentations)
                    throw std::runtime_error("Additive runtime validation observed an incomplete render submission.");
            };
            switch (Current)
            {
            case Phase::ObserveTwoScenes:
                requireSubmission(2U, 2U);
                TwoSceneUiCommands = uiCommands;
                Current = Phase::StartNoPresentationLoad;
                break;
            case Phase::ObserveThreeScenes:
                requireSubmission(3U, 2U);
                ThreeSceneUiCommands = uiCommands;
                Current = Phase::StartUnload;
                break;
            case Phase::ObserveUnload:
                requireSubmission(2U, 1U);
                Current = Phase::StartReload;
                break;
            case Phase::ObserveOcclusionFrame:
            {
                requireSubmission(4U, 3U);
                const auto capabilities = renderer->Capabilities();
                const auto statistics = renderer->Statistics();
                if (statistics.LastRetiredFrame == 0U || statistics.LastRetiredFrame == LastOcclusionRetiredFrame)
                    break;
                LastOcclusionRetiredFrame = statistics.LastRetiredFrame;
                ++OcclusionObservationFrames;
                const auto device = renderer->DeviceIdentity();
                const auto diagnostics = surface->OcclusionDiagnostics();
                const bool ready = HasExactOcclusionEvidence(capabilities, statistics, device, *surface, diagnostics);
                if (!ready)
                    break;
                OcclusionStatistics = statistics;
                OcclusionDevice = device;
                OcclusionSurfaceGeneration = surface->Generation();
                OcclusionDiagnostics = diagnostics;
                Current = Phase::ObserveFinalFrame;
                break;
            }
            case Phase::ObserveFinalFrame:
            {
                requireSubmission(4U, 3U);
                if (!InputHandled || !FailedLoadPreserved)
                    throw std::runtime_error("Additive runtime validation did not complete input and rollback checks.");
                const auto nativeWindow = Keire::WindowSystemInternalAccess::NativeWindow(
                    *application.Windows(), application.MainWindow()->Id());
                const bool renderedWindowLoop = renderer->Mode() == Keire::RenderMode::Rendered;
                const auto build = Keire::GetBuildInfo();
                PendingResult = nlohmann::json{{"schemaVersion", 1},
                                               {"status", "passed"},
                                               {"build",
                                                {{"gitCommit", std::string(build.GitCommit)},
                                                 {"configuration", std::string(build.Configuration)},
                                                 {"dirty", build.Dirty}}},
                                               {"renderMode", renderedWindowLoop ? "rendered" : "headless"},
                                               {"renderedWindowLoop", renderedWindowLoop},
                                               {"nativeWindowCreated", nativeWindow != nullptr},
                                               {"validationWindowHidden", !application.MainWindow()->Visible()},
                                               {"twoSceneContributions", 2},
                                               {"threeSceneContributions", 3},
                                               {"twoSceneUiCommands", TwoSceneUiCommands},
                                               {"threeSceneUiCommands", ThreeSceneUiCommands},
                                               {"noPresentationSession", true},
                                               {"unloadReloadOrder", true},
                                               {"inputHandledByActiveTopmostPresentation", true},
                                               {"failedLoadPreservedWorld", true}};
                (*PendingResult)["gpuOcclusion"] = {
                    {"automaticStressScene", true},
                    {"fourSceneContributions", 4},
                    {"observationFrames", OcclusionObservationFrames},
                    {"surfaceState", "active"},
                    {"readbackValid", OcclusionDiagnostics.ReadbackValid},
                    {"pyramidValid", OcclusionDiagnostics.PyramidValid},
                    {"candidates", OcclusionDiagnostics.Candidates},
                    {"visible", OcclusionDiagnostics.Visible},
                    {"culled", OcclusionDiagnostics.Culled},
                    {"safeOccluders", OcclusionDiagnostics.SafeOccluders},
                    {"ownership",
                     {{"sourceFrame", OcclusionDiagnostics.SourceFrame},
                      {"lastRetiredFrame", OcclusionStatistics.LastRetiredFrame},
                      {"sourceSurfaceEpoch", OcclusionDiagnostics.SourceSurfaceEpoch},
                      {"surfaceGeneration", OcclusionSurfaceGeneration},
                      {"sourceFrameSlot", OcclusionDiagnostics.SourceFrameSlot},
                      {"allowedFramesInFlight", OcclusionStatistics.AllowedFramesInFlight},
                      {"deviceAvailable", OcclusionDevice.Available},
                      {"sourceDeviceGeneration", OcclusionDiagnostics.SourceDeviceGeneration},
                      {"deviceGeneration", OcclusionDevice.DeviceGeneration}}},
                    {"localLightVisibility",
                     {{"candidates", OcclusionDiagnostics.LocalLightCandidates},
                      {"visible", OcclusionDiagnostics.LocalLightVisible},
                      {"culled", OcclusionDiagnostics.LocalLightCulled},
                      {"maskConsumed", OcclusionDiagnostics.LocalLightMaskConsumed}}},
                    {"freshPoseSkinned",
                     {{"candidates", OcclusionDiagnostics.FreshPoseSkinnedCandidates},
                      {"depthDraws", OcclusionDiagnostics.FreshPoseSkinnedDepthDraws}}},
                    {"vfxVisibility",
                     {{"maskEntries", OcclusionDiagnostics.VfxMaskEntries},
                      {"maskedDraws", OcclusionDiagnostics.VfxMaskedDraws},
                      {"maskConsumed", OcclusionDiagnostics.VfxMaskConsumed}}}};
#if defined(KEIRE_ENABLE_TEST_HOOKS)
                if (ValidateDeviceLoss)
                {
                    if (!nativeWindow || !Keire::RenderSystemInternalAccess::Device(*renderer))
                    {
                        throw std::runtime_error(
                            "Cooked runtime device-loss validation requires a real rendered window and GPU device.");
                    }
                    const auto retries = Keire::RenderSystemInternalAccess::RecoveryAttemptCountForTest(*renderer);
                    if (!DeviceLossInjectedDuringLoading || !RecoveredDeviceLoss || retries != 1U ||
                        Keire::RenderSystemInternalAccess::LostGenerationGpuCleanupCallCount(*renderer) != 0U ||
                        Keire::RenderSystemInternalAccess::LastRetriedVfxSnapshotCount(*renderer) == 0U)
                    {
                        throw std::runtime_error(
                            "Cooked runtime device-loss validation did not recover and retry exactly once.");
                    }
                    (*PendingResult)["deviceLoss"] = {
                        {"duringLoading", true},
                        {"recoverySucceeded", RecoveredDeviceLoss->RecoverySucceeded},
                        {"operation", RecoveredDeviceLoss->Operation},
                        {"backend", RecoveredDeviceLoss->Backend},
                        {"adapter", RecoveredDeviceLoss->Adapter},
                        {"recoveryAttempt", RecoveredDeviceLoss->RecoveryAttempt},
                        {"oldGeneration", RecoveredDeviceLoss->DeviceGeneration},
                        {"newGeneration", RecoveredDeviceLoss->RecoveredDeviceGeneration},
                        {"retryCount", retries},
                        {"lostGenerationGpuCleanupCalls", 0},
                        {"continuedAfterRecovery", InputHandled && FailedLoadPreserved},
                        {"retainedVfxSnapshots",
                         Keire::RenderSystemInternalAccess::LastRetriedVfxSnapshotCount(*renderer)}};
                    renderer->Flush();
                    Keire::RenderSystemInternalAccess::BlockNextAcceptedFrame(*renderer);
                    Keire::RenderSystemInternalAccess::InjectDeviceLoss(*renderer);
                    ShutdownDeviceLossArmed = true;
                    Current = Phase::AwaitShutdown;
                    application.RequestExit();
                    break;
                }
#endif
                Keire::Detail::WriteTextFileAtomically(Output, PendingResult->dump(2) + '\n');
                Current = Phase::Complete;
                application.RequestExit();
                break;
            }
            default:
                break;
            }
        }

        std::filesystem::path Output;
        std::vector<Keire::AssetId> BuildScenes;
        Phase Current = Phase::Disabled;
        std::chrono::steady_clock::time_point ValidationStartedAt = std::chrono::steady_clock::now();
        std::uint64_t LastOcclusionRetiredFrame = 0;
        Keire::Ref<Keire::SceneRuntimeLoadOperation> AdditiveLoad;
        Keire::Ref<Keire::SceneLoadOperation> NoPresentationLoad;
        Keire::Ref<Keire::SceneRuntimeLoadOperation> Reload;
        Keire::Ref<Keire::SceneRuntimeLoadOperation> FailedLoad;
        Keire::Ref<Keire::SceneRuntimeLoadOperation> OcclusionLoad;
        Keire::Ref<Keire::SceneRuntimeSession> NoPresentationSession;
        Keire::SceneHandle First;
        Keire::SceneHandle Second;
        Keire::SceneHandle Third;
        Keire::SceneHandle Occlusion;
        ValidationButton FirstButton;
        ValidationButton SecondButton;
        ValidationButton OcclusionButton;
        std::vector<Keire::SceneHandle> BeforeFailedLoad;
        std::size_t TwoSceneUiCommands = 0;
        std::size_t ThreeSceneUiCommands = 0;
        bool InputHandled = false;
        bool FailedLoadPreserved = false;
        std::uint32_t OcclusionObservationFrames = 0;
        Keire::RenderStatistics OcclusionStatistics;
        Keire::RenderDeviceIdentity OcclusionDevice;
        std::uint64_t OcclusionSurfaceGeneration = 0;
        Keire::GpuOcclusionSurfaceDiagnostics OcclusionDiagnostics;
        std::optional<nlohmann::json> PendingResult;
#if defined(KEIRE_ENABLE_TEST_HOOKS)
        bool ValidateDeviceLoss = false;
        bool DeviceLossInjectedDuringLoading = false;
        bool ShutdownDeviceLossArmed = false;
        std::optional<Keire::GpuDeviceLossDiagnostic> RecoveredDeviceLoss;
#endif

#if defined(KEIRE_ENABLE_TEST_HOOKS)
      public:
        void FinalizeDeviceLossShutdown(Keire::RenderSystem& renderer) noexcept
        {
            if (Current != Phase::AwaitShutdown || !ShutdownDeviceLossArmed)
                return;

            try
            {
                if (!Keire::RenderSystemInternalAccess::WaitForAcceptedFrameBlock(renderer))
                {
                    throw std::runtime_error(
                        "Cooked runtime shutdown validation did not observe its accepted frame block.");
                }
                renderer.Close();

                const auto diagnostic = renderer.LastDeviceLoss();
                const auto statistics = renderer.Statistics();
                const auto recoveryAttempts = Keire::RenderSystemInternalAccess::RecoveryAttemptCountForTest(renderer);
                const auto cleanupCalls =
                    Keire::RenderSystemInternalAccess::LostGenerationGpuCleanupCallCount(renderer);
                const auto healthyCandidateCleanups =
                    Keire::RenderSystemInternalAccess::HealthyRecoveryCandidateCleanupCount(renderer);
                if (!PendingResult || !RecoveredDeviceLoss || !diagnostic ||
                    diagnostic->Operation != "test frame injection" || diagnostic->RecoverySucceeded ||
                    diagnostic->RecoveryAttempt != 0U || diagnostic->RecoveredDeviceGeneration != 0U ||
                    diagnostic->DeviceGeneration != RecoveredDeviceLoss->RecoveredDeviceGeneration ||
                    renderer.DeviceState() != Keire::RenderDeviceState::Closed || recoveryAttempts != 1U ||
                    cleanupCalls != 0U || healthyCandidateCleanups != 0U || statistics.OutstandingFrames != 0U ||
                    Keire::RenderSystemInternalAccess::TerminalFailure(renderer))
                {
                    throw std::runtime_error(
                        "Cooked runtime shutdown device loss started recovery or failed to close safely.");
                }

                (*PendingResult)["deviceLoss"]["shutdown"] = {
                    {"duringShutdown", true},
                    {"acceptedFrameBlockedBeforeClose", true},
                    {"operation", diagnostic->Operation},
                    {"recoverySucceeded", false},
                    {"recoveryAttempt", 0},
                    {"oldGeneration", diagnostic->DeviceGeneration},
                    {"newGeneration", 0},
                    {"recoveryAttemptCount", recoveryAttempts},
                    {"rendererClosed", true},
                    {"outstandingFrames", statistics.OutstandingFrames},
                    {"lostGenerationGpuCleanupCalls", cleanupCalls},
                    {"healthyCandidateCleanupCalls", healthyCandidateCleanups}};
                Keire::Detail::WriteTextFileAtomically(Output, PendingResult->dump(2) + '\n');
            }
            catch (const std::exception& error)
            {
                renderer.Close();
                try
                {
                    if (!PendingResult)
                        PendingResult = nlohmann::json::object();
                    (*PendingResult)["status"] = "failed";
                    (*PendingResult)["shutdownDeviceLossError"] = error.what();
                    Keire::Detail::WriteTextFileAtomically(Output, PendingResult->dump(2) + '\n');
                }
                catch (...)
                {
                }
            }
            catch (...)
            {
                renderer.Close();
            }
            Current = Phase::Complete;
        }
#endif
    };

    RuntimeAdditiveValidation::RuntimeAdditiveValidation(std::filesystem::path output,
                                                         std::vector<Keire::AssetId> buildScenes
#if defined(KEIRE_ENABLE_TEST_HOOKS)
                                                         ,
                                                         const bool validateDeviceLoss
#endif
                                                         )
        : m_Impl(std::make_unique<Impl>(std::move(output), std::move(buildScenes)
#if defined(KEIRE_ENABLE_TEST_HOOKS)
                                                               ,
                                        validateDeviceLoss
#endif
                                        ))
    {
    }

    RuntimeAdditiveValidation::~RuntimeAdditiveValidation() = default;

    bool RuntimeAdditiveValidation::Enabled() const noexcept { return m_Impl->Current != Impl::Phase::Disabled; }

    bool RuntimeAdditiveValidation::Complete() const noexcept { return m_Impl->Current == Impl::Phase::Complete; }

    void RuntimeAdditiveValidation::Update(Keire::Application& application,
                                           const Keire::Ref<Keire::SceneRuntimeWorld>& world, const float width,
                                           const float height)
    {
        if (!world)
            throw std::invalid_argument("Additive runtime validation requires a runtime world.");
        m_Impl->Update(application, world, width, height);
    }

    void RuntimeAdditiveValidation::ObserveSubmission(Keire::Application& application,
                                                      const Keire::Ref<Keire::RenderSystem>& renderer,
                                                      const Keire::Ref<Keire::RenderSurface>& surface)
    {
        m_Impl->ObserveSubmission(application, renderer, surface);
    }

#if defined(KEIRE_ENABLE_TEST_HOOKS)
    void RuntimeAdditiveValidation::FinalizeDeviceLossShutdown(Keire::RenderSystem& renderer) noexcept
    {
        m_Impl->FinalizeDeviceLossShutdown(renderer);
    }
#endif
} // namespace KeireRuntime
