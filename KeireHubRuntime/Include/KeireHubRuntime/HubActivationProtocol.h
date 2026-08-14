#pragma once

#include "KeireHubRuntime/HubError.h"

#include <filesystem>
#include <string>
#include <string_view>

namespace KeireHub
{
    inline constexpr std::string_view HubActivationProtocolScheme = "keirehub";

    struct HubActivationProtocolRegistration final
    {
        std::filesystem::path Executable;
        std::string Description;
        std::string Icon;
        std::string Command;

        [[nodiscard]] bool operator==(const HubActivationProtocolRegistration&) const = default;
    };

    [[nodiscard]] HubResult<HubActivationProtocolRegistration>
    PlanHubActivationProtocolRegistration(std::filesystem::path executable);
    [[nodiscard]] HubStatus EnsureHubActivationProtocolRegistration(const std::filesystem::path& executable);
} // namespace KeireHub
