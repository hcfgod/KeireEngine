#include "KeireHubRuntime/DesktopOAuth.h"

#include <KeireHubRuntimeInternal/Persistence.h>
#include <KeireHubRuntimeInternal/Sha256.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <exception>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <utility>

namespace KeireHub
{
    namespace
    {
        constexpr std::size_t MaximumResponseBytes = std::size_t{1024U} * 1024U;

        [[nodiscard]] HubError OAuthError(const HubErrorCode code, std::string message, std::string details = {},
                                          const bool retryable = false)
        {
            return {.Code = code,
                    .Message = std::move(message),
                    .Retryable = retryable,
                    .AffectedItem = "hub-oauth",
                    .TechnicalDetails = std::move(details)};
        }

        [[nodiscard]] bool IsSafeHttpsUrl(const std::string_view value, const bool allowQuery = false) noexcept
        {
            if (!value.starts_with("https://") || value.size() > 2048U || value.find_first_of("\r\n#") != value.npos ||
                (!allowQuery && value.find('?') != value.npos))
            {
                return false;
            }
            const auto authority = value.substr(8U, value.find('/', 8U) - 8U);
            return !authority.empty() && authority.find('@') == value.npos;
        }

        [[nodiscard]] bool IsSafeToken(const std::string_view value, const std::size_t minimum,
                                       const std::size_t maximum) noexcept
        {
            return value.size() >= minimum && value.size() <= maximum &&
                   std::ranges::all_of(value, [](const unsigned char character)
                                       { return character >= 0x21U && character <= 0x7eU; });
        }

        [[nodiscard]] std::string FormEncode(const std::string_view value)
        {
            constexpr char Hex[] = "0123456789ABCDEF";
            std::string result;
            result.reserve(value.size());
            for (const auto character : value)
            {
                const auto byte = static_cast<unsigned char>(character);
                if ((byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') || (byte >= '0' && byte <= '9') ||
                    byte == '-' || byte == '_' || byte == '.' || byte == '~')
                {
                    result.push_back(character);
                }
                else
                {
                    result.push_back('%');
                    result.push_back(Hex[(byte >> 4U) & 0x0fU]);
                    result.push_back(Hex[byte & 0x0fU]);
                }
            }
            return result;
        }

        [[nodiscard]] std::string Base64Url(const std::span<const std::byte> bytes)
        {
            constexpr std::string_view Alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
            std::string result;
            result.reserve((bytes.size() * 4U + 2U) / 3U);
            for (std::size_t offset = 0; offset < bytes.size(); offset += 3U)
            {
                const auto first = std::to_integer<unsigned int>(bytes[offset]);
                const auto second = offset + 1U < bytes.size() ? std::to_integer<unsigned int>(bytes[offset + 1U]) : 0U;
                const auto third = offset + 2U < bytes.size() ? std::to_integer<unsigned int>(bytes[offset + 2U]) : 0U;
                const auto packed = (first << 16U) | (second << 8U) | third;
                result.push_back(Alphabet[(packed >> 18U) & 0x3fU]);
                result.push_back(Alphabet[(packed >> 12U) & 0x3fU]);
                if (offset + 1U < bytes.size())
                    result.push_back(Alphabet[(packed >> 6U) & 0x3fU]);
                if (offset + 2U < bytes.size())
                    result.push_back(Alphabet[packed & 0x3fU]);
            }
            return result;
        }

        [[nodiscard]] std::string Sha256Base64Url(const std::string_view value)
        {
            Detail::Sha256Builder builder;
            builder.Update(std::as_bytes(std::span(value.data(), value.size())));
            const auto digest = builder.Finish();
            return Base64Url(digest);
        }

        [[nodiscard]] std::optional<std::vector<std::byte>> DecodeBase64Url(const std::string_view value)
        {
            if (value.empty() || value.size() > std::size_t{16U} * 1024U || value.size() % 4U == 1U)
                return std::nullopt;
            const auto decode = [](const char character) -> std::optional<unsigned int>
            {
                if (character >= 'A' && character <= 'Z')
                    return static_cast<unsigned int>(character - 'A');
                if (character >= 'a' && character <= 'z')
                    return static_cast<unsigned int>(character - 'a' + 26);
                if (character >= '0' && character <= '9')
                    return static_cast<unsigned int>(character - '0' + 52);
                if (character == '-')
                    return 62U;
                if (character == '_')
                    return 63U;
                return std::nullopt;
            };
            std::vector<std::byte> result;
            result.reserve(value.size() * 3U / 4U);
            unsigned int accumulator = 0;
            unsigned int bits = 0;
            for (const auto character : value)
            {
                const auto decoded = decode(character);
                if (!decoded)
                    return std::nullopt;
                accumulator = (accumulator << 6U) | *decoded;
                bits += 6U;
                if (bits >= 8U)
                {
                    bits -= 8U;
                    result.push_back(std::byte{static_cast<unsigned char>((accumulator >> bits) & 0xffU)});
                }
            }
            if (bits != 0U && (accumulator & ((1U << bits) - 1U)) != 0U)
                return std::nullopt;
            return result;
        }

        [[nodiscard]] bool ConstantTimeEqual(const std::string_view left, const std::string_view right) noexcept
        {
            const auto maximum = std::max(left.size(), right.size());
            std::size_t difference = left.size() ^ right.size();
            for (std::size_t index = 0; index < maximum; ++index)
            {
                const auto leftValue = index < left.size() ? static_cast<unsigned char>(left[index]) : 0U;
                const auto rightValue = index < right.size() ? static_cast<unsigned char>(right[index]) : 0U;
                difference |= leftValue ^ rightValue;
            }
            return difference == 0U;
        }

        [[nodiscard]] bool IsBearerTokenType(const std::string_view value) noexcept
        {
            constexpr std::string_view Bearer = "bearer";
            return value.size() == Bearer.size() &&
                   std::ranges::equal(value, Bearer, {}, [](const unsigned char character)
                                      { return static_cast<unsigned char>(std::tolower(character)); });
        }

        [[nodiscard]] std::optional<unsigned int> HexValue(const char value) noexcept
        {
            if (value >= '0' && value <= '9')
                return static_cast<unsigned int>(value - '0');
            if (value >= 'a' && value <= 'f')
                return static_cast<unsigned int>(value - 'a' + 10);
            if (value >= 'A' && value <= 'F')
                return static_cast<unsigned int>(value - 'A' + 10);
            return std::nullopt;
        }

        [[nodiscard]] std::optional<std::string> DecodeQueryValue(const std::string_view value)
        {
            std::string result;
            result.reserve(value.size());
            for (std::size_t index = 0; index < value.size(); ++index)
            {
                if (value[index] == '+')
                    result.push_back(' ');
                else if (value[index] == '%')
                {
                    if (index + 2U >= value.size())
                        return std::nullopt;
                    const auto high = HexValue(value[index + 1U]);
                    const auto low = HexValue(value[index + 2U]);
                    if (!high || !low)
                        return std::nullopt;
                    result.push_back(static_cast<char>((*high << 4U) | *low));
                    index += 2U;
                }
                else
                    result.push_back(value[index]);
            }
            return result;
        }

        [[nodiscard]] std::optional<std::string> QueryParameter(const std::string_view url, const std::string_view name)
        {
            const auto question = url.find('?');
            if (question == url.npos)
                return std::nullopt;
            auto query = url.substr(question + 1U);
            if (const auto fragment = query.find('#'); fragment != query.npos)
                query = query.substr(0U, fragment);
            while (!query.empty())
            {
                const auto separator = query.find('&');
                const auto field = query.substr(0U, separator);
                const auto equals = field.find('=');
                if (equals != field.npos && field.substr(0U, equals) == name)
                    return DecodeQueryValue(field.substr(equals + 1U));
                if (separator == query.npos)
                    break;
                query.remove_prefix(separator + 1U);
            }
            return std::nullopt;
        }

        [[nodiscard]] std::vector<std::byte> Bytes(const std::string_view value)
        {
            const auto bytes = std::as_bytes(std::span(value.data(), value.size()));
            return {bytes.begin(), bytes.end()};
        }

        [[nodiscard]] HubResult<DesktopOAuthTokens> ParseTokens(const NativeHttpResponse& response)
        {
            try
            {
                const auto* data = reinterpret_cast<const char*>(response.Body.data());
                const auto document = Detail::Json::parse(std::string_view(data, response.Body.size()));
                if (response.StatusCode < 200U || response.StatusCode >= 300U)
                {
                    return HubResult<DesktopOAuthTokens>::Failure(
                        OAuthError(response.StatusCode == 400U || response.StatusCode == 401U
                                       ? HubErrorCode::AccountAuthenticationFailed
                                       : HubErrorCode::AccountTransportFailed,
                                   "The Hub authorization could not be completed.",
                                   document.value("error", "oauth_token_error") + ": " +
                                       document.value("error_description", "token exchange rejected"),
                                   response.StatusCode == 429U || response.StatusCode >= 500U));
                }
                DesktopOAuthTokens result;
                result.AccessToken = document.at("access_token").get<std::string>();
                result.RefreshToken = document.at("refresh_token").get<std::string>();
                result.IdToken = document.value("id_token", "");
                result.TokenType = document.at("token_type").get<std::string>();
                result.ExpiresInSeconds = document.at("expires_in").get<std::uint64_t>();
                if (!IsSafeToken(result.AccessToken, 16U, std::size_t{16U} * 1024U) ||
                    !IsSafeToken(result.RefreshToken, 1U, 4096U) ||
                    (!result.IdToken.empty() && !IsSafeToken(result.IdToken, 16U, std::size_t{16U} * 1024U)) ||
                    !IsBearerTokenType(result.TokenType) || result.ExpiresInSeconds == 0U ||
                    result.ExpiresInSeconds > 7ULL * 24ULL * 60ULL * 60ULL)
                {
                    throw std::invalid_argument("invalid OAuth token fields");
                }
                result.TokenType = "bearer";
                return HubResult<DesktopOAuthTokens>::Success(std::move(result));
            }
            catch (const std::exception& error)
            {
                return HubResult<DesktopOAuthTokens>::Failure(
                    OAuthError(HubErrorCode::AccountSessionInvalid,
                               "The authorization service returned invalid tokens.", error.what()));
            }
        }

        [[nodiscard]] HubResult<DesktopOAuthTokens> SendTokenRequest(const DesktopOAuthTransport& transport,
                                                                     const std::string& tokenEndpoint,
                                                                     const std::string_view form)
        {
            auto response = transport(
                {.Method = NativeHttpMethod::Post,
                 .Url = tokenEndpoint,
                 .Headers = {{"Accept", "application/json"}, {"Content-Type", "application/x-www-form-urlencoded"}},
                 .Body = Bytes(form),
                 .MaximumResponseBytes = MaximumResponseBytes});
            if (!response)
            {
                auto error = response.Error();
                error.Code = HubErrorCode::AccountTransportFailed;
                error.Message = "The Hub authorization service could not be reached.";
                error.AffectedItem = "hub-oauth";
                return HubResult<DesktopOAuthTokens>::Failure(std::move(error));
            }
            return ParseTokens(response.Value());
        }

        [[nodiscard]] HubStatus ValidateIdTokenNonce(const std::string_view idToken,
                                                     const std::string_view expectedNonce)
        {
            try
            {
                const auto first = idToken.find('.');
                const auto second = first == idToken.npos ? idToken.npos : idToken.find('.', first + 1U);
                if (first == idToken.npos || second == idToken.npos || idToken.find('.', second + 1U) != idToken.npos)
                    throw std::invalid_argument("invalid ID token structure");
                const auto payload = DecodeBase64Url(idToken.substr(first + 1U, second - first - 1U));
                if (!payload)
                    throw std::invalid_argument("invalid ID token encoding");
                const auto* data = reinterpret_cast<const char*>(payload->data());
                const auto document = Detail::Json::parse(std::string_view(data, payload->size()));
                const auto nonce = document.at("nonce").get<std::string>();
                if (!IsSafeToken(nonce, 32U, 128U) || !ConstantTimeEqual(nonce, expectedNonce))
                    throw std::invalid_argument("ID token nonce mismatch");
                return HubStatus::Success();
            }
            catch (const std::exception& error)
            {
                return HubStatus::Failure(OAuthError(HubErrorCode::AccountAuthenticationFailed,
                                                     "The Hub authorization response could not be verified.",
                                                     error.what()));
            }
        }
    } // namespace

    DesktopOAuthClient::DesktopOAuthClient(DesktopOAuthOptions options, DesktopOAuthTransport transport)
        : m_Options(std::move(options)), m_Transport(std::move(transport))
    {
    }

    HubResult<DesktopOAuthClient> DesktopOAuthClient::Create(DesktopOAuthOptions options,
                                                             DesktopOAuthTransport transport)
    {
        if (!IsSafeHttpsUrl(options.SupabaseProjectUrl) || options.SupabaseProjectUrl.ends_with('/') ||
            !IsSafeHttpsUrl(options.WebsiteCallbackUrl, true) || !IsSafeToken(options.ClientId, 8U, 128U) ||
            options.Scope.empty() || options.Scope.size() > 128U || !transport)
        {
            return HubResult<DesktopOAuthClient>::Failure(
                OAuthError(HubErrorCode::AccountConfigurationInvalid, "The Hub OAuth configuration is invalid."));
        }
        return HubResult<DesktopOAuthClient>::Success(DesktopOAuthClient(std::move(options), std::move(transport)));
    }

    HubResult<DesktopOAuthAuthorization>
    DesktopOAuthClient::BeginAuthorization(const DesktopOAuthEntropy& entropy) const
    {
        std::array<std::byte, 64> verifierBytes{};
        std::array<std::byte, 32> stateBytes{};
        std::array<std::byte, 32> nonceBytes{};
        if (!entropy || !entropy(verifierBytes) || !entropy(stateBytes) || !entropy(nonceBytes))
        {
            return HubResult<DesktopOAuthAuthorization>::Failure(
                OAuthError(HubErrorCode::AccountSessionInvalid, "Secure random data for Hub sign-in is unavailable."));
        }
        DesktopOAuthAuthorization result;
        result.CodeVerifier = Base64Url(verifierBytes);
        result.State = Base64Url(stateBytes);
        result.Nonce = Base64Url(nonceBytes);
        result.AuthorizationUrl =
            m_Options.SupabaseProjectUrl +
            "/auth/v1/oauth/authorize?response_type=code&client_id=" + FormEncode(m_Options.ClientId) +
            "&redirect_uri=" + FormEncode(m_Options.WebsiteCallbackUrl) + "&scope=" + FormEncode(m_Options.Scope) +
            "&code_challenge=" + FormEncode(Sha256Base64Url(result.CodeVerifier)) +
            "&code_challenge_method=S256&state=" + FormEncode(result.State) + "&nonce=" + FormEncode(result.Nonce);
        return HubResult<DesktopOAuthAuthorization>::Success(std::move(result));
    }

    HubResult<DesktopOAuthCallback> DesktopOAuthClient::ValidateCallback(const std::string_view callbackUrl,
                                                                         const std::string_view expectedState) const
    {
        if (!callbackUrl.starts_with("keirehub://oauth/callback?") || callbackUrl.size() > 4096U ||
            !IsSafeToken(expectedState, 32U, 128U))
        {
            return HubResult<DesktopOAuthCallback>::Failure(
                OAuthError(HubErrorCode::InvalidArgument, "The Hub OAuth callback is invalid."));
        }
        const auto error = QueryParameter(callbackUrl, "error");
        if (error)
        {
            return HubResult<DesktopOAuthCallback>::Failure(
                OAuthError(HubErrorCode::AccountAuthenticationFailed, "Hub sign-in was cancelled or denied.", *error));
        }
        auto code = QueryParameter(callbackUrl, "code");
        auto state = QueryParameter(callbackUrl, "state");
        if (!code || !state || !IsSafeToken(*code, 16U, 2048U) || !IsSafeToken(*state, 32U, 128U) ||
            !ConstantTimeEqual(*state, expectedState))
        {
            return HubResult<DesktopOAuthCallback>::Failure(
                OAuthError(HubErrorCode::AccountAuthenticationFailed,
                           "The Hub OAuth callback did not match the sign-in request."));
        }
        return HubResult<DesktopOAuthCallback>::Success({.Code = std::move(*code), .State = std::move(*state)});
    }

    HubResult<DesktopOAuthTokens> DesktopOAuthClient::Exchange(const DesktopOAuthAuthorization& authorization,
                                                               const DesktopOAuthCallback& callback) const
    {
        if (!ConstantTimeEqual(authorization.State, callback.State) ||
            !IsSafeToken(authorization.CodeVerifier, 43U, 128U) || !IsSafeToken(callback.Code, 16U, 2048U))
        {
            return HubResult<DesktopOAuthTokens>::Failure(
                OAuthError(HubErrorCode::AccountAuthenticationFailed, "The Hub OAuth exchange request is invalid."));
        }
        const auto form = "grant_type=authorization_code&code=" + FormEncode(callback.Code) +
                          "&client_id=" + FormEncode(m_Options.ClientId) +
                          "&redirect_uri=" + FormEncode(m_Options.WebsiteCallbackUrl) +
                          "&code_verifier=" + FormEncode(authorization.CodeVerifier);
        auto tokens = SendTokenRequest(m_Transport, m_Options.SupabaseProjectUrl + "/auth/v1/oauth/token", form);
        if (!tokens)
            return tokens;
        if (const auto nonce = ValidateIdTokenNonce(tokens.Value().IdToken, authorization.Nonce); !nonce)
            return HubResult<DesktopOAuthTokens>::Failure(nonce.Error());
        return tokens;
    }

    HubResult<DesktopOAuthTokens> DesktopOAuthClient::Refresh(const std::string_view refreshToken) const
    {
        if (!IsSafeToken(refreshToken, 1U, 4096U))
        {
            return HubResult<DesktopOAuthTokens>::Failure(
                OAuthError(HubErrorCode::InvalidArgument, "The Hub OAuth refresh token is invalid."));
        }
        const auto form = "grant_type=refresh_token&refresh_token=" + FormEncode(refreshToken) +
                          "&client_id=" + FormEncode(m_Options.ClientId);
        return SendTokenRequest(m_Transport, m_Options.SupabaseProjectUrl + "/auth/v1/oauth/token", form);
    }
} // namespace KeireHub
