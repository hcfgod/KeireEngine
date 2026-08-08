#pragma once

#include "KeireHub/HubProductUi.h"
#include "KeireHub/HubProjectCreationUi.h"
#include "KeireHub/HubTemplateWorkflow.h"

namespace KeireHub
{
    [[nodiscard]] HubStatus StartHubProjectCreation(HubTemplateWorkflow& workflow, const HubProductSnapshot& snapshot,
                                                    const HubCreateProjectRequest& request);
} // namespace KeireHub
