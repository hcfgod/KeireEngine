#pragma once

#include "KeireHub/HubProjectMutationWorkflow.h"

namespace KeireHub
{
    class HubProjectWorkflow;

    // The referenced mutation authority must outlive every workflow created from these services.
    [[nodiscard]] HubProjectMutationServices CreateHubProjectMutationServices(HubProjectWorkflow& workflow);
} // namespace KeireHub
