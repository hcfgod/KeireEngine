#include "GameplayModule.h"

#include "Keire/Core.h"

#include <array>
#include <cstdlib>
#include <memory>

namespace
{
    constexpr std::array Options{
        Keire::ApplicationCommandLineOption{"--module-smoke", "Run the source-module SDK smoke test."}};

    class SourceModuleApplication final : public Keire::Application
    {
      public:
        SourceModuleApplication() : Application(BuildSpecification()) {}

      protected:
        void OnInitialize() override
        {
            if (Modules()->OrderedCatalog().size() != 1)
                throw std::runtime_error("The source-module catalog was not linked into the application.");
            RequestExit();
        }

      private:
        [[nodiscard]] static Keire::ApplicationSpecification BuildSpecification()
        {
            Keire::ApplicationSpecification specification;
            specification.Modules.Modules.push_back(CreateGameplayModule());
            specification.MainWindow.Visible = false;
            specification.Ui.Mode = Keire::UiMode::Disabled;
            specification.ManageLogging = false;
            return specification;
        }
    };

    void SelectDummyVideoDriver()
    {
#if defined(_WIN32)
        if (_putenv_s("SDL_VIDEODRIVER", "dummy") != 0)
            throw Keire::CommandLineError("Unable to select SDL's dummy video driver.");
#else
        if (setenv("SDL_VIDEODRIVER", "dummy", 1) != 0)
            throw Keire::CommandLineError("Unable to select SDL's dummy video driver.");
#endif
    }
} // namespace

namespace Keire
{
    ApplicationCommandLineDescription GetApplicationCommandLineDescription() noexcept
    {
        return {"--module-smoke", Options};
    }

    std::unique_ptr<Application> CreateApplication(const ApplicationCommandLineArguments& arguments)
    {
        if (arguments.Size() != 2 || arguments[1] != "--module-smoke")
            throw CommandLineError("The source-module consumer requires --module-smoke.");
        SelectDummyVideoDriver();
        return std::make_unique<SourceModuleApplication>();
    }
} // namespace Keire
