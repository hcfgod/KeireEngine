#include "Keire/Core.h"

#include "KeireHub/HubApplicationFactory.h"

#include "KeireInternal/FileSystem.h"
#include "KeireProjectModules/SourceModulePack.h"

#include <array>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    constexpr std::array HubOptions{
        Keire::ApplicationCommandLineOption{"--smoke-ui", "Render several project hub frames and exit."},
        Keire::ApplicationCommandLineOption{"--show", "Show the existing Hub window."},
        Keire::ApplicationCommandLineOption{"--navigate <page>", "Open a Hub product page."},
        Keire::ApplicationCommandLineOption{"--open-project <path>", "Open a project through the Hub."},
        Keire::ApplicationCommandLineOption{"--import-package <path>",
                                            "Import a validated legacy Build Support package."},
        Keire::ApplicationCommandLineOption{"--locate-package <path>",
                                            "Locate and import a validated legacy Build Support package."},
        Keire::ApplicationCommandLineOption{"--install-version <package-or-version>",
                                            "Open the verified editor installation review."},
        Keire::ApplicationCommandLineOption{"--build-support <platform> <architecture>",
                                            "Open the matching editor's Build Support components."},
    };
} // namespace

namespace Keire
{
    ApplicationCommandLineDescription GetApplicationCommandLineDescription() noexcept
    {
        return {"[--smoke-ui] [activation action]", HubOptions};
    }

    std::unique_ptr<Application> CreateApplication(const ApplicationCommandLineArguments& arguments)
    {
        const auto executable = Detail::PathFromUtf8(arguments.Executable());
        bool smoke = false;
        std::vector<std::string_view> activationArguments;
        for (std::size_t index = 1; index < arguments.Size(); ++index)
        {
            if (arguments[index] == "--smoke-ui")
                smoke = true;
            else
                activationArguments.push_back(arguments[index]);
        }
        auto activation = KeireHub::ParseHubActivationArguments(activationArguments);
        if (!activation)
            throw CommandLineError(activation.Error().Message);
        std::optional<KeireHub::HubActivationRequest> pendingStartupActivation;
        if (!activationArguments.empty())
            pendingStartupActivation = activation.Value();

        ApplicationSpecification specification;
        specification.Modules.Modules = KeireProjectModules::CreateSourceModules();
#if defined(_WIN32) && defined(KEIRE_DISTRIBUTION)
        specification.Logging.EnableConsole = false;
#endif
        specification.MainWindow.Title = "Kéire Project Hub";
        specification.MainWindow.Width = 1280;
        specification.MainWindow.Height = 800;
        specification.MainWindow.MinimumWidth = 960;
        specification.MainWindow.MinimumHeight = 640;
        specification.MainWindow.Decoration = WindowDecoration::Custom;
        specification.Logging.LogDirectory = (GetPreferenceDirectory() / "Hub" / "Logs").string();
        specification.Ui.Mode = UiMode::Rendered;
        specification.Ui.EnableDocking = false;
        auto fontRoot = executable.parent_path().parent_path() / "content" / "Fonts";
        if (!std::filesystem::is_regular_file(fontRoot / "Inter-Variable.ttf"))
            fontRoot = std::filesystem::current_path() / "KeireHubContent" / "Fonts";
        if (std::filesystem::is_regular_file(fontRoot / "Inter-Variable.ttf") &&
            std::filesystem::is_regular_file(fontRoot / "MaterialSymbolsRounded-Subset.ttf"))
        {
            specification.Ui.Fonts = {{UiFontRole::Body, fontRoot / "Inter-Variable.ttf", 15.0F},
                                      {UiFontRole::Heading, fontRoot / "Inter-Variable.ttf", 20.0F},
                                      {UiFontRole::Icons, fontRoot / "MaterialSymbolsRounded-Subset.ttf", 20.0F}};
        }
        specification.TargetFrameRate = smoke ? 240 : 30;
        auto instance = std::make_shared<KeireHub::HubInstanceCoordinator>(executable, activation.Value(), !smoke);
        if (!instance->IsPrimary())
        {
            specification.MainWindow.Visible = false;
            specification.Ui.Mode = UiMode::Disabled;
            specification.Render.Mode = RenderMode::Disabled;
        }
        return KeireHub::CreateHubApplication(std::move(specification), executable, smoke,
                                              std::move(pendingStartupActivation), std::move(instance));
    }
} // namespace Keire
