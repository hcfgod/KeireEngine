#pragma once

#include "KeireHubRuntime/InstallTransaction.h"

#include <optional>

namespace KeireInstallWorker
{
    [[nodiscard]] KeireHub::HubStatus
    PrepareShellIntegrations(KeireHub::InstallProduct product, const KeireHub::InstallRegistration& registration,
                             const std::optional<KeireHub::InstallRegistration>& previousRegistration,
                             bool startMenu, bool desktop);

    [[nodiscard]] KeireHub::HubStatus
    CommitShellIntegrations(KeireHub::InstallProduct product, const KeireHub::InstallRegistration& registration);

    [[nodiscard]] KeireHub::HubStatus
    ReconcileShellIntegrations(KeireHub::InstallProduct product,
                               const std::optional<KeireHub::InstallRegistration>& activeRegistration);

    [[nodiscard]] KeireHub::HubStatus
    RemoveShellIntegrations(KeireHub::InstallProduct product, const KeireHub::InstallRegistration& registration);
} // namespace KeireInstallWorker
