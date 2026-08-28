#include "KeireRuntimeInternal/RuntimeCommandLine.h"

#include "KeireInternal/Build/PlayerPackage.h"
#include "KeireInternal/FileSystem.h"
#include "KeireRuntimeInternal/RuntimeAdditiveValidation.h"

#include <charconv>
#include <stdexcept>
#include <string>
#include <string_view>

namespace KeireRuntime
{
    namespace
    {
        template <typename T> void ParsePositiveCount(const std::string_view value, T& output, const char* option)
        {
            const auto parsed = std::from_chars(value.data(), value.data() + value.size(), output);
            if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() || output == 0)
                throw Keire::CommandLineError(std::string(option) + " requires a positive count.");
        }
    } // namespace

    RuntimeCommandLine ParseRuntimeCommandLine(const Keire::ApplicationCommandLineArguments& arguments)
    {
        RuntimeCommandLine result;
        const auto executable =
            std::filesystem::absolute(Keire::Detail::PathFromUtf8(arguments.Executable())).lexically_normal();
        const auto packaged = Keire::Detail::LoadPackagedPlayerConfiguration(executable);
        for (std::size_t index = 1; index < arguments.Size(); ++index)
        {
            const auto option = arguments[index];
            if (option == "--content")
            {
                if (++index >= arguments.Size())
                    throw Keire::CommandLineError("--content requires a path.");
                result.Content = Keire::Detail::PathFromUtf8(arguments[index]);
            }
            else if (option == "--frames")
            {
                if (++index >= arguments.Size())
                    throw Keire::CommandLineError("--frames requires a positive count.");
                ParsePositiveCount(arguments[index], result.Frames, "--frames");
            }
            else if (option == "--tick-limit")
            {
                if (++index >= arguments.Size())
                    throw Keire::CommandLineError("--tick-limit requires a positive count.");
                ParsePositiveCount(arguments[index], result.TickLimit, "--tick-limit");
            }
            else if (option == "--headless")
                result.Headless = true;
            else if (option == "--scene")
            {
                if (++index >= arguments.Size())
                    throw Keire::CommandLineError("--scene requires an asset ID.");
                try
                {
                    result.Scene = Keire::AssetId::Parse(arguments[index]);
                }
                catch (const std::exception&)
                {
                    throw Keire::CommandLineError("--scene requires a valid asset ID.");
                }
            }
            else if (option == "--validate-additive-runtime")
                ParseRuntimeAdditiveValidationOption(arguments, index, result.AdditiveValidationOutput);
#if defined(KEIRE_ENABLE_TEST_HOOKS)
            else if (option == "--validate-device-loss")
                result.ValidateDeviceLoss = true;
            else if (option == "--hidden-validation-window")
                result.HiddenValidationWindow = true;
#endif
            else if (option == "--render-benchmark")
            {
                if (++index >= arguments.Size())
                    throw Keire::CommandLineError("--render-benchmark requires an output path.");
                result.RenderBenchmarkOutput =
                    std::filesystem::absolute(Keire::Detail::PathFromUtf8(arguments[index])).lexically_normal();
            }
            else if (option == "--present-mode")
            {
                if (++index >= arguments.Size())
                    throw Keire::CommandLineError("--present-mode requires vsync or immediate.");
                if (arguments[index] == "vsync")
                    result.PresentMode = Keire::RenderPresentMode::VSync;
                else if (arguments[index] == "immediate")
                    result.PresentMode = Keire::RenderPresentMode::Immediate;
                else
                    throw Keire::CommandLineError("--present-mode requires vsync or immediate.");
                result.PresentModeExplicit = true;
            }
            else if (option == "--record" || option == "--play" || option == "--verify")
            {
                if (result.ReplayAction != RuntimeReplayAction::None)
                    throw Keire::CommandLineError("--record, --play, and --verify are mutually exclusive.");
                if (++index >= arguments.Size())
                    throw Keire::CommandLineError(std::string(option) + " requires a replay path.");
                result.ReplayAction = option == "--record" ? RuntimeReplayAction::Record
                                      : option == "--play" ? RuntimeReplayAction::Play
                                                           : RuntimeReplayAction::Verify;
                result.ReplayPath =
                    std::filesystem::absolute(Keire::Detail::PathFromUtf8(arguments[index])).lexically_normal();
            }
            else if (option == "--profile")
            {
                if (++index >= arguments.Size())
                    throw Keire::CommandLineError("--profile requires strict or performance.");
                if (arguments[index] == "strict")
                    result.ReplayProfile = Keire::ReplayProfile::StrictVerified;
                else if (arguments[index] == "performance")
                    result.ReplayProfile = Keire::ReplayProfile::PerformanceCapture;
                else
                    throw Keire::CommandLineError("--profile requires strict or performance.");
            }
            else if (option == "--output")
            {
                if (++index >= arguments.Size())
                    throw Keire::CommandLineError("--output requires a path.");
                result.OutputPath =
                    std::filesystem::absolute(Keire::Detail::PathFromUtf8(arguments[index])).lexically_normal();
            }
            else
                throw Keire::CommandLineError("Unknown runtime option: " + std::string(option));
        }
        if (packaged)
        {
            if (result.Content.empty())
                result.Content = packaged->Content;
            result.ManagedRuntime = packaged->ManagedRuntime;
            result.ProductName = packaged->Settings.ProductName;
            result.ProductVersion = packaged->Settings.Version;
            result.WindowTitle = packaged->Settings.WindowTitle;
            result.ApplicationIdentifier = packaged->Settings.ApplicationIdentifier;
        }
        if (result.Content.empty())
            throw Keire::CommandLineError(
                "KeireRuntime requires --content <path> unless launched from a packaged player.");
        if (!result.OutputPath.empty() && result.ReplayAction == RuntimeReplayAction::None)
            throw Keire::CommandLineError("--output requires --record, --play, or --verify.");
        if (result.PresentModeExplicit && result.RenderBenchmarkOutput.empty())
            throw Keire::CommandLineError("--present-mode requires --render-benchmark.");
#if defined(KEIRE_ENABLE_TEST_HOOKS)
        if (result.ValidateDeviceLoss && result.AdditiveValidationOutput.empty())
            throw Keire::CommandLineError("--validate-device-loss requires --validate-additive-runtime.");
        if (result.ValidateDeviceLoss && result.Headless)
        {
            throw Keire::CommandLineError(
                "--validate-device-loss does not support --headless; use --hidden-validation-window instead.");
        }
        if (result.HiddenValidationWindow && result.AdditiveValidationOutput.empty())
            throw Keire::CommandLineError("--hidden-validation-window requires --validate-additive-runtime.");
        if (result.HiddenValidationWindow && result.Headless)
            throw Keire::CommandLineError("--hidden-validation-window and --headless are mutually exclusive.");
#endif
        if (!result.RenderBenchmarkOutput.empty() && (result.Frames != 0U || result.Headless))
            throw Keire::CommandLineError("--render-benchmark requires a visible runtime and controls frame count.");
        result.Content = std::filesystem::absolute(result.Content).lexically_normal();
        if (result.ManagedRuntime.empty())
            result.ManagedRuntime = executable.parent_path() / "Managed";
        return result;
    }
} // namespace KeireRuntime
