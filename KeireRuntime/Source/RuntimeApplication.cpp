#include "Keire/Core.h"
#include "KeireProjectModules/SourceModulePack.h"

#include "KeireInternal/Assets/AssetInternal.h"
#include "KeireInternal/Build/PlayerPackage.h"
#include "KeireInternal/FileSystem.h"
#include "KeireInternal/RenderInternal.h"
#include "KeireInternal/Scripting/ManagedRuntimeApplicationServices.h"
#include "KeireInternal/Scripting/ManagedRuntimeUiServices.h"
#include "KeireInternal/WindowInternal.h"
#include "KeireRuntimeInternal/RuntimeUiInput.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace
{
    constexpr std::array RuntimeOptions{
        Keire::ApplicationCommandLineOption{"--content <path>", "Mount cooked Kéire runtime content."},
        Keire::ApplicationCommandLineOption{"--frames <count>", "Exit after a finite number of rendered frames."},
        Keire::ApplicationCommandLineOption{"--headless", "Run with a hidden window and headless audio."},
        Keire::ApplicationCommandLineOption{"--scene <asset-id>", "Override the cooked startup scene."},
        Keire::ApplicationCommandLineOption{"--tick-limit <count>", "Exit after a finite number of fixed ticks."},
        Keire::ApplicationCommandLineOption{"--record <path>", "Record a deterministic replay."},
        Keire::ApplicationCommandLineOption{"--play <path>", "Play a replay."},
        Keire::ApplicationCommandLineOption{"--verify <path>",
                                            "Verify a replay and return a failure exit code on divergence."},
        Keire::ApplicationCommandLineOption{"--profile <strict|performance>", "Select the replay recording profile."},
        Keire::ApplicationCommandLineOption{"--output <path>", "Write an atomic replay result report."}};

    enum class RuntimeReplayAction : std::uint8_t
    {
        None,
        Record,
        Play,
        Verify
    };

    struct RuntimeCommandLine final
    {
        std::filesystem::path Content;
        std::uint32_t Frames = 0;
        std::uint64_t TickLimit = 0;
        Keire::AssetId Scene;
        RuntimeReplayAction ReplayAction = RuntimeReplayAction::None;
        std::filesystem::path ReplayPath;
        std::filesystem::path OutputPath;
        std::filesystem::path ManagedRuntime;
        std::string ProductName = "Keire Runtime";
        std::string ProductVersion;
        std::string WindowTitle = "Kéire Runtime";
        std::string ApplicationIdentifier;
        Keire::ReplayProfile ReplayProfile = Keire::ReplayProfile::StrictVerified;
        bool Headless = false;
    };

    struct RuntimeManifest final
    {
        std::filesystem::path ContentRoot;
        Keire::AssetId StartupScene;
        std::vector<Keire::AssetId> BuildScenes;
        Keire::AssetId DefaultInput;
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

    template <typename T> void ParsePositiveCount(const std::string_view value, T& output, const char* option)
    {
        const auto parsed = std::from_chars(value.data(), value.data() + value.size(), output);
        if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() || output == 0)
            throw Keire::CommandLineError(std::string(option) + " requires a positive count.");
    }

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

    [[nodiscard]] RuntimeCommandLine ParseCommandLine(const Keire::ApplicationCommandLineArguments& arguments)
    {
        RuntimeCommandLine result;
        const auto executable =
            std::filesystem::absolute(Keire::Detail::PathFromUtf8(arguments.Executable())).lexically_normal();
        const auto packaged = Keire::Detail::LoadPackagedPlayerConfiguration(executable);
        for (std::size_t index = 1; index < arguments.Size(); ++index)
        {
            const auto option = arguments[index];
            if (option == "--content")
            {
                if (++index >= arguments.Size())
                    throw Keire::CommandLineError("--content requires a path.");
                result.Content = Keire::Detail::PathFromUtf8(arguments[index]);
            }
            else if (option == "--frames")
            {
                if (++index >= arguments.Size())
                    throw Keire::CommandLineError("--frames requires a positive count.");
                ParsePositiveCount(arguments[index], result.Frames, "--frames");
            }
            else if (option == "--tick-limit")
            {
                if (++index >= arguments.Size())
                    throw Keire::CommandLineError("--tick-limit requires a positive count.");
                ParsePositiveCount(arguments[index], result.TickLimit, "--tick-limit");
            }
            else if (option == "--headless")
                result.Headless = true;
            else if (option == "--scene")
            {
                if (++index >= arguments.Size())
                    throw Keire::CommandLineError("--scene requires an asset ID.");
                try
                {
                    result.Scene = Keire::AssetId::Parse(arguments[index]);
                }
                catch (const std::exception&)
                {
                    throw Keire::CommandLineError("--scene requires a valid asset ID.");
                }
            }
            else if (option == "--record" || option == "--play" || option == "--verify")
            {
                if (result.ReplayAction != RuntimeReplayAction::None)
                    throw Keire::CommandLineError("--record, --play, and --verify are mutually exclusive.");
                if (++index >= arguments.Size())
                    throw Keire::CommandLineError(std::string(option) + " requires a replay path.");
                result.ReplayAction = option == "--record" ? RuntimeReplayAction::Record
                                      : option == "--play" ? RuntimeReplayAction::Play
                                                           : RuntimeReplayAction::Verify;
                result.ReplayPath =
                    std::filesystem::absolute(Keire::Detail::PathFromUtf8(arguments[index])).lexically_normal();
            }
            else if (option == "--profile")
            {
                if (++index >= arguments.Size())
                    throw Keire::CommandLineError("--profile requires strict or performance.");
                if (arguments[index] == "strict")
                    result.ReplayProfile = Keire::ReplayProfile::StrictVerified;
                else if (arguments[index] == "performance")
                    result.ReplayProfile = Keire::ReplayProfile::PerformanceCapture;
                else
                    throw Keire::CommandLineError("--profile requires strict or performance.");
            }
            else if (option == "--output")
            {
                if (++index >= arguments.Size())
                    throw Keire::CommandLineError("--output requires a path.");
                result.OutputPath =
                    std::filesystem::absolute(Keire::Detail::PathFromUtf8(arguments[index])).lexically_normal();
            }
            else
                throw Keire::CommandLineError("Unknown runtime option: " + std::string(option));
        }
        if (packaged)
        {
            if (result.Content.empty())
                result.Content = packaged->Content;
            result.ManagedRuntime = packaged->ManagedRuntime;
            result.ProductName = packaged->Settings.ProductName;
            result.ProductVersion = packaged->Settings.Version;
            result.WindowTitle = packaged->Settings.WindowTitle;
            result.ApplicationIdentifier = packaged->Settings.ApplicationIdentifier;
        }
        if (result.Content.empty())
            throw Keire::CommandLineError(
                "KeireRuntime requires --content <path> unless launched from a packaged player.");
        if (!result.OutputPath.empty() && result.ReplayAction == RuntimeReplayAction::None)
            throw Keire::CommandLineError("--output requires --record, --play, or --verify.");
        result.Content = std::filesystem::absolute(result.Content).lexically_normal();
        if (result.ManagedRuntime.empty())
            result.ManagedRuntime = executable.parent_path() / "Managed";
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
        if (const auto scenes = source.find("buildScenes"); scenes != source.end())
        {
            if (!scenes->is_array() || scenes->empty() || scenes->size() > 1024)
                throw Keire::CommandLineError("Runtime manifest buildScenes must be a non-empty bounded array.");
            for (const auto& encoded : *scenes)
            {
                const auto scene = Keire::AssetId::Parse(encoded.get<std::string>());
                if (std::ranges::find(result.BuildScenes, scene) != result.BuildScenes.end())
                    throw Keire::CommandLineError("Runtime manifest buildScenes contains a duplicate scene.");
                result.BuildScenes.push_back(scene);
            }
            if (result.BuildScenes.front() != result.StartupScene)
                throw Keire::CommandLineError("Runtime manifest startupScene must be the first enabled build scene.");
        }
        else
        {
            result.BuildScenes.push_back(result.StartupScene);
        }
        if (source.contains("defaultInput") && !source.at("defaultInput").is_null())
            result.DefaultInput = Keire::AssetId::Parse(source.at("defaultInput").get<std::string>());
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

    struct SceneCamera final
    {
        Keire::Entity Entity;
        Keire::Ref<Keire::CameraComponent> Camera;
        Keire::Ref<Keire::TransformComponent> Transform;
    };

    [[nodiscard]] std::optional<SceneCamera> SelectCamera(const Keire::Ref<Keire::Scene>& scene)
    {
        std::optional<SceneCamera> selected;
        bool selectedPrimary = false;
        for (const auto& entity : scene->Query<Keire::CameraComponent>())
        {
            const auto camera = entity.GetComponent<Keire::CameraComponent>();
            const auto transform = entity.GetComponent<Keire::TransformComponent>();
            if (!camera || !transform || !camera->Enabled() || !entity.ActiveInHierarchy())
                continue;
            if (!selected || (camera->Primary() && !selectedPrimary) ||
                (camera->Primary() == selectedPrimary && camera->Priority() > selected->Camera->Priority()))
            {
                selected = SceneCamera{entity, camera, transform};
                selectedPrimary = camera->Primary();
            }
        }
        return selected;
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

    class RuntimeLayer final : public Keire::Layer, public Keire::Detail::ManagedRuntimeApplicationServices
    {
      public:
        RuntimeLayer(const Keire::AssetId startupScene, const Keire::AssetId defaultInput,
                     const Keire::AssetId defaultMixer, const Keire::RenderEnvironmentSettings rendering,
                     RuntimeCommandLine commandLine, Keire::ReplayFingerprints fingerprints,
                     std::shared_ptr<RuntimeReplayState> replayState)
            : Layer("Runtime"), ManagedRuntimeApplicationServices(false), m_StartupScene(startupScene),
              m_DefaultInput(defaultInput), m_DefaultMixer(defaultMixer), m_Rendering(rendering),
              m_CommandLine(std::move(commandLine)), m_ReplayFingerprints(std::move(fingerprints)),
              m_ReplayState(std::move(replayState))
        {
        }

      protected:
        void OnAttach() override
        {
            BindManagedApplication(Owner());
            m_Load = Owner().Scenes()->Load(m_StartupScene);
            Keire::RenderSurfaceSpecification surface;
            surface.Name = "Runtime";
            const auto pixels = Owner().MainWindow()->PixelSize();
            surface.Width = std::max(pixels.Width, 1U);
            surface.Height = std::max(pixels.Height, 1U);
            m_View = Owner().Renderer()->CreateView(surface);
            Keire::RenderSystemInternalAccess::SetPresentationSurface(*Owner().Renderer(), m_View->Surface());
            m_EventSink = Keire::WindowSystemInternalAccess::AddEventSink(*Owner().Windows(), this, HandleNativeEvent);
            if (const auto scripts = Owner().Scripts())
                scripts->SetRuntimeServices(this);
            if (m_DefaultInput)
            {
                if (const auto input = Owner().Input())
                {
                    m_InputUser = input->CreateUser("Player");
                    for (const auto& device : input->Devices())
                    {
                        if (device.Connected && !input->PairDevice(m_InputUser, device.Id))
                            throw std::runtime_error("The player could not claim a connected input device.");
                    }
                    m_InputContext = input->CreateActionContext(m_DefaultInput, m_InputUser);
                }
            }
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
            if (m_Runtime)
            {
                m_Runtime->FixedUpdate(static_cast<float>(time.FixedDeltaTime().Seconds()));
                if (m_CommandLine.TickLimit != 0 && ++m_FixedTicks >= m_CommandLine.TickLimit)
                    Owner().RequestExit();
            }
        }

        void OnUpdate(const Keire::Time& time) override
        {
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
            m_Runtime->SetPresentationViewport(static_cast<float>(width), static_cast<float>(height));
            m_Runtime->Update(static_cast<float>(time.DeltaTime().Seconds()));
            if (m_Runtime->State() == Keire::ScenePlayState::Faulted)
                throw std::runtime_error("Startup scene runtime failed: " + m_Runtime->Diagnostic().Message);
            KeireRuntime::SynchronizeRuntimeUiTextInput(m_Presentation, Owner().Windows(), Owner().MainWindow());
            const auto selected = SelectCamera(m_Scene);
            if (!selected)
                throw std::runtime_error("The startup scene has no active camera.");
            m_View->Surface()->RequestSize(width, height);
            Keire::RenderCamera camera;
            camera.View = Keire::Math::Inverse(selected->Transform->WorldMatrix());
            camera.Projection =
                selected->Camera->ProjectionMatrix(static_cast<float>(width) / static_cast<float>(height));
            camera.ClearColor = selected->Camera->ClearColor();
            camera.NearPlane = selected->Camera->NearPlane();
            camera.FarPlane = selected->Camera->FarPlane();
            m_View->SetCamera(camera);
            auto environment = m_Rendering;
            environment.SkyVisible =
                environment.SkyVisible && selected->Camera->ClearMode() == Keire::CameraClearMode::Skybox;
            Keire::SceneRenderRequest renderRequest{m_Scene, m_View, false, environment};
            if (const auto vfx = m_Runtime->Vfx())
                renderRequest.Vfx = vfx->CaptureRenderSnapshot();
            Owner().Renderer()->SubmitRuntimeUi(m_Presentation->Ui());
            Owner().Renderer()->Submit(std::move(renderRequest));
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
            if (!m_Presentation)
                return;
            const auto logical = Owner().MainWindow()->LogicalSize();
            const auto pixels = Owner().MainWindow()->PixelSize();
            const float scaleX =
                logical.Width == 0 ? 1.0F : static_cast<float>(pixels.Width) / static_cast<float>(logical.Width);
            const float scaleY =
                logical.Height == 0 ? 1.0F : static_cast<float>(pixels.Height) / static_cast<float>(logical.Height);
            KeireRuntime::ProcessRuntimeUiEvent(m_Presentation, event, scaleX, scaleY, m_UiPointer);
            KeireRuntime::SynchronizeRuntimeUiTextInput(m_Presentation, Owner().Windows(), Owner().MainWindow());
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
                if (!m_Runtime)
                    return std::nullopt;
                Keire::PhysicsRayQuery native;
                native.Origin = query.Origin;
                native.Direction = query.Direction;
                native.MaximumDistance = query.MaximumDistance;
                native.Mask = query.Mask;
                native.IncludeTriggers = query.IncludeTriggers;
                const auto hits = m_Runtime->RayCast(native, Keire::EntityId(query.IgnoredEntity));
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
                auto target = m_Scene ? m_Scene->FindEntity(Keire::EntityId(playback.Entity)) : Keire::Entity{};
                if (!target || !m_Presentation || !playback.Clip)
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
                (void)m_Presentation->Stop(target.Id());
                (void)m_Presentation->Play(target.Id());
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
                const auto target = m_Scene ? m_Scene->FindEntity(Keire::EntityId(entity)) : Keire::Entity{};
                if (!target || !m_Presentation)
                    return false;
                if (const auto source = target.GetComponent<Keire::AudioSourceComponent>())
                    source->SetPlayOnAwake(false);
                return m_Presentation->Stop(target.Id());
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
                return m_Presentation && (paused ? m_Presentation->Pause(Keire::EntityId(entity))
                                                 : m_Presentation->Resume(Keire::EntityId(entity)));
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
                return m_Presentation && m_Presentation->Seek(Keire::EntityId(entity), positionSeconds);
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
                if (!m_Presentation)
                    return {};
                const auto state = m_Presentation->Playback(Keire::EntityId(entity));
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
                return m_Runtime && m_Runtime->PlayVfx(Keire::EntityId(entity), effect, restart);
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
                return m_Runtime && m_Runtime->StopVfx(Keire::EntityId(entity));
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
                return m_Runtime && m_Runtime->PauseVfx(Keire::EntityId(entity), paused);
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
                return m_Runtime && m_Runtime->IsVfxAlive(Keire::EntityId(entity));
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
                return m_Runtime && m_Runtime->SendVfxEvent(Keire::EntityId(entity), eventName, spawnCount);
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
                return m_Runtime && m_Runtime->SetVfxParameter(Keire::EntityId(entity), value);
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
                const auto target = m_Scene ? m_Scene->FindEntity(Keire::EntityId(entity)) : Keire::Entity{};
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
                return m_Presentation && m_Presentation->ConsumeClick(Keire::EntityId(entity));
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
            return Keire::Detail::ReadManagedUiScalar(m_Scene, entity, property);
        }

        [[nodiscard]] bool SetManagedUiScalar(const Keire::AssetId entity,
                                              const Keire::ManagedUiScalarProperty property,
                                              const float value) noexcept override
        {
            return Keire::Detail::SetManagedUiScalar(m_Scene, entity, property, value);
        }

        [[nodiscard]] std::optional<bool>
        ReadManagedUiFlag(const Keire::AssetId entity, const Keire::ManagedUiFlagProperty property) noexcept override
        {
            return Keire::Detail::ReadManagedUiFlag(m_Scene, m_Presentation, entity, property);
        }

        [[nodiscard]] bool SetManagedUiFlag(const Keire::AssetId entity, const Keire::ManagedUiFlagProperty property,
                                            const bool value) noexcept override
        {
            return Keire::Detail::SetManagedUiFlag(m_Scene, m_Presentation, entity, property, value);
        }

        [[nodiscard]] std::optional<Keire::Vector2>
        ReadManagedUiVector(const Keire::AssetId entity,
                            const Keire::ManagedUiVectorProperty property) noexcept override
        {
            return Keire::Detail::ReadManagedUiVector(m_Scene, entity, property);
        }

        [[nodiscard]] bool SetManagedUiVector(const Keire::AssetId entity,
                                              const Keire::ManagedUiVectorProperty property,
                                              const Keire::Vector2 value) noexcept override
        {
            return Keire::Detail::SetManagedUiVector(m_Scene, entity, property, value);
        }

        [[nodiscard]] std::optional<std::string> ReadManagedUiInputText(const Keire::AssetId entity) noexcept override
        {
            return Keire::Detail::ReadManagedUiInputText(m_Scene, entity);
        }

        [[nodiscard]] bool SetManagedUiInputText(const Keire::AssetId entity,
                                                 const std::string_view text) noexcept override
        {
            return Keire::Detail::SetManagedUiInputText(m_Scene, entity, text);
        }

        [[nodiscard]] bool ConsumeManagedUiEvent(const Keire::AssetId entity,
                                                 const Keire::RuntimeUiEventType type) noexcept override
        {
            return Keire::Detail::ConsumeManagedUiEvent(m_Presentation, entity, type);
        }

        [[nodiscard]] bool FocusManagedUi(const Keire::AssetId entity) noexcept override
        {
            return Keire::Detail::FocusManagedUi(m_Presentation, entity);
        }

        [[nodiscard]] Keire::Ref<Keire::Scene> ManagedRuntimeScene() const noexcept override { return m_Scene; }

      private:
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
        Keire::AssetId m_DefaultMixer;
        Keire::RenderEnvironmentSettings m_Rendering;
        RuntimeCommandLine m_CommandLine;
        Keire::ReplayFingerprints m_ReplayFingerprints;
        std::shared_ptr<RuntimeReplayState> m_ReplayState;
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
            (void)PushLayer(std::make_unique<RuntimeLayer>(m_Manifest.StartupScene, m_Manifest.DefaultInput,
                                                           m_Manifest.DefaultMixer, m_Manifest.Rendering, m_CommandLine,
                                                           m_ReplayFingerprints, m_ReplayState));
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
    ApplicationCommandLineDescription GetApplicationCommandLineDescription() noexcept
    {
        return {"[--content <path>] [runtime/replay options]", RuntimeOptions};
    }

    std::unique_ptr<Application> CreateApplication(const ApplicationCommandLineArguments& arguments)
    {
        const auto commandLine = ParseCommandLine(arguments);
        auto manifest = LoadManifest(commandLine.Content);
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
        specification.Ui.Mode = UiMode::Disabled;
        specification.Input.Mode = manifest.DefaultInput ? InputMode::Enabled : InputMode::Disabled;
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
