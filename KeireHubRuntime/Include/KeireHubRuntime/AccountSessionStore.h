#pragma once

#include "KeireHubRuntime/HubError.h"
#include "KeireHubRuntime/SupabaseAccount.h"

#include <filesystem>
#include <optional>
#include <string>

namespace KeireHub
{
    struct AccountSessionBinding final
    {
        std::string Provider;
        std::string Origin;
        std::string ClientId;

        bool operator==(const AccountSessionBinding&) const = default;
    };

    struct StoredAccountSession final
    {
        std::string RefreshToken;
        std::optional<AccountSessionKind> Kind;
        std::optional<AccountSessionBinding> Binding;
    };

    class AccountSessionStore final
    {
      public:
        explicit AccountSessionStore(std::filesystem::path path);

        [[nodiscard]] bool PersistentStorageAvailable() const noexcept;
        [[nodiscard]] HubResult<std::optional<StoredAccountSession>> LoadSession() const;
        [[nodiscard]] HubStatus SaveSession(AccountSessionKind kind, std::string_view refreshToken) const;
        [[nodiscard]] HubStatus SaveSession(AccountSessionKind kind, std::string_view refreshToken,
                                            const AccountSessionBinding& binding) const;
        [[nodiscard]] HubResult<std::optional<std::string>> LoadRefreshToken() const;
        [[nodiscard]] HubStatus SaveRefreshToken(std::string_view refreshToken) const;
        [[nodiscard]] HubStatus Clear() const;
        [[nodiscard]] const std::filesystem::path& Path() const noexcept;

      private:
        [[nodiscard]] HubResult<std::optional<std::string>> LoadStoredPayload() const;
        [[nodiscard]] HubStatus SaveStoredPayload(std::string_view payload) const;

        std::filesystem::path m_Path;
    };
} // namespace KeireHub
