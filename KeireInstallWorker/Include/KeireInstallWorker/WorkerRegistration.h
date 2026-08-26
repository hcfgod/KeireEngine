#pragma once

#include "KeireHubRuntime/InstallTransaction.h"

namespace KeireInstallWorker
{
    [[nodiscard]] KeireHub::InstallRegistrationStore CreateProductRegistrationStore(KeireHub::InstallProduct product);
} // namespace KeireInstallWorker
