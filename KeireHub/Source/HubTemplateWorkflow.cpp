#include "KeireHub/HubTemplateWorkflow.h"

#include "KeireHub/HubLocalContent.h"

#include "Keire/Project/Project.h"
#include "KeireInternal/FileSystem.h"
#include "KeireInternal/Process.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <exception>
#include <ranges>
#include <thread>
#include <utility>

namespace KeireHub
{
    namespace
    {
        [[nodiscard]] HubStatus ValidateProject(const std::filesystem::path& root)
        {
            const auto inspection = Keire::Project::InspectMetadata(root);
            if (inspection.Status == Keire::ProjectStatus::Ready)
                return HubStatus::Success();
            return HubStatus::Failure({.Code = HubErrorCode::ProjectValidationFailed,
                                       .Message = "The staged project did not pass editor-compatible validation.",
                                       .AffectedItem = inspection.Name,
                                       .TechnicalDetails = inspection.Diagnostic});
        }

        [[nodiscard]] HubStatus ValidateProjectWithEditor(const std::filesystem::path& assetTool,
                                                          const std::filesystem::path& root, const std::stop_token stop)
        {
            if (assetTool.empty() || !assetTool.is_absolute())
            {
                return HubStatus::Failure({.Code = HubErrorCode::ProjectValidationFailed,
                                           .Message = "The selected editor has no explicit Asset Tool entrypoint.",
                                           .AffectedItem = Keire::Detail::PathToUtf8(root)});
            }
            try
            {
                constexpr auto ValidationTimeout = std::chrono::minutes(2);
                const std::array arguments{std::string("validate-project"), std::string("--project"),
                                           Keire::Detail::PathToUtf8(root)};
                auto process = Keire::Detail::ChildProcess::Start(assetTool, arguments, root);
                const auto deadline = std::chrono::steady_clock::now() + ValidationTimeout;
                while (!process.Poll())
                {
                    if (stop.stop_requested())
                    {
                        process.Terminate();
                        return HubStatus::Failure({.Code = HubErrorCode::WorkerInterrupted,
                                                   .Message = "Project creation was cancelled during validation.",
                                                   .Retryable = true,
                                                   .AffectedItem = Keire::Detail::PathToUtf8(root),
                                                   .TechnicalDetails = process.TakeOutput()});
                    }
                    if (std::chrono::steady_clock::now() >= deadline)
                    {
                        process.Terminate();
                        return HubStatus::Failure(
                            {.Code = HubErrorCode::ProjectValidationFailed,
                             .Message = "The selected editor timed out while validating the project.",
                             .Retryable = true,
                             .AffectedItem = Keire::Detail::PathToUtf8(root),
                             .TechnicalDetails = process.TakeOutput()});
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
                const auto output = process.TakeOutput();
                const auto exitCode = process.ExitCode().value_or(127);
                if (exitCode != 0)
                {
                    return HubStatus::Failure({.Code = HubErrorCode::ProjectValidationFailed,
                                               .Message = "The selected editor rejected the staged project.",
                                               .AffectedItem = Keire::Detail::PathToUtf8(root),
                                               .TechnicalDetails = "Asset Tool exited with code " +
                                                                   std::to_string(exitCode) +
                                                                   (output.empty() ? std::string{} : ".\n" + output)});
                }
                return HubStatus::Success();
            }
            catch (const std::exception& error)
            {
                return HubStatus::Failure({.Code = HubErrorCode::ProjectValidationFailed,
                                           .Message = "The selected editor could not validate the staged project.",
                                           .Retryable = true,
                                           .AffectedItem = Keire::Detail::PathToUtf8(root),
                                           .TechnicalDetails = error.what()});
            }
        }

        [[nodiscard]] std::string_view CreationPhase(const HubTemplateCreationState state) noexcept
        {
            switch (state)
            {
            case HubTemplateCreationState::Idle:
                return "Idle";
            case HubTemplateCreationState::Queued:
                return "Queued";
            case HubTemplateCreationState::Staging:
                return "Creating";
            case HubTemplateCreationState::Validating:
                return "Validating";
            case HubTemplateCreationState::Completed:
                return "Completed";
            case HubTemplateCreationState::Failed:
                return "Failed";
            }
            return "Unknown";
        }
    } // namespace

    HubTemplateWorkflow::HubTemplateWorkflow(const std::filesystem::path& hubExecutable)
        : m_DistributionRoot(ResolveHubDistributionRoot(hubExecutable)),
          m_Manager(std::make_unique<TemplateManager>(ResolveHubTemplatesRoot(hubExecutable))),
          m_CreationSnapshot(std::make_shared<const HubTemplateCreationSnapshot>()),
          m_CreationWorker([this](const std::stop_token stop) { CreationWorker(stop); })
    {
    }

    HubTemplateWorkflow::~HubTemplateWorkflow()
    {
        m_CreationWorker.request_stop();
        m_CreationCondition.notify_all();
    }

    HubStatus HubTemplateWorkflow::Load()
    {
        if (CreationSnapshot()->Busy())
        {
            return HubStatus::Failure({.Code = HubErrorCode::InstallationBusy,
                                       .Message = "A project is currently being created.",
                                       .AffectedItem = "templates"});
        }
        return m_Manager->Load();
    }

    std::vector<HubTemplateUiRecord> HubTemplateWorkflow::UiSnapshot() const
    {
        std::vector<HubTemplateUiRecord> result;
        const auto templates = m_Manager->Snapshot();
        result.reserve(templates->size());
        for (const auto& item : *templates)
            result.push_back(MakeHubTemplateUiRecord(item, m_Manager->Root()));
        return result;
    }

    HubStatus HubTemplateWorkflow::StartCreate(HubTemplateCreationRequest request)
    {
        if (request.TemplateId.empty() || request.ProjectName.empty() || request.ParentDirectory.empty() ||
            request.EditorId.empty() || request.EditorVersion.empty() || request.EditorAssetToolEntrypoint.empty() ||
            !request.EditorAssetToolEntrypoint.is_absolute() || request.HostPlatform.empty() ||
            request.HostArchitecture.empty() || request.MinimumProjectSchema == 0 ||
            request.MaximumProjectSchema < request.MinimumProjectSchema)
        {
            return HubStatus::Failure({.Code = HubErrorCode::InvalidArgument,
                                       .Message = "The project creation request is incomplete.",
                                       .AffectedItem = request.ProjectName});
        }

        std::unique_lock lock(m_CreationMutex);
        if (m_PendingCreation || m_CreationSnapshot->Busy())
        {
            return HubStatus::Failure({.Code = HubErrorCode::InstallationBusy,
                                       .Message = "Another project is already being created.",
                                       .AffectedItem = m_CreationSnapshot->ProjectName});
        }
        const auto operationId = m_NextOperationId++;
        const auto projectName = request.ProjectName;
        const auto editorId = request.EditorId;
        m_PendingCreation = std::move(request);
        m_CreationSnapshot = std::make_shared<const HubTemplateCreationSnapshot>(
            HubTemplateCreationSnapshot{.OperationId = operationId,
                                        .State = HubTemplateCreationState::Queued,
                                        .Progress = 0.02F,
                                        .Message = "Project creation is queued.",
                                        .ProjectName = projectName,
                                        .EditorId = editorId});
        lock.unlock();
        m_CreationCondition.notify_one();
        return HubStatus::Success();
    }

    std::shared_ptr<const HubTemplateCreationSnapshot> HubTemplateWorkflow::CreationSnapshot() const
    {
        std::scoped_lock lock(m_CreationMutex);
        return m_CreationSnapshot;
    }

    void HubTemplateWorkflow::ApplyCreationSnapshot(HubProductSnapshot& product) const
    {
        constexpr std::string_view TaskId = "project-creation";
        const auto creation = CreationSnapshot();
        product.ProjectCreationBusy = creation->Busy();
        product.ProjectCreationEditorId = creation->EditorId;
        product.ProjectCreationProgress = creation->Progress;
        product.ProjectCreationMessage = creation->Message;
        std::erase_if(product.Tasks,
                      [](const HubTaskUiRecord& task) { return task.Id == std::string_view("project-creation"); });
        if (creation->State == HubTemplateCreationState::Idle)
            return;

        product.Tasks.push_back(
            {.Id = std::string(TaskId),
             .Title = creation->ProjectName.empty() ? "Create project" : "Create " + creation->ProjectName,
             .Phase = std::string(CreationPhase(creation->State)),
             .Message = creation->Failure ? creation->Failure->Message : creation->Message,
             .Progress = std::clamp(creation->Progress, 0.0F, 1.0F),
             .Active = creation->Busy(),
             .Pausable = false,
             .Paused = false,
             .Cancellable = false,
             .Retryable = false});
    }

    void HubTemplateWorkflow::CreationWorker(const std::stop_token stop)
    {
        for (;;)
        {
            std::unique_lock lock(m_CreationMutex);
            m_CreationCondition.wait(lock,
                                     [this, stop] { return stop.stop_requested() || m_PendingCreation.has_value(); });
            if (stop.stop_requested())
            {
                m_PendingCreation.reset();
                return;
            }

            auto request = std::move(*m_PendingCreation);
            m_PendingCreation.reset();
            const auto operationId = m_CreationSnapshot->OperationId;
            m_CreationSnapshot = std::make_shared<const HubTemplateCreationSnapshot>(
                HubTemplateCreationSnapshot{.OperationId = operationId,
                                            .State = HubTemplateCreationState::Staging,
                                            .Progress = 0.10F,
                                            .Message = "Staging verified template files.",
                                            .ProjectName = request.ProjectName,
                                            .EditorId = request.EditorId});
            lock.unlock();

            try
            {
                auto parsedVersion = SemanticVersion::Parse(request.EditorVersion);
                if (!parsedVersion)
                {
                    PublishCreation({.OperationId = operationId,
                                     .State = HubTemplateCreationState::Failed,
                                     .Progress = 1.0F,
                                     .Message = parsedVersion.Error().Message,
                                     .ProjectName = request.ProjectName,
                                     .EditorId = request.EditorId,
                                     .Failure = parsedVersion.Error()});
                    continue;
                }

                TemplateCreateRequest creation;
                creation.TemplateId = request.TemplateId;
                creation.ProjectName = request.ProjectName;
                creation.Destination = request.ParentDirectory / Keire::Detail::PathFromUtf8(request.ProjectName);
                creation.EditorVersion = std::move(parsedVersion).Value();
                creation.EditorMinimumProjectSchema = request.MinimumProjectSchema;
                creation.EditorMaximumProjectSchema = request.MaximumProjectSchema;
                creation.HostPlatform = request.HostPlatform;
                creation.HostArchitecture = request.HostArchitecture;
                creation.ForbiddenDestinationRoots = {m_DistributionRoot};
                creation.CancellationRequested = [stop] { return stop.stop_requested(); };
                creation.ValidateStagedProject =
                    [this, operationId, projectName = request.ProjectName, editorId = request.EditorId,
                     assetTool = request.EditorAssetToolEntrypoint, stop](const std::filesystem::path& root)
                {
                    PublishCreation({.OperationId = operationId,
                                     .State = HubTemplateCreationState::Validating,
                                     .Progress = 0.82F,
                                     .Message = "Validating with the selected editor.",
                                     .ProjectName = projectName,
                                     .EditorId = editorId});
                    return ValidateProjectWithEditor(assetTool, root, stop);
                };
                auto result = m_Manager->CreateProject(creation);
                if (!result)
                {
                    PublishCreation({.OperationId = operationId,
                                     .State = HubTemplateCreationState::Failed,
                                     .Progress = 1.0F,
                                     .Message = result.Error().Message,
                                     .ProjectName = request.ProjectName,
                                     .EditorId = request.EditorId,
                                     .Failure = result.Error()});
                    continue;
                }
                const auto root = result.Value().Root;
                PublishCreation({.OperationId = operationId,
                                 .State = HubTemplateCreationState::Completed,
                                 .Progress = 1.0F,
                                 .Message = "Project created at " + Keire::Detail::PathToUtf8(root) + ".",
                                 .ProjectName = request.ProjectName,
                                 .EditorId = request.EditorId,
                                 .Result = std::move(result).Value()});
            }
            catch (const std::exception& error)
            {
                HubError failure{.Code = HubErrorCode::ProjectValidationFailed,
                                 .Message = "Project creation failed unexpectedly.",
                                 .AffectedItem = request.ProjectName,
                                 .TechnicalDetails = error.what()};
                PublishCreation({.OperationId = operationId,
                                 .State = HubTemplateCreationState::Failed,
                                 .Progress = 1.0F,
                                 .Message = failure.Message,
                                 .ProjectName = request.ProjectName,
                                 .EditorId = request.EditorId,
                                 .Failure = std::move(failure)});
            }
            catch (...)
            {
                HubError failure{.Code = HubErrorCode::ProjectValidationFailed,
                                 .Message = "Project creation failed unexpectedly.",
                                 .AffectedItem = request.ProjectName,
                                 .TechnicalDetails = "The creation worker failed with a non-standard exception."};
                PublishCreation({.OperationId = operationId,
                                 .State = HubTemplateCreationState::Failed,
                                 .Progress = 1.0F,
                                 .Message = failure.Message,
                                 .ProjectName = request.ProjectName,
                                 .EditorId = request.EditorId,
                                 .Failure = std::move(failure)});
            }
        }
    }

    void HubTemplateWorkflow::PublishCreation(HubTemplateCreationSnapshot snapshot)
    {
        std::scoped_lock lock(m_CreationMutex);
        m_CreationSnapshot = std::make_shared<const HubTemplateCreationSnapshot>(std::move(snapshot));
    }

    HubResult<TemplateCreationResult>
    HubTemplateWorkflow::Create(const std::string_view templateId, std::string projectName,
                                const std::filesystem::path& parentDirectory, const std::string_view editorVersion,
                                const std::string_view hostPlatform, const std::string_view hostArchitecture,
                                const std::uint32_t minimumProjectSchema,
                                const std::uint32_t maximumProjectSchema) const
    {
        auto parsedVersion = SemanticVersion::Parse(editorVersion);
        if (!parsedVersion)
            return HubResult<TemplateCreationResult>::Failure(parsedVersion.Error());
        TemplateCreateRequest request;
        request.TemplateId = std::string(templateId);
        request.ProjectName = std::move(projectName);
        request.Destination = parentDirectory / Keire::Detail::PathFromUtf8(request.ProjectName);
        request.EditorVersion = std::move(parsedVersion).Value();
        request.EditorMinimumProjectSchema = minimumProjectSchema;
        request.EditorMaximumProjectSchema = maximumProjectSchema;
        request.HostPlatform = std::string(hostPlatform);
        request.HostArchitecture = std::string(hostArchitecture);
        request.ForbiddenDestinationRoots = {m_DistributionRoot};
        request.ValidateStagedProject = &ValidateProject;
        return m_Manager->CreateProject(request);
    }
} // namespace KeireHub
