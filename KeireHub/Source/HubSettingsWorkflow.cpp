#include "KeireHub/HubSettingsWorkflow.h"

#include "KeireHubRuntime/HubMaintenance.h"

#include "Keire/PlatformDirectories.h"

#include <string>
#include <system_error>
#include <utility>

namespace KeireHub
{
    std::filesystem::path DefaultHubProjectLocation()
    {
        return Keire::GetUserDocumentsDirectory() / std::filesystem::path(u8"Kéire Projects");
    }

    HubResult<HubSettings> ResetHubSettings(HubController& controller)
    {
        return ResetHubSettings(controller, DefaultHubProjectLocation());
    }

    HubResult<HubSettings> ResetHubSettings(HubController& controller, const std::filesystem::path& defaultProjectRoot)
    {
        HubSettings settings;
        const auto preferenceRoot = controller.Settings().Path().parent_path();
        settings.DefaultProjectLocation = defaultProjectRoot;
        settings.DefaultEditorRoot = preferenceRoot / "Editors";
        settings.CacheRoot = preferenceRoot / "Cache";
        settings.TemporaryRoot = preferenceRoot / "Temporary";
        if (const auto status = controller.Settings().Save(settings); !status)
            return HubResult<HubSettings>::Failure(status.Error());
        return HubResult<HubSettings>::Success(std::move(settings));
    }

    HubStatus ClearHubVerifiedCache(HubController& controller)
    {
        const auto tasks = controller.Tasks().Snapshot();
        return ClearHubVerifiedCache(controller, *tasks);
    }

    HubStatus ClearHubVerifiedCache(HubController& controller, const std::span<const HubTask> tasks)
    {
        if (const auto status = ValidateVerifiedPackageCacheClear(tasks); !status)
            return status;
        return ClearVerifiedPackageCache(controller.Settings().Snapshot()->CacheRoot);
    }

    HubStatus SaveHubProjectPreferences(HubController& controller, const bool cards, const std::uint8_t sortIndex)
    {
        if (sortIndex > static_cast<std::uint8_t>(ProjectSort::Size))
            return HubStatus::Failure({.Code = HubErrorCode::InvalidArgument,
                                       .Message = "The project sort option is invalid.",
                                       .AffectedItem = "projectsSort"});
        auto settings = *controller.Settings().Snapshot();
        settings.ProjectsView = cards ? ProjectView::Cards : ProjectView::Table;
        settings.ProjectsSort = static_cast<ProjectSort>(sortIndex);
        return controller.Settings().Save(std::move(settings));
    }

    HubStatus EnsureHubSettingsDirectory(const std::filesystem::path& path, const std::string_view label)
    {
        if (path.empty() || !path.is_absolute())
            return HubStatus::Failure({.Code = HubErrorCode::InvalidArgument,
                                       .Message = std::string(label) + " must be an absolute folder path."});
        std::error_code error;
        std::filesystem::create_directories(path, error);
        if (error || !std::filesystem::is_directory(path))
            return HubStatus::Failure({.Code = HubErrorCode::IoWrite,
                                       .Message = std::string(label) + " could not be created or opened.",
                                       .TechnicalDetails = error.message()});
        return HubStatus::Success();
    }
} // namespace KeireHub
