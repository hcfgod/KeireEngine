#include "KeireHub/HubFirstRunImportPreparation.h"

#include "KeireHub/HubEditorDiscovery.h"

#include "Keire/Project/Project.h"

#include <algorithm>
#include <exception>
#include <ranges>
#include <set>
#include <string>
#include <string_view>
#include <utility>

namespace KeireHub
{
    namespace
    {
        constexpr std::size_t MaximumPreparedProjects = 256;
        constexpr std::size_t MaximumPreparedEditors = 128;

        [[nodiscard]] HubError PreparationError(const HubErrorCode code, std::string message, std::string item,
                                                std::string details = {})
        {
            return {.Code = code,
                    .Message = std::move(message),
                    .AffectedItem = std::move(item),
                    .TechnicalDetails = std::move(details)};
        }

        [[nodiscard]] HubProjectStatus CatalogStatus(const Keire::ProjectStatus status) noexcept
        {
            switch (status)
            {
            case Keire::ProjectStatus::Ready:
                return HubProjectStatus::Ready;
            case Keire::ProjectStatus::UpgradeAvailable:
                return HubProjectStatus::UpgradeAvailable;
            case Keire::ProjectStatus::RecoveryRequired:
                return HubProjectStatus::RecoveryRequired;
            case Keire::ProjectStatus::Missing:
                return HubProjectStatus::Missing;
            case Keire::ProjectStatus::Invalid:
                return HubProjectStatus::Invalid;
            case Keire::ProjectStatus::RequiresNewerEngine:
                return HubProjectStatus::MissingEditor;
            case Keire::ProjectStatus::InUse:
                return HubProjectStatus::Locked;
            case Keire::ProjectStatus::UnsupportedSchema:
                return HubProjectStatus::UnsupportedSchema;
            }
            return HubProjectStatus::Invalid;
        }

        [[nodiscard]] bool SameRoot(const std::filesystem::path& left, const std::filesystem::path& right)
        {
            if (left.lexically_normal() == right.lexically_normal())
                return true;
            std::error_code error;
            return std::filesystem::equivalent(left, right, error) && !error;
        }

        [[nodiscard]] bool Cancelled(const HubFirstRunImportPreparationHooks& hooks)
        {
            return hooks.IsCancelled && hooks.IsCancelled();
        }

        [[nodiscard]] HubError CancelledError()
        {
            return PreparationError(HubErrorCode::InvalidTransition, "First-run import preparation was cancelled.",
                                    "first-run-import");
        }

        [[nodiscard]] HubResult<HubRecentProject> PrepareProject(const HubFirstRunProjectCandidate& candidate,
                                                                 const std::uint64_t nowUnixSeconds,
                                                                 const HubFirstRunImportPreparationHooks& hooks)
        {
            if (hooks.BeforeProjectInspection)
                hooks.BeforeProjectInspection(candidate.Root);
            if (Cancelled(hooks))
                return HubResult<HubRecentProject>::Failure(CancelledError());
            const auto inspection = Keire::Project::InspectMetadata(candidate.Root);
            if (!inspection.HasIdentity() || inspection.Status == Keire::ProjectStatus::Missing ||
                inspection.Status == Keire::ProjectStatus::Invalid || inspection.Id.ToString() != candidate.Id ||
                !SameRoot(inspection.Root, candidate.Root))
            {
                return HubResult<HubRecentProject>::Failure(PreparationError(
                    HubErrorCode::ProjectValidationFailed, "A discovered project changed before it could be imported.",
                    candidate.Id, inspection.Diagnostic));
            }
            return HubResult<HubRecentProject>::Success(
                {.Id = inspection.Id.ToString(),
                 .Root = inspection.Root,
                 .Name = inspection.Name,
                 .AddedUnixSeconds = nowUnixSeconds,
                 .CachedMetadata = {.CreatedUnixSeconds = nowUnixSeconds,
                                    .CreatedWithEngineVersion = inspection.CreatedWithEngineVersion,
                                    .LastSavedWithEngineVersion = inspection.LastSavedWithEngineVersion,
                                    .MinimumEngineVersion = inspection.MinimumEngineVersion,
                                    .ProjectSchemaVersion = inspection.SchemaVersion,
                                    .Status = CatalogStatus(inspection.Status)}});
        }

        [[nodiscard]] HubResult<EditorInstallation> PrepareEditor(const HubFirstRunEditorCandidate& candidate,
                                                                  const std::size_t index,
                                                                  const HubFirstRunImportPreparationHooks& hooks)
        {
            if (hooks.BeforeEditorInspection)
                hooks.BeforeEditorInspection(candidate.Root);
            if (Cancelled(hooks))
                return HubResult<EditorInstallation>::Failure(CancelledError());
            const auto prefix = candidate.ManifestFingerprint.substr(
                0, std::min<std::size_t>(48, candidate.ManifestFingerprint.size()));
            auto inspected = InspectExternalEditor(candidate.Root, "external-" + prefix + "-" + std::to_string(index));
            if (!inspected)
                return inspected;
            if (!SameRoot(inspected.Value().Root, candidate.Root) ||
                inspected.Value().ManifestFingerprint != candidate.ManifestFingerprint ||
                inspected.Value().Version != candidate.Version || inspected.Value().Platform != candidate.Platform ||
                inspected.Value().Architecture != candidate.Architecture)
            {
                return HubResult<EditorInstallation>::Failure(PreparationError(
                    HubErrorCode::EditorManifestInvalid, "A discovered editor changed before it could be imported.",
                    candidate.ManifestFingerprint));
            }
            return inspected;
        }
    } // namespace

    HubResult<HubFirstRunPreparedImport> PrepareHubFirstRunImport(const HubFirstRunDiscoverySnapshot& discovery,
                                                                  const std::uint64_t nowUnixSeconds,
                                                                  const HubFirstRunImportPreparationHooks& hooks)
    {
        if (discovery.State != HubFirstRunDiscoveryState::Completed || nowUnixSeconds == 0 ||
            discovery.Projects.size() > MaximumPreparedProjects || discovery.Editors.size() > MaximumPreparedEditors)
        {
            return HubResult<HubFirstRunPreparedImport>::Failure(
                PreparationError(HubErrorCode::InvalidArgument, "The first-run import preparation request is invalid.",
                                 "first-run-import"));
        }
        try
        {
            HubFirstRunPreparedImport prepared;
            prepared.Projects.reserve(discovery.Projects.size());
            prepared.Editors.reserve(discovery.Editors.size());
            std::set<std::string, std::less<>> projectIds;
            std::set<std::filesystem::path> projectRoots;
            for (const auto& candidate : discovery.Projects)
            {
                if (Cancelled(hooks))
                    return HubResult<HubFirstRunPreparedImport>::Failure(CancelledError());
                auto project = PrepareProject(candidate, nowUnixSeconds, hooks);
                if (!project)
                    return HubResult<HubFirstRunPreparedImport>::Failure(project.Error());
                if (!projectIds.insert(project.Value().Id).second ||
                    !projectRoots.insert(project.Value().Root.lexically_normal()).second)
                {
                    return HubResult<HubFirstRunPreparedImport>::Failure(
                        PreparationError(HubErrorCode::DuplicateIdentifier,
                                         "The discovered project import contains a duplicate identity or location.",
                                         project.Value().Id));
                }
                prepared.Projects.push_back(std::move(project).Value());
            }

            std::set<std::string, std::less<>> editorIds;
            std::set<std::filesystem::path> editorRoots;
            for (std::size_t index = 0; index < discovery.Editors.size(); ++index)
            {
                if (Cancelled(hooks))
                    return HubResult<HubFirstRunPreparedImport>::Failure(CancelledError());
                auto editor = PrepareEditor(discovery.Editors[index], index, hooks);
                if (!editor)
                    return HubResult<HubFirstRunPreparedImport>::Failure(editor.Error());
                if (!editorIds.insert(editor.Value().Id).second ||
                    !editorRoots.insert(editor.Value().Root.lexically_normal()).second)
                {
                    return HubResult<HubFirstRunPreparedImport>::Failure(PreparationError(
                        HubErrorCode::DuplicateIdentifier,
                        "The discovered editor import contains a duplicate identity or location.", editor.Value().Id));
                }
                prepared.Editors.push_back(std::move(editor).Value());
            }
            return HubResult<HubFirstRunPreparedImport>::Success(std::move(prepared));
        }
        catch (const std::exception& error)
        {
            return HubResult<HubFirstRunPreparedImport>::Failure(
                PreparationError(HubErrorCode::InvalidData, "First-run import preparation failed unexpectedly.",
                                 "first-run-import", error.what()));
        }
        catch (...)
        {
            return HubResult<HubFirstRunPreparedImport>::Failure(PreparationError(
                HubErrorCode::InvalidData, "First-run import preparation failed unexpectedly.", "first-run-import"));
        }
    }

    HubStatus CommitHubFirstRunImport(const HubFirstRunPreparedImport& prepared, HubController& controller)
    {
        return controller.UpsertProjectsAndInstallations(prepared.Projects, prepared.Editors);
    }
} // namespace KeireHub
