#include "KeireHub/HubAccountWorkflow.h"

#include "KeireHubRuntime/NativeHttpTransport.h"

#include <chrono>
#include <exception>
#include <functional>
#include <utility>

namespace KeireHub
{
    namespace
    {
        [[nodiscard]] HubError BusyError()
        {
            return {.Code = HubErrorCode::InvalidTransition,
                    .Message = "Wait for the current account action to finish.",
                    .AffectedItem = "supabase-account"};
        }

        [[nodiscard]] HubAccountWorkflowSnapshot FailureSnapshot(HubError error, const bool configured,
                                                                 const bool persistent)
        {
            return {.Configured = configured,
                    .PersistentSessionAvailable = persistent,
                    .Message = error.Message,
                    .Failure = std::move(error)};
        }
    } // namespace

    HubAccountWorkflow::HubAccountWorkflow(HubAccountClientFactory clientFactory,
                                           HubOAuthClientFactory oauthClientFactory)
        : m_Snapshot(std::make_shared<const HubAccountWorkflowSnapshot>()), m_ClientFactory(std::move(clientFactory)),
          m_OAuthClientFactory(std::move(oauthClientFactory))
    {
    }

    HubAccountWorkflow::~HubAccountWorkflow() { Stop(); }

    HubStatus HubAccountWorkflow::Start(const std::filesystem::path& configurationPath,
                                        const std::filesystem::path& sessionPath, const HubSettings& settings)
    {
        Stop();
        {
            std::scoped_lock lock(m_Mutex);
            m_Configuration.reset();
            m_Session.reset();
            m_PendingOAuthAuthorization.reset();
            m_NextRefreshAttemptUnixSeconds = 0;
            m_SessionPath = sessionPath;
            m_Settings = settings;
        }
        Publish({.Busy = true});
        m_Worker = std::jthread(
            [this, configurationPath, sessionPath, settings]
            {
                try
                {
                    auto configuration = LoadSupabaseConfiguration(configurationPath);
                    if (!configuration)
                    {
                        Publish(FailureSnapshot(configuration.Error(), false, false));
                        return;
                    }
                    AccountSessionStore store(sessionPath);
                    {
                        std::scoped_lock lock(m_Mutex);
                        m_Configuration = configuration.Value();
                    }
                    if (!configuration.Value().Enabled)
                    {
                        Publish({.Message = "Accounts are not configured in this Hub package."});
                        return;
                    }
                    if (settings.OfflineMode)
                    {
                        Publish({.Configured = true,
                                 .PersistentSessionAvailable = store.PersistentStorageAvailable(),
                                 .Message = "Account sign-in is unavailable while the Hub is offline."});
                        return;
                    }
                    auto saved = store.LoadRefreshToken();
                    if (!saved)
                    {
                        Publish(FailureSnapshot(saved.Error(), true, store.PersistentStorageAvailable()));
                        return;
                    }
                    if (!saved.Value())
                    {
                        Publish({.Configured = true,
                                 .PersistentSessionAvailable = store.PersistentStorageAvailable(),
                                 .Message = store.PersistentStorageAvailable()
                                                ? "Sign in to synchronize your Kéire profile."
                                                : "Sign in is available for this session; secure persistence is not "
                                                  "available on this platform."});
                        return;
                    }
                    auto client = CreateClient();
                    if (!client)
                    {
                        Publish(FailureSnapshot(client.Error(), true, store.PersistentStorageAvailable()));
                        return;
                    }
                    auto session = client.Value().Refresh(std::move(*saved.Value()));
                    if (!session)
                    {
                        if (!session.Error().Retryable)
                            (void)store.Clear();
                        Publish(FailureSnapshot(session.Error(), true, store.PersistentStorageAvailable()));
                        return;
                    }
                    auto persisted = store.SaveRefreshToken(session.Value().RefreshToken);
                    if (!persisted)
                    {
                        Publish(FailureSnapshot(persisted.Error(), true, store.PersistentStorageAvailable()));
                        return;
                    }
                    auto profile = client.Value().FetchProfile(session.Value());
                    PublishSession(std::move(session).Value(), profile ? std::move(profile).Value() : AccountProfile{},
                                   "Signed in.");
                }
                catch (const std::exception& error)
                {
                    Publish(FailureSnapshot({.Code = HubErrorCode::AccountSessionInvalid,
                                             .Message = "The account service failed unexpectedly.",
                                             .Retryable = true,
                                             .AffectedItem = "supabase-account",
                                             .TechnicalDetails = error.what()},
                                            true, AccountSessionStore(sessionPath).PersistentStorageAvailable()));
                }
            });
        return HubStatus::Success();
    }

    HubStatus HubAccountWorkflow::SignIn(std::string email, std::string password)
    {
        return Begin(
            [this, email = std::move(email), password = std::move(password)]() mutable
            {
                auto client = CreateClient();
                if (!client)
                {
                    Publish(FailureSnapshot(client.Error(), true,
                                            AccountSessionStore(m_SessionPath).PersistentStorageAvailable()));
                    return;
                }
                auto session = client.Value().SignIn(std::move(email), std::move(password));
                password.clear();
                if (!session)
                {
                    Publish(FailureSnapshot(session.Error(), true,
                                            AccountSessionStore(m_SessionPath).PersistentStorageAvailable()));
                    return;
                }
                AccountSessionStore store(m_SessionPath);
                auto persisted = store.SaveRefreshToken(session.Value().RefreshToken);
                if (!persisted)
                {
                    Publish(FailureSnapshot(persisted.Error(), true, store.PersistentStorageAvailable()));
                    return;
                }
                auto profile = client.Value().FetchProfile(session.Value());
                PublishSession(std::move(session).Value(), profile ? std::move(profile).Value() : AccountProfile{},
                               "Signed in.");
            });
    }

    HubStatus HubAccountWorkflow::SignUp(std::string email, std::string password)
    {
        return Begin(
            [this, email = std::move(email), password = std::move(password)]() mutable
            {
                auto client = CreateClient();
                if (!client)
                {
                    Publish(FailureSnapshot(client.Error(), true,
                                            AccountSessionStore(m_SessionPath).PersistentStorageAvailable()));
                    return;
                }
                auto result = client.Value().SignUp(std::move(email), std::move(password));
                password.clear();
                if (!result)
                {
                    Publish(FailureSnapshot(result.Error(), true,
                                            AccountSessionStore(m_SessionPath).PersistentStorageAvailable()));
                    return;
                }
                if (!result.Value().Session)
                {
                    Publish(
                        {.Configured = true,
                         .PersistentSessionAvailable = AccountSessionStore(m_SessionPath).PersistentStorageAvailable(),
                         .ConfirmationRequired = true,
                         .Email = result.Value().User.Email,
                         .Message = "Check your email to confirm the account, then sign in."});
                    return;
                }
                AccountSessionStore store(m_SessionPath);
                auto session = std::move(*result.Value().Session);
                auto persisted = store.SaveRefreshToken(session.RefreshToken);
                if (!persisted)
                {
                    Publish(FailureSnapshot(persisted.Error(), true, store.PersistentStorageAvailable()));
                    return;
                }
                PublishSession(std::move(session), {}, "Account created and signed in.");
            });
    }

    HubResult<std::string> HubAccountWorkflow::BeginBrowserSignIn(const DesktopOAuthEntropy& entropy)
    {
        {
            std::scoped_lock lock(m_Mutex);
            if (m_Snapshot->Busy)
                return HubResult<std::string>::Failure(BusyError());
            if (!m_Configuration || !m_Configuration->HubOAuthEnabled)
            {
                return HubResult<std::string>::Failure(
                    {.Code = HubErrorCode::AccountConfigurationInvalid,
                     .Message = "Browser sign-in is not enabled in this Hub package.",
                     .AffectedItem = "hub-oauth"});
            }
        }
        auto client = CreateOAuthClient();
        if (!client)
            return HubResult<std::string>::Failure(client.Error());
        auto authorization = client.Value().BeginAuthorization(entropy);
        if (!authorization)
            return HubResult<std::string>::Failure(authorization.Error());
        auto url = authorization.Value().AuthorizationUrl;
        {
            std::scoped_lock lock(m_Mutex);
            m_PendingOAuthAuthorization = std::move(authorization).Value();
            auto snapshot = *m_Snapshot;
            snapshot.BrowserSignInPending = true;
            snapshot.Message = "Complete sign-in in your browser, then return to the Hub.";
            snapshot.Failure.reset();
            m_Snapshot = std::make_shared<const HubAccountWorkflowSnapshot>(std::move(snapshot));
        }
        return HubResult<std::string>::Success(std::move(url));
    }

    HubStatus HubAccountWorkflow::CancelBrowserSignIn()
    {
        std::scoped_lock lock(m_Mutex);
        if (m_Snapshot->Busy)
            return HubStatus::Failure(BusyError());
        if (!m_PendingOAuthAuthorization)
        {
            return HubStatus::Failure({.Code = HubErrorCode::InvalidTransition,
                                       .Message = "No browser sign-in is waiting to be completed.",
                                       .AffectedItem = "hub-oauth"});
        }

        m_PendingOAuthAuthorization.reset();
        auto snapshot = *m_Snapshot;
        snapshot.BrowserSignInPending = false;
        snapshot.Message = "Browser sign-in cancelled. No Hub session was created.";
        snapshot.Failure.reset();
        m_Snapshot = std::make_shared<const HubAccountWorkflowSnapshot>(std::move(snapshot));
        return HubStatus::Success();
    }

    HubStatus HubAccountWorkflow::CompleteBrowserSignIn(std::string callbackUrl)
    {
        std::optional<DesktopOAuthAuthorization> authorization;
        {
            std::scoped_lock lock(m_Mutex);
            if (!m_PendingOAuthAuthorization)
            {
                return HubStatus::Failure({.Code = HubErrorCode::InvalidTransition,
                                           .Message = "Start browser sign-in from this Hub before using a callback.",
                                           .AffectedItem = "hub-oauth"});
            }
            authorization = m_PendingOAuthAuthorization;
        }
        auto oauth = CreateOAuthClient();
        if (!oauth)
            return HubStatus::Failure(oauth.Error());
        auto callback = oauth.Value().ValidateCallback(callbackUrl, authorization->State);
        if (!callback)
            return HubStatus::Failure(callback.Error());

        auto status = Begin(
            [this, oauth = std::move(oauth).Value(), authorization = std::move(*authorization),
             callback = std::move(callback).Value()]() mutable
            {
                auto tokens = oauth.Exchange(authorization, callback);
                if (!tokens)
                {
                    Publish(FailureSnapshot(tokens.Error(), true,
                                            AccountSessionStore(m_SessionPath).PersistentStorageAvailable()));
                    return;
                }
                auto account = CreateClient();
                if (!account)
                {
                    Publish(FailureSnapshot(account.Error(), true,
                                            AccountSessionStore(m_SessionPath).PersistentStorageAvailable()));
                    return;
                }
                auto user = account.Value().FetchUser(tokens.Value().AccessToken);
                if (!user)
                {
                    Publish(FailureSnapshot(user.Error(), true,
                                            AccountSessionStore(m_SessionPath).PersistentStorageAvailable()));
                    return;
                }
                const auto now = std::chrono::duration_cast<std::chrono::seconds>(
                                     std::chrono::system_clock::now().time_since_epoch())
                                     .count();
                AccountSession session{.AccessToken = std::move(tokens.Value().AccessToken),
                                       .RefreshToken = std::move(tokens.Value().RefreshToken),
                                       .ExpiresAtUnixSeconds =
                                           static_cast<std::uint64_t>(now) + tokens.Value().ExpiresInSeconds,
                                       .User = std::move(user).Value()};
                AccountSessionStore store(m_SessionPath);
                auto persisted = store.SaveRefreshToken(session.RefreshToken);
                if (!persisted)
                {
                    Publish(FailureSnapshot(persisted.Error(), true, store.PersistentStorageAvailable()));
                    return;
                }
                auto profile = account.Value().FetchProfile(session);
                PublishSession(std::move(session), profile ? std::move(profile).Value() : AccountProfile{},
                               "Signed in through your browser.");
            });
        if (!status)
            return status;
        {
            std::scoped_lock lock(m_Mutex);
            m_PendingOAuthAuthorization.reset();
            auto snapshot = *m_Snapshot;
            snapshot.BrowserSignInPending = false;
            m_Snapshot = std::make_shared<const HubAccountWorkflowSnapshot>(std::move(snapshot));
        }
        return HubStatus::Success();
    }

    HubStatus HubAccountWorkflow::SignOut()
    {
        std::optional<AccountSession> session;
        {
            std::scoped_lock lock(m_Mutex);
            session = m_Session;
        }
        if (!session)
            return HubStatus::Failure({.Code = HubErrorCode::InvalidTransition,
                                       .Message = "No account is signed in.",
                                       .AffectedItem = "supabase-account"});
        return Begin(
            [this, session = std::move(*session)]
            {
                auto client = CreateClient();
                std::optional<HubError> remoteFailure;
                if (client)
                {
                    auto signedOut = client.Value().SignOut(session.AccessToken);
                    if (!signedOut)
                        remoteFailure = signedOut.Error();
                }
                AccountSessionStore store(m_SessionPath);
                auto cleared = store.Clear();
                {
                    std::scoped_lock lock(m_Mutex);
                    m_Session.reset();
                }
                if (!cleared)
                {
                    Publish(FailureSnapshot(cleared.Error(), true, store.PersistentStorageAvailable()));
                    return;
                }
                HubAccountWorkflowSnapshot snapshot{.Configured = true,
                                                    .PersistentSessionAvailable = store.PersistentStorageAvailable(),
                                                    .Message = remoteFailure
                                                                   ? "Signed out locally; the service could not "
                                                                     "revoke this session."
                                                                   : "Signed out."};
                snapshot.Failure = std::move(remoteFailure);
                Publish(std::move(snapshot));
            });
    }

    HubStatus HubAccountWorkflow::SaveProfile(std::string displayName)
    {
        std::optional<AccountSession> session;
        {
            std::scoped_lock lock(m_Mutex);
            session = m_Session;
        }
        if (!session)
            return HubStatus::Failure({.Code = HubErrorCode::InvalidTransition,
                                       .Message = "Sign in before changing the profile.",
                                       .AffectedItem = "supabase-account"});
        return Begin(
            [this, session = std::move(*session), displayName = std::move(displayName)]() mutable
            {
                auto client = CreateClient();
                if (!client)
                {
                    PublishSessionFailure(client.Error());
                    return;
                }
                auto profile = client.Value().SaveProfile(session, std::move(displayName));
                if (!profile)
                {
                    PublishSessionFailure(profile.Error());
                    return;
                }
                PublishSession(std::move(session), std::move(profile).Value(), "Profile saved.");
            });
    }

    void HubAccountWorkflow::RefreshIfNeeded(const std::uint64_t nowUnixSeconds)
    {
        std::optional<AccountSession> session;
        {
            std::scoped_lock lock(m_Mutex);
            if (m_Snapshot->Busy || !m_Session || m_Session->ExpiresAtUnixSeconds > nowUnixSeconds + 300U ||
                nowUnixSeconds < m_NextRefreshAttemptUnixSeconds)
                return;
            session = m_Session;
            m_NextRefreshAttemptUnixSeconds = nowUnixSeconds + 60U;
        }
        (void)Begin(
            [this, session = std::move(*session)]() mutable
            {
                auto client = CreateClient();
                if (!client)
                {
                    PublishSessionFailure(client.Error());
                    return;
                }
                auto refreshed = client.Value().Refresh(std::move(session.RefreshToken));
                if (!refreshed)
                {
                    if (refreshed.Error().Retryable)
                    {
                        PublishSessionFailure(refreshed.Error());
                    }
                    else
                    {
                        AccountSessionStore store(m_SessionPath);
                        (void)store.Clear();
                        {
                            std::scoped_lock lock(m_Mutex);
                            m_Session.reset();
                        }
                        Publish(FailureSnapshot(refreshed.Error(), true, store.PersistentStorageAvailable()));
                    }
                    return;
                }
                AccountSessionStore store(m_SessionPath);
                auto persisted = store.SaveRefreshToken(refreshed.Value().RefreshToken);
                if (!persisted)
                {
                    PublishSessionFailure(persisted.Error());
                    return;
                }
                AccountProfile existing;
                const auto current = Snapshot();
                existing.DisplayName = current->DisplayName;
                existing.AvatarUrl = current->AvatarUrl;
                PublishSession(std::move(refreshed).Value(), std::move(existing), "Signed in.");
            });
    }

    void HubAccountWorkflow::Stop() noexcept
    {
        if (!m_Worker.joinable())
            return;
        m_Worker.request_stop();
        m_Worker.join();
    }

    std::shared_ptr<const HubAccountWorkflowSnapshot> HubAccountWorkflow::Snapshot() const
    {
        std::scoped_lock lock(m_Mutex);
        return m_Snapshot;
    }

    HubStatus HubAccountWorkflow::Begin(std::function<void()> operation)
    {
        {
            std::scoped_lock lock(m_Mutex);
            if (m_Snapshot->Busy)
                return HubStatus::Failure(BusyError());
        }
        Stop();
        auto snapshot = *Snapshot();
        snapshot.Busy = true;
        snapshot.Failure.reset();
        snapshot.Message = "Working...";
        Publish(std::move(snapshot));
        m_Worker = std::jthread([operation = std::move(operation)] { operation(); });
        return HubStatus::Success();
    }

    HubResult<SupabaseAccountClient> HubAccountWorkflow::CreateClient() const
    {
        SupabaseConfiguration configuration;
        HubSettings settings;
        {
            std::scoped_lock lock(m_Mutex);
            if (!m_Configuration)
            {
                return HubResult<SupabaseAccountClient>::Failure(
                    {.Code = HubErrorCode::AccountConfigurationInvalid,
                     .Message = "Accounts are not configured in this Hub package.",
                     .AffectedItem = "supabase-account"});
            }
            configuration = *m_Configuration;
            settings = m_Settings;
        }
        if (settings.OfflineMode)
        {
            return HubResult<SupabaseAccountClient>::Failure(
                {.Code = HubErrorCode::AccountTransportFailed,
                 .Message = "Account actions are unavailable while the Hub is offline.",
                 .AffectedItem = "supabase-account"});
        }
        if (m_ClientFactory)
            return m_ClientFactory(configuration, settings);
        NativeHttpTransportOptions options;
        if (settings.NetworkProxyMode == ProxyMode::Custom && !settings.CustomProxyUrl.empty())
            options.CustomProxyUrl = settings.CustomProxyUrl;
        auto transport = NativeHttpTransport::Create(std::move(options));
        if (!transport)
            return HubResult<SupabaseAccountClient>::Failure(transport.Error());
        auto sharedTransport = std::make_shared<NativeHttpTransport>(std::move(transport).Value());
        return SupabaseAccountClient::Create(std::move(configuration),
                                             [sharedTransport](const NativeHttpRequest& request)
                                             { return sharedTransport->Send(request); });
    }

    HubResult<DesktopOAuthClient> HubAccountWorkflow::CreateOAuthClient() const
    {
        SupabaseConfiguration configuration;
        HubSettings settings;
        {
            std::scoped_lock lock(m_Mutex);
            if (!m_Configuration || !m_Configuration->HubOAuthEnabled)
            {
                return HubResult<DesktopOAuthClient>::Failure(
                    {.Code = HubErrorCode::AccountConfigurationInvalid,
                     .Message = "Browser sign-in is not configured in this Hub package.",
                     .AffectedItem = "hub-oauth"});
            }
            configuration = *m_Configuration;
            settings = m_Settings;
        }
        if (settings.OfflineMode)
        {
            return HubResult<DesktopOAuthClient>::Failure(
                {.Code = HubErrorCode::AccountTransportFailed,
                 .Message = "Browser sign-in is unavailable while the Hub is offline.",
                 .AffectedItem = "hub-oauth"});
        }
        if (m_OAuthClientFactory)
            return m_OAuthClientFactory(configuration, settings);
        NativeHttpTransportOptions options;
        if (settings.NetworkProxyMode == ProxyMode::Custom && !settings.CustomProxyUrl.empty())
            options.CustomProxyUrl = settings.CustomProxyUrl;
        auto transport = NativeHttpTransport::Create(std::move(options));
        if (!transport)
            return HubResult<DesktopOAuthClient>::Failure(transport.Error());
        auto sharedTransport = std::make_shared<NativeHttpTransport>(std::move(transport).Value());
        return DesktopOAuthClient::Create({.SupabaseProjectUrl = configuration.ProjectUrl,
                                           .ClientId = configuration.HubOAuthClientId,
                                           .WebsiteCallbackUrl = configuration.HubOAuthWebsiteCallbackUrl},
                                          [sharedTransport](const NativeHttpRequest& request)
                                          { return sharedTransport->Send(request); });
    }

    void HubAccountWorkflow::Publish(HubAccountWorkflowSnapshot snapshot)
    {
        std::scoped_lock lock(m_Mutex);
        snapshot.BrowserSignInAvailable = m_Configuration && m_Configuration->HubOAuthEnabled;
        snapshot.BrowserSignInPending = m_PendingOAuthAuthorization.has_value();
        m_Snapshot = std::make_shared<const HubAccountWorkflowSnapshot>(std::move(snapshot));
    }

    void HubAccountWorkflow::PublishSessionFailure(HubError error)
    {
        std::scoped_lock lock(m_Mutex);
        auto snapshot = *m_Snapshot;
        snapshot.Busy = false;
        snapshot.SignedIn = m_Session.has_value();
        snapshot.ConfirmationRequired = false;
        snapshot.Message = error.Message;
        snapshot.Failure = std::move(error);
        m_Snapshot = std::make_shared<const HubAccountWorkflowSnapshot>(std::move(snapshot));
    }

    void HubAccountWorkflow::PublishSession(AccountSession session, AccountProfile profile, std::string message)
    {
        HubAccountWorkflowSnapshot snapshot{
            .Configured = true,
            .BrowserSignInAvailable = m_Configuration && m_Configuration->HubOAuthEnabled,
            .SignedIn = true,
            .PersistentSessionAvailable = AccountSessionStore(m_SessionPath).PersistentStorageAvailable(),
            .UserId = session.User.Id,
            .Email = session.User.Email,
            .DisplayName = std::move(profile.DisplayName),
            .AvatarUrl = std::move(profile.AvatarUrl),
            .Message = std::move(message),
            .ExpiresAtUnixSeconds = session.ExpiresAtUnixSeconds};
        {
            std::scoped_lock lock(m_Mutex);
            m_Session = std::move(session);
            m_NextRefreshAttemptUnixSeconds = 0;
            m_Snapshot = std::make_shared<const HubAccountWorkflowSnapshot>(std::move(snapshot));
        }
    }

    void ApplyHubAccountSnapshot(const HubAccountWorkflowSnapshot& account, HubProductSnapshot& product)
    {
        product.AccountConfigured = account.Configured;
        product.AccountBrowserSignInAvailable = account.BrowserSignInAvailable;
        product.AccountBrowserSignInPending = account.BrowserSignInPending;
        product.AccountBusy = account.Busy;
        product.AccountSignedIn = account.SignedIn;
        product.AccountHasError = account.Failure.has_value();
        product.AccountPersistentSessionAvailable = account.PersistentSessionAvailable;
        product.AccountConfirmationRequired = account.ConfirmationRequired;
        product.AccountEmail = account.Email;
        product.AccountDisplayName = account.DisplayName;
        product.AccountMessage = account.Message;
    }
} // namespace KeireHub
