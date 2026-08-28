#pragma once

#include "Keire/Core.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace KeireRuntime
{
    enum class RuntimeReplayAction : std::uint8_t
    {
        None,
        Record,
        Play,
        Verify
    };

    struct RuntimeCommandLine final
    {
        std::filesystem::path Content;
        std::uint32_t Frames = 0;
        std::uint64_t TickLimit = 0;
        Keire::AssetId Scene;
        RuntimeReplayAction ReplayAction = RuntimeReplayAction::None;
        std::filesystem::path ReplayPath;
        std::filesystem::path OutputPath;
        std::filesystem::path ManagedRuntime;
        std::filesystem::path AdditiveValidationOutput;
        std::vector<Keire::AssetId> ValidationScenes;
        std::filesystem::path RenderBenchmarkOutput;
        Keire::RenderPresentMode PresentMode = Keire::RenderPresentMode::VSync;
        std::string ProductName = "Keire Runtime";
        std::string ProductVersion;
        std::string WindowTitle = "Kéire Runtime";
        std::string ApplicationIdentifier;
        Keire::ReplayProfile ReplayProfile = Keire::ReplayProfile::StrictVerified;
        bool Headless = false;
        bool PresentModeExplicit = false;
#if defined(KEIRE_ENABLE_TEST_HOOKS)
        bool ValidateDeviceLoss = false;
        bool HiddenValidationWindow = false;
#endif
    };

    [[nodiscard]] RuntimeCommandLine ParseRuntimeCommandLine(const Keire::ApplicationCommandLineArguments& arguments);
} // namespace KeireRuntime
