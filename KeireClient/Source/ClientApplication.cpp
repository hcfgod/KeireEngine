#include "Keire/Core.h"

#include "KeireClient/Editor/EditorWindowPlacement.h"
#include "KeireClient/EditorWorkspaceLayer.h"

#include "KeireInternal/FileSystem.h"
#include "KeireProjectModules/SourceModulePack.h"

#include <array>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace
{
    constexpr std::array ClientCommandLineOptions{
        Keire::ApplicationCommandLineOption{"--config <path>", "Load a specific client configuration file."},
        Keire::ApplicationCommandLineOption{"--project <path>", "Open a Kéire editor project."},
        Keire::ApplicationCommandLineOption{"--smoke-window", "Create a window, pump several iterations, and exit."},
        Keire::ApplicationCommandLineOption{"--smoke-ui", "Render several UI frames and exit."},
        Keire::ApplicationCommandLineOption{"--smoke-project", "Open a project, render several frames, and exit."},
    };

    [[nodiscard]] std::filesystem::path ResolveManagedApiAssembly(const std::filesystem::path& executable,
                                                                  const std::filesystem::path& project)
    {
        auto selected = executable.parent_path() / "Managed" / "Keire.Managed.dll";
        std::error_code error;
        auto selectedWriteTime = std::filesystem::is_regular_file(selected, error)
                                     ? std::filesystem::last_write_time(selected, error)
                                     : std::filesystem::file_time_type::min();
        const auto considerAncestors = [&](std::filesystem::path root)
        {
            if (root.empty())
                return;
            root = std::filesystem::absolute(root, error).lexically_normal();
            for (std::size_t depth = 0; depth < 8 && !root.empty(); ++depth)
            {
                const auto candidate = root / "Build" / "Managed" / "Keire.Managed.dll";
                error.clear();
                if (std::filesystem::is_regular_file(candidate, error))
                {
                    const auto writeTime = std::filesystem::last_write_time(candidate, error);
                    if (!error && writeTime > selectedWriteTime)
                    {
                        selected = candidate;
                        selectedWriteTime = writeTime;
                    }
                }
                const auto parent = root.parent_path();
                if (parent == root)
                    break;
                root = parent;
            }
        };
        considerAncestors(std::filesystem::current_path(error));
        considerAncestors(project);
        return selected;
    }

    struct CommandLine
    {
        std::filesystem::path ExecutablePath;
        std::filesystem::path ConfigurationPath = "Config/Client.json";
        std::filesystem::path ProjectPath;
        bool ConfigurationExplicit = false;
        bool SmokeWindow = false;
        bool SmokeUi = false;
        bool SmokeProject = false;
    };

    CommandLine ParseCommandLine(const Keire::ApplicationCommandLineArguments& arguments)
    {
        CommandLine result;
        result.ExecutablePath = std::filesystem::absolute(Keire::Detail::PathFromUtf8(arguments.Executable()));
        for (std::size_t index = 1; index < arguments.Size(); ++index)
        {
            const auto argument = arguments[index];
            if (argument == "--smoke-window")
            {
                result.SmokeWindow = true;
            }
            else if (argument == "--smoke-ui")
            {
                result.SmokeUi = true;
            }
            else if (argument == "--smoke-project")
            {
                result.SmokeProject = true;
            }
            else if (argument == "--config")
            {
                if (++index >= arguments.Size())
                    throw Keire::CommandLineError("--config requires a path.");
                result.ConfigurationPath = Keire::Detail::PathFromUtf8(arguments[index]);
                result.ConfigurationExplicit = true;
            }
            else if (argument == "--project")
            {
                if (++index >= arguments.Size())
                    throw Keire::CommandLineError("--project requires a path.");
                result.ProjectPath = Keire::Detail::PathFromUtf8(arguments[index]);
            }
            else
            {
                throw Keire::CommandLineError("Unknown argument '" + std::string(argument) + "'. Run '" +
                                              std::string(arguments.Executable()) + " --help' for usage.");
            }
        }
        const auto smokeModes = static_cast<unsigned>(result.SmokeWindow) + static_cast<unsigned>(result.SmokeUi) +
                                static_cast<unsigned>(result.SmokeProject);
        if (smokeModes > 1)
            throw Keire::CommandLineError("Smoke modes are mutually exclusive.");
        if (!result.SmokeWindow && !result.SmokeUi && result.ProjectPath.empty())
            throw Keire::CommandLineError("KeireClient requires --project <path>; launch the Kéire Project Hub.");
        return result;
    }

    void BuildEditorLayout(Keire::UiLayoutBuilder& layout)
    {
        const auto left = layout.Split(layout.Root(), Keire::UiDockDirection::Left, 0.18F);
        const auto right = layout.Split(left.Far, Keire::UiDockDirection::Right, 0.22F);
        const auto bottom = layout.Split(right.Far, Keire::UiDockDirection::Down, 0.27F);
        layout.Dock("editor.hierarchy", left.Near);
        layout.Dock("editor.inspector", right.Near);
        layout.Dock("editor.theme", right.Near);
        layout.Dock("editor.input-actions", bottom.Far);
        layout.Dock("editor.project", bottom.Near);
        layout.Dock("editor.console", bottom.Near);
        layout.Dock("editor.diagnostics", bottom.Near);
        layout.Dock("editor.scene", bottom.Far);
        layout.Dock("editor.game", bottom.Far);
    }

    class SmokeLayer final : public Keire::Layer
    {
      public:
        SmokeLayer() : Layer("SmokeLayer") {}

      protected:
        void OnUpdate(const Keire::Time&) override
        {
            if (++m_FrameCount >= 8)
                Owner().RequestExit();
        }

      private:
        std::uint32_t m_FrameCount = 0;
    };

    class EditorWindowPlacementLayer final : public Keire::Layer
    {
      public:
        EditorWindowPlacementLayer(std::filesystem::path path,
                                   std::optional<KeireEditor::EditorWindowPlacement> placement)
            : Layer("EditorWindowPlacement"), m_Path(std::move(path)), m_Placement(std::move(placement))
        {
        }

      protected:
        void OnAttach() override
        {
            const auto window = Owner().MainWindow();
            if (m_Placement)
            {
                try
                {
                    *m_Placement =
                        KeireEditor::CorrectEditorWindowPlacement(*m_Placement, Owner().Windows()->Displays());
                    window->SetSize(m_Placement->WindowedSize);
                    window->SetPosition(m_Placement->Position);
                    if (m_Placement->Mode == Keire::WindowMode::BorderlessFullscreen)
                        window->SetMode(m_Placement->Mode);
                    else if (m_Placement->Maximized)
                        window->Maximize();
                }
                catch (const std::exception& error)
                {
                    KEIRE_CLIENT_WARN("Could not fully restore the previous editor window placement: {}", error.what());
                }
                window->SetVisible(true);
            }
            else
            {
                m_Placement = KeireEditor::EditorWindowPlacement{};
                CaptureWindowedBounds();
            }
            m_Placement->Mode = window->Mode();
            m_Placement->Maximized = window->Maximized();

            Listen<Keire::WindowMovedEvent>(
                [this](const auto& event)
                {
                    if (IsMainWindow(event.Header) && WindowedBoundsAreActive())
                        m_Placement->Position = event.Position;
                    return Keire::EventFlow::Continue;
                });
            Listen<Keire::WindowResizedEvent>(
                [this](const auto& event)
                {
                    if (IsMainWindow(event.Header) && WindowedBoundsAreActive())
                        m_Placement->WindowedSize = event.Size;
                    return Keire::EventFlow::Continue;
                });
            Listen<Keire::WindowMaximizedEvent>(
                [this](const auto& event)
                {
                    if (IsMainWindow(event.Header))
                        m_Placement->Maximized = true;
                    return Keire::EventFlow::Continue;
                });
            Listen<Keire::WindowRestoredEvent>(
                [this](const auto& event)
                {
                    if (IsMainWindow(event.Header))
                    {
                        m_Placement->Maximized = Owner().MainWindow()->Maximized();
                        CaptureWindowedBounds();
                    }
                    return Keire::EventFlow::Continue;
                });
            Listen<Keire::WindowEnteredFullscreenEvent>(
                [this](const auto& event)
                {
                    if (IsMainWindow(event.Header))
                    {
                        m_Placement->Mode = Keire::WindowMode::BorderlessFullscreen;
                        m_Placement->Maximized = false;
                    }
                    return Keire::EventFlow::Continue;
                });
            Listen<Keire::WindowLeftFullscreenEvent>(
                [this](const auto& event)
                {
                    if (IsMainWindow(event.Header))
                    {
                        m_Placement->Mode = Keire::WindowMode::Windowed;
                        CaptureWindowedBounds();
                    }
                    return Keire::EventFlow::Continue;
                });
        }

        void OnDetach() noexcept override
        {
            try
            {
                const auto window = Owner().MainWindow();
                m_Placement->Mode = window->Mode();
                m_Placement->Maximized = window->Maximized();
                CaptureWindowedBounds();
                if (!KeireEditor::SaveEditorWindowPlacement(m_Path, *m_Placement))
                    KEIRE_CLIENT_WARN("Could not save the editor window placement.");
            }
            catch (...)
            {
            }
        }

      private:
        [[nodiscard]] bool IsMainWindow(const Keire::WindowEventHeader& header) const noexcept
        {
            return header.Window == Owner().MainWindow()->Id();
        }

        [[nodiscard]] bool WindowedBoundsAreActive() const noexcept
        {
            return m_Placement->Mode == Keire::WindowMode::Windowed && !m_Placement->Maximized &&
                   !Owner().MainWindow()->Minimized();
        }

        void CaptureWindowedBounds()
        {
            const auto window = Owner().MainWindow();
            if (window->Mode() != Keire::WindowMode::Windowed || window->Maximized() || window->Minimized())
                return;
            m_Placement->Position = window->Position();
            m_Placement->WindowedSize = window->LogicalSize();
        }

        std::filesystem::path m_Path;
        std::optional<KeireEditor::EditorWindowPlacement> m_Placement;
    };

    class ClientApplication final : public Keire::Application
    {
      public:
        ClientApplication(Keire::ApplicationSpecification specification, const bool smokeWindow, const bool smokeUi,
                          const bool smokeProject, std::filesystem::path windowPlacementPath,
                          std::optional<KeireEditor::EditorWindowPlacement> windowPlacement,
                          std::filesystem::path executablePath)
            : Application(std::move(specification)), m_SmokeWindow(smokeWindow), m_SmokeUi(smokeUi),
              m_SmokeProject(smokeProject), m_WindowPlacementPath(std::move(windowPlacementPath)),
              m_WindowPlacement(std::move(windowPlacement)), m_ExecutablePath(std::move(executablePath))
        {
        }

      protected:
        void OnInitialize() override
        {
            const auto window = MainWindow();
            KEIRE_CLIENT_INFO("Created window '{}' (id={}, logical={}x{}, pixels={}x{}, scale={})", window->Title(),
                              window->Id().Value(), window->LogicalSize().Width, window->LogicalSize().Height,
                              window->PixelSize().Width, window->PixelSize().Height, window->DisplayScale());
            if (m_SmokeWindow)
                (void)Layers().PushLayer(std::make_unique<SmokeLayer>());
            else
            {
                if (!m_WindowPlacementPath.empty())
                    (void)Layers().PushLayer(std::make_unique<EditorWindowPlacementLayer>(
                        m_WindowPlacementPath, std::move(m_WindowPlacement)));
                (void)Layers().PushOverlay(std::make_unique<EditorWorkspaceLayer>(m_SmokeUi || m_SmokeProject,
                                                                                  m_SmokeProject, m_ExecutablePath));
            }
        }

      private:
        bool m_SmokeWindow = false;
        bool m_SmokeUi = false;
        bool m_SmokeProject = false;
        std::filesystem::path m_WindowPlacementPath;
        std::optional<KeireEditor::EditorWindowPlacement> m_WindowPlacement;
        std::filesystem::path m_ExecutablePath;
    };
} // namespace

namespace Keire
{
    ApplicationCommandLineDescription GetApplicationCommandLineDescription() noexcept
    {
        return {"--project <path> [--config <path>] [--smoke-window | --smoke-ui | --smoke-project]",
                ClientCommandLineOptions};
    }

    std::unique_ptr<Application> CreateApplication(const ApplicationCommandLineArguments& arguments)
    {
        const auto commandLine = ParseCommandLine(arguments);
        ApplicationSpecification specification;
        specification.Modules.Modules = KeireProjectModules::CreateSourceModules();
#if defined(_WIN32) && defined(KEIRE_DISTRIBUTION)
        specification.Logging.EnableConsole = false;
#endif

        if (std::filesystem::exists(commandLine.ConfigurationPath))
            specification.MainWindow = LoadWindowSpecification(commandLine.ConfigurationPath);
        else if (commandLine.ConfigurationExplicit)
            throw ConfigurationError(commandLine.ConfigurationPath, "/", "explicit configuration file does not exist");

        auto windowIcon = commandLine.ExecutablePath.parent_path().parent_path() / "Config/Branding/Keire.png";
        if (!std::filesystem::is_regular_file(windowIcon))
            windowIcon = std::filesystem::current_path() / "Config/Branding/Keire.png";
        if (std::filesystem::is_regular_file(windowIcon))
            specification.MainWindow.Icon = std::move(windowIcon);

        specification.TargetFrameRate = commandLine.SmokeWindow ? 240 : 0;
        specification.Ui.Mode = commandLine.SmokeWindow ? UiMode::Disabled : UiMode::Rendered;
        specification.Ui.PresentMode = UiPresentMode::Mailbox;
        specification.Ui.Workspace.Enabled = !commandLine.SmokeWindow;
        specification.Ui.Workspace.Ephemeral = commandLine.SmokeUi;
        specification.Ui.Workspace.BuildFactoryLayout = BuildEditorLayout;
        specification.Assets.Mode =
            commandLine.SmokeWindow || commandLine.SmokeUi ? AssetMode::Disabled : AssetMode::Development;
        specification.Projects.Mode =
            commandLine.SmokeWindow || commandLine.SmokeUi ? ProjectMode::Disabled : ProjectMode::Editor;
        specification.Projects.Root = commandLine.ProjectPath;
        specification.Scenes.Mode =
            commandLine.SmokeWindow || commandLine.SmokeUi ? SceneMode::Disabled : SceneMode::Enabled;
        specification.Input.Mode =
            commandLine.SmokeWindow || commandLine.SmokeUi ? InputMode::Disabled : InputMode::Enabled;
        // The editor creates and owns one explicit input user. Automatic joining can consume the fixed keyboard and
        // mouse devices before that user is paired, leaving Play Mode with a valid action context that always reads 0.
        specification.Input.AutoJoin = false;
        if (!commandLine.SmokeWindow && !commandLine.SmokeUi)
        {
            specification.Profiling.Mode = ProfilerMode::Enabled;
            specification.Scripting.Mode = ScriptMode::Enabled;
            specification.Scripting.ProjectRoot = commandLine.ProjectPath;
            specification.Scripting.RuntimeHostDirectory = commandLine.ExecutablePath.parent_path() / "Managed";
            specification.Scripting.RuntimeRootDirectory =
                commandLine.ExecutablePath.parent_path() / "Managed" / "Dotnet";
            specification.Scripting.ManagedApiAssembly =
                ResolveManagedApiAssembly(commandLine.ExecutablePath, commandLine.ProjectPath);
            specification.Physics.Mode = PhysicsMode::Enabled;
            specification.Audio.Mode = AudioMode::Enabled;
            try
            {
                const auto authoring = LoadProjectAuthoringSettings(commandLine.ProjectPath);
                specification.Audio.MaximumVoices = authoring.Audio.MaximumVoices;
                specification.Audio.MaximumVirtualVoices = authoring.Audio.MaximumVirtualVoices;
                specification.Audio.MixSampleRate = authoring.Audio.MixSampleRate;
                specification.Audio.PeriodFrames = authoring.Audio.PeriodFrames;
                specification.Audio.OutputLayout = authoring.Audio.OutputLayout;
                specification.Audio.PlaybackDeviceId = authoring.Audio.PlaybackDeviceId;
            }
            catch (const std::exception&)
            {
                // The workspace reports malformed authoring settings and remains usable with safe audio defaults.
            }
            specification.Navigation.Mode = NavigationMode::Enabled;
        }
        std::filesystem::path windowPlacementPath;
        std::optional<KeireEditor::EditorWindowPlacement> windowPlacement;
        if (!commandLine.SmokeWindow && !commandLine.SmokeUi && !commandLine.SmokeProject)
        {
            windowPlacementPath = std::filesystem::absolute(commandLine.ProjectPath) / "Library" / "UserSettings" /
                                  "Workspace" / "editor-window.state";
            windowPlacement = KeireEditor::LoadEditorWindowPlacement(windowPlacementPath);
            if (windowPlacement)
                KeireEditor::PrepareEditorWindow(*windowPlacement, specification.MainWindow);
        }
        return std::make_unique<ClientApplication>(
            std::move(specification), commandLine.SmokeWindow, commandLine.SmokeUi, commandLine.SmokeProject,
            std::move(windowPlacementPath), std::move(windowPlacement), std::move(commandLine.ExecutablePath));
    }
} // namespace Keire
