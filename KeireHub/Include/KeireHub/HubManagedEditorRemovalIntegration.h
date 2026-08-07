#pragma once

#include "KeireHubRuntime/EditorInstallationManager.h"

#include <string>

namespace KeireHub
{
    class HubPackageTaskWorkflow;

    [[nodiscard]] HubResult<std::string> ExecuteHubManagedEditorRemovalPlan(EditorManagedOperationPlan plan,
                                                                            HubPackageTaskWorkflow& tasks);
} // namespace KeireHub
