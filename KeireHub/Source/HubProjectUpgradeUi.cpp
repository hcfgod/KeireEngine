#include "KeireHub/HubProjectUpgradeUi.h"

#include "Keire/Log.h"

#include "KeireHub/HubModalUi.h"
#include "KeireHub/HubProjectUiSupport.h"

#include <string>
#include <utility>

namespace KeireHub
{
    namespace
    {
        [[nodiscard]] HubProjectUpgradeUiResult Failure(const HubError& error)
        {
            if (!error.TechnicalDetails.empty())
            {
                KEIRE_CLIENT_ERROR("[Project Hub] Project upgrade failed [{}]: {}", ToString(error.Code),
                                   error.TechnicalDetails);
            }
            return {.Failure = error};
        }
    } // namespace

    HubProjectUpgradeUi::HubProjectUpgradeUi() : m_Workflow(CreateHubProjectUpgradeWorkflowServices()) {}

    void HubProjectUpgradeUi::SetAppearance(const HubAppearance appearance, const bool systemPrefersDark) noexcept
    {
        m_Tokens = HubDesignTokens::For(appearance, systemPrefersDark);
    }

    void HubProjectUpgradeUi::Begin(const std::filesystem::path& root,
                                    const std::span<const Keire::ProjectUpgradeStep> upgrades)
    {
        const auto started = m_Workflow.Start(root, upgrades);
        if (started)
            m_StartFailure.reset();
        else if (!m_Workflow.Snapshot()->IsActive())
            m_StartFailure = started.Error();
    }

    HubProjectUpgradeUiResult HubProjectUpgradeUi::Draw(Keire::UiFrame& ui)
    {
        HubProjectUpgradeUiResult result;
        PrepareHubModal(ui, {680.0F, 420.0F});
        HubModalStyleScope modalStyle(ui, m_Tokens);
        auto dialog = ui.BeginPopupModal("Project Upgrade", nullptr, HubModalWindowOptions(), false);
        if (!dialog)
            return result;

        const auto snapshot = m_Workflow.Snapshot();
        DrawHubModalHeader(ui, m_Tokens, snapshot->Interrupted ? "Recover project upgrade" : "Project upgrade",
                           snapshot->Interrupted
                               ? "Choose whether to resume the interrupted transaction or restore its before-images."
                               : "Review and apply the versioned upgrade transaction safely.",
                           snapshot->Interrupted ? "PROJECT RECOVERY" : "PROJECT UPGRADE");
        if (snapshot->State == HubProjectUpgradeWorkflowState::Completed)
        {
            result.Root = snapshot->Root;
            result.Action = snapshot->Completion == HubProjectUpgradeCompletion::Reopen
                                ? HubProjectUpgradeAction::Reopen
                                : HubProjectUpgradeAction::Refresh;
            const auto dismissed = m_Workflow.Dismiss();
            if (!dismissed)
                result = Failure(dismissed.Error());
            ui.CloseCurrentPopup();
            return result;
        }
        if (m_StartFailure)
        {
            ui.TextColoredWrapped(m_Tokens.Danger, m_StartFailure->Message);
        }
        else if (snapshot->State == HubProjectUpgradeWorkflowState::Inspecting)
        {
            ui.TextColored(m_Tokens.PrimaryText, "Inspecting the project and preparing a safe upgrade plan...");
            ui.TextColored(m_Tokens.SecondaryText, "The filesystem work is running in the background.");
        }
        else if (snapshot->State == HubProjectUpgradeWorkflowState::Applying ||
                 snapshot->State == HubProjectUpgradeWorkflowState::Recovering ||
                 snapshot->State == HubProjectUpgradeWorkflowState::RollingBack)
        {
            const auto message = snapshot->State == HubProjectUpgradeWorkflowState::Applying
                                     ? "Applying and validating the project upgrade..."
                                 : snapshot->State == HubProjectUpgradeWorkflowState::Recovering
                                     ? "Recovering the interrupted project upgrade..."
                                     : "Rolling back the interrupted project upgrade...";
            ui.TextColored(m_Tokens.PrimaryText, message);
            ui.TextColored(m_Tokens.SecondaryText, "Keep the Hub open until this atomic operation finishes.");
        }
        else if (snapshot->State == HubProjectUpgradeWorkflowState::Failed)
        {
            if (snapshot->Failure)
                ui.TextColoredWrapped(m_Tokens.Danger, snapshot->Failure->Message);
            if (HubPrimaryButton(ui, m_Tokens, "Retry", {112.0F, 36.0F}))
            {
                const auto retried = m_Workflow.Retry();
                if (!retried)
                    result = Failure(retried.Error());
            }
        }
        else if (snapshot->State == HubProjectUpgradeWorkflowState::Ready && snapshot->Interrupted)
        {
            ui.TextColored(m_Tokens.Warning, "An interrupted project upgrade was detected.");
            ui.TextColoredWrapped(m_Tokens.SecondaryText,
                                  "Recover continues publication from the durable journal. Rollback restores the "
                                  "project files captured before the upgrade began.");
            if (HubPrimaryButton(ui, m_Tokens, "Recover", {112.0F, 36.0F}))
            {
                const auto started = m_Workflow.Recover();
                if (!started)
                    result = Failure(started.Error());
            }
            ui.SameLine();
            if (HubDangerButton(ui, m_Tokens, "Rollback", {112.0F, 36.0F}))
            {
                const auto started = m_Workflow.Rollback();
                if (!started)
                    result = Failure(started.Error());
            }
        }
        else if (snapshot->State == HubProjectUpgradeWorkflowState::Ready && snapshot->Plan)
        {
            const auto& plan = *snapshot->Plan;
            ui.TextColored(m_Tokens.Warning, "Project schema " + std::to_string(plan.CurrentSchema) + " -> " +
                                                 std::to_string(plan.TargetSchema));
            ui.TextColored(m_Tokens.SecondaryText,
                           "Backup estimate: " + std::to_string(plan.EstimatedBackupBytes) + " bytes");
            ui.Separator();
            for (const auto& step : plan.Steps)
            {
                ui.TextColored(m_Tokens.PrimaryText, step.Id);
                for (const auto& path : step.AffectedPaths)
                    ui.TextColored(m_Tokens.SecondaryText, "  " + Utf8Path(path));
                if (!step.Warning.empty())
                    ui.TextColoredWrapped(m_Tokens.Warning, step.Warning);
            }
            ui.Separator();
            ui.TextColoredWrapped(m_Tokens.SecondaryText,
                                  "The project is locked while staged files are validated and atomically published.");
            if (HubPrimaryButton(ui, m_Tokens, "Apply Upgrade", {136.0F, 36.0F}))
            {
                const auto started = m_Workflow.Apply();
                if (!started)
                    result = Failure(started.Error());
            }
        }
        else
        {
            ui.TextColored(m_Tokens.SecondaryText, "No project upgrade is pending.");
        }

        ui.SameLine();
        if (auto disabled = ui.BeginDisabled(snapshot->IsActive()); disabled)
        {
            if (HubSecondaryButton(ui, m_Tokens, "Cancel", {88.0F, 36.0F}))
            {
                const auto dismissed = m_Workflow.Dismiss();
                if (!dismissed)
                    result = Failure(dismissed.Error());
                else
                {
                    m_StartFailure.reset();
                    ui.CloseCurrentPopup();
                }
            }
        }
        return result;
    }
} // namespace KeireHub
