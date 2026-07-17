#include "Keire/Core.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <sstream>
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
                {
                    throw Keire::CommandLineError("--config requires a path.");
                }
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
        {
            throw Keire::CommandLineError("--smoke-window and --smoke-ui are mutually exclusive.");
        }
        return result;
    }

    class SmokeLayer final : public Keire::Layer
    {
      public:
        SmokeLayer() : Layer("SmokeLayer") {}

      protected:
        void OnUpdate(const Keire::Time&) override
        {
            if (++m_FrameCount >= 8)
            {
                Owner().RequestExit();
            }
        }

      private:
        std::uint32_t m_FrameCount = 0;
    };

    class EditorLayer final : public Keire::Layer
    {
      public:
        explicit EditorLayer(const bool smoke) : Layer("EditorLayer"), m_Smoke(smoke) {}

      protected:
        void OnUpdate(const Keire::Time&) override
        {
            if (m_Smoke && ++m_FrameCount >= 8)
            {
                Owner().RequestExit();
            }
        }

        void OnUi(Keire::UiFrame& ui) override
        {
            ui.SetNextWindowSize({520.0F, 340.0F});
            Keire::UiWindowOptions options;
            options.MenuBar = true;
            if (auto window = ui.BeginWindow("Kéire Engine", nullptr, options); window)
            {
                if (auto menuBar = ui.BeginMenuBar(); menuBar)
                {
                    if (auto application = ui.BeginMenu("Application"); application)
                    {
                        if (ui.MenuItem("Exit"))
                        {
                            Owner().RequestExit();
                        }
                    }
                    if (auto view = ui.BeginMenu("View"); view)
                    {
                        if (ui.MenuItem("Diagnostics", m_ShowDiagnostics))
                        {
                            m_ShowDiagnostics = !m_ShowDiagnostics;
                        }
                    }
                }

                ui.Text("Kéire Engine UI");
                ui.Separator();
                ui.Text("Dear ImGui is isolated behind the Kéire UI facade.");
                ui.Spacing();
                if (ui.Button("Request clean exit"))
                {
                    Owner().RequestExit();
                }
            }

            if (m_ShowDiagnostics)
            {
                ui.SetNextWindowSize({420.0F, 260.0F});
                if (auto diagnostics = ui.BeginWindow("Diagnostics", &m_ShowDiagnostics); diagnostics)
                {
                    const auto& time = Owner().GetTime();
                    std::ostringstream frame;
                    frame << "Frame: " << time.FrameCount();
                    ui.Text(frame.str());

                    std::ostringstream delta;
                    delta << "Delta: " << time.UnscaledDeltaTime().Milliseconds() << " ms";
                    ui.Text(delta.str());

                    const auto window = Owner().MainWindow();
                    std::ostringstream extent;
                    extent << "Window: " << window->LogicalSize().Width << 'x' << window->LogicalSize().Height
                           << " logical pixels";
                    ui.Text(extent.str());

                    auto capture = Owner().UiCapture();
                    (void)ui.Checkbox("Pointer capture", capture.Pointer);
                    (void)ui.Checkbox("Keyboard capture", capture.Keyboard);
                    ui.TextColored({0.35F, 0.78F, 0.55F, 1.0F}, "Docking enabled; native viewports disabled.");
                }
            }
        }

      private:
        std::uint32_t m_FrameCount = 0;
        bool m_ShowDiagnostics = true;
        bool m_Smoke = false;
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
            {
                (void)Layers().PushLayer(std::make_unique<SmokeLayer>());
            }
            else
            {
                (void)Layers().PushOverlay(std::make_unique<EditorLayer>(m_SmokeUi));
            }
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
        {
            specification.MainWindow = LoadWindowSpecification(commandLine.ConfigurationPath);
        }
        else if (commandLine.ConfigurationExplicit)
        {
            throw ConfigurationError(commandLine.ConfigurationPath, "/", "explicit configuration file does not exist");
        }

        specification.TargetFrameRate = commandLine.SmokeWindow ? 240 : 0;
        specification.Ui.Mode = commandLine.SmokeWindow ? UiMode::Disabled : UiMode::Rendered;
        specification.Ui.LayoutPath = commandLine.SmokeUi ? std::filesystem::path{} : "Build/User/UiLayout.ini";
        return std::make_unique<ClientApplication>(std::move(specification), commandLine.SmokeWindow,
                                                   commandLine.SmokeUi);
    }
} // namespace Keire
