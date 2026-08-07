#pragma once

#include "KeireHubRuntime/EditorInstallationRegistry.h"
#include "KeireHubRuntime/HubController.h"
#include "KeireHubRuntime/HubFirstRunDiscovery.h"
#include "KeireHubRuntime/HubProjectCatalog.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <vector>

namespace KeireHub
{
    struct HubFirstRunPreparedImport final
    {
        std::vector<HubRecentProject> Projects;
        std::vector<EditorInstallation> Editors;
    };

    struct HubFirstRunImportPreparationHooks final
    {
        std::function<bool()> IsCancelled;
        std::function<void(const std::filesystem::path&)> BeforeProjectInspection;
        std::function<void(const std::filesystem::path&)> BeforeEditorInspection;
    };

    [[nodiscard]] HubResult<HubFirstRunPreparedImport>
    PrepareHubFirstRunImport(const HubFirstRunDiscoverySnapshot& discovery, std::uint64_t nowUnixSeconds,
                             const HubFirstRunImportPreparationHooks& hooks = {});
    [[nodiscard]] HubStatus CommitHubFirstRunImport(const HubFirstRunPreparedImport& prepared,
                                                    HubController& controller);
} // namespace KeireHub
