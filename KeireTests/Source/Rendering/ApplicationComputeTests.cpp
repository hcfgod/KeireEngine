#include "Keire/Application.h"

#include <SDL3/SDL.h>
#include <doctest/doctest.h>

#include <stdexcept>
#include <thread>

namespace
{
    class ComputeUnavailableApplication final : public Keire::Application
    {
      public:
        explicit ComputeUnavailableApplication(const Keire::RenderMode mode) : Application(Specification(mode)) {}
        bool Checked = false;

      protected:
        void OnInitialize() override
        {
            CHECK_THROWS_AS(Compute(), std::logic_error);
            Checked = true;
            RequestExit();
        }

      private:
        static Keire::ApplicationSpecification Specification(const Keire::RenderMode mode)
        {
            Keire::ApplicationSpecification result;
            result.MainWindow.Visible = false;
            result.ManageLogging = false;
            result.Render.Mode = mode;
            result.Ui.Mode = Keire::UiMode::Disabled;
            return result;
        }
    };
} // namespace

TEST_CASE("application compute rejects unavailable lifecycle and headless modes")
{
    REQUIRE(SDL_SetEnvironmentVariable(SDL_GetEnvironment(), "SDL_VIDEODRIVER", "dummy", true));
    for (const auto mode : {Keire::RenderMode::Disabled, Keire::RenderMode::Headless})
    {
        ComputeUnavailableApplication application(mode);
        CHECK_THROWS_AS(application.Compute(), std::logic_error);
        bool rejected = false;
        std::thread worker(
            [&]
            {
                try
                {
                    (void)application.Compute();
                }
                catch (const std::logic_error&)
                {
                    rejected = true;
                }
            });
        worker.join();
        CHECK(rejected);
        CHECK_FALSE(application.IsRunning());
        CHECK(application.Run() == 0);
        CHECK(application.Checked);
        CHECK_THROWS_AS(application.Compute(), std::logic_error);
    }
}
