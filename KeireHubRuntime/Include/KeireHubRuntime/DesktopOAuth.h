#pragma once

#include "KeireHubRuntime/HubError.h"
#include "KeireHubRuntime/NativeHttpTransport.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>

namespace KeireHub
{
    struct DesktopOAuthOptions final
    {
        std::string SupabaseProjectUrl;
        std::string ClientId;
        std::string WebsiteCallbackUrl;
        std::string Scope = "openid email profile";
    };

    struct DesktopOAuthAuthorization final
    {
        std::string AuthorizationUrl;
        std::string State;
        std::string Nonce;
        std::string CodeVerifier;
    };

    struct DesktopOAuthCallback final
    {
        std::string Code;
        std::string State;
    };

    struct DesktopOAuthTokens final
    {
        std::string AccessToken;
        std::string RefreshToken;
        std::string IdToken;
        std::string TokenType;
        std::uint64_t ExpiresInSeconds = 0;
    };

    using DesktopOAuthEntropy = std::function<bool(std::span<std::byte>)>;
    using DesktopOAuthTransport = std::function<HubResult<NativeHttpResponse>(const NativeHttpRequest&)>;

    class DesktopOAuthClient final
    {
      public:
        [[nodiscard]] static HubResult<DesktopOAuthClient> Create(DesktopOAuthOptions options,
                                                                  DesktopOAuthTransport transport);

        [[nodiscard]] HubResult<DesktopOAuthAuthorization> BeginAuthorization(const DesktopOAuthEntropy& entropy) const;
        [[nodiscard]] HubResult<DesktopOAuthCallback> ValidateCallback(std::string_view callbackUrl,
                                                                       std::string_view expectedState) const;
        [[nodiscard]] HubResult<DesktopOAuthTokens> Exchange(const DesktopOAuthAuthorization& authorization,
                                                             const DesktopOAuthCallback& callback) const;
        [[nodiscard]] HubResult<DesktopOAuthTokens> Refresh(std::string_view refreshToken) const;

      private:
        DesktopOAuthClient(DesktopOAuthOptions options, DesktopOAuthTransport transport);

        DesktopOAuthOptions m_Options;
        DesktopOAuthTransport m_Transport;
    };
} // namespace KeireHub
