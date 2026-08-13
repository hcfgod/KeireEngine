#include "KeireHub/HubAccountIntegration.h"

#include "Keire/BuildInfo.h"
#include "Keire/PlatformDirectories.h"

#include "KeireHub/HubDistributionWorkflow.h"
#include "KeireHub/HubLocalContent.h"
#include "KeireHub/HubProductUi.h"

#include <cerrno>
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
        m_RefreshPending = false;
        return m_Workflow.Start(m_ConfigurationPath, m_SessionPath, settings);
    }

    void HubAccountIntegration::Stop() noexcept
    {
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
            return m_Workflow.SignOut();
        case HubUiCommandType::SaveAccountProfile:
            return m_Workflow.SaveProfile(command.AccountDisplayName);
        default:
            return HubStatus::Failure({.Code = HubErrorCode::InvalidTransition,
                                       .Message = "The account action is invalid.",
                                       .AffectedItem = "supabase-account"});
        }
    }

    HubResult<std::string> HubAccountIntegration::BeginBrowserSignIn()
    {
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
             .AccessToken = std::move(token).Value(),
             .ServiceBaseUrl = service->ServiceBaseUrl,
             .TrustedPublicKeyDocument = *trustedKey,
             .CacheRoot = Keire::GetPreferenceDirectory() / "Hub" / "MarketplacePackages",
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
