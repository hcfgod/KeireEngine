#include "KeireHub/HubAccountIntegration.h"

#include "Keire/BuildInfo.h"
#include "Keire/PlatformDirectories.h"

#include "KeireHub/HubDistributionWorkflow.h"
#include "KeireHub/HubLocalContent.h"
#include "KeireHub/HubProductUi.h"

#include "KeireHubRuntime/HubActivationProtocol.h"
#include "KeireHubRuntime/MarketplaceCache.h"

#include <cerrno>
#include <exception>
#include <fstream>
#include <iterator>
#include <limits>
#include <optional>
#include <span>
#include <string_view>
#include <utility>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <bcrypt.h>
#elif defined(__linux__)
#include <sys/random.h>
#elif defined(__APPLE__)
#include <cstdlib>
#endif

namespace KeireHub
{
    namespace
    {
        [[nodiscard]] bool FillSecureRandom(const std::span<std::byte> bytes) noexcept
        {
#if defined(_WIN32)
            if (bytes.size() > std::numeric_limits<ULONG>::max())
                return false;
            return BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(bytes.data()), static_cast<ULONG>(bytes.size()),
                                   BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0;
#elif defined(__linux__)
            std::size_t offset = 0;
            while (offset < bytes.size())
            {
                const auto received = ::getrandom(bytes.data() + offset, bytes.size() - offset, 0);
                if (received > 0)
                    offset += static_cast<std::size_t>(received);
                else if (received == -1 && errno == EINTR)
                    continue;
                else
                    return false;
            }
            return true;
#elif defined(__APPLE__)
            arc4random_buf(bytes.data(), bytes.size());
            return true;
#else
            return false;
#endif
        }

        [[nodiscard]] std::string_view HostPlatform() noexcept
        {
#if defined(_WIN32)
            return "windows";
#elif defined(__APPLE__)
            return "macos";
#else
            return "linux";
#endif
        }

        [[nodiscard]] std::string_view HostArchitecture() noexcept
        {
#if defined(_M_ARM64) || defined(__aarch64__)
            return "arm64";
#else
            return "x86_64";
#endif
        }

        [[nodiscard]] std::optional<std::string> ReadBoundedText(const std::filesystem::path& path)
        {
            std::ifstream stream(path, std::ios::binary);
            std::string value{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
            if (!stream || value.empty() || value.size() > 16U * 1024U)
                return std::nullopt;
            return value;
        }
    } // namespace

    HubStatus HubAccountIntegration::Start(const std::filesystem::path& configurationPath,
                                           const std::filesystem::path& sessionPath, const HubSettings& settings)
    {
        m_ConfigurationPath = configurationPath;
        m_SessionPath = sessionPath;
        m_MarketplaceCacheRoot = Keire::GetPreferenceDirectory() / "Hub" / "MarketplacePackages";
        m_LeasedAccountId.clear();
        m_LeasedSignedIn.reset();
        m_NextMarketplaceLeaseRefresh = 0;
        m_RefreshPending = false;
        return m_Workflow.Start(m_ConfigurationPath, m_SessionPath, settings);
    }

    void HubAccountIntegration::Stop() noexcept
    {
        try
        {
            if (!m_MarketplaceCacheRoot.empty())
                static_cast<void>(MarketplaceSessionLeaseStore(m_MarketplaceCacheRoot).Save({}));
        }
        catch (const std::exception&)
        {
        }
        m_Marketplace.Stop();
        m_Workflow.Stop();
    }

    void HubAccountIntegration::RequestRefresh() noexcept { m_RefreshPending = true; }

    HubStatus HubAccountIntegration::Tick(const HubSettings& settings, const std::uint64_t nowUnixSeconds,
                                          const std::filesystem::path& executable,
                                          const HubDistributionWorkflow* distribution)
    {
        if (m_RefreshPending && !m_Workflow.Snapshot()->Busy)
        {
            auto status = m_Workflow.Start(m_ConfigurationPath, m_SessionPath, settings);
            m_RefreshPending = false;
            if (!status)
                return status;
        }
        m_Workflow.RefreshIfNeeded(nowUnixSeconds);
        if (const auto lease = RefreshMarketplaceSessionLease(nowUnixSeconds); !lease)
            return lease;
        if (!m_PendingMarketplaceProduct.empty())
        {
            const auto status = StartMarketplaceProduct(executable, distribution, settings, nowUnixSeconds);
            if (!status && status.Error().Code != HubErrorCode::AccountSessionInvalid)
            {
                m_PendingMarketplaceProduct.clear();
                return status;
            }
        }
        return HubStatus::Success();
    }

    HubStatus HubAccountIntegration::RefreshMarketplaceSessionLease(const std::uint64_t nowUnixSeconds)
    {
        const auto account = m_Workflow.Snapshot();
        const bool signedIn = account->SignedIn && !account->UserId.empty();
        const auto accountId = signedIn ? account->UserId : std::string{};
        const bool stateChanged = !m_LeasedSignedIn || *m_LeasedSignedIn != signedIn || m_LeasedAccountId != accountId;
        if (!stateChanged && nowUnixSeconds < m_NextMarketplaceLeaseRefresh)
            return HubStatus::Success();

        MarketplaceSessionLease lease;
        lease.SignedIn = signedIn;
        lease.AccountId = accountId;
        lease.ExpiresAtUnixSeconds = signedIn ? nowUnixSeconds + MarketplaceSessionLeaseDurationSeconds : 0U;
        MarketplaceSessionLeaseStore store(m_MarketplaceCacheRoot);
        if (const auto saved = store.Save(lease); !saved)
            return saved;
        m_LeasedSignedIn = signedIn;
        m_LeasedAccountId = accountId;
        m_NextMarketplaceLeaseRefresh = nowUnixSeconds + MarketplaceSessionLeaseRefreshSeconds;
        return HubStatus::Success();
    }

    void HubAccountIntegration::ApplySnapshot(HubProductSnapshot& product) const
    {
        ApplyHubAccountSnapshot(*m_Workflow.Snapshot(), product);
    }

    void HubAccountIntegration::ApplyMarketplaceNotice(std::string& notice, bool& noticeError)
    {
        const auto marketplace = m_Marketplace.Snapshot();
        if (marketplace->Running || marketplace->Completion == 0U ||
            marketplace->Completion == m_HandledMarketplaceCompletion)
        {
            return;
        }
        m_HandledMarketplaceCompletion = marketplace->Completion;
        notice = marketplace->Message;
        noticeError = marketplace->Failure.has_value();
    }

    HubStatus HubAccountIntegration::Execute(const HubUiCommand& command)
    {
        switch (command.Type)
        {
        case HubUiCommandType::AccountSignIn:
            return m_Workflow.SignIn(command.AccountEmail, command.AccountPassword);
        case HubUiCommandType::AccountSignUp:
            return m_Workflow.SignUp(command.AccountEmail, command.AccountPassword);
        case HubUiCommandType::AccountCancelBrowserSignIn:
            return m_Workflow.CancelBrowserSignIn();
        case HubUiCommandType::AccountSignOut:
        {
            auto status = m_Workflow.SignOut();
            if (!status)
                return status;
            m_Marketplace.Stop();
            if (!m_MarketplaceCacheRoot.empty())
            {
                status = MarketplaceSessionLeaseStore(m_MarketplaceCacheRoot).Save({});
                if (!status)
                    return status;
            }
            m_LeasedAccountId.clear();
            m_LeasedSignedIn = false;
            m_NextMarketplaceLeaseRefresh = 0;
            return HubStatus::Success();
        }
        case HubUiCommandType::SaveAccountProfile:
            return m_Workflow.SaveProfile(command.AccountDisplayName);
        default:
            return HubStatus::Failure({.Code = HubErrorCode::InvalidTransition,
                                       .Message = "The account action is invalid.",
                                       .AffectedItem = "supabase-account"});
        }
    }

    HubResult<std::string> HubAccountIntegration::BeginBrowserSignIn(const std::filesystem::path& executable)
    {
        if (const auto registered = EnsureHubActivationProtocolRegistration(executable); !registered)
            return HubResult<std::string>::Failure(registered.Error());
        return m_Workflow.BeginBrowserSignIn(FillSecureRandom);
    }

    HubStatus HubAccountIntegration::CompleteBrowserSignIn(std::string callbackUrl)
    {
        return m_Workflow.CompleteBrowserSignIn(std::move(callbackUrl));
    }

    HubResult<std::string> HubAccountIntegration::AccessToken(const std::uint64_t nowUnixSeconds) const
    {
        return m_Workflow.AccessToken(nowUnixSeconds);
    }

    HubStatus HubAccountIntegration::OpenMarketplaceProduct(std::string productId, HubProductUi& productUi,
                                                            const std::filesystem::path& executable,
                                                            const HubDistributionWorkflow* distribution,
                                                            const HubSettings& settings,
                                                            const std::uint64_t nowUnixSeconds)
    {
        m_PendingMarketplaceProduct = std::move(productId);
        const auto status = StartMarketplaceProduct(executable, distribution, settings, nowUnixSeconds);
        if (!status && status.Error().Code == HubErrorCode::AccountSessionInvalid)
        {
            productUi.RequestAccountDialog();
            return HubStatus::Success();
        }
        if (!status)
            m_PendingMarketplaceProduct.clear();
        return status;
    }

    HubStatus HubAccountIntegration::StartMarketplaceProduct(const std::filesystem::path& executable,
                                                             const HubDistributionWorkflow* distribution,
                                                             const HubSettings& settings,
                                                             const std::uint64_t nowUnixSeconds)
    {
        if (m_PendingMarketplaceProduct.empty() || m_Marketplace.Snapshot()->Running)
            return HubStatus::Success();
        auto token = m_Workflow.AccessToken(nowUnixSeconds);
        if (!token)
            return HubStatus::Failure(token.Error());
        const auto account = m_Workflow.Snapshot();
        if (!account->SignedIn || account->UserId.empty())
        {
            return HubStatus::Failure({.Code = HubErrorCode::AccountSessionInvalid,
                                       .Message = "Sign in before opening a marketplace asset.",
                                       .AffectedItem = "marketplace"});
        }
        if (!distribution)
        {
            return HubStatus::Failure({.Code = HubErrorCode::InvalidTransition,
                                       .Message = "Marketplace distribution services are unavailable.",
                                       .AffectedItem = "marketplace"});
        }
        const auto service = distribution->Snapshot();
        if (service->ServiceBaseUrl.empty())
        {
            return HubStatus::Failure({.Code = HubErrorCode::CatalogTransportFailed,
                                       .Message = "The marketplace service endpoint is unavailable.",
                                       .AffectedItem = "marketplace"});
        }
        const auto trustedKey = ReadBoundedText(ResolveHubDistributionRoot(executable) / "Config" / "Marketplace" /
                                                "trusted-marketplace-key.json");
        if (!trustedKey)
        {
            return HubStatus::Failure({.Code = HubErrorCode::CatalogUntrustedKey,
                                       .Message = "The trusted Kéire Marketplace key is unavailable.",
                                       .AffectedItem = "marketplace"});
        }
        const auto status = m_Marketplace.Request(
            {.ProductId = m_PendingMarketplaceProduct,
             .AccountId = account->UserId,
             .AccessToken = std::move(token).Value(),
             .ServiceBaseUrl = service->ServiceBaseUrl,
             .TrustedPublicKeyDocument = *trustedKey,
             .CacheRoot = m_MarketplaceCacheRoot,
             .EngineVersion = std::string(Keire::GetBuildInfo().Version),
             .Platform = std::string(HostPlatform()),
             .Architecture = std::string(HostArchitecture()),
             .CustomProxyUrl =
                 settings.NetworkProxyMode == ProxyMode::Custom ? std::optional(settings.CustomProxyUrl) : std::nullopt,
             .BandwidthLimitBytesPerSecond = settings.BandwidthLimitBytesPerSecond,
             .AllowInsecureLoopbackDevelopment = service->AllowInsecureLoopbackDevelopment});
        if (status)
            m_PendingMarketplaceProduct.clear();
        return status;
    }
} // namespace KeireHub
