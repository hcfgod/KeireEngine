#pragma once

#include "KeireHub/HubEditorManagementWorkflow.h"

#include <string>

namespace KeireHub
{
    class HubEditorInstallWorkflow;
    class HubPackageTaskWorkflow;

    [[nodiscard]] HubStatus BeginHubEditorManagementCommand(HubEditorManagementWorkflow& workflow,
                                                            HubEditorInstallWorkflow* installs,
                                                            HubPackageTaskWorkflow* tasks, const HubUiCommand& command,
                                                            std::string& notice, bool& noticeError);
    void PollHubEditorManagement(HubEditorManagementWorkflow& workflow, HubEditorInstallWorkflow* installs,
                                 HubPackageTaskWorkflow* tasks, std::string& notice, bool& noticeError);
} // namespace KeireHub
