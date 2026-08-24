#include "Keire/Core.h"

#include <array>

namespace
{
    constexpr std::array RuntimeOptions{
        Keire::ApplicationCommandLineOption{"--content <path>", "Mount cooked Kéire runtime content."},
        Keire::ApplicationCommandLineOption{"--frames <count>", "Exit after a finite number of rendered frames."},
        Keire::ApplicationCommandLineOption{"--headless", "Run with a hidden window and headless audio."},
        Keire::ApplicationCommandLineOption{"--scene <asset-id>", "Override the cooked startup scene."},
        Keire::ApplicationCommandLineOption{"--tick-limit <count>", "Exit after a finite number of fixed ticks."},
        Keire::ApplicationCommandLineOption{"--record <path>", "Record a deterministic replay."},
        Keire::ApplicationCommandLineOption{"--play <path>", "Play a replay."},
        Keire::ApplicationCommandLineOption{"--verify <path>",
                                            "Verify a replay and return a failure exit code on divergence."},
        Keire::ApplicationCommandLineOption{"--profile <strict|performance>", "Select the replay recording profile."},
        Keire::ApplicationCommandLineOption{"--output <path>", "Write an atomic replay result report."}};
} // namespace

namespace Keire
{
    ApplicationCommandLineDescription GetApplicationCommandLineDescription() noexcept
    {
        return {"[--content <path>] [runtime/replay options]", RuntimeOptions};
    }
} // namespace Keire
