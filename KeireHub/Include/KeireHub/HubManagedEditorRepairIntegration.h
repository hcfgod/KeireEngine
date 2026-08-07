#pragma once

#include "KeireHubRuntime/EditorInstallationManager.h"

#include <string>

namespace KeireHub
{
    class HubEditorInstallWorkflow;
    class HubPackageTaskWorkflow;

    [[nodiscard]] HubResult<std::string>
    ExecuteHubManagedEditorRepairPlan(const EditorManagedOperationPlan& authorization,
                                      HubEditorInstallWorkflow& installs, HubPackageTaskWorkflow& tasks);
} // namespace KeireHub
