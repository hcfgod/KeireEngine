#pragma once

#include "KeireHubRuntime/HubError.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace KeireHub
{
    struct DistributionConfiguration final
    {
        static constexpr std::uint32_t CurrentSchemaVersion = 1;

        bool OnlineDiscoveryEnabled = false;
        std::string ServiceBaseUrl;
        std::vector<std::string> TrustedPublicKeyDocuments;
        std::uint64_t MinimumSequence = 1;
    };

    [[nodiscard]] HubResult<DistributionConfiguration> LoadDistributionConfiguration(const std::filesystem::path& path);
} // namespace KeireHub
