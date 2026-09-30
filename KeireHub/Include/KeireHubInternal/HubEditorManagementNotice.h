#pragma once

#include "KeireHub/HubEditorManagementWorkflow.h"

namespace KeireHub::Detail
{
    [[nodiscard]] constexpr bool
    ShouldAnnounceSuccessfulEditorManagementCompletion(const HubEditorManagementOperation operation) noexcept
    {
        switch (operation)
        {
        case HubEditorManagementOperation::None:
        case HubEditorManagementOperation::Refresh:
            return false;
        case HubEditorManagementOperation::Verify:
        case HubEditorManagementOperation::RefreshRegistration:
        case HubEditorManagementOperation::AuthorizeRepair:
        case HubEditorManagementOperation::AuthorizeRemoval:
            return true;
        }
        return false;
    }
} // namespace KeireHub::Detail
