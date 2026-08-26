#include "Keire/Core.h"
#include "KeireProjectModules/SourceModulePack.h"

#include "KeireInternal/Assets/AssetInternal.h"
#include "KeireInternal/Build/PlayerPackage.h"
#include "KeireInternal/FileSystem.h"
#include "KeireInternal/RenderInternal.h"
#include "KeireInternal/Scenes/SceneRuntimeRenderingInternal.h"
#include "KeireInternal/Scripting/ManagedRuntimeApplicationServices.h"
#include "KeireInternal/Scripting/ManagedRuntimeUiServices.h"
#include "KeireInternal/WindowInternal.h"
#include "KeireRuntimeInternal/ManagedWorldRuntime.h"
#include "KeireRuntimeInternal/RuntimeAdditiveValidation.h"
#include "KeireRuntimeInternal/RuntimeCommandLine.h"
#include "KeireRuntimeInternal/RuntimeRenderBenchmark.h"
#include "KeireRuntimeInternal/RuntimeSceneRendering.h"
#include "KeireRuntimeInternal/RuntimeUiInput.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace
{
    using KeireRuntime::RuntimeCommandLine;
    using KeireRuntime::RuntimeReplayAction;

    struct RuntimeManifest final
    {
        std::filesystem::path ContentRoot;
        Keire::AssetId StartupScene;
        std::vector<Keire::AssetId> BuildScenes;
        Keire::AssetId DefaultInput;
        Keire::AssetId DefaultInputMap;
        Keire::AssetId DefaultMixer;
        Keire::AudioProjectSettings AudioSettings;
        Keire::RenderEnvironmentSettings Rendering;
        std::array<std::uint32_t, Keire::PhysicsCollisionLayerCount> PhysicsCollisionMatrix;
        std::vector<std::filesystem::path> ManagedAssemblyRoots;
        std::vector<Keire::RequiredSourceModule> RequiredModules;
        bool Scripting = false;
        bool Physics = false;
        bool Audio = false;
        bool Navigation = false;
    };

    [[nodiscard]] nlohmann::json EncodeAnimatorCheckpoint(const Keire::SceneAnimatorCheckpoint& animator)
    {
        nlohmann::json parameters = nlohmann::json::array();
        for (const auto& parameter : animator.State.Parameters)
            parameters.push_back({{"id", parameter.Id},
                                  {"type", static_cast<std::uint8_t>(parameter.Type)},
                                  {"float", parameter.FloatValue},
                                  {"integer", parameter.IntegerValue},
                                  {"boolean", parameter.BooleanValue}});
        nlohmann::json layers = nlohmann::json::array();
        for (const auto& layer : animator.State.Layers)
        {
            nlohmann::json encoded{{"id", layer.Id},
                                   {"state", layer.StateId},
                                   {"time", layer.Time},
                                   {"weight", layer.Weight},
                                   {"normalizedTime", layer.NormalizedTime}};
            if (layer.Transition)
            {
                const auto& transition = *layer.Transition;
                encoded["transition"] = {{"id", transition.Id},
                                         {"source", transition.SourceStateId},
                                         {"destination", transition.DestinationStateId},
                                         {"sourceTime", transition.SourceTime},
                                         {"destinationTime", transition.DestinationTime},
                                         {"elapsed", transition.Elapsed},
                                         {"duration", transition.Duration}};
            }
            layers.push_back(std::move(encoded));
        }
        const auto& root = animator.State.PreviousRoot;
        return {{"entity", animator.Entity.ToString()},
                {"playing", animator.State.Playing},
                {"hasPreviousRootRotation", animator.State.HasPreviousRootRotation},
                {"previousRoot",
                 {{"translation", {root.Translation.X, root.Translation.Y, root.Translation.Z}},
                  {"rotation", {root.Rotation.X, root.Rotation.Y, root.Rotation.Z, root.Rotation.W}},
                  {"scale", {root.Scale.X, root.Scale.Y, root.Scale.Z}}}},
                {"parameters", std::move(parameters)},
                {"layers", std::move(layers)}};
    }

    [[nodiscard]] Keire::SceneAnimatorCheckpoint DecodeAnimatorCheckpoint(const nlohmann::json& encoded)
    {
        const auto vector3 = [](const nlohmann::json& value)
        {
            if (!value.is_array() || value.size() != 3U)
                throw std::runtime_error("Animator replay checkpoint vector is malformed.");
            return Keire::Vector3{value[0].get<float>(), value[1].get<float>(), value[2].get<float>()};
        };
        Keire::SceneAnimatorCheckpoint result;
        result.Entity = Keire::EntityId::Parse(encoded.at("entity").get<std::string>());
        result.State.Playing = encoded.at("playing").get<bool>();
        result.State.HasPreviousRootRotation = encoded.at("hasPreviousRootRotation").get<bool>();
        const auto& root = encoded.at("previousRoot");
        result.State.PreviousRoot.Translation = vector3(root.at("translation"));
        const auto& rotation = root.at("rotation");
        if (!rotation.is_array() || rotation.size() != 4U)
            throw std::runtime_error("Animator replay checkpoint rotation is malformed.");
        result.State.PreviousRoot.Rotation = {rotation[0].get<float>(), rotation[1].get<float>(),
                                              rotation[2].get<float>(), rotation[3].get<float>()};
        result.State.PreviousRoot.Scale = vector3(root.at("scale"));
        for (const auto& parameter : encoded.at("parameters"))
            result.State.Parameters.push_back(
                {parameter.at("id").get<std::string>(),
                 static_cast<Keire::AnimationParameterType>(parameter.at("type").get<std::uint8_t>()),
                 parameter.at("float").get<float>(), parameter.at("integer").get<std::int32_t>(),
                 parameter.at("boolean").get<bool>()});
        for (const auto& layer : encoded.at("layers"))
        {
            Keire::AnimatorCheckpointLayer decoded{
                layer.at("id").get<std::string>(), layer.at("state").get<std::string>(), layer.at("time").get<float>(),
                layer.at("weight").get<float>(), layer.at("normalizedTime").get<float>()};
            if (layer.contains("transition"))
            {
                const auto& transition = layer.at("transition");
                decoded.Transition = Keire::AnimatorCheckpointTransition{
                    transition.at("id").get<std::string>(),          transition.at("source").get<std::string>(),
                    transition.at("destination").get<std::string>(), transition.at("sourceTime").get<float>(),
                    transition.at("destinationTime").get<float>(),   transition.at("elapsed").get<float>(),
                    transition.at("duration").get<float>()};
            }
            result.State.Layers.push_back(std::move(decoded));
        }
        return result;
    }

    [[nodiscard]] RuntimeManifest LoadManifest(const std::filesystem::path& content)
    {
        std::ifstream stream(content / "runtime-manifest.json", std::ios::binary);
        if (!stream)
            throw Keire::CommandLineError("Cooked content has no readable runtime-manifest.json.");
        nlohmann::json source;
        stream >> source;
        if (!stream || !source.is_object())
            throw Keire::CommandLineError("Cooked content has no valid runtime-manifest.json.");
        const auto schema = source.value("schemaVersion", 0U);
        if (schema < 4)
            throw Keire::CommandLineError(
                "Cooked runtime manifest schema is obsolete; recook the project with this Kéire version.");
        if (schema > 4)
            throw Keire::CommandLineError(
                "Cooked runtime manifest requires a newer Kéire runtime (supported schema: 4).");
        RuntimeManifest result;
        result.ContentRoot = std::filesystem::absolute(content).lexically_normal();
        const auto& modules = source.at("sourceModules");
        if (!modules.is_array())
            throw Keire::CommandLineError("Runtime manifest sourceModules must be an array.");
        for (const auto& module : modules)
            result.RequiredModules.push_back(
                {module.at("id").get<std::string>(), module.at("version").get<std::string>()});
        result.StartupScene = Keire::AssetId::Parse(source.at("startupScene").get<std::string>());
        result.BuildScenes = KeireRuntime::ParseRuntimeValidationScenes(source, result.StartupScene);
        if (source.contains("defaultInput") && !source.at("defaultInput").is_null())
            result.DefaultInput = Keire::AssetId::Parse(source.at("defaultInput").get<std::string>());
        if (source.contains("defaultInputMap") && !source.at("defaultInputMap").is_null())
            result.DefaultInputMap = Keire::AssetId::Parse(source.at("defaultInputMap").get<std::string>());
        if (source.contains("defaultMixer") && !source.at("defaultMixer").is_null())
            result.DefaultMixer = Keire::AssetId::Parse(source.at("defaultMixer").get<std::string>());
        const auto& rendering = source.at("rendering");
        const auto& ambient = rendering.at("ambientColor");
        if (!ambient.is_array() || ambient.size() != 4)
            throw Keire::CommandLineError("Runtime rendering settings contain an invalid ambient color.");
        result.Rendering.AmbientColor = {ambient[0].get<float>(), ambient[1].get<float>(), ambient[2].get<float>(),
                                         ambient[3].get<float>()};
        result.Rendering.AmbientIntensity = rendering.at("ambientIntensity").get<float>();
        result.Rendering.Exposure = rendering.at("exposure").get<float>();
        const auto& subsystems = source.at("subsystems");
        result.Scripting = subsystems.at("scripting").get<bool>();
        result.Physics = subsystems.at("physics").get<bool>();
        result.Audio = subsystems.at("audio").get<bool>();
        result.Navigation = subsystems.at("navigation").get<bool>();
        if (const auto audio = source.find("audio"); audio != source.end())
        {
            if (!audio->is_object())
                throw Keire::CommandLineError("Runtime audio settings must be an object.");
            result.AudioSettings.MixSampleRate = audio->at("mixSampleRate").get<std::uint32_t>();
            result.AudioSettings.PeriodFrames = audio->at("periodFrames").get<std::uint32_t>();
            const auto layout = audio->at("outputLayout").get<std::string>();
            if (layout == "mono")
                result.AudioSettings.OutputLayout = Keire::AudioChannelLayout::Mono;
            else if (layout == "stereo")
                result.AudioSettings.OutputLayout = Keire::AudioChannelLayout::Stereo;
            else if (layout == "5.1")
                result.AudioSettings.OutputLayout = Keire::AudioChannelLayout::Surround51;
            else if (layout == "7.1")
                result.AudioSettings.OutputLayout = Keire::AudioChannelLayout::Surround71;
            else
                throw Keire::CommandLineError("Runtime audio output layout is unsupported.");
            result.AudioSettings.MaximumVoices = audio->at("maximumVoices").get<std::uint32_t>();
            result.AudioSettings.MaximumVirtualVoices = audio->at("maximumVirtualVoices").get<std::uint32_t>();
        }
        const auto& physics = source.at("physics");
        const auto& layerNames = physics.at("layerNames");
        const auto& collisionMatrix = physics.at("collisionMatrix");
        if (!layerNames.is_array() || layerNames.size() != Keire::PhysicsCollisionLayerCount ||
            !collisionMatrix.is_array() || collisionMatrix.size() != Keire::PhysicsCollisionLayerCount)
        {
            throw Keire::CommandLineError("Runtime physics settings must define exactly 32 collision layers.");
        }
        auto physicsSettings = Keire::DefaultProjectAuthoringSettings();
        physicsSettings.DefaultMixer = result.DefaultMixer;
        physicsSettings.Audio = result.AudioSettings;
        for (std::size_t index = 0; index < Keire::PhysicsCollisionLayerCount; ++index)
        {
            physicsSettings.PhysicsLayerNames[index] = layerNames[index].get<std::string>();
            physicsSettings.PhysicsCollisionMatrix[index] = collisionMatrix[index].get<std::uint32_t>();
        }
        try
        {
            Keire::ValidateProjectAuthoringSettings(physicsSettings);
        }
        catch (const std::exception& error)
        {
            throw Keire::CommandLineError(std::string("Runtime physics settings are invalid: ") + error.what());
        }
        result.PhysicsCollisionMatrix = physicsSettings.PhysicsCollisionMatrix;
        for (const auto& root : source.at("managedAssemblyRoots"))
        {
            const auto path = std::filesystem::path(root.get<std::string>()).lexically_normal();
            if (path.empty() || path.is_absolute() || std::ranges::find(path, "..") != path.end())
                throw Keire::CommandLineError("Runtime manifest contains an invalid managed assembly root.");
            result.ManagedAssemblyRoots.push_back(path);
        }
        const auto& streaming = source.at("streaming");
        const auto pageBytes = streaming.at("pageBytes").get<std::uint64_t>();
        const auto concurrentReads = streaming.at("maximumConcurrentReads").get<std::uint32_t>();
        if (!source.at("buildIdentity").is_object() || pageBytes < 4096 ||
            pageBytes > std::uint64_t{16U} * 1024U * 1024U || concurrentReads == 0 || concurrentReads > 256)
            throw Keire::CommandLineError("Runtime manifest contains invalid build or streaming settings.");
        if (!result.StartupScene || !Keire::Math::IsFinite(result.Rendering.AmbientColor) ||
            !std::isfinite(result.Rendering.AmbientIntensity) || !std::isfinite(result.Rendering.Exposure))
            throw Keire::CommandLineError("Runtime manifest contains invalid identities or rendering values.");
        return result;
    }

    struct RuntimeReplayState final
    {
        Keire::Ref<Keire::SceneRuntimeSession> Session;
        bool Started = false;
        bool ReportWritten = false;
    };

    [[nodiscard]] Keire::ReplayFingerprints BuildReplayFingerprints(const RuntimeManifest& manifest,
                                                                    const Keire::ModuleRegistry& modules)
    {
        const auto& build = Keire::GetBuildInfo();
        const auto manifestText = Keire::Detail::ReadTextFile(manifest.ContentRoot / "runtime-manifest.json",
                                                              std::size_t{64} * 1024U * 1024U);
        const auto catalogText =
            Keire::Detail::ReadTextFile(manifest.ContentRoot / "catalog.json", std::size_t{64} * 1024U * 1024U);
        return {.EngineBuild = std::string(build.Version) + '-' + std::string(build.GitCommit) + '-' +
                               std::string(build.Configuration) + '-' + std::string(build.Platform) + '-' +
                               std::string(build.Architecture),
                .Project = Keire::Detail::DigestToString(Keire::Detail::Sha256(std::as_bytes(std::span(manifestText)))),
                .Modules = modules.Fingerprint(),
                .Content = Keire::Detail::DigestToString(Keire::Detail::Sha256(std::as_bytes(std::span(catalogText)))),
                .DeterministicConfiguration = "fixed-tick-v1;scene=" + manifest.StartupScene.ToString()};
    }

    [[nodiscard]] std::string_view ReplayStateName(const Keire::ReplaySessionState state) noexcept
    {
        switch (state)
        {
        case Keire::ReplaySessionState::Idle:
            return "idle";
        case Keire::ReplaySessionState::Recording:
            return "recording";
        case Keire::ReplaySessionState::Playing:
            return "playing";
        case Keire::ReplaySessionState::Verifying:
            return "verifying";
        case Keire::ReplaySessionState::Paused:
            return "paused";
        case Keire::ReplaySessionState::Completed:
            return "completed";
        case Keire::ReplaySessionState::Diverged:
            return "diverged";
        case Keire::ReplaySessionState::Failed:
            return "failed";
        }
        return "unknown";
    }

    void WriteReplayReport(const RuntimeCommandLine& commandLine, RuntimeReplayState& state,
                           const Keire::ReplaySessionStatus& status)
    {
        if (commandLine.OutputPath.empty() || state.ReportWritten)
            return;
        nlohmann::json report{
            {"state", ReplayStateName(status.State)},
            {"profile", status.Profile == Keire::ReplayProfile::StrictVerified ? "strict" : "performance"},
            {"currentTick", status.CurrentTick},
            {"tickCount", status.TickCount},
            {"checkpointCount", status.CheckpointCount},
            {"diagnostic", status.Diagnostic}};
        if (status.Divergence)
        {
            report["divergence"] = {{"tick", status.Divergence->Tick},
                                    {"expected", Keire::Detail::DigestToString(status.Divergence->Expected)},
                                    {"actual", Keire::Detail::DigestToString(status.Divergence->Actual)},
                                    {"message", status.Divergence->Message}};
        }
        Keire::Detail::WriteTextFileAtomically(commandLine.OutputPath, report.dump(2) + '\n');
        state.ReportWritten = true;
    }

    class RuntimeLayer final : public Keire::Layer, public KeireRuntime::ManagedWorldRuntimeServices
    {
      public:
        RuntimeLayer(const Keire::AssetId startupScene, const Keire::AssetId defaultInput,
                     const Keire::AssetId defaultInputMap, const Keire::AssetId defaultMixer,
                     const Keire::RenderEnvironmentSettings rendering, RuntimeCommandLine commandLine,
                     Keire::ReplayFingerprints fingerprints, std::shared_ptr<RuntimeReplayState> replayState)
            : Layer("Runtime"), ManagedWorldRuntimeServices(false, rendering), m_StartupScene(startupScene),
              m_DefaultInput(defaultInput), m_DefaultInputMap(defaultInputMap), m_DefaultMixer(defaultMixer),
              m_CommandLine(std::move(commandLine)), m_ReplayFingerprints(std::move(fingerprints)),
              m_ReplayState(std::move(replayState)),
              m_AdditiveValidation(m_CommandLine.AdditiveValidationOutput, m_CommandLine.ValidationScenes
#if defined(KEIRE_ENABLE_TEST_HOOKS)
                                   ,
                                   m_CommandLine.ValidateDeviceLoss
#endif
                                   ),
              m_RenderBenchmark(m_CommandLine.RenderBenchmarkOutput, m_CommandLine.PresentMode)
        {
        }

      protected:
        void OnAttach() override
        {
            BindManagedApplication(Owner());
            BindManagedWorld(Owner(), m_Runtime, m_Scene, m_Presentation, m_ReplayState->Session,
                             m_ReplayState->Started, m_DefaultMixer);
            m_Load = Owner().Scenes()->Load(m_StartupScene);
            Keire::RenderSurfaceSpecification surface;
            surface.Name = "Runtime";
            const auto pixels = Owner().MainWindow()->PixelSize();
            surface.Width = std::max(pixels.Width, 1U);
            surface.Height = std::max(pixels.Height, 1U);
            m_View = Owner().Renderer()->CreateView(surface);
            Keire::RenderSystemInternalAccess::SetPresentationSurface(*Owner().Renderer(), m_View->Surface());
            m_EventSink = Keire::WindowSystemInternalAccess::AddEventSink(*Owner().Windows(), this, HandleNativeEvent);
            if (const auto input = Owner().Input())
            {
                m_InputUser = input->CreateUser("Player");
                for (const auto& device : input->Devices())
                {
                    if (device.Connected && !input->PairDevice(m_InputUser, device.Id))
                        throw std::runtime_error("The player could not claim a connected input device.");
                }
                if (m_DefaultInput)
                {
                    m_InputContext = input->CreateActionContext(m_DefaultInput, m_InputUser);
                    m_InputMapEnabled = TryEnableDefaultInputMap();
                }
            }
            BindManagedInput(m_InputContext, m_InputUser);
            if (const auto scripts = Owner().Scripts())
                scripts->SetRuntimeServices(this);
        }

        void OnDetach() noexcept override
        {
            ApplyManagedCursorMode(true);
            KeireRuntime::SynchronizeRuntimeUiTextInput({}, Owner().Windows(), Owner().MainWindow());
            if (m_EventSink)
            {
                if (const auto windows = Owner().Windows())
                    Keire::WindowSystemInternalAccess::RemoveEventSink(*windows, m_EventSink);
                m_EventSink = 0;
            }
            try
            {
                const auto replay = Owner().Replay();
                if (replay && replay->IsOpen() && m_ReplayState->Started)
                {
                    WriteReplayReport(m_CommandLine, *m_ReplayState, replay->Status());
                    replay->Stop();
                }
            }
            catch (...)
            {
            }
            if (const auto scripts = Owner().Scripts())
                scripts->SetRuntimeServices(nullptr);
            UnbindManagedWorld();
            UnbindManagedApplication();
            if (const auto renderer = Owner().Renderer())
                Keire::RenderSystemInternalAccess::SetPresentationSurface(*renderer, {});
            if (m_Presentation)
                m_Presentation->Clear();
            if (m_Runtime)
                m_Runtime->Stop();
            m_Runtime.Reset();
            m_ReplayState->Session.Reset();
            m_Presentation.Reset();
            m_Scene.Reset();
        }

        void OnFixedUpdate(const Keire::Time& time) override
        {
            if (const auto world = RuntimeWorld(); world && m_Runtime)
            {
                world->FixedUpdate(static_cast<float>(time.FixedDeltaTime().Seconds()));
                if (m_CommandLine.TickLimit != 0 && ++m_FixedTicks >= m_CommandLine.TickLimit)
                    Owner().RequestExit();
            }
        }

        void OnUpdate(const Keire::Time& time) override
        {
            if (!m_InputMapEnabled)
                m_InputMapEnabled = TryEnableDefaultInputMap();
            [[maybe_unused]] const bool transitioned =
                ProcessManagedSceneTransition(m_CommandLine.ReplayAction != RuntimeReplayAction::None &&
                                                  m_CommandLine.ReplayProfile == Keire::ReplayProfile::StrictVerified,
                                              nullptr);
            if (!m_Runtime)
            {
                if (m_Load->State() == Keire::SceneLoadState::Failed)
                    throw std::runtime_error("Startup scene load failed: " + m_Load->Diagnostic().Message);
                if (m_Load->State() != Keire::SceneLoadState::Ready)
                    return;
                m_Runtime = Keire::CreateRef<Keire::SceneRuntimeSession>(m_Load->Result(), Owner().Assets(),
                                                                         Owner().Audio(), Owner().Physics());
                if (const auto presentation = m_Runtime->Presentation())
                    presentation->SetDefaultMixer(m_DefaultMixer);
                m_Runtime->Play();
                if (m_Runtime->State() == Keire::ScenePlayState::Faulted)
                    throw std::runtime_error("Startup scene Play failed: " + m_Runtime->Diagnostic().Message);
                m_Scene = m_Runtime->RuntimeScene();
                m_Presentation = m_Runtime->Presentation();
                if (!AdoptManagedScene(m_Runtime))
                    throw std::runtime_error("Startup scene runtime registration failed.");
                m_ReplayState->Session = m_Runtime;
                if (m_CommandLine.ReplayAction != RuntimeReplayAction::None)
                {
                    m_Runtime->SetDeterministicSimulation(m_CommandLine.ReplayProfile ==
                                                          Keire::ReplayProfile::StrictVerified);
                }
                if (m_CommandLine.ReplayAction == RuntimeReplayAction::Record)
                {
                    Owner().Replay()->BeginRecording(
                        {m_CommandLine.ReplayPath, m_CommandLine.ReplayProfile, m_ReplayFingerprints});
                    m_ReplayState->Started = true;
                }
                else if (m_CommandLine.ReplayAction == RuntimeReplayAction::Play ||
                         m_CommandLine.ReplayAction == RuntimeReplayAction::Verify)
                {
                    Owner().Replay()->BeginPlayback({m_CommandLine.ReplayPath, m_ReplayFingerprints,
                                                     m_CommandLine.ReplayAction == RuntimeReplayAction::Verify});
                    m_ReplayState->Started = true;
                }
            }
            m_Scene = m_Runtime->RuntimeScene();
            m_Presentation = m_Runtime->Presentation();
            if (m_ReplayState->Started)
            {
                const auto status = Owner().Replay()->Status();
                if (status.State == Keire::ReplaySessionState::Completed ||
                    status.State == Keire::ReplaySessionState::Diverged ||
                    status.State == Keire::ReplaySessionState::Failed)
                {
                    WriteReplayReport(m_CommandLine, *m_ReplayState, status);
                    Owner().RequestExit(status.State == Keire::ReplaySessionState::Diverged ||
                                                status.State == Keire::ReplaySessionState::Failed
                                            ? 2
                                            : 0);
                    return;
                }
            }
            const auto pixels = Owner().MainWindow()->PixelSize();
            const auto width = std::max(pixels.Width, 1U);
            const auto height = std::max(pixels.Height, 1U);
            const auto world = RuntimeWorld();
            if (!world)
                throw std::runtime_error("The scene runtime world is unavailable.");
            world->SetPresentationViewport(static_cast<float>(width), static_cast<float>(height));
            world->Update(static_cast<float>(time.DeltaTime().Seconds()));
            if (m_Runtime->State() == Keire::ScenePlayState::Faulted)
                throw std::runtime_error("Startup scene runtime failed: " + m_Runtime->Diagnostic().Message);
            m_AdditiveValidation.Update(Owner(), world, static_cast<float>(width), static_cast<float>(height));
            const auto activePresentation = Keire::Internal::ActiveRuntimePresentation(world);
            KeireRuntime::SynchronizeRuntimeUiTextInput(activePresentation, Owner().Windows(), Owner().MainWindow());
            const auto selected = Keire::Internal::SelectRuntimeRenderSession(world);
            m_View->Surface()->RequestSize(width, height);
            Keire::RenderCamera camera;
            if (selected.Camera)
            {
                camera.View = Keire::Math::Inverse(selected.Camera->Transform->WorldMatrix());
                camera.Projection =
                    selected.Camera->Camera->ProjectionMatrix(static_cast<float>(width) / static_cast<float>(height));
                camera.ClearColor = selected.Camera->Camera->ClearColor();
                camera.NearPlane = selected.Camera->Camera->NearPlane();
                camera.FarPlane = selected.Camera->Camera->FarPlane();
            }
            m_View->SetCamera(camera);
            auto environment = RenderEnvironment();
            environment.SkyVisible = selected.Camera && environment.SkyVisible &&
                                     selected.Camera->Camera->ClearMode() == Keire::CameraClearMode::Skybox;
            KeireRuntime::SubmitRuntimeWorldRendering(Owner().Renderer(), world, m_View, environment,
                                                      MaterialParameters(), selected.Session,
                                                      selected.Camera.has_value());
            if (m_AdditiveValidation.Enabled())
                m_AdditiveValidation.ObserveSubmission(Owner(), Owner().Renderer(), m_View->Surface());
            if (m_RenderBenchmark.Enabled())
                m_RenderBenchmark.Update(Owner(), Owner().Renderer());
            if (m_CommandLine.Frames != 0 && ++m_RenderedFrames >= m_CommandLine.Frames)
                Owner().RequestExit();
        }

        static void HandleNativeEvent(void* context, const SDL_Event& event) noexcept
        {
            try
            {
                static_cast<RuntimeLayer*>(context)->ProcessNativeEvent(event);
            }
            catch (...)
            {
            }
        }

        void ProcessNativeEvent(const SDL_Event& event)
        {
            const auto world = RuntimeWorld();
            if (!world)
                return;
            const auto logical = Owner().MainWindow()->LogicalSize();
            const auto pixels = Owner().MainWindow()->PixelSize();
            const float scaleX =
                logical.Width == 0 ? 1.0F : static_cast<float>(pixels.Width) / static_cast<float>(logical.Width);
            const float scaleY =
                logical.Height == 0 ? 1.0F : static_cast<float>(pixels.Height) / static_cast<float>(logical.Height);
            const auto focused =
                KeireRuntime::ProcessRuntimeUiEventStack(world, m_Presentation, event, scaleX, scaleY, m_UiPointer);
            KeireRuntime::SynchronizeRuntimeUiTextInput(focused, Owner().Windows(), Owner().MainWindow());
        }

        void WriteManagedLog(const Keire::ManagedLogLevel level, const std::string_view message) noexcept override
        {
            const auto logger = Keire::Log::GetClientLogger();
            if (logger)
            {
                const auto native = static_cast<Keire::LogLevel>(std::min(
                    static_cast<std::uint8_t>(level), static_cast<std::uint8_t>(Keire::ManagedLogLevel::Critical)));
                logger.Write(native, message);
            }
        }

        [[nodiscard]] float ManagedDeltaTime() const noexcept override
        {
            return static_cast<float>(Owner().GetTime().DeltaTime().Seconds());
        }

        [[nodiscard]] float ManagedFixedDeltaTime() const noexcept override
        {
            return static_cast<float>(Owner().GetTime().FixedDeltaTime().Seconds());
        }

        [[nodiscard]] float ManagedUnscaledDeltaTime() const noexcept override
        {
            return static_cast<float>(Owner().GetTime().UnscaledDeltaTime().Seconds());
        }

        [[nodiscard]] double ManagedElapsedTime() const noexcept override
        {
            return Owner().GetTime().TimeSinceStartup().Seconds();
        }

        void SetManagedCursorVisible(const bool visible) noexcept override
        {
            m_ManagedCursorVisible = visible;
            ApplyManagedCursorMode();
        }

        void SetManagedCursorLocked(const bool locked) noexcept override
        {
            m_ManagedCursorLocked = locked;
            ApplyManagedCursorMode();
        }

        [[nodiscard]] bool IsManagedCursorVisible() const noexcept override { return m_ManagedCursorVisible; }

        [[nodiscard]] bool IsManagedCursorLocked() const noexcept override { return m_ManagedCursorLocked; }

        [[nodiscard]] Keire::Vector2 ReadManagedInput(const std::string_view action) noexcept override
        {
            try
            {
                if (!m_InputContext || !m_InputContext->EnableMap("Player"))
                    return {};
                const auto handle = m_InputContext->FindAction("Player", action);
                if (!handle)
                    return {};
                const auto value = handle.Value().AsAxis2D();
                return {value.X, value.Y};
            }
            catch (...)
            {
                return {};
            }
        }

        [[nodiscard]] Keire::ManagedInputState ReadManagedInputState(const std::string_view action) noexcept override
        {
            try
            {
                if (!m_InputContext || !m_InputContext->EnableMap("Player"))
                    return Keire::ManagedInputState::None;
                const auto handle = m_InputContext->FindAction("Player", action);
                if (!handle)
                    return Keire::ManagedInputState::None;
                auto state = Keire::ManagedInputState::None;
                if (handle.Value().Magnitude() >= 0.5F)
                    state = state | Keire::ManagedInputState::Held;
                if (handle.WasStartedThisFrame() || handle.WasPerformedThisFrame())
                    state = state | Keire::ManagedInputState::Pressed;
                if (handle.WasCanceledThisFrame())
                    state = state | Keire::ManagedInputState::Released;
                return state;
            }
            catch (...)
            {
                return Keire::ManagedInputState::None;
            }
        }

        [[nodiscard]] std::optional<Keire::ManagedRaycastHit>
        RaycastManaged(const Keire::ManagedRaycastQuery& query) noexcept override
        {
            try
            {
                const auto runtime = RuntimeSessionForWorld(query.World);
                if (!runtime)
                    return std::nullopt;
                Keire::PhysicsRayQuery native;
                native.Origin = query.Origin;
                native.Direction = query.Direction;
                native.MaximumDistance = query.MaximumDistance;
                native.Mask = query.Mask;
                native.IncludeTriggers = query.IncludeTriggers;
                const auto hits = runtime->RayCast(native, Keire::EntityId(query.IgnoredEntity));
                if (hits.empty())
                    return std::nullopt;
                const auto& hit = hits.front();
                return Keire::ManagedRaycastHit{hit.Entity.Value(), hit.Hit.Position, hit.Hit.Normal, hit.Hit.Distance};
            }
            catch (...)
            {
                return std::nullopt;
            }
        }

        [[nodiscard]] bool PlayManagedAudio(const Keire::AssetId entity, const Keire::AssetId clip,
                                            const float gain) noexcept override
        {
            return PlayManagedAudio({.Entity = entity, .Clip = clip, .Gain = gain});
        }

        [[nodiscard]] bool PlayManagedAudio(const Keire::ManagedAudioPlayback& playback) noexcept override
        {
            try
            {
                const auto runtime = RuntimeSessionFor(playback.Entity);
                const auto scene = runtime ? runtime->RuntimeScene() : Keire::Ref<Keire::Scene>{};
                const auto presentation =
                    runtime ? runtime->Presentation() : Keire::Ref<Keire::ScenePresentationRuntime>{};
                auto target = scene ? scene->FindEntity(Keire::EntityId(playback.Entity)) : Keire::Entity{};
                if (!target || !presentation || !playback.Clip)
                    return false;
                Keire::AudioSourceComponentState candidate{
                    .Clip = playback.Clip,
                    .Mixer = playback.Mixer,
                    .BusId = playback.BusId,
                    .Bus = playback.Bus,
                    .Gain = playback.Gain,
                    .Pitch = playback.Pitch,
                    .Priority = playback.Priority,
                    .MinimumDistance = playback.MinimumDistance,
                    .MaximumDistance = playback.MaximumDistance,
                    .Attenuation = playback.Attenuation,
                    .Loop = playback.Loop,
                    .Spatial = playback.Spatial,
                    .PlayOnAwake = true,
                };
                Keire::AudioSourceComponent::ValidateState(candidate);

                auto source = target.GetComponent<Keire::AudioSourceComponent>();
                if (!source)
                    source = target.AddComponent<Keire::AudioSourceComponent>();
                source->ApplyState(std::move(candidate));

                // Commit the component before replacing its voice. A newly added source may not be tracked until the
                // next presentation synchronization, so PlayOnAwake remains the reliable deferred-play fallback.
                (void)presentation->Stop(target.Id());
                (void)presentation->Play(target.Id());
                return true;
            }
            catch (...)
            {
                return false;
            }
        }

        [[nodiscard]] bool StopManagedAudio(const Keire::AssetId entity) noexcept override
        {
            try
            {
                const auto runtime = RuntimeSessionFor(entity);
                const auto scene = runtime ? runtime->RuntimeScene() : Keire::Ref<Keire::Scene>{};
                const auto presentation =
                    runtime ? runtime->Presentation() : Keire::Ref<Keire::ScenePresentationRuntime>{};
                const auto target = scene ? scene->FindEntity(Keire::EntityId(entity)) : Keire::Entity{};
                if (!target || !presentation)
                    return false;
                if (const auto source = target.GetComponent<Keire::AudioSourceComponent>())
                    source->SetPlayOnAwake(false);
                return presentation->Stop(target.Id());
            }
            catch (...)
            {
                return false;
            }
        }

        [[nodiscard]] bool PauseManagedAudio(const Keire::AssetId entity, const bool paused) noexcept override
        {
            try
            {
                const auto presentation = PresentationFor(entity);
                return presentation && (paused ? presentation->Pause(Keire::EntityId(entity))
                                               : presentation->Resume(Keire::EntityId(entity)));
            }
            catch (...)
            {
                return false;
            }
        }

        [[nodiscard]] bool SeekManagedAudio(const Keire::AssetId entity, const float positionSeconds) noexcept override
        {
            try
            {
                const auto presentation = PresentationFor(entity);
                return presentation && presentation->Seek(Keire::EntityId(entity), positionSeconds);
            }
            catch (...)
            {
                return false;
            }
        }

        [[nodiscard]] Keire::ManagedAudioSourceStatus
        ManagedAudioStatus(const Keire::AssetId entity) const noexcept override
        {
            try
            {
                const auto presentation = PresentationFor(entity);
                if (!presentation)
                    return {};
                const auto state = presentation->Playback(Keire::EntityId(entity));
                return {static_cast<Keire::ManagedAudioPlaybackState>(state.State), state.PositionSeconds,
                        state.DurationSeconds};
            }
            catch (...)
            {
                return {};
            }
        }

        [[nodiscard]] bool PlayManagedVfx(const Keire::AssetId entity, const Keire::AssetId effect,
                                          const bool restart) noexcept override
        {
            try
            {
                const auto runtime = RuntimeSessionFor(entity);
                return runtime && runtime->PlayVfx(Keire::EntityId(entity), effect, restart);
            }
            catch (...)
            {
                return false;
            }
        }

        [[nodiscard]] bool StopManagedVfx(const Keire::AssetId entity) noexcept override
        {
            try
            {
                const auto runtime = RuntimeSessionFor(entity);
                return runtime && runtime->StopVfx(Keire::EntityId(entity));
            }
            catch (...)
            {
                return false;
            }
        }

        [[nodiscard]] bool PauseManagedVfx(const Keire::AssetId entity, const bool paused) noexcept override
        {
            try
            {
                const auto runtime = RuntimeSessionFor(entity);
                return runtime && runtime->PauseVfx(Keire::EntityId(entity), paused);
            }
            catch (...)
            {
                return false;
            }
        }

        [[nodiscard]] bool IsManagedVfxAlive(const Keire::AssetId entity) const noexcept override
        {
            try
            {
                const auto runtime = RuntimeSessionFor(entity);
                return runtime && runtime->IsVfxAlive(Keire::EntityId(entity));
            }
            catch (...)
            {
                return false;
            }
        }

        [[nodiscard]] bool SendManagedVfxEvent(const Keire::AssetId entity, const std::string_view eventName,
                                               const std::uint32_t spawnCount) noexcept override
        {
            try
            {
                const auto runtime = RuntimeSessionFor(entity);
                return runtime && runtime->SendVfxEvent(Keire::EntityId(entity), eventName, spawnCount);
            }
            catch (...)
            {
                return false;
            }
        }

        [[nodiscard]] bool SetManagedVfxParameter(const Keire::AssetId entity,
                                                  const Keire::VfxParameterOverride& value) noexcept override
        {
            try
            {
                const auto runtime = RuntimeSessionFor(entity);
                return runtime && runtime->SetVfxParameter(Keire::EntityId(entity), value);
            }
            catch (...)
            {
                return false;
            }
        }

        [[nodiscard]] bool SetManagedUiText(const Keire::AssetId entity, const std::string_view text) noexcept override
        {
            try
            {
                const auto scene = RuntimeSceneFor(entity);
                const auto target = scene ? scene->FindEntity(Keire::EntityId(entity)) : Keire::Entity{};
                const auto component =
                    target ? target.GetComponent<Keire::UiTextComponent>() : Keire::Ref<Keire::UiTextComponent>{};
                if (!component)
                    return false;
                component->SetText(std::string(text));
                return true;
            }
            catch (...)
            {
                return false;
            }
        }

        [[nodiscard]] bool ConsumeManagedUiClick(const Keire::AssetId entity) noexcept override
        {
            try
            {
                const auto presentation = PresentationFor(entity);
                return presentation && presentation->ConsumeClick(Keire::EntityId(entity));
            }
            catch (...)
            {
                return false;
            }
        }

        [[nodiscard]] std::optional<float>
        ReadManagedUiScalar(const Keire::AssetId entity,
                            const Keire::ManagedUiScalarProperty property) noexcept override
        {
            return Keire::Detail::ReadManagedUiScalar(RuntimeSceneFor(entity), entity, property);
        }

        [[nodiscard]] bool SetManagedUiScalar(const Keire::AssetId entity,
                                              const Keire::ManagedUiScalarProperty property,
                                              const float value) noexcept override
        {
            return Keire::Detail::SetManagedUiScalar(RuntimeSceneFor(entity), entity, property, value);
        }

        [[nodiscard]] std::optional<bool>
        ReadManagedUiFlag(const Keire::AssetId entity, const Keire::ManagedUiFlagProperty property) noexcept override
        {
            return Keire::Detail::ReadManagedUiFlag(RuntimeSceneFor(entity), PresentationFor(entity), entity, property);
        }

        [[nodiscard]] bool SetManagedUiFlag(const Keire::AssetId entity, const Keire::ManagedUiFlagProperty property,
                                            const bool value) noexcept override
        {
            return Keire::Detail::SetManagedUiFlag(RuntimeSceneFor(entity), PresentationFor(entity), entity, property,
                                                   value);
        }

        [[nodiscard]] std::optional<Keire::Vector2>
        ReadManagedUiVector(const Keire::AssetId entity,
                            const Keire::ManagedUiVectorProperty property) noexcept override
        {
            return Keire::Detail::ReadManagedUiVector(RuntimeSceneFor(entity), entity, property);
        }

        [[nodiscard]] bool SetManagedUiVector(const Keire::AssetId entity,
                                              const Keire::ManagedUiVectorProperty property,
                                              const Keire::Vector2 value) noexcept override
        {
            return Keire::Detail::SetManagedUiVector(RuntimeSceneFor(entity), entity, property, value);
        }

        [[nodiscard]] std::optional<std::string> ReadManagedUiInputText(const Keire::AssetId entity) noexcept override
        {
            return Keire::Detail::ReadManagedUiInputText(RuntimeSceneFor(entity), entity);
        }

        [[nodiscard]] bool SetManagedUiInputText(const Keire::AssetId entity,
                                                 const std::string_view text) noexcept override
        {
            return Keire::Detail::SetManagedUiInputText(RuntimeSceneFor(entity), entity, text);
        }

        [[nodiscard]] bool ConsumeManagedUiEvent(const Keire::AssetId entity,
                                                 const Keire::RuntimeUiEventType type) noexcept override
        {
            return Keire::Detail::ConsumeManagedUiEvent(PresentationFor(entity), entity, type);
        }

        [[nodiscard]] bool FocusManagedUi(const Keire::AssetId entity) noexcept override
        {
            return Keire::Detail::FocusManagedUi(PresentationFor(entity), entity);
        }

        [[nodiscard]] Keire::Ref<Keire::Scene>
        ManagedRuntimeScene(const Keire::AssetId entity = {}) const noexcept override
        {
            return RuntimeSceneFor(entity);
        }

      private:
        [[nodiscard]] Keire::Ref<Keire::SceneRuntimeSession>
        RuntimeSessionFor(const Keire::AssetId entity) const noexcept
        {
            const auto world = RuntimeWorld();
            if (world && entity)
                if (const auto session = world->SessionForEntity(Keire::EntityId(entity)))
                    return session;
            return m_Runtime;
        }

        [[nodiscard]] Keire::Ref<Keire::SceneRuntimeSession>
        RuntimeSessionForWorld(const std::uint64_t worldId) const noexcept
        {
            const auto world = RuntimeWorld();
            if (worldId != 0)
                return world ? world->SessionForWorld(worldId) : Keire::Ref<Keire::SceneRuntimeSession>{};
            return m_Runtime;
        }

        [[nodiscard]] Keire::Ref<Keire::Scene> RuntimeSceneFor(const Keire::AssetId entity) const noexcept
        {
            const auto session = RuntimeSessionFor(entity);
            return session ? session->RuntimeScene() : Keire::Ref<Keire::Scene>{};
        }

        [[nodiscard]] Keire::Ref<Keire::ScenePresentationRuntime>
        PresentationFor(const Keire::AssetId entity) const noexcept
        {
            const auto session = RuntimeSessionFor(entity);
            return session ? session->Presentation() : Keire::Ref<Keire::ScenePresentationRuntime>{};
        }

        [[nodiscard]] bool TryEnableDefaultInputMap()
        {
            if (!m_InputContext)
                return true;
            if (m_DefaultInputMap)
                return m_InputContext->EnableMap(m_DefaultInputMap);
            if (m_InputContext->EnableMap("Player"))
                return true;
            const auto definition = m_InputContext->Definition();
            return !definition.ActionMaps.empty() && m_InputContext->EnableMap(definition.ActionMaps.front().Id);
        }

        void ApplyManagedCursorMode(const bool restore = false) noexcept
        {
            try
            {
                const auto windows = Owner().Windows();
                const auto window = Owner().MainWindow();
                if (!windows || !window)
                    return;
                const auto mode =
                    restore ? Keire::CursorMode::Normal
                            : (m_ManagedCursorLocked
                                   ? Keire::CursorMode::RelativeLocked
                                   : (m_ManagedCursorVisible ? Keire::CursorMode::Normal : Keire::CursorMode::Hidden));
                windows->SetCursorMode(window->Id(), mode);
            }
            catch (...)
            {
            }
        }

        Keire::AssetId m_StartupScene;
        Keire::AssetId m_DefaultInput;
        Keire::AssetId m_DefaultInputMap;
        Keire::AssetId m_DefaultMixer;
        RuntimeCommandLine m_CommandLine;
        Keire::ReplayFingerprints m_ReplayFingerprints;
        std::shared_ptr<RuntimeReplayState> m_ReplayState;
        KeireRuntime::RuntimeAdditiveValidation m_AdditiveValidation;
        KeireRuntime::RuntimeRenderBenchmark m_RenderBenchmark;
        std::uint32_t m_RenderedFrames = 0;
        std::uint64_t m_FixedTicks = 0;
        Keire::Ref<Keire::SceneLoadOperation> m_Load;
        Keire::Ref<Keire::SceneRuntimeSession> m_Runtime;
        Keire::Ref<Keire::Scene> m_Scene;
        Keire::Ref<Keire::RenderView> m_View;
        Keire::Ref<Keire::ScenePresentationRuntime> m_Presentation;
        Keire::WindowSystemInternalAccess::EventSinkToken m_EventSink = 0;
        KeireRuntime::RuntimeUiPointerState m_UiPointer;
        Keire::InputUserId m_InputUser;
        Keire::Ref<Keire::InputActionContext> m_InputContext;
        bool m_InputMapEnabled = false;
        bool m_ManagedCursorVisible = true;
        bool m_ManagedCursorLocked = false;
    };

    class RuntimeApplication final : public Keire::Application
    {
      public:
        RuntimeApplication(Keire::ApplicationSpecification specification, RuntimeManifest manifest,
                           RuntimeCommandLine commandLine, Keire::ReplayFingerprints fingerprints)
            : Application(std::move(specification)), m_Manifest(std::move(manifest)),
              m_CommandLine(std::move(commandLine)), m_ReplayFingerprints(std::move(fingerprints)),
              m_ReplayState(std::make_shared<RuntimeReplayState>())
        {
        }

      protected:
        void OnInitialize() override
        {
            Replay()->RegisterSerializer(
                {.Id = "runtime.scene",
                 .Version = 1,
                 .Deterministic = true,
                 .Capture =
                     [state = m_ReplayState]
                 {
                     if (!state->Session || !state->Session->RuntimeScene())
                         throw std::logic_error("Runtime scene replay state is unavailable.");
                     return Keire::SceneAsset::Encode(state->Session->RuntimeScene()->Snapshot());
                 },
                 .Restore =
                     [state = m_ReplayState](const std::span<const std::byte> bytes)
                 {
                     if (!state->Session)
                         throw std::logic_error("Runtime scene replay state is unavailable.");
                     state->Session->ReplaceRuntime(Keire::SceneAsset::Decode(bytes)->Definition());
                 }});
            Replay()->RegisterSerializer(
                {.Id = "runtime.scene.animation",
                 .Version = 1,
                 .Deterministic = true,
                 .Capture =
                     [state = m_ReplayState]
                 {
                     if (!state->Session)
                         throw std::logic_error("Runtime animation replay state is unavailable.");
                     nlohmann::json animators = nlohmann::json::array();
                     for (const auto& animator : state->Session->CaptureAnimatorCheckpoint())
                         animators.push_back(EncodeAnimatorCheckpoint(animator));
                     const auto encoded = nlohmann::json::to_cbor(
                         nlohmann::json{{"schemaVersion", 1}, {"animators", std::move(animators)}});
                     return std::vector<std::byte>(reinterpret_cast<const std::byte*>(encoded.data()),
                                                   reinterpret_cast<const std::byte*>(encoded.data() + encoded.size()));
                 },
                 .Restore =
                     [state = m_ReplayState](const std::span<const std::byte> bytes)
                 {
                     if (!state->Session)
                         throw std::logic_error("Runtime animation replay state is unavailable.");
                     const auto document =
                         nlohmann::json::from_cbor(reinterpret_cast<const std::uint8_t*>(bytes.data()),
                                                   reinterpret_cast<const std::uint8_t*>(bytes.data() + bytes.size()));
                     if (document.value("schemaVersion", 0) != 1 || !document.at("animators").is_array())
                         throw std::runtime_error("Runtime animation replay checkpoint is malformed.");
                     std::vector<Keire::SceneAnimatorCheckpoint> animators;
                     animators.reserve(document.at("animators").size());
                     for (const auto& animator : document.at("animators"))
                         animators.push_back(DecodeAnimatorCheckpoint(animator));
                     state->Session->RestoreAnimatorCheckpoint(animators);
                 }});
            Replay()->RegisterSerializer(
                {.Id = "runtime.scene.physics",
                 .Version = 1,
                 .Deterministic = true,
                 .Capture =
                     [state = m_ReplayState]
                 {
                     if (!state->Session)
                         throw std::logic_error("Runtime physics replay state is unavailable.");
                     nlohmann::json bodies = nlohmann::json::array();
                     for (const auto& body : state->Session->CapturePhysicsCheckpoint())
                     {
                         bodies.push_back(
                             {{"entity", body.Entity.ToString()},
                              {"position", {body.Position.X, body.Position.Y, body.Position.Z}},
                              {"rotation", {body.Rotation.X, body.Rotation.Y, body.Rotation.Z, body.Rotation.W}},
                              {"linearVelocity", {body.LinearVelocity.X, body.LinearVelocity.Y, body.LinearVelocity.Z}},
                              {"angularVelocity",
                               {body.AngularVelocity.X, body.AngularVelocity.Y, body.AngularVelocity.Z}},
                              {"sleeping", body.Sleeping}});
                     }
                     const auto encoded =
                         nlohmann::json::to_cbor(nlohmann::json{{"schemaVersion", 1}, {"bodies", std::move(bodies)}});
                     return std::vector<std::byte>(reinterpret_cast<const std::byte*>(encoded.data()),
                                                   reinterpret_cast<const std::byte*>(encoded.data() + encoded.size()));
                 },
                 .Restore =
                     [state = m_ReplayState](const std::span<const std::byte> bytes)
                 {
                     if (!state->Session)
                         throw std::logic_error("Runtime physics replay state is unavailable.");
                     const auto document =
                         nlohmann::json::from_cbor(reinterpret_cast<const std::uint8_t*>(bytes.data()),
                                                   reinterpret_cast<const std::uint8_t*>(bytes.data() + bytes.size()));
                     if (document.value("schemaVersion", 0) != 1 || !document.at("bodies").is_array())
                         throw std::runtime_error("Runtime physics replay checkpoint is malformed.");
                     std::vector<Keire::ScenePhysicsCheckpointBody> bodies;
                     bodies.reserve(document.at("bodies").size());
                     for (const auto& encoded : document.at("bodies"))
                     {
                         const auto vector3 = [&encoded](const char* name)
                         {
                             const auto& value = encoded.at(name);
                             if (!value.is_array() || value.size() != 3U)
                                 throw std::runtime_error("Runtime physics checkpoint vector is malformed.");
                             return Keire::Vector3{value[0].get<float>(), value[1].get<float>(), value[2].get<float>()};
                         };
                         const auto& rotation = encoded.at("rotation");
                         if (!rotation.is_array() || rotation.size() != 4U)
                             throw std::runtime_error("Runtime physics checkpoint rotation is malformed.");
                         bodies.push_back({Keire::EntityId::Parse(encoded.at("entity").get<std::string>()),
                                           vector3("position"),
                                           {rotation[0].get<float>(), rotation[1].get<float>(),
                                            rotation[2].get<float>(), rotation[3].get<float>()},
                                           vector3("linearVelocity"),
                                           vector3("angularVelocity"),
                                           encoded.at("sleeping").get<bool>()});
                     }
                     state->Session->RestorePhysicsCheckpoint(bodies);
                 }});
            Replay()->RegisterSerializer(
                {.Id = "runtime.scene.presentation",
                 .Version = 1,
                 .Deterministic = true,
                 .Capture =
                     [state = m_ReplayState]
                 {
                     if (!state->Session || !state->Session->Presentation())
                         throw std::logic_error("Runtime presentation replay state is unavailable.");
                     const auto checkpoint = state->Session->Presentation()->CaptureCheckpoint();
                     nlohmann::json audio = nlohmann::json::array();
                     for (const auto& source : checkpoint.AudioSources)
                     {
                         audio.push_back({{"entity", source.Entity.ToString()},
                                          {"clip", source.Clip.ToString()},
                                          {"state", static_cast<std::uint8_t>(source.State)},
                                          {"frame", source.Frame},
                                          {"manualPlayRequested", source.ManualPlayRequested},
                                          {"playOnAwakeConsumed", source.PlayOnAwakeConsumed}});
                     }
                     nlohmann::json events = nlohmann::json::array();
                     for (const auto& event : checkpoint.PendingUiEvents)
                     {
                         events.push_back({{"type", static_cast<std::uint8_t>(event.Type)},
                                           {"target", event.Target.ToString()},
                                           {"pointerX", event.PointerX},
                                           {"pointerY", event.PointerY},
                                           {"button", static_cast<std::uint8_t>(event.Button)}});
                     }
                     const auto encoded = nlohmann::json::to_cbor(
                         nlohmann::json{{"schemaVersion", 1},
                                        {"focus", checkpoint.FocusedEntity ? checkpoint.FocusedEntity.ToString() : ""},
                                        {"audio", std::move(audio)},
                                        {"events", std::move(events)}});
                     return std::vector<std::byte>(reinterpret_cast<const std::byte*>(encoded.data()),
                                                   reinterpret_cast<const std::byte*>(encoded.data() + encoded.size()));
                 },
                 .Restore =
                     [state = m_ReplayState](const std::span<const std::byte> bytes)
                 {
                     if (!state->Session || !state->Session->Presentation())
                         throw std::logic_error("Runtime presentation replay state is unavailable.");
                     const auto document =
                         nlohmann::json::from_cbor(reinterpret_cast<const std::uint8_t*>(bytes.data()),
                                                   reinterpret_cast<const std::uint8_t*>(bytes.data() + bytes.size()));
                     if (document.value("schemaVersion", 0) != 1 || !document.at("audio").is_array() ||
                         !document.at("events").is_array())
                     {
                         throw std::runtime_error("Runtime presentation replay checkpoint is malformed.");
                     }
                     Keire::ScenePresentationCheckpoint checkpoint;
                     const auto focus = document.at("focus").get<std::string>();
                     if (!focus.empty())
                         checkpoint.FocusedEntity = Keire::EntityId::Parse(focus);
                     checkpoint.AudioSources.reserve(document.at("audio").size());
                     for (const auto& source : document.at("audio"))
                     {
                         checkpoint.AudioSources.push_back(
                             {Keire::EntityId::Parse(source.at("entity").get<std::string>()),
                              Keire::AssetId::Parse(source.at("clip").get<std::string>()),
                              static_cast<Keire::AudioSourcePlaybackState>(source.at("state").get<std::uint8_t>()),
                              source.at("frame").get<std::uint64_t>(), source.at("manualPlayRequested").get<bool>(),
                              source.at("playOnAwakeConsumed").get<bool>()});
                     }
                     checkpoint.PendingUiEvents.reserve(document.at("events").size());
                     for (const auto& event : document.at("events"))
                     {
                         checkpoint.PendingUiEvents.push_back(
                             {static_cast<Keire::RuntimeUiEventType>(event.at("type").get<std::uint8_t>()),
                              Keire::EntityId::Parse(event.at("target").get<std::string>()),
                              event.at("pointerX").get<float>(), event.at("pointerY").get<float>(),
                              static_cast<Keire::RuntimeUiPointerButton>(event.at("button").get<std::uint8_t>())});
                     }
                     state->Session->Presentation()->RestoreCheckpoint(checkpoint);
                 }});
            Replay()->RegisterSerializer({.Id = "runtime.scene.vfx",
                                          .Version = 1,
                                          .Deterministic = true,
                                          .Capture =
                                              [state = m_ReplayState]
                                          {
                                              if (!state->Session)
                                                  throw std::logic_error("Runtime VFX replay state is unavailable.");
                                              return state->Session->CaptureVfxCheckpoint();
                                          },
                                          .Restore =
                                              [state = m_ReplayState](const std::span<const std::byte> bytes)
                                          {
                                              if (!state->Session)
                                                  throw std::logic_error("Runtime VFX replay state is unavailable.");
                                              state->Session->RestoreVfxCheckpoint(bytes);
                                          }});
            if (m_Manifest.Scripting)
            {
                Keire::ManagedReloadRequest reload;
                for (const auto& root : m_Manifest.ManagedAssemblyRoots)
                {
                    const auto directory = m_Manifest.ContentRoot / root;
                    if (!std::filesystem::is_directory(directory))
                        throw std::runtime_error("Cooked managed assembly root is missing: " + directory.string());
                    for (const auto& entry : std::filesystem::directory_iterator(directory))
                        if (entry.is_regular_file() && entry.path().extension() == ".dll" &&
                            entry.path().filename() != "Keire.Managed.dll")
                            reload.Assemblies.push_back(entry.path());
                }
                std::ranges::sort(reload.Assemblies);
                if (reload.Assemblies.empty())
                    throw std::runtime_error("Cooked scripting is enabled but no managed gameplay assemblies exist.");
                if (!Scripts()->PrepareReload(std::move(reload)))
                    throw std::runtime_error("Managed gameplay startup failed: " +
                                             Scripts()->ReloadStatus().Diagnostic);
                Scripts()->CommitReload();
                Scripts()->InstallManagedComponents(Scenes()->Components());
                Replay()->RegisterSerializer(
                    {.Id = "runtime.scene.scripting",
                     .Version = 1,
                     .Deterministic = true,
                     .Capture =
                         [scripts = Scripts()]
                     {
                         nlohmann::json behaviours = nlohmann::json::array();
                         for (const auto& behaviour : scripts->CaptureReplayCheckpoint())
                         {
                             behaviours.push_back({{"type", behaviour.TypeName},
                                                   {"component", behaviour.ComponentType.ToString()},
                                                   {"world", behaviour.World},
                                                   {"entity", behaviour.Entity.ToString()},
                                                   {"state", behaviour.State},
                                                   {"enabled", behaviour.Enabled},
                                                   {"faulted", behaviour.Faulted}});
                         }
                         const auto encoded = nlohmann::json::to_cbor(
                             nlohmann::json{{"schemaVersion", 1}, {"behaviours", std::move(behaviours)}});
                         return std::vector<std::byte>(
                             reinterpret_cast<const std::byte*>(encoded.data()),
                             reinterpret_cast<const std::byte*>(encoded.data() + encoded.size()));
                     },
                     .Restore =
                         [scripts = Scripts()](const std::span<const std::byte> bytes)
                     {
                         const auto document = nlohmann::json::from_cbor(
                             reinterpret_cast<const std::uint8_t*>(bytes.data()),
                             reinterpret_cast<const std::uint8_t*>(bytes.data() + bytes.size()));
                         if (document.value("schemaVersion", 0) != 1 || !document.at("behaviours").is_array())
                             throw std::runtime_error("Managed replay checkpoint is malformed.");
                         std::vector<Keire::ManagedBehaviourCheckpoint> behaviours;
                         behaviours.reserve(document.at("behaviours").size());
                         for (const auto& encoded : document.at("behaviours"))
                         {
                             behaviours.push_back(
                                 {encoded.at("type").get<std::string>(),
                                  Keire::ComponentTypeId::Parse(encoded.at("component").get<std::string>()),
                                  encoded.at("world").get<std::uint64_t>(),
                                  Keire::AssetId::Parse(encoded.at("entity").get<std::string>()),
                                  encoded.at("state").get<std::string>(), encoded.at("enabled").get<bool>(),
                                  encoded.at("faulted").get<bool>()});
                         }
                         scripts->RestoreReplayCheckpoint(behaviours);
                     }});
            }
            (void)PushLayer(std::make_unique<RuntimeLayer>(
                m_Manifest.StartupScene, m_Manifest.DefaultInput, m_Manifest.DefaultInputMap, m_Manifest.DefaultMixer,
                m_Manifest.Rendering, m_CommandLine, m_ReplayFingerprints, m_ReplayState));
        }

      private:
        RuntimeManifest m_Manifest;
        RuntimeCommandLine m_CommandLine;
        Keire::ReplayFingerprints m_ReplayFingerprints;
        std::shared_ptr<RuntimeReplayState> m_ReplayState;
    };
} // namespace

namespace Keire
{
    std::unique_ptr<Application> CreateApplication(const ApplicationCommandLineArguments& arguments)
    {
        auto commandLine = KeireRuntime::ParseRuntimeCommandLine(arguments);
        auto manifest = LoadManifest(commandLine.Content);
        commandLine.ValidationScenes =
            KeireRuntime::SelectRuntimeValidationScenes(commandLine.AdditiveValidationOutput, manifest.BuildScenes);
        ApplicationSpecification specification;
        specification.Windowing.ApplicationName = commandLine.ProductName;
        specification.Windowing.ApplicationVersion = commandLine.ProductVersion;
        specification.Windowing.ApplicationIdentifier = commandLine.ApplicationIdentifier;
        specification.Modules.Modules = KeireProjectModules::CreateSourceModules();
        auto moduleValidation = CreateRef<ModuleRegistry>(specification.Modules);
        moduleValidation->ValidateCatalog(manifest.RequiredModules);
        if (commandLine.Scene)
            manifest.StartupScene = commandLine.Scene;
        const auto replayFingerprints = BuildReplayFingerprints(manifest, *moduleValidation);
        specification.MainWindow.Title = commandLine.WindowTitle;
        specification.MainWindow.Visible = commandLine.Frames == 0 && !commandLine.Headless;
        specification.TargetFrameRate = specification.MainWindow.Visible ? 0 : 240;
        specification.SuspendWhenMainWindowMinimized = false;
        specification.Assets.Mode = AssetMode::Cooked;
        specification.Assets.Mounts.push_back({commandLine.Content / "catalog.json", 0, false});
        specification.Scenes.Mode = SceneMode::Enabled;
        specification.Render.Mode = RenderMode::Rendered;
        specification.Render.PresentMode = commandLine.PresentMode;
        specification.Ui.Mode = UiMode::Disabled;
        specification.Input.Mode = InputMode::Enabled;
        specification.Input.AutoJoin = false;
        if (manifest.Scripting)
        {
            specification.Scripting.Mode = ScriptMode::Enabled;
            specification.Scripting.ProjectRoot = commandLine.Content;
            specification.Scripting.AssemblyDirectory = ".";
            specification.Scripting.RuntimeHostDirectory = commandLine.ManagedRuntime;
            specification.Scripting.RuntimeRootDirectory = commandLine.ManagedRuntime / "Dotnet";
            specification.Scripting.ManagedApiAssembly = commandLine.ManagedRuntime / "Keire.Managed.dll";
        }
        if (manifest.Physics || manifest.Navigation)
        {
            specification.Physics.Mode = PhysicsMode::Enabled;
            specification.Physics.CollisionMatrix = manifest.PhysicsCollisionMatrix;
        }
        if (manifest.Audio)
        {
            specification.Audio.Mode = specification.MainWindow.Visible ? AudioMode::Enabled : AudioMode::Headless;
            specification.Audio.MixSampleRate = manifest.AudioSettings.MixSampleRate;
            specification.Audio.PeriodFrames = manifest.AudioSettings.PeriodFrames;
            specification.Audio.OutputLayout = manifest.AudioSettings.OutputLayout;
            specification.Audio.MaximumVoices = manifest.AudioSettings.MaximumVoices;
            specification.Audio.MaximumVirtualVoices = manifest.AudioSettings.MaximumVirtualVoices;
        }
        if (manifest.Navigation)
            specification.Navigation.Mode = NavigationMode::Enabled;
        return std::make_unique<RuntimeApplication>(std::move(specification), std::move(manifest), commandLine,
                                                    replayFingerprints);
    }
} // namespace Keire
