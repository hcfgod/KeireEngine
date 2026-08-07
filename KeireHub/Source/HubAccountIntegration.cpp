#include "KeireHub/HubAccountIntegration.h"

namespace KeireHub
{
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
} // namespace KeireHub
