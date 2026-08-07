#include "KeireHubRuntime/SupabaseAccount.h"

#include "NativeHttpTransportPolicy.h"
#include "Persistence.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <exception>
#include <ranges>
#include <string_view>
#include <utility>

namespace KeireHub
{
    namespace
    {
        constexpr std::size_t MaximumConfigurationBytes = 16U * 1024U;
        constexpr std::size_t MaximumResponseBytes = 1024U * 1024U;

        void ClearSecret(std::string& value) noexcept
        {
            auto* bytes = reinterpret_cast<volatile unsigned char*>(value.data());
            for (std::size_t index = 0; index < value.size(); ++index)
                bytes[index] = 0U;
            value.clear();
        }

        void ClearSecret(std::vector<std::byte>& value) noexcept
        {
            auto* bytes = reinterpret_cast<volatile unsigned char*>(value.data());
            for (std::size_t index = 0; index < value.size(); ++index)
                bytes[index] = 0U;
            value.clear();
        }

        class SecretTextGuard final
        {
          public:
            explicit SecretTextGuard(std::string& value) noexcept : m_Value(value) {}
            ~SecretTextGuard() { ClearSecret(m_Value); }

            SecretTextGuard(const SecretTextGuard&) = delete;
            SecretTextGuard& operator=(const SecretTextGuard&) = delete;

          private:
            std::string& m_Value;
        };

        class SecretBodyGuard final
        {
          public:
            explicit SecretBodyGuard(std::vector<std::byte>& value) noexcept : m_Value(value) {}
            ~SecretBodyGuard() { ClearSecret(m_Value); }

            SecretBodyGuard(const SecretBodyGuard&) = delete;
            SecretBodyGuard& operator=(const SecretBodyGuard&) = delete;

          private:
            std::vector<std::byte>& m_Value;
        };

        [[nodiscard]] HubError ConfigurationError(const std::string_view message, const std::string_view details = {})
        {
            return {.Code = HubErrorCode::AccountConfigurationInvalid,
                    .Message = std::string(message),
                    .AffectedItem = "supabase-account",
                    .TechnicalDetails = std::string(details)};
        }

        [[nodiscard]] HubError AccountError(const HubErrorCode code, const std::string_view message,
                                            const bool retryable = false, const std::string_view details = {})
        {
            return {.Code = code,
                    .Message = std::string(message),
                    .Retryable = retryable,
                    .AffectedItem = "supabase-account",
                    .TechnicalDetails = std::string(details)};
        }

        [[nodiscard]] bool IsBoundedAscii(const std::string_view value, const std::size_t maximum) noexcept
        {
            return !value.empty() && value.size() <= maximum &&
                   std::ranges::all_of(value, [](const unsigned char character)
                                       { return character >= 0x21U && character <= 0x7eU; });
        }

        [[nodiscard]] HubStatus ValidateCredentials(const std::string_view email, const std::string_view password)
        {
            if (email.empty() || email.size() > 254U || email.find('@') == std::string_view::npos ||
                std::ranges::any_of(email, [](const unsigned char value) { return value <= 0x20U || value == 0x7fU; }))
            {
                return HubStatus::Failure(AccountError(HubErrorCode::InvalidArgument, "Enter a valid email address."));
            }
            if (password.size() < 8U || password.size() > 1024U)
            {
                return HubStatus::Failure(
                    AccountError(HubErrorCode::InvalidArgument, "Password must contain at least 8 characters."));
            }
            return HubStatus::Success();
        }

        [[nodiscard]] std::vector<std::byte> JsonBody(const Detail::Json& value)
        {
            const auto text = value.dump();
            const auto bytes = std::as_bytes(std::span(text.data(), text.size()));
            return {bytes.begin(), bytes.end()};
        }

        [[nodiscard]] HubResult<Detail::Json> ParseResponse(const NativeHttpResponse& response)
        {
            try
            {
                const auto* data = reinterpret_cast<const char*>(response.Body.data());
                return HubResult<Detail::Json>::Success(
                    Detail::Json::parse(std::string_view(data, response.Body.size())));
            }
            catch (const std::exception& error)
            {
                return HubResult<Detail::Json>::Failure(
                    AccountError(HubErrorCode::AccountSessionInvalid,
                                 "The account service returned an invalid response.", false, error.what()));
            }
        }

        [[nodiscard]] std::string ResponseDiagnostic(const NativeHttpResponse& response)
        {
            auto parsed = ParseResponse(response);
            if (!parsed || !parsed.Value().is_object())
                return "HTTP status " + std::to_string(response.StatusCode);
            for (const auto field : {"error_code", "code", "msg", "message", "error_description"})
            {
                const auto iterator = parsed.Value().find(field);
                if (iterator != parsed.Value().end() && iterator->is_string())
                {
                    auto result = iterator->get<std::string>();
                    if (result.size() > 512U)
                        result.resize(512U);
                    return "HTTP status " + std::to_string(response.StatusCode) + "; " + field + ": " + result;
                }
            }
            return "HTTP status " + std::to_string(response.StatusCode);
        }

        [[nodiscard]] HubError ResponseError(const NativeHttpResponse& response, const std::string_view action)
        {
            if (response.StatusCode == 400U || response.StatusCode == 401U || response.StatusCode == 422U)
            {
                return AccountError(action == "profile" ? HubErrorCode::AccountProfileInvalid
                                                        : HubErrorCode::AccountAuthenticationFailed,
                                    action == "sign-in" ? "The email or password was not accepted."
                                                        : "The account details were not accepted.",
                                    false, ResponseDiagnostic(response));
            }
            if (response.StatusCode == 403U)
            {
                return AccountError(action == "profile" ? HubErrorCode::AccountProfileInvalid
                                                        : HubErrorCode::AccountAuthenticationFailed,
                                    "This account is not permitted to perform that action.", false,
                                    ResponseDiagnostic(response));
            }
            if (response.StatusCode == 409U)
            {
                return AccountError(HubErrorCode::AccountAuthenticationFailed, "That account already exists.", false,
                                    ResponseDiagnostic(response));
            }
            if (response.StatusCode == 429U && action == "sign-up")
            {
                return AccountError(HubErrorCode::AccountTransportFailed,
                                    "A confirmation email was requested too recently. Check your inbox or wait before "
                                    "trying again.",
                                    true, ResponseDiagnostic(response));
            }
            if (response.StatusCode == 429U || response.StatusCode >= 500U)
            {
                return AccountError(HubErrorCode::AccountTransportFailed,
                                    "The account service is temporarily unavailable.", true,
                                    ResponseDiagnostic(response));
            }
            return AccountError(action == "profile" ? HubErrorCode::AccountProfileInvalid
                                                    : HubErrorCode::AccountAuthenticationFailed,
                                "The account service rejected the request.", false, ResponseDiagnostic(response));
        }

        [[nodiscard]] HubResult<AccountUser> ParseUser(const Detail::Json& value)
        {
            try
            {
                AccountUser user;
                user.Id = value.at("id").get<std::string>();
                user.Email = value.value("email", "");
                user.EmailConfirmed = value.contains("email_confirmed_at") && !value.at("email_confirmed_at").is_null();
                if (user.Id.size() != 36U || user.Email.size() > 254U)
                    throw std::invalid_argument("invalid account user fields");
                return HubResult<AccountUser>::Success(std::move(user));
            }
            catch (const std::exception& error)
            {
                return HubResult<AccountUser>::Failure(AccountError(HubErrorCode::AccountSessionInvalid,
                                                                    "The account service returned invalid user data.",
                                                                    false, error.what()));
            }
        }

        [[nodiscard]] HubResult<AccountSession> ParseSession(const Detail::Json& value)
        {
            try
            {
                auto user = ParseUser(value.at("user"));
                if (!user)
                    return HubResult<AccountSession>::Failure(user.Error());
                const auto accessToken = value.at("access_token").get<std::string>();
                const auto refreshToken = value.at("refresh_token").get<std::string>();
                const auto expiresIn = value.at("expires_in").get<std::uint64_t>();
                if (!IsBoundedAscii(accessToken, 16U * 1024U) || !IsBoundedAscii(refreshToken, 4096U) ||
                    expiresIn == 0U || expiresIn > 7U * 24U * 60U * 60U)
                {
                    throw std::invalid_argument("invalid account session fields");
                }
                const auto now = std::chrono::duration_cast<std::chrono::seconds>(
                                     std::chrono::system_clock::now().time_since_epoch())
                                     .count();
                return HubResult<AccountSession>::Success(
                    {.AccessToken = accessToken,
                     .RefreshToken = refreshToken,
                     .ExpiresAtUnixSeconds = static_cast<std::uint64_t>(now) + expiresIn,
                     .User = std::move(user).Value()});
            }
            catch (const std::exception& error)
            {
                return HubResult<AccountSession>::Failure(
                    AccountError(HubErrorCode::AccountSessionInvalid,
                                 "The account service returned an invalid session.", false, error.what()));
            }
        }

        [[nodiscard]] NativeHttpRequest Request(const SupabaseConfiguration& configuration,
                                                const NativeHttpMethod method, const std::string_view path,
                                                const std::optional<std::string_view> accessToken,
                                                std::vector<std::byte> body = {})
        {
            NativeHttpRequest result{
                .Method = method,
                .Url = configuration.ProjectUrl + std::string(path),
                .Headers = {{"apikey", configuration.PublishableKey}, {"Accept", "application/json"}},
                .Body = std::move(body),
                .MaximumResponseBytes = MaximumResponseBytes};
            if (method == NativeHttpMethod::Post || method == NativeHttpMethod::Patch)
                result.Headers.push_back({"Content-Type", "application/json"});
            if (accessToken)
                result.Headers.push_back({"Authorization", "Bearer " + std::string(*accessToken)});
            return result;
        }

        [[nodiscard]] HubResult<NativeHttpResponse> Send(const AccountTransport& transport,
                                                         const NativeHttpRequest& request)
        {
            auto response = transport(request);
            if (!response)
            {
                auto error = response.Error();
                error.Code = HubErrorCode::AccountTransportFailed;
                error.Message = "The account service could not be reached.";
                error.AffectedItem = "supabase-account";
                return HubResult<NativeHttpResponse>::Failure(std::move(error));
            }
            return response;
        }
    } // namespace

    HubResult<SupabaseConfiguration> LoadSupabaseConfiguration(const std::filesystem::path& path)
    {
        std::error_code error;
        const auto status = std::filesystem::symlink_status(path, error);
        if (status.type() == std::filesystem::file_type::not_found ||
            error == std::make_error_code(std::errc::no_such_file_or_directory))
        {
            return HubResult<SupabaseConfiguration>::Success({});
        }
        if (error || !std::filesystem::is_regular_file(status) || std::filesystem::is_symlink(status))
        {
            return HubResult<SupabaseConfiguration>::Failure(
                ConfigurationError("The packaged account configuration is missing or unsafe.", error.message()));
        }
        auto value = Detail::ReadJsonFile(path, MaximumConfigurationBytes);
        if (!value)
            return HubResult<SupabaseConfiguration>::Failure(value.Error());
        try
        {
            if (!value.Value().is_object() ||
                value.Value().at("schemaVersion").get<std::uint32_t>() != SupabaseConfiguration::CurrentSchemaVersion ||
                !value.Value().at("enabled").is_boolean())
            {
                throw std::invalid_argument("invalid Supabase configuration header");
            }
            SupabaseConfiguration result;
            result.Enabled = value.Value().at("enabled").get<bool>();
            if (!result.Enabled)
            {
                if (value.Value().size() != 2U)
                    throw std::invalid_argument("disabled Supabase configuration contains account fields");
                return HubResult<SupabaseConfiguration>::Success(std::move(result));
            }
            if (value.Value().size() != 4U)
                throw std::invalid_argument("enabled Supabase configuration is incomplete");
            result.ProjectUrl = value.Value().at("projectUrl").get<std::string>();
            result.PublishableKey = value.Value().at("publishableKey").get<std::string>();
            auto parsed = Detail::ParseHttpUrl(result.ProjectUrl, false);
            if (!parsed || !parsed.Value().Secure || parsed.Value().Target != "/" || result.ProjectUrl.ends_with('/') ||
                !parsed.Value().Host.ends_with(".supabase.co") ||
                (parsed.Value().Port && *parsed.Value().Port != 443U) ||
                !result.PublishableKey.starts_with("sb_publishable_") || result.PublishableKey.size() < 32U ||
                !IsBoundedAscii(result.PublishableKey, 256U))
            {
                throw std::invalid_argument("invalid Supabase project URL or publishable key");
            }
            return HubResult<SupabaseConfiguration>::Success(std::move(result));
        }
        catch (const std::exception& exception)
        {
            return HubResult<SupabaseConfiguration>::Failure(
                ConfigurationError("The packaged account configuration is invalid.", exception.what()));
        }
    }

    HubResult<SupabaseAccountClient> SupabaseAccountClient::Create(SupabaseConfiguration configuration,
                                                                   AccountTransport transport)
    {
        if (!configuration.Enabled || configuration.ProjectUrl.empty() || configuration.PublishableKey.empty() ||
            !transport)
        {
            return HubResult<SupabaseAccountClient>::Failure(
                ConfigurationError("Accounts are not configured in this Hub package."));
        }
        return HubResult<SupabaseAccountClient>::Success(
            SupabaseAccountClient(std::move(configuration), std::move(transport)));
    }

    HubResult<AccountSignUpResult> SupabaseAccountClient::SignUp(std::string email, std::string password) const
    {
        if (auto status = ValidateCredentials(email, password); !status)
            return HubResult<AccountSignUpResult>::Failure(status.Error());
        SecretTextGuard passwordGuard(password);
        auto request = Request(m_Configuration, NativeHttpMethod::Post, "/auth/v1/signup", std::nullopt,
                               JsonBody({{"email", std::move(email)}, {"password", password}}));
        SecretBodyGuard bodyGuard(request.Body);
        auto response = Send(m_Transport, request);
        if (!response)
            return HubResult<AccountSignUpResult>::Failure(response.Error());
        if (response.Value().StatusCode < 200U || response.Value().StatusCode >= 300U)
            return HubResult<AccountSignUpResult>::Failure(ResponseError(response.Value(), "sign-up"));
        auto value = ParseResponse(response.Value());
        if (!value)
            return HubResult<AccountSignUpResult>::Failure(value.Error());
        try
        {
            const auto& payload = value.Value();
            if (!payload.is_object())
                throw std::invalid_argument("sign-up response is not an object");
            const auto userValue = payload.find("user");
            auto user = ParseUser(userValue == payload.end() ? payload : *userValue);
            if (!user)
                return HubResult<AccountSignUpResult>::Failure(user.Error());
            AccountSignUpResult result{.User = user.Value()};
            if (value.Value().contains("access_token") && !value.Value().at("access_token").is_null())
            {
                auto session = ParseSession(value.Value());
                if (!session)
                    return HubResult<AccountSignUpResult>::Failure(session.Error());
                result.Session = std::move(session).Value();
            }
            result.ConfirmationRequired = !result.Session.has_value();
            return HubResult<AccountSignUpResult>::Success(std::move(result));
        }
        catch (const std::exception& error)
        {
            return HubResult<AccountSignUpResult>::Failure(
                AccountError(HubErrorCode::AccountSessionInvalid, "The account service returned invalid sign-up data.",
                             false, error.what()));
        }
    }

    HubResult<AccountSession> SupabaseAccountClient::SignIn(std::string email, std::string password) const
    {
        if (auto status = ValidateCredentials(email, password); !status)
            return HubResult<AccountSession>::Failure(status.Error());
        SecretTextGuard passwordGuard(password);
        auto request = Request(m_Configuration, NativeHttpMethod::Post, "/auth/v1/token?grant_type=password",
                               std::nullopt, JsonBody({{"email", std::move(email)}, {"password", password}}));
        SecretBodyGuard bodyGuard(request.Body);
        auto response = Send(m_Transport, request);
        if (!response)
            return HubResult<AccountSession>::Failure(response.Error());
        if (response.Value().StatusCode < 200U || response.Value().StatusCode >= 300U)
            return HubResult<AccountSession>::Failure(ResponseError(response.Value(), "sign-in"));
        auto value = ParseResponse(response.Value());
        if (!value)
            return HubResult<AccountSession>::Failure(value.Error());
        return ParseSession(value.Value());
    }

    HubResult<AccountSession> SupabaseAccountClient::Refresh(std::string refreshToken) const
    {
        if (!IsBoundedAscii(refreshToken, 4096U))
            return HubResult<AccountSession>::Failure(
                AccountError(HubErrorCode::InvalidArgument, "The saved account session is invalid."));
        SecretTextGuard refreshGuard(refreshToken);
        auto request = Request(m_Configuration, NativeHttpMethod::Post, "/auth/v1/token?grant_type=refresh_token",
                               std::nullopt, JsonBody({{"refresh_token", refreshToken}}));
        SecretBodyGuard bodyGuard(request.Body);
        auto response = Send(m_Transport, request);
        if (!response)
            return HubResult<AccountSession>::Failure(response.Error());
        if (response.Value().StatusCode < 200U || response.Value().StatusCode >= 300U)
            return HubResult<AccountSession>::Failure(ResponseError(response.Value(), "refresh"));
        auto value = ParseResponse(response.Value());
        if (!value)
            return HubResult<AccountSession>::Failure(value.Error());
        return ParseSession(value.Value());
    }

    HubStatus SupabaseAccountClient::SignOut(const std::string_view accessToken) const
    {
        if (!IsBoundedAscii(accessToken, 16U * 1024U))
            return HubStatus::Failure(AccountError(HubErrorCode::InvalidArgument, "The account session is invalid."));
        auto response = Send(
            m_Transport, Request(m_Configuration, NativeHttpMethod::Post, "/auth/v1/logout?scope=local", accessToken));
        if (!response)
            return HubStatus::Failure(response.Error());
        if (response.Value().StatusCode != 204U &&
            (response.Value().StatusCode < 200U || response.Value().StatusCode >= 300U))
        {
            return HubStatus::Failure(ResponseError(response.Value(), "sign-out"));
        }
        return HubStatus::Success();
    }

    HubResult<AccountProfile> SupabaseAccountClient::FetchProfile(const AccountSession& session) const
    {
        auto response = Send(m_Transport, Request(m_Configuration, NativeHttpMethod::Get,
                                                  "/rest/v1/profiles?select=display_name,avatar_url&user_id=eq." +
                                                      session.User.Id + "&limit=1",
                                                  session.AccessToken));
        if (!response)
            return HubResult<AccountProfile>::Failure(response.Error());
        if (response.Value().StatusCode < 200U || response.Value().StatusCode >= 300U)
            return HubResult<AccountProfile>::Failure(ResponseError(response.Value(), "profile"));
        auto value = ParseResponse(response.Value());
        if (!value)
            return HubResult<AccountProfile>::Failure(value.Error());
        try
        {
            if (!value.Value().is_array() || value.Value().size() > 1U)
                throw std::invalid_argument("profile response is not a bounded array");
            if (value.Value().empty())
                return HubResult<AccountProfile>::Success({});
            const auto& profile = value.Value().front();
            const auto text = [&](const std::string_view field)
            {
                const auto iterator = profile.find(field);
                return iterator == profile.end() || iterator->is_null() ? std::string{} : iterator->get<std::string>();
            };
            auto result = AccountProfile{.DisplayName = text("display_name"), .AvatarUrl = text("avatar_url")};
            if (result.DisplayName.size() > 64U || result.AvatarUrl.size() > 2048U)
                throw std::invalid_argument("profile fields exceed their limits");
            return HubResult<AccountProfile>::Success(std::move(result));
        }
        catch (const std::exception& error)
        {
            return HubResult<AccountProfile>::Failure(AccountError(
                HubErrorCode::AccountProfileInvalid, "The account profile response is invalid.", false, error.what()));
        }
    }

    HubResult<AccountProfile> SupabaseAccountClient::SaveProfile(const AccountSession& session,
                                                                 std::string displayName) const
    {
        if (displayName.empty() || displayName.size() > 64U)
            return HubResult<AccountProfile>::Failure(
                AccountError(HubErrorCode::InvalidArgument, "Display name must contain 1 to 64 characters."));
        auto request =
            Request(m_Configuration, NativeHttpMethod::Post, "/rest/v1/profiles?on_conflict=user_id",
                    session.AccessToken, JsonBody({{"user_id", session.User.Id}, {"display_name", displayName}}));
        request.Headers.push_back({"Prefer", "resolution=merge-duplicates,return=representation"});
        auto response = Send(m_Transport, request);
        if (!response)
            return HubResult<AccountProfile>::Failure(response.Error());
        if (response.Value().StatusCode < 200U || response.Value().StatusCode >= 300U)
            return HubResult<AccountProfile>::Failure(ResponseError(response.Value(), "profile"));
        return HubResult<AccountProfile>::Success({.DisplayName = std::move(displayName)});
    }

    SupabaseAccountClient::SupabaseAccountClient(SupabaseConfiguration configuration, AccountTransport transport)
        : m_Configuration(std::move(configuration)), m_Transport(std::move(transport))
    {
    }
} // namespace KeireHub
