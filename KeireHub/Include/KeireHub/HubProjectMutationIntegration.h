#pragma once

#include "KeireHub/HubProjectMutationWorkflow.h"

namespace KeireHub
{
    class HubProjectWorkflow;
    class HubProjectsUi;
    struct HubProjectUiCommand;

    // The referenced mutation authority must outlive every workflow created from these services.
    [[nodiscard]] HubProjectMutationServices CreateHubProjectMutationServices(HubProjectWorkflow& workflow);
    [[nodiscard]] HubStatus SubmitProjectDuplicate(HubProjectMutationWorkflow& workflow, HubProjectsUi& ui,
                                                   const HubProjectUiCommand& command);
} // namespace KeireHub
