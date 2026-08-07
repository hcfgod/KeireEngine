#pragma once

#include "KeireHub/HubProductUi.h"

#include "KeireHubRuntime/HubController.h"
#include "KeireHubRuntime/ProjectMetadataScanner.h"

#include <atomic>
#include <future>
#include <memory>
#include <mutex>
#include <optional>

namespace KeireHub
{
    struct HubProjectMetadataWorkflowSnapshot final
    {
        bool Running = false;
        std::size_t Completed = 0;
        std::size_t Total = 0;
        std::shared_ptr<const std::vector<ProjectThumbnail>> Thumbnails;
        std::optional<HubError> Failure;
    };

    class HubProjectMetadataWorkflow final
    {
      public:
        HubProjectMetadataWorkflow();
        ~HubProjectMetadataWorkflow();

        HubProjectMetadataWorkflow(const HubProjectMetadataWorkflow&) = delete;
        HubProjectMetadataWorkflow& operator=(const HubProjectMetadataWorkflow&) = delete;

        [[nodiscard]] HubStatus Start(HubController& controller);
        [[nodiscard]] HubResult<bool> Poll(HubController& controller);
        void Cancel() noexcept;
        [[nodiscard]] std::shared_ptr<const HubProjectMetadataWorkflowSnapshot> Snapshot() const;

      private:
        using ScanResult = HubResult<std::shared_ptr<const ProjectMetadataScanSnapshot>>;

        void Publish(HubProjectMetadataWorkflowSnapshot snapshot);

        ProjectMetadataScanner m_Scanner;
        ProjectThumbnailCache m_ThumbnailCache;
        std::future<ScanResult> m_Future;
        std::atomic_bool m_CancelRequested = false;
        mutable std::mutex m_Mutex;
        std::shared_ptr<const HubProjectMetadataWorkflowSnapshot> m_Snapshot;
    };

    void ApplyHubProjectMetadataSnapshot(const HubProjectMetadataWorkflowSnapshot& metadata,
                                         HubProductSnapshot& product);
} // namespace KeireHub
