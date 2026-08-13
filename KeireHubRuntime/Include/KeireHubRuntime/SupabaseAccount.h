#pragma once

#include "KeireHubRuntime/HubError.h"
#include "KeireHubRuntime/NativeHttpTransport.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>

namespace KeireHub
{
    enum class AccountSessionKind : std::uint8_t
    {
        SupabaseAuth,
        DesktopOAuth
    };

    struct SupabaseConfiguration final
    {
        static constexpr std::uint32_t MinimumSchemaVersion = 1;
        static constexpr std::uint32_t CurrentSchemaVersion = 2;

        bool Enabled = false;
        bool HubOAuthEnabled = false;
        std::string ProjectUrl;
        std::string PublishableKey;
        std::string HubOAuthClientId;
        std::string HubOAuthWebsiteCallbackUrl;
    };

    struct AccountUser final
    {
        std::string Id;
        std::string Email;
        bool EmailConfirmed = false;
    };

    struct AccountSession final
    {
        std::string AccessToken;
        std::string RefreshToken;
        std::uint64_t ExpiresAtUnixSeconds = 0;
        AccountUser User;
        AccountSessionKind Kind = AccountSessionKind::SupabaseAuth;
    };

    struct AccountSignUpResult final
    {
        AccountUser User;
        std::optional<AccountSession> Session;
        bool ConfirmationRequired = false;
    };

    struct AccountProfile final
    {
        std::string DisplayName;
        std::string AvatarUrl;
    };

    using AccountTransport = std::function<HubResult<NativeHttpResponse>(const NativeHttpRequest&)>;

    [[nodiscard]] HubResult<SupabaseConfiguration> LoadSupabaseConfiguration(const std::filesystem::path& path);

    class SupabaseAccountClient final
    {
      public:
        [[nodiscard]] static HubResult<SupabaseAccountClient> Create(SupabaseConfiguration configuration,
                                                                     AccountTransport transport);

        [[nodiscard]] HubResult<AccountSignUpResult> SignUp(std::string email, std::string password) const;
        [[nodiscard]] HubResult<AccountSession> SignIn(std::string email, std::string password) const;
        [[nodiscard]] HubResult<AccountSession> Refresh(std::string refreshToken) const;
        [[nodiscard]] HubResult<AccountUser> FetchUser(std::string_view accessToken) const;
        [[nodiscard]] HubStatus SignOut(std::string_view accessToken) const;
        [[nodiscard]] HubResult<AccountProfile> FetchProfile(const AccountSession& session) const;
        [[nodiscard]] HubResult<AccountProfile> SaveProfile(const AccountSession& session,
                                                            std::string displayName) const;

      private:
        SupabaseAccountClient(SupabaseConfiguration configuration, AccountTransport transport);

        SupabaseConfiguration m_Configuration;
        AccountTransport m_Transport;
    };
} // namespace KeireHub
