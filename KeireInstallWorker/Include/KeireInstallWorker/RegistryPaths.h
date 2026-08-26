#pragma once

#include "KeireHubRuntime/InstallTransaction.h"

#include <string>

namespace KeireInstallWorker::Detail
{
    [[nodiscard]] std::wstring ProductRegistrationKey(KeireHub::InstallProduct product);
    [[nodiscard]] std::wstring UninstallRegistrationKey(KeireHub::InstallProduct product);
    [[nodiscard]] std::wstring HubProtocolRegistrationKey();
} // namespace KeireInstallWorker::Detail
