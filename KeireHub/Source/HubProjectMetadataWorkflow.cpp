#include "KeireHub/HubProjectMetadataWorkflow.h"

#include "Keire/Log.h"

#include <algorithm>
#include <chrono>
#include <ranges>
#include <utility>
#include <vector>

namespace KeireHub
{
    HubProjectMetadataWorkflow::HubProjectMetadataWorkflow()
        : m_Snapshot(std::make_shared<const HubProjectMetadataWorkflowSnapshot>(
              HubProjectMetadataWorkflowSnapshot{.Thumbnails = m_ThumbnailCache.Snapshot()}))
    {
    }

    HubProjectMetadataWorkflow::~HubProjectMetadataWorkflow()
    {
        Cancel();
        if (m_Future.valid())
            m_Future.wait();
    }

    HubStatus HubProjectMetadataWorkflow::Start(HubController& controller)
    {
        if (m_Future.valid() && m_Future.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        {
            return HubStatus::Failure({.Code = HubErrorCode::InvalidTransition,
                                       .Message = "Project metadata is already being refreshed.",
                                       .Retryable = true,
                                       .AffectedItem = "project-metadata-scan"});
        }
        if (m_Future.valid())
            (void)m_Future.get();
        ProjectMetadataScanRequest request;
        const auto projects = controller.Projects().Snapshot();
        request.Projects.reserve(projects->size());
        for (const auto& project : *projects)
            request.Projects.push_back({.ProjectId = project.Id, .Root = project.Root});
        if (request.Projects.empty())
        {
            m_ThumbnailCache.Clear();
            Publish({.Thumbnails = m_ThumbnailCache.Snapshot()});
            return HubStatus::Success();
        }

        m_CancelRequested.store(false, std::memory_order_release);
        Publish({.Running = true, .Total = request.Projects.size()});
        m_Future = m_Scanner.ScanAsync(
            std::move(request),
            {.IsCancelled = [this] { return m_CancelRequested.load(std::memory_order_acquire); },
             .ReportProgress =
                 [this](const ProjectMetadataScanProgress& progress)
             {
                 Publish(
                     {.Running = true, .Completed = progress.CandidatesCompleted, .Total = progress.TotalCandidates});
             }});
        return HubStatus::Success();
    }

    HubResult<bool> HubProjectMetadataWorkflow::Poll(HubController& controller)
    {
        if (!m_Future.valid() || m_Future.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            return HubResult<bool>::Success(false);
        auto scanned = m_Future.get();
        if (!scanned)
        {
            Publish({.Failure = scanned.Error()});
            return HubResult<bool>::Failure(scanned.Error());
        }
        const auto& snapshot = *scanned.Value();
        for (const auto& result : snapshot.Results)
        {
            if (!result.Error)
                continue;
            const auto& error = *result.Error;
            KEIRE_CLIENT_ERROR("[Project Hub] Project metadata scan failed [{}] for {}: {}{}{}", ToString(error.Code),
                               result.ProjectId, error.Message, error.TechnicalDetails.empty() ? "" : " ",
                               error.TechnicalDetails);
        }
        std::vector<HubProjectMetadataUpdate> metadataUpdates;
        metadataUpdates.reserve(snapshot.Results.size());
        for (const auto& result : snapshot.Results)
            metadataUpdates.push_back({.ProjectId = result.ProjectId, .Metadata = result.Metadata});
        if (const auto status = controller.Projects().UpdateCachedMetadataMany(metadataUpdates); !status)
        {
            Publish({.Completed = snapshot.CandidatesCompleted,
                     .Total = snapshot.TotalCandidates,
                     .Failure = status.Error()});
            return HubResult<bool>::Failure(status.Error());
        }
        auto updatedThumbnails = m_ThumbnailCache;
        if (snapshot.State == ProjectMetadataScanState::Completed)
            updatedThumbnails.Clear();
        for (const auto& result : snapshot.Results)
        {
            if (result.Thumbnail && result.ThumbnailImage)
            {
                updatedThumbnails.Store(
                    {.ProjectId = result.ProjectId, .Metadata = *result.Thumbnail, .Image = *result.ThumbnailImage});
            }
            else
                updatedThumbnails.Erase(result.ProjectId);
        }
        m_ThumbnailCache = std::move(updatedThumbnails);
        Publish({.Completed = snapshot.CandidatesCompleted,
                 .Total = snapshot.TotalCandidates,
                 .Thumbnails = m_ThumbnailCache.Snapshot()});
        return HubResult<bool>::Success(true);
    }

    void HubProjectMetadataWorkflow::Cancel() noexcept { m_CancelRequested.store(true, std::memory_order_release); }

    std::shared_ptr<const HubProjectMetadataWorkflowSnapshot> HubProjectMetadataWorkflow::Snapshot() const
    {
        std::scoped_lock lock(m_Mutex);
        return m_Snapshot;
    }

    void HubProjectMetadataWorkflow::Publish(HubProjectMetadataWorkflowSnapshot snapshot)
    {
        std::scoped_lock lock(m_Mutex);
        if (!snapshot.Thumbnails)
            snapshot.Thumbnails = m_Snapshot->Thumbnails;
        m_Snapshot = std::make_shared<const HubProjectMetadataWorkflowSnapshot>(std::move(snapshot));
    }

    void ApplyHubProjectMetadataSnapshot(const HubProjectMetadataWorkflowSnapshot& metadata,
                                         HubProductSnapshot& product)
    {
        std::erase_if(product.Tasks, [](const auto& task) { return task.Id == "project-metadata-scan"; });
        if (!metadata.Running)
            return;
        const auto progress =
            metadata.Total == 0 ? 0.0F : static_cast<float>(metadata.Completed) / static_cast<float>(metadata.Total);
        product.Tasks.push_back(
            {.Id = "project-metadata-scan",
             .Title = "Refresh project metadata",
             .Phase = "Scanning",
             .Message = std::to_string(metadata.Completed) + " / " + std::to_string(metadata.Total) + " projects",
             .Progress = progress,
             .Active = true});
    }
} // namespace KeireHub
