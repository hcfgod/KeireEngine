#include "KeireHub/HubProjectWorkflow.h"

#include <ranges>
#include <utility>

namespace KeireHub
{
    namespace
    {
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
    } // namespace

    HubProjectWorkflow::HubProjectWorkflow(HubController& controller)
        : m_Controller(controller), m_Workflows(controller.Projects())
    {
    }

    HubStatus HubProjectWorkflow::ReloadAuthoritativeCatalog() { return m_Controller.Projects().Load(); }

    HubStatus HubProjectWorkflow::SetPinned(const std::string& projectId, const bool pinned)
    {
        if (const auto status = ReloadAuthoritativeCatalog(); !status)
            return status;
        return m_Controller.Projects().SetPinned(projectId, pinned);
    }

    HubStatus HubProjectWorkflow::Add(const Keire::ProjectInspectionResult& inspection,
                                      const std::uint64_t nowUnixSeconds)
    {
        if (const auto status = ReloadAuthoritativeCatalog(); !status)
            return status;
        if (!inspection.HasIdentity() || inspection.Root.empty() ||
            inspection.Status == Keire::ProjectStatus::Missing || inspection.Status == Keire::ProjectStatus::Invalid)
        {
            return HubStatus::Failure({.Code = HubErrorCode::ProjectValidationFailed,
                                       .Message = "The selected folder is not a valid Kéire project.",
                                       .AffectedItem = inspection.Name,
                                       .TechnicalDetails = inspection.Diagnostic});
        }
        HubRecentProject recent{.Id = inspection.Id.ToString(),
                                .Root = inspection.Root,
                                .Name = inspection.Name,
                                .AddedUnixSeconds = nowUnixSeconds};
        const auto projects = m_Controller.Projects().Snapshot();
        if (const auto found = std::ranges::find(*projects, recent.Id, &HubRecentProject::Id); found != projects->end())
        {
            if (!SameRoot(found->Root, inspection.Root))
            {
                return HubStatus::Failure(
                    {.Code = HubErrorCode::DuplicateIdentifier,
                     .Message = "This project is already registered at another location. Use Locate moved project "
                                "to update its path.",
                     .AffectedItem = recent.Id});
            }
            recent = *found;
            recent.Root = inspection.Root;
            recent.Name = inspection.Name;
        }
        if (!recent.CachedMetadata.CreatedUnixSeconds)
            recent.CachedMetadata.CreatedUnixSeconds = nowUnixSeconds;
        recent.CachedMetadata.CreatedWithEngineVersion = inspection.CreatedWithEngineVersion;
        recent.CachedMetadata.LastSavedWithEngineVersion = inspection.LastSavedWithEngineVersion;
        recent.CachedMetadata.MinimumEngineVersion = inspection.MinimumEngineVersion;
        recent.CachedMetadata.ProjectSchemaVersion = inspection.SchemaVersion;
        recent.CachedMetadata.Status = CatalogStatus(inspection.Status);
        return m_Controller.Projects().Upsert(std::move(recent));
    }
} // namespace KeireHub
