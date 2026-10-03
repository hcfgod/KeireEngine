#include "Keire/Core.h"

#include <array>
#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <string>

namespace
{
    static_assert(Keire::EntityLayerCount == 32);
    static_assert(Keire::EntityLayerBit(7) == (1U << 7U));

    constexpr std::array ManagedOptions{
        Keire::ApplicationCommandLineOption{"--managed-smoke", "Run the managed SDK entrypoint smoke test."},
    };

    class ManagedUiLayer final : public Keire::Layer
    {
      public:
        ManagedUiLayer() : Layer("ManagedUiLayer") {}

      protected:
        void OnUi(Keire::UiFrame& ui) override
        {
            if (auto window = ui.BeginWindow("Managed SDK UI"); window)
            {
                ui.Text("Headless Kéire UI frame completed.");
            }
            Owner().RequestExit();
        }
    };

    class ManagedConsumerApplication final : public Keire::Application
    {
      public:
        ManagedConsumerApplication() : Application(BuildSpecification()) {}

      protected:
        void OnInitialize() override
        {
            // This is the C++ managed-entrypoint consumer; exercise the native Animator contract here.
            Keire::AnimatorComponent animator;
            animator.SetLimbIk({{7}, {1, 2, 3}, {0, 0, 1}, 0.5F}, Keire::AnimatorIkSpace::Model);
            const auto goals = animator.LimbIkTargets();
            if (goals.size() != 1 || goals[0].Target.Id != Keire::LimbId{7} || goals[0].Target.Weight != 0.5F ||
                goals[0].Space != Keire::AnimatorIkSpace::Model || !animator.ClearLimbIk({7}) ||
                !animator.LimbIkTargets().empty())
                throw std::runtime_error("SDK Animator limb goal validation failed.");
            (void)PushLayer(std::make_unique<ManagedUiLayer>());
        }

      private:
        static Keire::ApplicationSpecification BuildSpecification()
        {
            Keire::ApplicationSpecification specification;
            specification.MainWindow.Title = "Keire managed SDK consumer";
            specification.MainWindow.Visible = false;
            specification.SuspendWhenMainWindowMinimized = false;
            specification.ManageLogging = false;
            specification.Ui.Mode = Keire::UiMode::Headless;
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
