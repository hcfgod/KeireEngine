#pragma once

#include "KeireHub/HubProductUi.h"

#include "KeireHubRuntime/TemplateManager.h"

#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace KeireHub
{
    struct HubTemplateCreationRequest final
    {
        std::string TemplateId;
        std::string ProjectName;
        std::filesystem::path ParentDirectory;
        std::string EditorId;
        std::string EditorVersion;
        std::filesystem::path EditorAssetToolEntrypoint;
        std::string HostPlatform;
        std::string HostArchitecture;
        std::uint32_t MinimumProjectSchema = 1;
        std::uint32_t MaximumProjectSchema = 3;
    };

    enum class HubTemplateCreationState
    {
        Idle,
        Queued,
        Staging,
        Validating,
        Completed,
        Failed
    };

    struct HubTemplateCreationSnapshot final
    {
        std::uint64_t OperationId = 0;
        HubTemplateCreationState State = HubTemplateCreationState::Idle;
        float Progress = 0.0F;
        std::string Message;
        std::string ProjectName;
        std::string EditorId;
        std::optional<TemplateCreationResult> Result;
        std::optional<HubError> Failure;

        [[nodiscard]] bool Busy() const noexcept
        {
            return State == HubTemplateCreationState::Queued || State == HubTemplateCreationState::Staging ||
                   State == HubTemplateCreationState::Validating;
        }
    };

    class HubTemplateWorkflow final
    {
      public:
        explicit HubTemplateWorkflow(const std::filesystem::path& hubExecutable);
        ~HubTemplateWorkflow();

        HubTemplateWorkflow(const HubTemplateWorkflow&) = delete;
        HubTemplateWorkflow& operator=(const HubTemplateWorkflow&) = delete;
        HubTemplateWorkflow(HubTemplateWorkflow&&) = delete;
        HubTemplateWorkflow& operator=(HubTemplateWorkflow&&) = delete;

        [[nodiscard]] HubStatus Load();
        [[nodiscard]] std::vector<HubTemplateUiRecord> UiSnapshot() const;
        [[nodiscard]] HubStatus StartCreate(HubTemplateCreationRequest request);
        [[nodiscard]] std::shared_ptr<const HubTemplateCreationSnapshot> CreationSnapshot() const;
        void ApplyCreationSnapshot(HubProductSnapshot& product) const;

        [[nodiscard]] HubResult<TemplateCreationResult>
        Create(std::string_view templateId, std::string projectName, const std::filesystem::path& parentDirectory,
               std::string_view editorVersion, std::string_view hostPlatform, std::string_view hostArchitecture,
               std::uint32_t minimumProjectSchema, std::uint32_t maximumProjectSchema) const;

      private:
        void CreationWorker(std::stop_token stop);
        void PublishCreation(HubTemplateCreationSnapshot snapshot);

        std::filesystem::path m_DistributionRoot;
        std::unique_ptr<TemplateManager> m_Manager;
        mutable std::mutex m_CreationMutex;
        std::condition_variable m_CreationCondition;
        std::optional<HubTemplateCreationRequest> m_PendingCreation;
        std::shared_ptr<const HubTemplateCreationSnapshot> m_CreationSnapshot;
        std::uint64_t m_NextOperationId = 1;
        std::jthread m_CreationWorker;
    };
} // namespace KeireHub
