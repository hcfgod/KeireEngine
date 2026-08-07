#pragma once

#include "KeireHub/HubProductUi.h"

#include <filesystem>
#include <vector>

namespace KeireHub
{
    struct HubLocalContentLoadReport final
    {
        std::vector<HubError> Failures;

        [[nodiscard]] bool Succeeded() const noexcept { return Failures.empty(); }
    };

    [[nodiscard]] HubLocalContentLoadReport PopulateLocalHubContent(const std::filesystem::path& executable,
                                                                    HubProductSnapshot& snapshot);
    [[nodiscard]] std::filesystem::path ResolveHubDistributionRoot(const std::filesystem::path& executable);
    [[nodiscard]] std::filesystem::path ResolveHubTemplatesRoot(const std::filesystem::path& executable);
    [[nodiscard]] std::filesystem::path ResolveHubContentCatalogPath(const std::filesystem::path& executable);
    [[nodiscard]] std::filesystem::path ResolveHubLicenseCatalogPath(const std::filesystem::path& executable);
} // namespace KeireHub
