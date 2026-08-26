#pragma once

#include "KeireHubRuntime/InstallTransaction.h"

#include <filesystem>
#include <optional>
#include <string>

namespace KeireHub::Detail
{
    class InstallMutationAuthority;

    inline constexpr const char* InstallTransactionLocatorSuffix = ".__keire-install-transaction.locator.json";
    inline constexpr const char* InstallTransactionRootPrefix = ".__keire-install-transaction-";

    struct InstallTransactionLocator final
    {
        std::string TransactionId;
        InstallProduct Product = InstallProduct::Editor;
        std::filesystem::path DestinationRoot;
        std::filesystem::path LocatorPath;
        std::filesystem::path TransactionRoot;
        InstallOwnedFile LocatorFile;
        std::string DocumentSha256;
    };

    [[nodiscard]] std::string SecureInstallRandomId();
    [[nodiscard]] std::filesystem::path InstallTransactionLocatorPath(const std::filesystem::path& destination);
    [[nodiscard]] HubResult<InstallTransactionLocator>
    CreateInstallTransactionLocator(InstallMutationAuthority& mutation, const std::filesystem::path& destination,
                                    InstallProduct product);
    [[nodiscard]] HubResult<std::optional<InstallTransactionLocator>>
    ReadInstallTransactionLocator(InstallMutationAuthority& mutation, const std::filesystem::path& destination,
                                  InstallProduct product);
    [[nodiscard]] HubStatus RemoveInstallTransactionLocator(InstallMutationAuthority& mutation,
                                                            const InstallTransactionLocator& locator);
} // namespace KeireHub::Detail
