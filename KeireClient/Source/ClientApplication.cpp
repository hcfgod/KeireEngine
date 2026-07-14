#include "Keire/Core.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>

namespace
{
    struct CommandLine
    {
        std::filesystem::path ConfigurationPath = "Config/Client.json";
        bool ConfigurationExplicit = false;
        bool SmokeWindow = false;
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

    class ClientApplication final : public Keire::Application
    {
      public:
        ClientApplication(Keire::ApplicationSpecification specification, const bool smokeWindow)
            : Application(std::move(specification)), m_SmokeWindow(smokeWindow)
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
        }

      private:
        bool m_SmokeWindow = false;
    };
} // namespace

namespace Keire
{
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

        specification.TargetFrameRate = 240;
        return std::make_unique<ClientApplication>(std::move(specification), commandLine.SmokeWindow);
    }
} // namespace Keire
