#include "KeireHub/HubAccountIntegration.h"

#include <cerrno>
#include <limits>
#include <span>
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
    } // namespace

    HubStatus HubAccountIntegration::Start(const std::filesystem::path& configurationPath,
                                           const std::filesystem::path& sessionPath, const HubSettings& settings)
    {
        m_ConfigurationPath = configurationPath;
        m_SessionPath = sessionPath;
        m_RefreshPending = false;
        return m_Workflow.Start(m_ConfigurationPath, m_SessionPath, settings);
    }

    void HubAccountIntegration::Stop() noexcept { m_Workflow.Stop(); }

    void HubAccountIntegration::RequestRefresh() noexcept { m_RefreshPending = true; }

    HubStatus HubAccountIntegration::Tick(const HubSettings& settings, const std::uint64_t nowUnixSeconds)
    {
        if (m_RefreshPending && !m_Workflow.Snapshot()->Busy)
        {
            auto status = m_Workflow.Start(m_ConfigurationPath, m_SessionPath, settings);
            m_RefreshPending = false;
            if (!status)
                return status;
        }
        m_Workflow.RefreshIfNeeded(nowUnixSeconds);
        return HubStatus::Success();
    }

    void HubAccountIntegration::ApplySnapshot(HubProductSnapshot& product) const
    {
        ApplyHubAccountSnapshot(*m_Workflow.Snapshot(), product);
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
} // namespace KeireHub
