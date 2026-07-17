#include "Keire/Core.h"

#include "EditorWorkspaceLayer.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>

namespace
{
    constexpr std::array ClientCommandLineOptions{
        Keire::ApplicationCommandLineOption{"--config <path>", "Load a specific client configuration file."},
        Keire::ApplicationCommandLineOption{"--smoke-window", "Create a window, pump several iterations, and exit."},
        Keire::ApplicationCommandLineOption{"--smoke-ui", "Render several UI frames and exit."},
    };

    struct CommandLine
    {
        std::filesystem::path ConfigurationPath = "Config/Client.json";
        bool ConfigurationExplicit = false;
        bool SmokeWindow = false;
        bool SmokeUi = false;
    };

    CommandLine ParseCommandLine(const Keire::ApplicationCommandLineArguments& arguments)
    {
        CommandLine result;
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
            else if (argument == "--config")
            {
                if (++index >= arguments.Size())
                    throw Keire::CommandLineError("--config requires a path.");
                result.ConfigurationPath = std::string(arguments[index]);
                result.ConfigurationExplicit = true;
            }
            else
            {
                throw Keire::CommandLineError("Unknown argument '" + std::string(argument) + "'. Run '" +
                                              std::string(arguments.Executable()) + " --help' for usage.");
            }
        }
        if (result.SmokeWindow && result.SmokeUi)
            throw Keire::CommandLineError("--smoke-window and --smoke-ui are mutually exclusive.");
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

    class ClientApplication final : public Keire::Application
    {
      public:
        ClientApplication(Keire::ApplicationSpecification specification, const bool smokeWindow, const bool smokeUi)
            : Application(std::move(specification)), m_SmokeWindow(smokeWindow), m_SmokeUi(smokeUi)
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
                (void)Layers().PushOverlay(std::make_unique<EditorWorkspaceLayer>(m_SmokeUi));
        }

      private:
        bool m_SmokeWindow = false;
        bool m_SmokeUi = false;
    };
} // namespace

namespace Keire
{
    ApplicationCommandLineDescription GetApplicationCommandLineDescription() noexcept
    {
        return {"[--config <path>] [--smoke-window | --smoke-ui]", ClientCommandLineOptions};
    }

    std::unique_ptr<Application> CreateApplication(const ApplicationCommandLineArguments& arguments)
    {
        const auto commandLine = ParseCommandLine(arguments);
        ApplicationSpecification specification;

        if (std::filesystem::exists(commandLine.ConfigurationPath))
            specification.MainWindow = LoadWindowSpecification(commandLine.ConfigurationPath);
        else if (commandLine.ConfigurationExplicit)
            throw ConfigurationError(commandLine.ConfigurationPath, "/", "explicit configuration file does not exist");

        specification.TargetFrameRate = commandLine.SmokeWindow ? 240 : 0;
        specification.Ui.Mode = commandLine.SmokeWindow ? UiMode::Disabled : UiMode::Rendered;
        specification.Ui.Workspace.Enabled = !commandLine.SmokeWindow;
        specification.Ui.Workspace.Ephemeral = commandLine.SmokeUi;
        specification.Ui.Workspace.BuildFactoryLayout = BuildEditorLayout;
        return std::make_unique<ClientApplication>(std::move(specification), commandLine.SmokeWindow,
                                                   commandLine.SmokeUi);
    }
} // namespace Keire
