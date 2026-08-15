#include "KeireHub/HubEditorLaunch.h"

#include "KeireHubRuntime/EditorSelection.h"

#include "KeireInternal/Process.h"

#include <array>
#include <utility>
#include <vector>

namespace KeireHub
{
    namespace
    {
        [[nodiscard]] Keire::ProjectDescriptor
        DescriptorFromInspection(const Keire::ProjectInspectionResult& inspection)
        {
            Keire::ProjectDescriptor descriptor;
            descriptor.SchemaVersion = inspection.SchemaVersion;
            descriptor.Id = inspection.Id;
            descriptor.Name = inspection.Name;
            descriptor.CreatedWithEngineVersion = inspection.CreatedWithEngineVersion;
            descriptor.MinimumEngineVersion = inspection.MinimumEngineVersion;
            descriptor.CreatedAt = inspection.CreatedAt;
            descriptor.LastSavedWithEngineVersion = inspection.LastSavedWithEngineVersion;
            descriptor.Template = inspection.Template;
            return descriptor;
        }
    } // namespace

    HubResult<HubSelectedEditor> SelectEditorForProject(const std::span<const HubEditorUiRecord> editors,
                                                        const Keire::ProjectDescriptor& project,
                                                        const std::string_view preferredInstallationId)
    {
        std::vector<EditorInstallation> installations;
        installations.reserve(editors.size());
        for (const auto& editor : editors)
        {
            auto relative = editor.Entrypoint.lexically_relative(editor.Root);
            if (relative.empty() || relative.is_absolute() || *relative.begin() == "..")
                continue;
            installations.push_back(
                {.Id = editor.Id,
                 .Version = editor.Version,
                 .Root = editor.Root,
                 .Entrypoints = {std::move(relative)},
                 .MinimumProjectSchema = editor.MinimumProjectSchema,
                 .MaximumProjectSchema = editor.MaximumProjectSchema,
                 .Health = editor.Healthy ? InstallationHealth::Healthy : InstallationHealth::Damaged});
        }
        auto selected =
            SelectCompatibleEditor(installations, {.PreferredInstallationId = std::string(preferredInstallationId),
                                                   .LastSavedVersion = project.LastSavedWithEngineVersion,
                                                   .MinimumVersion = project.MinimumEngineVersion,
                                                   .ProjectSchema = project.SchemaVersion});
        if (!selected)
            return HubResult<HubSelectedEditor>::Failure(selected.Error());
        const auto& installation = selected.Value();
        const auto executable = installation.Root / ResolveEditorEntrypoint(installation);
        if (!std::filesystem::is_regular_file(executable))
        {
            return HubResult<HubSelectedEditor>::Failure({.Code = HubErrorCode::NotFound,
                                                          .Message = "The selected editor entrypoint is missing.",
                                                          .Retryable = true,
                                                          .AffectedItem = installation.Id});
        }
        return HubResult<HubSelectedEditor>::Success({.InstallationId = installation.Id, .Executable = executable});
    }

    HubResult<HubSelectedEditor> SelectEditorForProject(const std::span<const HubEditorUiRecord> editors,
                                                        const Keire::ProjectInspectionResult& inspection,
                                                        const std::string_view preferredInstallationId)
    {
        return SelectEditorForProject(editors, DescriptorFromInspection(inspection), preferredInstallationId);
    }

    HubResult<HubProjectLaunchResult> LaunchProjectEditor(const std::span<const HubEditorUiRecord> editors,
                                                          EditorProcessTracker& processes,
                                                          const Keire::ProjectInspectionResult& inspection,
                                                          const std::string_view preferredInstallationId,
                                                          const bool requirePreferred,
                                                          const std::uint64_t nowUnixSeconds)
    {
        if (!inspection.HasIdentity() || inspection.Root.empty() ||
            (inspection.Status != Keire::ProjectStatus::Ready &&
             inspection.Status != Keire::ProjectStatus::UpgradeAvailable &&
             inspection.Status != Keire::ProjectStatus::RequiresNewerEngine &&
             inspection.Status != Keire::ProjectStatus::UnsupportedSchema))
        {
            return HubResult<HubProjectLaunchResult>::Failure(
                {.Code = HubErrorCode::ProjectValidationFailed,
                 .Message = "The project is not in a state that an installed editor can open.",
                 .AffectedItem = inspection.Name,
                 .TechnicalDetails = inspection.Diagnostic});
        }
        (void)processes.Refresh();
        if (Keire::Project::IsLocked(inspection.Root) || processes.IsProjectRunning(inspection.Id.ToString()))
        {
            return HubResult<HubProjectLaunchResult>::Failure({.Code = HubErrorCode::EditorRunning,
                                                               .Message = "Project is already open in another editor.",
                                                               .AffectedItem = inspection.Name});
        }

        auto descriptor = DescriptorFromInspection(inspection);
        auto editor = SelectEditorForProject(editors, inspection, preferredInstallationId);
        if (!editor)
            return HubResult<HubProjectLaunchResult>::Failure(editor.Error());
        if (requirePreferred && editor.Value().InstallationId != preferredInstallationId)
        {
            return HubResult<HubProjectLaunchResult>::Failure(
                {.Code = HubErrorCode::ProjectValidationFailed,
                 .Message = "The selected editor is not compatible with this project.",
                 .AffectedItem = std::string(preferredInstallationId)});
        }

        const auto pathBytes = inspection.Root.generic_u8string();
        const std::array arguments{std::string("--project"),
                                   std::string(reinterpret_cast<const char*>(pathBytes.data()), pathBytes.size())};
        std::string diagnostic;
        std::uint64_t processId = 0;
        if (!Keire::Detail::LaunchDetachedProcessAtDesktopUserIntegrity(editor.Value().Executable, arguments,
                                                                        inspection.Root, diagnostic, &processId))
        {
            return HubResult<HubProjectLaunchResult>::Failure({.Code = HubErrorCode::ProcessLaunchFailed,
                                                               .Message = "The selected editor could not be started.",
                                                               .Retryable = true,
                                                               .AffectedItem = editor.Value().InstallationId,
                                                               .TechnicalDetails = std::move(diagnostic)});
        }
        auto tracked = processes.Track({.ProcessId = processId,
                                        .ProjectId = descriptor.Id.ToString(),
                                        .InstallationId = editor.Value().InstallationId,
                                        .ProjectRoot = std::filesystem::absolute(inspection.Root),
                                        .Executable = std::filesystem::absolute(editor.Value().Executable),
                                        .LaunchedUnixSeconds = nowUnixSeconds});
        HubProjectLaunchResult result{.Descriptor = std::move(descriptor),
                                      .InstallationId = editor.Value().InstallationId,
                                      .ProcessId = processId};
        if (!tracked)
            result.TrackingFailure = tracked.Error();
        return HubResult<HubProjectLaunchResult>::Success(std::move(result));
    }
} // namespace KeireHub
