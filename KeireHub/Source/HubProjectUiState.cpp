#include "KeireHub/HubProjectMutationIntegration.h"
#include "KeireHub/HubProjectMutationWorkflow.h"
#include "KeireHub/HubProjectsUi.h"

#include <utility>

namespace KeireHub
{
    HubStatus SubmitProjectDuplicate(HubProjectMutationWorkflow& workflow, HubProjectsUi& ui,
                                     const HubProjectUiCommand& command)
    {
        const auto started = workflow.StartDuplicate(command.ProjectId, command.Path, command.DisplayName);
        if (!started)
        {
            ui.ResolveDuplicateSubmission(started.Error().Message);
            return HubStatus::Failure(started.Error());
        }
        const auto mutation = workflow.Snapshot();
        ui.ResolveDuplicateStart(*mutation);
        if (mutation->Failure)
            return HubStatus::Failure(*mutation->Failure);
        return HubStatus::Success();
    }

    bool HubDuplicateSubmission::Begin()
    {
        if (Waiting || CloseRequested)
            return false;
        Error.clear();
        Waiting = true;
        return true;
    }

    void HubDuplicateSubmission::Resolve(std::string error)
    {
        if (!Waiting)
            return;
        Error = std::move(error);
        Waiting = false;
        CloseRequested = Error.empty();
    }

    void HubProjectsUi::ResolveDuplicateSubmission(std::string error)
    {
        m_DuplicateSubmission.Resolve(std::move(error));
    }

    void HubDuplicateSubmission::ResolveStart(const HubProjectMutationSnapshot& snapshot)
    {
        // An operation ID acknowledges the attempt, including synchronous preparation failures.
        if (snapshot.Failure)
            Resolve(snapshot.Failure->Message);
        else if (snapshot.State == HubProjectMutationState::Duplicating || snapshot.Result)
            Resolve({});
        else
            Resolve("Project duplication could not be started. Please retry.");
    }

    void HubProjectsUi::ResolveDuplicateStart(const HubProjectMutationSnapshot& snapshot)
    {
        m_DuplicateSubmission.ResolveStart(snapshot);
    }

    void HubProjectsUi::SetPreferences(const HubProjectsView view, const HubProjectsSort sort) noexcept
    {
        m_View = view;
        m_Sort = sort;
    }

    void HubProjectsUi::SetFilters(HubProjectFilters filters)
    {
        if (filters.Status > 8)
            filters.Status = 0;
        if (filters.Editor > 2)
            filters.Editor = 0;
        m_Filters = std::move(filters);
    }

    void HubProjectsUi::RevealCreatedProject(const std::string_view name)
    {
        // Creation is an explicit navigation action; stale filters must not hide its result.
        if (!name.empty())
            m_Filters = {.Search = std::string(name)};
    }

    HubProjectUiCommand TakeOpenWithEditorCommand(std::optional<HubProjectUiPendingSelection>& pending,
                                                  std::string editorId)
    {
        if (!pending)
            return {};
        auto selected = std::exchange(pending, std::nullopt);
        return {.Type = HubProjectUiCommandType::OpenWithEditor,
                .ProjectId = std::move(selected->Id),
                .EditorId = std::move(editorId),
                .Path = std::move(selected->Root)};
    }
} // namespace KeireHub
