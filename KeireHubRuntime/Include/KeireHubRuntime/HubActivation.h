#pragma once

#include "KeireHubRuntime/HubError.h"
#include "KeireHubRuntime/HubSettingsStore.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace KeireHub
{
    inline constexpr std::size_t MaximumHubActivationFrameBytes = 512;

    enum class HubActivationAction : std::uint8_t
    {
        Show = 1,
        Navigate,
        OpenProject,
        ImportPackage,
        InstallVersion,
        BuildSupport
    };

    struct HubActivationRequest final
    {
        HubActivationAction Action = HubActivationAction::Show;
        std::optional<HubPage> Page;
        std::optional<std::filesystem::path> Path;
        std::optional<std::string> VersionId;
        std::optional<std::string> Platform;
        std::optional<std::string> Architecture;

        [[nodiscard]] bool RequestsBuildSupport() const noexcept { return Action == HubActivationAction::BuildSupport; }
    };

    [[nodiscard]] HubStatus ValidateHubActivation(const HubActivationRequest& request);
    [[nodiscard]] HubResult<std::string> EncodeHubActivation(const HubActivationRequest& request);
    [[nodiscard]] HubResult<HubActivationRequest> DecodeHubActivation(std::string_view frame);
    [[nodiscard]] HubResult<HubActivationRequest>
    ParseHubActivationArguments(std::span<const std::string_view> arguments);
} // namespace KeireHub
