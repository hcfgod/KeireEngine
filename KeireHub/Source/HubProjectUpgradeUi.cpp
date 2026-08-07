#include "KeireHub/HubProjectUpgradeUi.h"

#include "Keire/Log.h"

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
        ui.SetNextWindowSize({680.0F, 440.0F}, false);
        if (auto dialog = ui.BeginPopupModal("Project Upgrade"); !dialog)
            return result;

        const auto snapshot = m_Workflow.Snapshot();
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
            ui.TextColored({0.96F, 0.50F, 0.25F, 1.0F}, m_StartFailure->Message);
        }
        else if (snapshot->State == HubProjectUpgradeWorkflowState::Inspecting)
        {
            ui.Text("Inspecting the project and preparing a safe upgrade plan...");
            ui.TextColored({0.55F, 0.60F, 0.68F, 1.0F}, "The filesystem work is running in the background.");
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
            ui.Text(message);
            ui.TextColored({0.55F, 0.60F, 0.68F, 1.0F}, "Keep the Hub open until this atomic operation finishes.");
        }
        else if (snapshot->State == HubProjectUpgradeWorkflowState::Failed)
        {
            if (snapshot->Failure)
                ui.TextColored({0.96F, 0.50F, 0.25F, 1.0F}, snapshot->Failure->Message);
            if (ui.Button("Retry", {112.0F, 34.0F}))
            {
                const auto retried = m_Workflow.Retry();
                if (!retried)
                    result = Failure(retried.Error());
            }
        }
        else if (snapshot->State == HubProjectUpgradeWorkflowState::Ready && snapshot->Interrupted)
        {
            ui.TextColored({0.96F, 0.50F, 0.25F, 1.0F}, "An interrupted project upgrade was detected.");
            ui.Text("Recover continues publication from the durable journal. Rollback restores before-images.");
            if (ui.Button("Recover", {112.0F, 34.0F}))
            {
                const auto started = m_Workflow.Recover();
                if (!started)
                    result = Failure(started.Error());
            }
            ui.SameLine();
            if (ui.Button("Rollback", {112.0F, 34.0F}))
            {
                const auto started = m_Workflow.Rollback();
                if (!started)
                    result = Failure(started.Error());
            }
        }
        else if (snapshot->State == HubProjectUpgradeWorkflowState::Ready && snapshot->Plan)
        {
            const auto& plan = *snapshot->Plan;
            ui.TextColored({0.96F, 0.72F, 0.28F, 1.0F}, "Project schema " + std::to_string(plan.CurrentSchema) +
                                                            " -> " + std::to_string(plan.TargetSchema));
            ui.Text("Backup estimate: " + std::to_string(plan.EstimatedBackupBytes) + " bytes");
            ui.Separator();
            for (const auto& step : plan.Steps)
            {
                ui.Text(step.Id);
                for (const auto& path : step.AffectedPaths)
                    ui.TextColored({0.55F, 0.60F, 0.68F, 1.0F}, "  " + Utf8Path(path));
                if (!step.Warning.empty())
                    ui.TextColored({0.96F, 0.72F, 0.28F, 1.0F}, step.Warning);
            }
            ui.Separator();
            ui.Text("The project is locked while staged files are validated and atomically published.");
            if (ui.Button("Apply Upgrade", {136.0F, 34.0F}))
            {
                const auto started = m_Workflow.Apply();
                if (!started)
                    result = Failure(started.Error());
            }
        }
        else
        {
            ui.Text("No project upgrade is pending.");
        }

        ui.SameLine();
        if (auto disabled = ui.BeginDisabled(snapshot->IsActive()); disabled)
        {
            if (ui.Button("Cancel"))
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
