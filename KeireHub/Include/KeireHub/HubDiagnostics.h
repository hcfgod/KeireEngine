#pragma once

#include "KeireHub/HubProductUi.h"

#include "Keire/BuildInfo.h"

#include <filesystem>
#include <string>

namespace KeireHub
{
    struct HubFatalRecoveryOutcome final
    {
        bool CloseRequested = false;
        std::string Message;
        std::string TechnicalDetails;
    };

    [[nodiscard]] std::string BuildHubDiagnosticReport(const HubProductSnapshot& snapshot,
                                                       const Keire::BuildInfo& build,
                                                       const std::filesystem::path& preferenceRoot);
    [[nodiscard]] bool HubLogsAvailable(const std::filesystem::path& preferenceRoot) noexcept;
    [[nodiscard]] HubFatalRecoveryOutcome HandleHubFatalRecoveryAction(HubFatalUiAction action,
                                                                       const HubProductSnapshot& snapshot,
                                                                       Keire::WindowSystem& windows,
                                                                       const std::filesystem::path& preferenceRoot);
    [[nodiscard]] bool DrawHubRuntimeStartupFailure(Keire::UiFrame& ui, HubProductUi& productUi,
                                                    const HubProductSnapshot& snapshot, Keire::Window& window,
                                                    Keire::WindowSystem& windows, std::string_view failure,
                                                    std::string& actionMessage,
                                                    const std::filesystem::path& preferenceRoot);
} // namespace KeireHub
