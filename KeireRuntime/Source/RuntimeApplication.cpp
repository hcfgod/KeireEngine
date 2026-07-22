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
        Keire::AssetId StartupScene;
        Keire::AssetId DefaultInput;
        Keire::RenderEnvironmentSettings Rendering;
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
        if (!stream || !source.is_object() || source.value("schemaVersion", 0U) != 1)
            throw Keire::CommandLineError("Cooked content has no valid runtime-manifest.json.");
        RuntimeManifest result;
        result.StartupScene = Keire::AssetId::Parse(source.at("startupScene").get<std::string>());
        if (source.contains("defaultInput") && !source.at("defaultInput").is_null())
            result.DefaultInput = Keire::AssetId::Parse(source.at("defaultInput").get<std::string>());
        const auto& rendering = source.at("rendering");
        const auto& ambient = rendering.at("ambientColor");
        if (!ambient.is_array() || ambient.size() != 4)
            throw Keire::CommandLineError("Runtime rendering settings contain an invalid ambient color.");
        result.Rendering.AmbientColor = {ambient[0].get<float>(), ambient[1].get<float>(), ambient[2].get<float>(),
                                         ambient[3].get<float>()};
        result.Rendering.AmbientIntensity = rendering.at("ambientIntensity").get<float>();
        result.Rendering.Exposure = rendering.at("exposure").get<float>();
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

    class RuntimeLayer final : public Keire::Layer
    {
      public:
        RuntimeLayer(const Keire::AssetId startupScene, const Keire::RenderEnvironmentSettings rendering,
                     const std::uint32_t frames)
            : Layer("Runtime"), m_StartupScene(startupScene), m_Rendering(rendering), m_MaximumFrames(frames)
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
        }

        void OnDetach() noexcept override
        {
            if (m_Playing && m_Scene)
                m_Scene->EndPlay();
        }

        void OnFixedUpdate(const Keire::Time& time) override
        {
            if (m_Playing && m_Scene)
                m_Scene->FixedUpdate(static_cast<float>(time.FixedDeltaTime().Seconds()));
        }

        void OnUpdate(const Keire::Time& time) override
        {
            if (!m_Scene)
            {
                if (m_Load->State() == Keire::SceneLoadState::Failed)
                    throw std::runtime_error("Startup scene load failed: " + m_Load->Diagnostic().Message);
                if (m_Load->State() != Keire::SceneLoadState::Ready)
                    return;
                m_Scene = m_Load->Result();
                m_Scene->BeginPlay();
                m_Playing = true;
            }
            m_Scene->Update(static_cast<float>(time.DeltaTime().Seconds()));
            const auto selected = SelectCamera(m_Scene);
            if (!selected)
                throw std::runtime_error("The startup scene has no active camera.");
            const auto pixels = Owner().MainWindow()->PixelSize();
            const auto width = std::max(pixels.Width, 1U);
            const auto height = std::max(pixels.Height, 1U);
            m_View->Surface()->RequestSize(width, height);
            Keire::RenderCamera camera;
            camera.View = Keire::Math::Inverse(selected->Transform->WorldMatrix());
            camera.Projection =
                selected->Camera->ProjectionMatrix(static_cast<float>(width) / static_cast<float>(height));
            camera.ClearColor = selected->Camera->ClearColor();
            m_View->SetCamera(camera);
            Owner().Renderer()->Submit({m_Scene, m_View, false, m_Rendering});
            if (m_MaximumFrames != 0 && ++m_RenderedFrames >= m_MaximumFrames)
                Owner().RequestExit();
        }

        void OnUi(Keire::UiFrame& ui) override
        {
            if (m_View)
                ui.Image(m_View->Surface(), ui.ContentAvailable());
        }

      private:
        Keire::AssetId m_StartupScene;
        Keire::RenderEnvironmentSettings m_Rendering;
        std::uint32_t m_MaximumFrames = 0;
        std::uint32_t m_RenderedFrames = 0;
        Keire::Ref<Keire::SceneLoadOperation> m_Load;
        Keire::Ref<Keire::Scene> m_Scene;
        Keire::Ref<Keire::RenderView> m_View;
        bool m_Playing = false;
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
            (void)PushLayer(std::make_unique<RuntimeLayer>(m_Manifest.StartupScene, m_Manifest.Rendering, m_Frames));
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
        specification.Input.Mode = InputMode::Disabled;
        return std::make_unique<RuntimeApplication>(std::move(specification), std::move(manifest), commandLine.Frames);
    }
} // namespace Keire
