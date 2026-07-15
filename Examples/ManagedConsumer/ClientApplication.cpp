#include "Keire/Core.h"

#include <array>
#include <cstdlib>
#include <memory>
#include <string>

namespace
{
    constexpr std::array ManagedOptions{
        Keire::ApplicationCommandLineOption{"--managed-smoke", "Run the managed SDK entrypoint smoke test."},
    };

    class ManagedConsumerApplication final : public Keire::Application
    {
      public:
        ManagedConsumerApplication() : Application(BuildSpecification()) {}

      protected:
        void OnInitialize() override { RequestExit(); }

      private:
        static Keire::ApplicationSpecification BuildSpecification()
        {
            Keire::ApplicationSpecification specification;
            specification.MainWindow.Title = "Keire managed SDK consumer";
            specification.MainWindow.Visible = false;
            specification.SuspendWhenMainWindowMinimized = false;
            specification.ManageLogging = false;
            return specification;
        }
    };

    void SelectDummyVideoDriver()
    {
#if defined(_WIN32)
        if (_putenv_s("SDL_VIDEODRIVER", "dummy") != 0)
        {
            throw Keire::CommandLineError("Unable to select SDL's dummy video driver.");
        }
#else
        if (setenv("SDL_VIDEODRIVER", "dummy", 1) != 0)
        {
            throw Keire::CommandLineError("Unable to select SDL's dummy video driver.");
        }
#endif
    }
} // namespace

namespace Keire
{
    ApplicationCommandLineDescription GetApplicationCommandLineDescription() noexcept
    {
        return {"--managed-smoke", ManagedOptions};
    }

    std::unique_ptr<Application> CreateApplication(const ApplicationCommandLineArguments& arguments)
    {
        if (arguments.Size() != 2 || arguments[1] != "--managed-smoke")
        {
            throw CommandLineError("The managed SDK consumer requires --managed-smoke.");
        }
        SelectDummyVideoDriver();
        return std::make_unique<ManagedConsumerApplication>();
    }
} // namespace Keire
