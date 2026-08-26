#pragma once

#include "KeireHubRuntime/InstallTransaction.h"

namespace KeireHub::Detail
{
    class InstallMutationAuthority;

    [[nodiscard]] HubResult<InstallReceipt> MigrateLegacyInstallation(InstallMutationAuthority& mutation,
                                                                      const InstallTransactionRequest& request);
} // namespace KeireHub::Detail
