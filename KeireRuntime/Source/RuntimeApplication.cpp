#include "Keire/Core.h"

#include "KeireInternal/FileSystem.h"

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

namespace
{
    constexpr std::array RuntimeOptions{
        Keire::ApplicationCommandLineOption{"--content <path>", "Mount cooked Kéire runtime content."},
        Keire::ApplicationCommandLineOption{"--frames <count>", "Exit after a finite number of rendered frames."}};

    struct RuntimeCommandLine final
    {
        std::filesystem::path Content;
        std::uint32_t Frames = 0;
    };

    struct RuntimeManifest final
    {
        std::filesystem::path ContentRoot;
        Keire::AssetId StartupScene;
        Keire::AssetId DefaultInput;
        Keire::AssetId DefaultMixer;
        Keire::RenderEnvironmentSettings Rendering;
        std::array<std::uint32_t, Keire::PhysicsCollisionLayerCount> PhysicsCollisionMatrix;
        std::vector<std::filesystem::path> ManagedAssemblyRoots;
        bool Scripting = false;
        bool Physics = false;
        bool Audio = false;
        bool Navigation = false;
    };

    [[nodiscard]] RuntimeCommandLine ParseCommandLine(const Keire::ApplicationCommandLineArguments& arguments)
    {
        RuntimeCommandLine result;
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
                const auto value = arguments[index];
                const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result.Frames);
                if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() || result.Frames == 0)
                    throw Keire::CommandLineError("--frames requires a positive count.");
            }
            else
                throw Keire::CommandLineError("Unknown runtime option: " + std::string(option));
        }
        if (result.Content.empty())
            throw Keire::CommandLineError("KeireRuntime requires --content <path>.");
        result.Content = std::filesystem::absolute(result.Content).lexically_normal();
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
        if (schema < 3)
            throw Keire::CommandLineError(
                "Cooked runtime manifest schema is obsolete; recook the project with this Kéire version.");
        if (schema > 3)
            throw Keire::CommandLineError(
                "Cooked runtime manifest requires a newer Kéire runtime (supported schema: 3).");
        RuntimeManifest result;
        result.ContentRoot = std::filesystem::absolute(content).lexically_normal();
        result.StartupScene = Keire::AssetId::Parse(source.at("startupScene").get<std::string>());
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
        if (!source.at("buildIdentity").is_object() || pageBytes < 4096 || pageBytes > 16U * 1024U * 1024U ||
            concurrentReads == 0 || concurrentReads > 256)
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

    class RuntimeLayer final : public Keire::Layer, public Keire::IScriptRuntimeServices
    {
      public:
        RuntimeLayer(const Keire::AssetId startupScene, const Keire::AssetId defaultInput,
                     const Keire::RenderEnvironmentSettings rendering, const std::uint32_t frames)
            : Layer("Runtime"), m_StartupScene(startupScene), m_DefaultInput(defaultInput), m_Rendering(rendering),
              m_MaximumFrames(frames)
        {
        }

      protected:
        void OnAttach() override
        {
            m_Load = Owner().Scenes()->Load(m_StartupScene);
            Keire::RenderSurfaceSpecification surface;
            surface.Name = "Runtime";
            const auto pixels = Owner().MainWindow()->PixelSize();
            surface.Width = std::max(pixels.Width, 1U);
            surface.Height = std::max(pixels.Height, 1U);
            m_View = Owner().Renderer()->CreateView(surface);
            if (const auto scripts = Owner().Scripts())
                scripts->SetRuntimeServices(this);
            if (m_DefaultInput)
            {
                if (const auto input = Owner().Input())
                {
                    m_InputUser = input->CreateUser("Player");
                    for (const auto& device : input->Devices())
                    {
                        if (device.Connected)
                            (void)input->PairDevice(m_InputUser, device.Id);
                    }
                    m_InputContext = input->CreateActionContext(m_DefaultInput, m_InputUser);
                }
            }
        }

        void OnDetach() noexcept override
        {
            if (const auto scripts = Owner().Scripts())
                scripts->SetRuntimeServices(nullptr);
            if (m_Presentation)
                m_Presentation->Clear();
            if (m_Runtime)
                m_Runtime->Stop();
            m_Runtime.Reset();
            m_Presentation.Reset();
            m_Scene.Reset();
        }

        void OnFixedUpdate(const Keire::Time& time) override
        {
            if (m_Runtime)
                m_Runtime->FixedUpdate(static_cast<float>(time.FixedDeltaTime().Seconds()));
        }

        void OnUpdate(const Keire::Time& time) override
        {
            m_DeltaTime = static_cast<float>(time.DeltaTime().Seconds());
            if (!m_Runtime)
            {
                if (m_Load->State() == Keire::SceneLoadState::Failed)
                    throw std::runtime_error("Startup scene load failed: " + m_Load->Diagnostic().Message);
                if (m_Load->State() != Keire::SceneLoadState::Ready)
                    return;
                m_Runtime = Keire::CreateRef<Keire::SceneRuntimeSession>(m_Load->Result(), Owner().Assets(),
                                                                         Owner().Audio(), Owner().Physics());
                m_Runtime->Play();
                if (m_Runtime->State() == Keire::ScenePlayState::Faulted)
                    throw std::runtime_error("Startup scene Play failed: " + m_Runtime->Diagnostic().Message);
                m_Scene = m_Runtime->RuntimeScene();
                m_Presentation = m_Runtime->Presentation();
            }
            const auto pixels = Owner().MainWindow()->PixelSize();
            const auto width = std::max(pixels.Width, 1U);
            const auto height = std::max(pixels.Height, 1U);
            m_Runtime->SetPresentationViewport(static_cast<float>(width), static_cast<float>(height));
            m_Runtime->Update(static_cast<float>(time.DeltaTime().Seconds()));
            if (m_Runtime->State() == Keire::ScenePlayState::Faulted)
                throw std::runtime_error("Startup scene runtime failed: " + m_Runtime->Diagnostic().Message);
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
            Owner().Renderer()->Submit(std::move(renderRequest));
            if (m_MaximumFrames != 0 && ++m_RenderedFrames >= m_MaximumFrames)
                Owner().RequestExit();
        }

        void OnUi(Keire::UiFrame& ui) override
        {
            if (m_View)
            {
                ui.Image(m_View->Surface(), ui.ContentAvailable());
                const auto rectangle = ui.LastItemRect();
                if (m_Presentation)
                {
                    m_Presentation->Draw(ui, rectangle.Minimum.X, rectangle.Minimum.Y);
                    const auto pointer = ui.PointerState();
                    const float localX = pointer.Position.X - rectangle.Minimum.X;
                    const float localY = pointer.Position.Y - rectangle.Minimum.Y;
                    m_Presentation->PointerMove(localX, localY);
                    if (rectangle.Contains(pointer.Position))
                    {
                        if (pointer.LeftPressed)
                            m_Presentation->PointerButton(localX, localY, Keire::RuntimeUiPointerButton::Primary, true);
                        if (pointer.RightPressed)
                            m_Presentation->PointerButton(localX, localY, Keire::RuntimeUiPointerButton::Secondary,
                                                          true);
                    }
                    if (pointer.LeftReleased)
                        m_Presentation->PointerButton(localX, localY, Keire::RuntimeUiPointerButton::Primary, false);
                    if (pointer.RightReleased)
                        m_Presentation->PointerButton(localX, localY, Keire::RuntimeUiPointerButton::Secondary, false);
                }
            }
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

        [[nodiscard]] float ManagedDeltaTime() const noexcept override { return m_DeltaTime; }

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

      private:
        Keire::AssetId m_StartupScene;
        Keire::AssetId m_DefaultInput;
        Keire::RenderEnvironmentSettings m_Rendering;
        std::uint32_t m_MaximumFrames = 0;
        std::uint32_t m_RenderedFrames = 0;
        Keire::Ref<Keire::SceneLoadOperation> m_Load;
        Keire::Ref<Keire::SceneRuntimeSession> m_Runtime;
        Keire::Ref<Keire::Scene> m_Scene;
        Keire::Ref<Keire::RenderView> m_View;
        Keire::Ref<Keire::ScenePresentationRuntime> m_Presentation;
        Keire::InputUserId m_InputUser;
        Keire::Ref<Keire::InputActionContext> m_InputContext;
        float m_DeltaTime = 0.0F;
    };

    class RuntimeApplication final : public Keire::Application
    {
      public:
        RuntimeApplication(Keire::ApplicationSpecification specification, RuntimeManifest manifest,
                           const std::uint32_t frames)
            : Application(std::move(specification)), m_Manifest(std::move(manifest)), m_Frames(frames)
        {
        }

      protected:
        void OnInitialize() override
        {
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
            }
            (void)PushLayer(std::make_unique<RuntimeLayer>(m_Manifest.StartupScene, m_Manifest.DefaultInput,
                                                           m_Manifest.Rendering, m_Frames));
        }

      private:
        RuntimeManifest m_Manifest;
        std::uint32_t m_Frames = 0;
    };
} // namespace

namespace Keire
{
    ApplicationCommandLineDescription GetApplicationCommandLineDescription() noexcept
    {
        return {"--content <path> [--frames <count>]", RuntimeOptions};
    }

    std::unique_ptr<Application> CreateApplication(const ApplicationCommandLineArguments& arguments)
    {
        const auto commandLine = ParseCommandLine(arguments);
        auto manifest = LoadManifest(commandLine.Content);
        ApplicationSpecification specification;
        specification.MainWindow.Title = "Kéire Runtime";
        specification.MainWindow.Visible = commandLine.Frames == 0;
        specification.TargetFrameRate = commandLine.Frames == 0 ? 0 : 240;
        specification.SuspendWhenMainWindowMinimized = false;
        specification.Assets.Mode = AssetMode::Cooked;
        specification.Assets.Mounts.push_back({commandLine.Content / "catalog.json", 0, false});
        specification.Scenes.Mode = SceneMode::Enabled;
        specification.Render.Mode = RenderMode::Rendered;
        specification.Ui.Mode = UiMode::Rendered;
        specification.Ui.Workspace.Enabled = false;
        specification.Input.Mode = manifest.DefaultInput ? InputMode::Enabled : InputMode::Disabled;
        if (manifest.Scripting)
        {
            specification.Scripting.Mode = ScriptMode::Enabled;
            specification.Scripting.ProjectRoot = commandLine.Content;
            specification.Scripting.AssemblyDirectory = ".";
            specification.Scripting.RuntimeHostDirectory =
                std::filesystem::absolute(Keire::Detail::PathFromUtf8(arguments.Executable())).parent_path() /
                "Managed";
            specification.Scripting.RuntimeRootDirectory =
                std::filesystem::absolute(Keire::Detail::PathFromUtf8(arguments.Executable())).parent_path() /
                "Managed" / "Dotnet";
            specification.Scripting.ManagedApiAssembly =
                std::filesystem::absolute(Keire::Detail::PathFromUtf8(arguments.Executable())).parent_path() /
                "Managed" / "Keire.Managed.dll";
        }
        if (manifest.Physics || manifest.Navigation)
        {
            specification.Physics.Mode = PhysicsMode::Enabled;
            specification.Physics.CollisionMatrix = manifest.PhysicsCollisionMatrix;
        }
        if (manifest.Audio)
            specification.Audio.Mode = commandLine.Frames == 0 ? AudioMode::Enabled : AudioMode::Headless;
        if (manifest.Navigation)
            specification.Navigation.Mode = NavigationMode::Enabled;
        return std::make_unique<RuntimeApplication>(std::move(specification), std::move(manifest), commandLine.Frames);
    }
} // namespace Keire
