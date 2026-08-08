#include "KeireHubRuntime/DistributionCatalog.h"

#include <KeireHubRuntimeInternal/DistributionEncoding.h>

#include <string_view>
#include <utility>

namespace KeireHub
{
    namespace
    {
        [[nodiscard]] HubError ConfigurationError(std::string message, std::string item = {}, std::string details = {})
        {
            return {.Code = HubErrorCode::DistributionConfigurationInvalid,
                    .Message = std::move(message),
                    .AffectedItem = std::move(item),
                    .TechnicalDetails = std::move(details)};
        }

        [[nodiscard]] bool IsHostPlatform(const std::string_view value) noexcept
        {
            return value == "windows" || value == "linux" || value == "macos";
        }

        [[nodiscard]] bool IsHostArchitecture(const std::string_view value) noexcept
        {
            return value == "x86_64" || value == "arm64";
        }

        [[nodiscard]] bool IsAbsoluteBoundedPath(const std::filesystem::path& path)
        {
            if (path.empty() || !path.is_absolute())
                return false;
            std::size_t encodedSize = 0;
            try
            {
                encodedSize = path.generic_u8string().size();
            }
            catch (...)
            {
                return false;
            }
            if (encodedSize == 0U || encodedSize > 4096U)
                return false;
            for (const auto& component : path)
            {
                if (component == "..")
                    return false;
            }
            return true;
        }

        [[nodiscard]] std::filesystem::path DefaultVerifierLibrary(const std::filesystem::path& executable)
        {
#if defined(_WIN32)
            return executable.parent_path() / "libsodium.dll";
#elif defined(__APPLE__)
            return executable.parent_path() / "libsodium.dylib";
#else
            return executable.parent_path() / "libsodium.so";
#endif
        }

        [[nodiscard]] std::vector<std::string> EnabledChannels(const HubSettings& settings)
        {
            std::vector<std::string> result;
            if (settings.EnableStableChannel)
                result.emplace_back("stable");
            if (settings.EnablePreReleaseChannel)
                result.emplace_back("preview");
            if (settings.EnableNightlyChannel)
                result.emplace_back("nightly");
            return result;
        }

        [[nodiscard]] DistributionCatalogSourceStatus InitialStatus(const bool onlineDiscoveryEnabled)
        {
            return {.State = onlineDiscoveryEnabled ? DistributionCatalogSourceState::NotLoaded
                                                    : DistributionCatalogSourceState::OnlineDisabled};
        }

        void MarkSuccess(DistributionCatalogSourceStatus& status, const VerifiedCatalogDocument& document,
                         const bool offline)
        {
            status.State = document.NetworkValidated ? DistributionCatalogSourceState::Online
                                                     : (offline ? DistributionCatalogSourceState::OfflineLastKnownGood
                                                                : DistributionCatalogSourceState::LastKnownGood);
            status.Sequence = document.Signature.Sequence;
            status.KeyId = document.Signature.KeyId;
            status.ExpiresAt = document.Signature.ExpiresAt;
            status.Error.reset();
        }

        void MarkFailure(DistributionCatalogSourceStatus& status, const bool hasPublishedCatalog, HubError error,
                         const bool offline)
        {
            status.State = hasPublishedCatalog ? (offline ? DistributionCatalogSourceState::OfflineLastKnownGood
                                                          : DistributionCatalogSourceState::LastKnownGood)
                                               : DistributionCatalogSourceState::Unavailable;
            if (!hasPublishedCatalog)
            {
                status.Sequence = 0;
                status.KeyId.clear();
                status.ExpiresAt.clear();
            }
            status.Error = std::move(error);
        }

        [[nodiscard]] DistributionPackageCatalogIdentity ExpectedIdentity(const VerifiedCatalogDocument& document)
        {
            return {.KeyId = document.Signature.KeyId,
                    .Sequence = document.Signature.Sequence,
                    .ExpiresAt = document.Signature.ExpiresAt,
                    .Channel = document.Endpoint.Channel,
                    .Platform = document.Endpoint.Platform,
                    .Architecture = document.Endpoint.Architecture};
        }

        [[nodiscard]] HubError MissingBytesError(const std::string& item)
        {
            return {.Code = HubErrorCode::InvalidData,
                    .Message = "A verified distribution catalog did not contain its exact bytes.",
                    .AffectedItem = item};
        }
    } // namespace

    HubResult<DistributionCatalogSession>
    DistributionCatalogSession::Create(const DistributionConfiguration& distribution, const HubSettings& settings,
                                       DistributionCatalogEnvironment environment)
    {
        const bool hasDevelopmentService = settings.DevelopmentServiceUrl.has_value();
        const bool hasDevelopmentKey = settings.DevelopmentTrustedKey.has_value();
#if defined(KEIRE_DISTRIBUTION)
        if (hasDevelopmentService || hasDevelopmentKey)
        {
            return HubResult<DistributionCatalogSession>::Failure(ConfigurationError(
                "Development distribution overrides are disabled in packaged Hub builds.", "distribution-session"));
        }
#endif
        if (hasDevelopmentService != hasDevelopmentKey || !IsHostPlatform(environment.HostPlatform) ||
            !IsHostArchitecture(environment.HostArchitecture) || !Detail::IsDistributionLocale(environment.Locale) ||
            environment.MinimumRemainingValidity.count() < 0)
        {
            return HubResult<DistributionCatalogSession>::Failure(
                ConfigurationError("The distribution catalog session identity is invalid.", "distribution-session"));
        }

        const bool usesDevelopmentOverride = hasDevelopmentService && hasDevelopmentKey;
        const bool onlineDiscoveryEnabled = distribution.OnlineDiscoveryEnabled || usesDevelopmentOverride;
        auto initial = std::make_shared<DistributionCatalogSnapshot>();
        initial->OnlineDiscoveryEnabled = onlineDiscoveryEnabled;
        initial->OfflineMode = settings.OfflineMode;
        for (auto& channel : EnabledChannels(settings))
        {
            initial->PackageCatalogs.push_back(
                {.Channel = std::move(channel), .Status = InitialStatus(onlineDiscoveryEnabled)});
        }
        initial->Content = {.Locale = environment.Locale, .Status = InitialStatus(onlineDiscoveryEnabled)};

        if (!onlineDiscoveryEnabled)
        {
            if (!distribution.ServiceBaseUrl.empty() || !distribution.TrustedPublicKeyDocuments.empty())
            {
                return HubResult<DistributionCatalogSession>::Failure(ConfigurationError(
                    "Disabled online discovery contains unexpected service trust data.", "distribution-service"));
            }
            return HubResult<DistributionCatalogSession>::Success(
                DistributionCatalogSession(std::nullopt, settings.OfflineMode,
                                           std::shared_ptr<const DistributionCatalogSnapshot>(std::move(initial))));
        }

        auto cacheRoot = environment.CatalogCacheRoot.empty() ? settings.CacheRoot / "Catalogs"
                                                              : std::move(environment.CatalogCacheRoot);
        if (!IsAbsoluteBoundedPath(environment.HubExecutable) || environment.HubExecutable.filename().empty() ||
            !IsAbsoluteBoundedPath(cacheRoot) || distribution.MinimumSequence == 0U)
        {
            return HubResult<DistributionCatalogSession>::Failure(ConfigurationError(
                "The distribution catalog executable or cache location is invalid.", "distribution-session"));
        }

        CatalogTrustConfiguration trustConfiguration;
        std::string serviceBaseUrl;
        bool allowInsecureLoopbackDevelopment = false;
        if (usesDevelopmentOverride)
        {
            serviceBaseUrl = *settings.DevelopmentServiceUrl;
            trustConfiguration.TrustedPublicKeyDocuments = {*settings.DevelopmentTrustedKey};
            allowInsecureLoopbackDevelopment = true;
        }
        else
        {
            serviceBaseUrl = distribution.ServiceBaseUrl;
            trustConfiguration.TrustedPublicKeyDocuments = distribution.TrustedPublicKeyDocuments;
        }
        trustConfiguration.NativeLibraryPath = environment.SignatureVerifierLibrary.empty()
                                                   ? DefaultVerifierLibrary(environment.HubExecutable)
                                                   : std::move(environment.SignatureVerifierLibrary);
        if (!IsAbsoluteBoundedPath(trustConfiguration.NativeLibraryPath))
        {
            return HubResult<DistributionCatalogSession>::Failure(
                ConfigurationError("The distribution signature verifier path is invalid.", "libsodium"));
        }

        auto trustStore = CatalogTrustStore::Create(trustConfiguration);
        if (!trustStore)
            return HubResult<DistributionCatalogSession>::Failure(trustStore.Error());

        CatalogClientOptions clientOptions{.ServiceBaseUrl = std::move(serviceBaseUrl),
                                           .Platform = std::move(environment.HostPlatform),
                                           .Architecture = std::move(environment.HostArchitecture),
                                           .CacheRoot = std::move(cacheRoot),
                                           .Offline = settings.OfflineMode,
                                           .AllowInsecureLoopbackDevelopment = allowInsecureLoopbackDevelopment,
                                           .MinimumSequence = distribution.MinimumSequence,
                                           .MinimumRemainingValidity = environment.MinimumRemainingValidity,
                                           .Clock = std::move(environment.Clock)};
        if (settings.NetworkProxyMode == ProxyMode::Custom)
            clientOptions.CustomProxyUrl = settings.CustomProxyUrl;
        auto client = CatalogClient::Create(std::move(clientOptions), std::move(trustStore).Value(),
                                            std::move(environment.Transport));
        if (!client)
            return HubResult<DistributionCatalogSession>::Failure(client.Error());

        return HubResult<DistributionCatalogSession>::Success(
            DistributionCatalogSession(std::move(client).Value(), settings.OfflineMode,
                                       std::shared_ptr<const DistributionCatalogSnapshot>(std::move(initial))));
    }

    HubStatus DistributionCatalogSession::Refresh()
    {
        if (!m_Client)
            return HubStatus::Success();

        auto next = std::make_shared<DistributionCatalogSnapshot>(*m_Snapshot);
        std::optional<HubError> firstFailure;
        const auto rememberFailure = [&](const HubError& error)
        {
            if (!firstFailure)
                firstFailure = error;
        };

        for (auto& packageSnapshot : next->PackageCatalogs)
        {
            auto fetched = m_Client->FetchPackageCatalog(packageSnapshot.Channel);
            if (!fetched)
            {
                rememberFailure(fetched.Error());
                MarkFailure(packageSnapshot.Status, static_cast<bool>(packageSnapshot.Catalog), fetched.Error(),
                            m_Offline);
                continue;
            }
            if (!fetched.Value().ExactBytes)
            {
                auto error = MissingBytesError(packageSnapshot.Channel);
                rememberFailure(error);
                MarkFailure(packageSnapshot.Status, static_cast<bool>(packageSnapshot.Catalog), std::move(error),
                            m_Offline);
                continue;
            }
            auto parsed =
                ParseDistributionPackageCatalog(*fetched.Value().ExactBytes, ExpectedIdentity(fetched.Value()));
            if (!parsed)
            {
                rememberFailure(parsed.Error());
                MarkFailure(packageSnapshot.Status, static_cast<bool>(packageSnapshot.Catalog), parsed.Error(),
                            m_Offline);
                continue;
            }
            packageSnapshot.Catalog = std::make_shared<const DistributionPackageCatalog>(std::move(parsed).Value());
            MarkSuccess(packageSnapshot.Status, fetched.Value(), m_Offline);
        }

        auto fetchedContent = m_Client->FetchContentCatalog(next->Content.Locale);
        if (!fetchedContent)
        {
            rememberFailure(fetchedContent.Error());
            MarkFailure(next->Content.Status, static_cast<bool>(next->Content.Catalog), fetchedContent.Error(),
                        m_Offline);
        }
        else if (!fetchedContent.Value().ExactBytes)
        {
            auto error = MissingBytesError(next->Content.Locale);
            rememberFailure(error);
            MarkFailure(next->Content.Status, static_cast<bool>(next->Content.Catalog), std::move(error), m_Offline);
        }
        else
        {
            const auto bytes = std::span<const std::byte>(*fetchedContent.Value().ExactBytes);
            const auto text = std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size());
            auto parsed = ParseContentCatalog(text);
            if (!parsed || parsed.Value().Locale != next->Content.Locale)
            {
                auto error = parsed ? HubError{.Code = HubErrorCode::CatalogIdentityMismatch,
                                               .Message = "The content catalog locale does not match its endpoint.",
                                               .AffectedItem = next->Content.Locale}
                                    : parsed.Error();
                rememberFailure(error);
                MarkFailure(next->Content.Status, static_cast<bool>(next->Content.Catalog), std::move(error),
                            m_Offline);
            }
            else
            {
                next->Content.Catalog = std::make_shared<const HubContentCatalog>(std::move(parsed).Value());
                MarkSuccess(next->Content.Status, fetchedContent.Value(), m_Offline);
            }
        }

        m_Snapshot = std::shared_ptr<const DistributionCatalogSnapshot>(std::move(next));
        if (firstFailure)
            return HubStatus::Failure(std::move(*firstFailure));
        return HubStatus::Success();
    }

    std::shared_ptr<const DistributionCatalogSnapshot> DistributionCatalogSession::Snapshot() const noexcept
    {
        return m_Snapshot;
    }

    DistributionCatalogSession::DistributionCatalogSession(std::optional<CatalogClient> client, const bool offline,
                                                           std::shared_ptr<const DistributionCatalogSnapshot> snapshot)
        : m_Client(std::move(client)), m_Offline(offline), m_Snapshot(std::move(snapshot))
    {
    }
} // namespace KeireHub
