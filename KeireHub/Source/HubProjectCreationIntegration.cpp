#include "KeireHub/HubProjectCreationIntegration.h"

#include <filesystem>
#include <ranges>

namespace KeireHub
{
    HubStatus StartHubProjectCreation(HubTemplateWorkflow& workflow, const HubProductSnapshot& snapshot,
                                      const HubCreateProjectRequest& request)
    {
        const auto editor = std::ranges::find(snapshot.Editors, request.EditorId, &HubEditorUiRecord::Id);
        if (editor == snapshot.Editors.end())
        {
            return HubStatus::Failure({.Code = HubErrorCode::InvalidArgument,
                                       .Message = "The selected editor is no longer installed.",
                                       .AffectedItem = request.EditorId});
        }

        std::error_code directoryError;
        std::filesystem::create_directories(request.ParentDirectory, directoryError);
        if (directoryError || !std::filesystem::is_directory(request.ParentDirectory, directoryError) || directoryError)
        {
            return HubStatus::Failure({.Code = HubErrorCode::IoWrite,
                                       .Message = "The selected project location could not be created or opened.",
                                       .Retryable = true,
                                       .AffectedItem = request.ParentDirectory.filename().string(),
                                       .TechnicalDetails = directoryError.message()});
        }

        return workflow.StartCreate({.TemplateId = request.TemplateId,
                                     .ProjectName = request.Name,
                                     .ParentDirectory = request.ParentDirectory,
                                     .EditorId = editor->Id,
                                     .EditorVersion = editor->Version,
                                     .EditorAssetToolEntrypoint = editor->AssetToolEntrypoint,
                                     .HostPlatform = editor->Platform,
                                     .HostArchitecture = editor->Architecture,
                                     .MinimumProjectSchema = editor->MinimumProjectSchema,
                                     .MaximumProjectSchema = editor->MaximumProjectSchema});
    }
} // namespace KeireHub
