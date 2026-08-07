#include "KeireHubRuntime/HubProjectCatalog.h"

#include "Persistence.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <ranges>
#include <set>
#include <string_view>
#include <tuple>

namespace KeireHub
{
    namespace
    {
        constexpr std::size_t MaximumRegistryBytes = 2 * 1024 * 1024;
        constexpr std::size_t MaximumProjects = 256;
        constexpr std::size_t MaximumUnpinnedProjects = 50;
        constexpr std::size_t MaximumNameBytes = 512;
        constexpr std::size_t MaximumPathBytes = 4096;

        [[nodiscard]] std::string_view ToString(const HubProjectStatus value) noexcept
        {
            constexpr std::array names{"unknown",      "ready",  "upgradeAvailable", "missing",
                                       "invalid",      "locked", "recoveryRequired", "unsupportedSchema",
                                       "missingEditor"};
            return names[static_cast<std::size_t>(value)];
        }

        [[nodiscard]] std::optional<HubProjectStatus> ParseStatus(const std::string_view value) noexcept
        {
            constexpr std::array values{HubProjectStatus::Unknown,          HubProjectStatus::Ready,
                                        HubProjectStatus::UpgradeAvailable, HubProjectStatus::Missing,
                                        HubProjectStatus::Invalid,          HubProjectStatus::Locked,
                                        HubProjectStatus::RecoveryRequired, HubProjectStatus::UnsupportedSchema,
                                        HubProjectStatus::MissingEditor};
            for (const auto candidate : values)
            {
                if (ToString(candidate) == value)
                    return candidate;
            }
            return std::nullopt;
        }

        [[nodiscard]] bool IsBoundedText(const std::string_view value, const std::size_t maximum,
                                         const bool allowEmpty = false) noexcept
        {
            if ((!allowEmpty && value.empty()) || value.size() > maximum)
                return false;
            return std::ranges::none_of(value, [](const unsigned char character) { return character < 0x20; });
        }

        [[nodiscard]] HubStatus Validate(const HubRecentProject& project)
        {
            if (!IsBoundedText(project.Id, 128) || !IsBoundedText(project.Name, MaximumNameBytes) ||
                Detail::PathToUtf8(project.Root).empty() ||
                Detail::PathToUtf8(project.Root).size() > MaximumPathBytes ||
                (project.PreferredEditorInstallationId && !IsBoundedText(*project.PreferredEditorInstallationId, 128)))
            {
                return HubStatus::Failure({.Code = HubErrorCode::InvalidArgument,
                                           .Message = "A recent project contains invalid identity or path metadata.",
                                           .AffectedItem = project.Id});
            }
            const auto checkVersion = [&](const std::optional<std::string>& version)
            { return !version || IsBoundedText(*version, 128); };
            if (!checkVersion(project.CachedMetadata.CreatedWithEngineVersion) ||
                !checkVersion(project.CachedMetadata.LastSavedWithEngineVersion) ||
                !checkVersion(project.CachedMetadata.MinimumEngineVersion))
            {
                return HubStatus::Failure({.Code = HubErrorCode::InvalidArgument,
                                           .Message = "A recent project contains invalid engine metadata.",
                                           .AffectedItem = project.Id});
            }
            return HubStatus::Success();
        }

        [[nodiscard]] std::string NormalizedPathKey(const std::filesystem::path& path)
        {
            auto key = Detail::PathToUtf8(path.lexically_normal());
#if defined(_WIN32)
            std::ranges::transform(key, key.begin(),
                                   [](const unsigned char value) { return static_cast<char>(std::tolower(value)); });
#endif
            return key;
        }

        [[nodiscard]] HubStatus DuplicateBatchItem(const HubRecentProject& project, const std::string_view field)
        {
            return HubStatus::Failure({.Code = HubErrorCode::DuplicateIdentifier,
                                       .Message = "The project import contains a duplicate " + std::string(field) + ".",
                                       .AffectedItem = project.Id});
        }

        [[nodiscard]] Detail::Json SerializeMetadata(const HubProjectMetadata& metadata)
        {
            Detail::Json result{{"status", ToString(metadata.Status)}};
            if (metadata.CreatedUnixSeconds)
                result["created"] = *metadata.CreatedUnixSeconds;
            if (metadata.ModifiedUnixSeconds)
                result["modified"] = *metadata.ModifiedUnixSeconds;
            if (metadata.SizeBytes)
                result["sizeBytes"] = *metadata.SizeBytes;
            if (metadata.CreatedWithEngineVersion)
                result["createdWithEngineVersion"] = *metadata.CreatedWithEngineVersion;
            if (metadata.LastSavedWithEngineVersion)
                result["lastSavedWithEngineVersion"] = *metadata.LastSavedWithEngineVersion;
            if (metadata.MinimumEngineVersion)
                result["minimumEngineVersion"] = *metadata.MinimumEngineVersion;
            if (metadata.ProjectSchemaVersion)
                result["projectSchemaVersion"] = *metadata.ProjectSchemaVersion;
            return result;
        }

        [[nodiscard]] Detail::Json Serialize(const std::vector<HubRecentProject>& projects)
        {
            Detail::Json values = Detail::Json::array();
            for (const auto& project : projects)
            {
                Detail::Json value{{"id", project.Id},
                                   {"root", Detail::PathToUtf8(project.Root)},
                                   {"name", project.Name},
                                   {"added", project.AddedUnixSeconds},
                                   {"lastOpened", project.LastOpenedUnixSeconds},
                                   {"pinned", project.Pinned},
                                   {"cachedMetadata", SerializeMetadata(project.CachedMetadata)}};
                if (project.PreferredEditorInstallationId)
                {
                    value["preferredEditorInstallationId"] = *project.PreferredEditorInstallationId;
                    value["preferredEditorInstallation"] = *project.PreferredEditorInstallationId;
                }
                if (project.CachedMetadata.CreatedUnixSeconds)
                    value["created"] = *project.CachedMetadata.CreatedUnixSeconds;
                if (project.CachedMetadata.LastSavedWithEngineVersion)
                    value["lastSavedWithEngineVersion"] = *project.CachedMetadata.LastSavedWithEngineVersion;
                values.push_back(std::move(value));
            }
            return {{"schemaVersion", HubProjectCatalog::CurrentSchemaVersion}, {"projects", std::move(values)}};
        }

        [[nodiscard]] HubProjectMetadata ParseMetadata(const Detail::Json& value)
        {
            HubProjectMetadata result;
            const auto status = ParseStatus(value.value("status", "unknown"));
            if (!status)
                throw std::invalid_argument("Unknown cached project status.");
            result.Status = *status;
            if (value.contains("created"))
                result.CreatedUnixSeconds = value.at("created").get<std::uint64_t>();
            if (value.contains("modified"))
                result.ModifiedUnixSeconds = value.at("modified").get<std::uint64_t>();
            if (value.contains("sizeBytes"))
                result.SizeBytes = value.at("sizeBytes").get<std::uint64_t>();
            if (value.contains("createdWithEngineVersion"))
                result.CreatedWithEngineVersion = value.at("createdWithEngineVersion").get<std::string>();
            if (value.contains("lastSavedWithEngineVersion"))
                result.LastSavedWithEngineVersion = value.at("lastSavedWithEngineVersion").get<std::string>();
            if (value.contains("minimumEngineVersion"))
                result.MinimumEngineVersion = value.at("minimumEngineVersion").get<std::string>();
            if (value.contains("projectSchemaVersion"))
                result.ProjectSchemaVersion = value.at("projectSchemaVersion").get<std::uint32_t>();
            return result;
        }

        [[nodiscard]] HubResult<std::pair<std::vector<HubRecentProject>, bool>> Parse(const Detail::Json& document)
        {
            try
            {
                if (!document.is_object() || !document.contains("schemaVersion") ||
                    !document.at("schemaVersion").is_number_unsigned())
                    throw std::invalid_argument("Missing registry schema.");
                const auto schema = document.at("schemaVersion").get<std::uint32_t>();
                if (schema != 1 && schema != HubProjectCatalog::CurrentSchemaVersion)
                {
                    return HubResult<std::pair<std::vector<HubRecentProject>, bool>>::Failure(
                        {.Code = HubErrorCode::UnsupportedSchema,
                         .Message = "This recent-project registry uses an unsupported schema.",
                         .AffectedItem = "projects"});
                }
                const auto& values = document.at("projects");
                if (!values.is_array() || values.size() > MaximumProjects)
                    throw std::invalid_argument("Invalid project collection.");
                std::vector<HubRecentProject> result;
                result.reserve(values.size());
                for (const auto& value : values)
                {
                    HubRecentProject project;
                    project.Id = value.at("id").get<std::string>();
                    project.Root = Detail::PathFromUtf8(value.at("root").get<std::string>());
                    project.Name = value.at("name").get<std::string>();
                    project.LastOpenedUnixSeconds = value.value("lastOpened", 0ULL);
                    project.Pinned = value.value("pinned", false);
                    if (schema == HubProjectCatalog::CurrentSchemaVersion)
                    {
                        project.AddedUnixSeconds = value.value("added", project.LastOpenedUnixSeconds);
                        if (value.contains("preferredEditorInstallationId"))
                        {
                            auto installation = value.at("preferredEditorInstallationId").get<std::string>();
                            if (!installation.empty())
                                project.PreferredEditorInstallationId = std::move(installation);
                        }
                        if (!project.PreferredEditorInstallationId && value.contains("preferredEditorInstallation"))
                        {
                            auto installation = value.at("preferredEditorInstallation").get<std::string>();
                            if (!installation.empty())
                                project.PreferredEditorInstallationId = std::move(installation);
                        }
                        if (value.contains("cachedMetadata"))
                            project.CachedMetadata = ParseMetadata(value.at("cachedMetadata"));
                        if (!project.CachedMetadata.CreatedUnixSeconds && value.contains("created"))
                            project.CachedMetadata.CreatedUnixSeconds = value.at("created").get<std::uint64_t>();
                        if (!project.CachedMetadata.LastSavedWithEngineVersion &&
                            value.contains("lastSavedWithEngineVersion"))
                        {
                            auto version = value.at("lastSavedWithEngineVersion").get<std::string>();
                            if (!version.empty())
                                project.CachedMetadata.LastSavedWithEngineVersion = std::move(version);
                        }
                    }
                    else
                    {
                        project.AddedUnixSeconds = project.LastOpenedUnixSeconds;
                    }
                    if (const auto status = Validate(project); !status)
                        throw std::invalid_argument(status.Error().Message);
                    if (std::ranges::find(result, project.Id, &HubRecentProject::Id) != result.end())
                        throw std::invalid_argument("Duplicate project identity.");
                    result.push_back(std::move(project));
                }
                return HubResult<std::pair<std::vector<HubRecentProject>, bool>>::Success(
                    {std::move(result), schema == 1});
            }
            catch (const std::exception& error)
            {
                return HubResult<std::pair<std::vector<HubRecentProject>, bool>>::Failure(
                    {.Code = HubErrorCode::InvalidData,
                     .Message = "The recent-project registry is malformed.",
                     .AffectedItem = "projects",
                     .TechnicalDetails = error.what()});
            }
        }

        void SortAndTrim(std::vector<HubRecentProject>& projects)
        {
            std::ranges::sort(projects,
                              [](const auto& left, const auto& right)
                              {
                                  return std::tie(left.Pinned, left.LastOpenedUnixSeconds, left.Id) >
                                         std::tie(right.Pinned, right.LastOpenedUnixSeconds, right.Id);
                              });
            std::size_t unpinned = 0;
            std::erase_if(projects,
                          [&](const auto& project) { return !project.Pinned && ++unpinned > MaximumUnpinnedProjects; });
            if (projects.size() > MaximumProjects)
                projects.resize(MaximumProjects);
        }
    } // namespace

    HubProjectCatalog::HubProjectCatalog(std::filesystem::path registryPath)
        : m_Path(std::move(registryPath)), m_Snapshot(std::make_shared<const std::vector<HubRecentProject>>())
    {
    }

    HubStatus HubProjectCatalog::Load()
    {
        m_MigratedSchemaOne = false;
        if (!std::filesystem::exists(m_Path))
        {
            m_Snapshot = std::make_shared<const std::vector<HubRecentProject>>();
            return HubStatus::Success();
        }
        auto document = Detail::ReadJsonFile(m_Path, MaximumRegistryBytes);
        if (!document)
        {
            if (document.Error().Code == HubErrorCode::InvalidData)
                (void)Detail::QuarantineCorruptFile(m_Path);
            return HubStatus::Failure(document.Error());
        }
        auto parsed = Parse(document.Value());
        if (!parsed)
        {
            if (parsed.Error().Code != HubErrorCode::UnsupportedSchema)
                (void)Detail::QuarantineCorruptFile(m_Path);
            return HubStatus::Failure(parsed.Error());
        }
        auto [projects, migrated] = std::move(parsed).Value();
        SortAndTrim(projects);
        if (migrated)
        {
            if (auto status = Commit(projects); !status)
                return HubStatus::Failure({.Code = HubErrorCode::MigrationFailed,
                                           .Message = "The recent-project registry could not be migrated.",
                                           .Retryable = true,
                                           .AffectedItem = "projects",
                                           .TechnicalDetails = status.Error().TechnicalDetails});
            m_MigratedSchemaOne = true;
            return HubStatus::Success();
        }
        m_Snapshot = std::make_shared<const std::vector<HubRecentProject>>(std::move(projects));
        return HubStatus::Success();
    }

    HubStatus HubProjectCatalog::Upsert(HubRecentProject project)
    {
        if (const auto status = Validate(project); !status)
            return status;
        auto projects = *m_Snapshot;
        const auto found = std::ranges::find(projects, project.Id, &HubRecentProject::Id);
        if (found == projects.end())
            projects.push_back(std::move(project));
        else
        {
            const bool preservePin = found->Pinned;
            const auto preserveAdded = found->AddedUnixSeconds;
            *found = std::move(project);
            found->Pinned = preservePin || found->Pinned;
            if (found->AddedUnixSeconds == 0)
                found->AddedUnixSeconds = preserveAdded;
        }
        return Commit(std::move(projects));
    }

    HubStatus HubProjectCatalog::UpsertMany(const std::span<const HubRecentProject> incoming)
    {
        auto prepared = PrepareUpsertMany(incoming);
        if (!prepared)
            return HubStatus::Failure(prepared.Error());
        if (incoming.empty())
            return HubStatus::Success();
        return Commit(std::move(prepared).Value());
    }

    HubResult<std::vector<HubRecentProject>>
    HubProjectCatalog::PrepareUpsertMany(const std::span<const HubRecentProject> incoming) const
    {
        if (incoming.size() > MaximumProjects)
        {
            return HubResult<std::vector<HubRecentProject>>::Failure(
                {.Code = HubErrorCode::InvalidArgument,
                 .Message = "The project import exceeds the registry safety limit.",
                 .AffectedItem = "projects"});
        }

        std::set<std::string, std::less<>> incomingIds;
        std::set<std::string, std::less<>> incomingRoots;
        for (const auto& project : incoming)
        {
            if (const auto status = Validate(project); !status)
                return HubResult<std::vector<HubRecentProject>>::Failure(status.Error());
            if (!incomingIds.insert(project.Id).second)
                return HubResult<std::vector<HubRecentProject>>::Failure(
                    DuplicateBatchItem(project, "identity").Error());
            if (!incomingRoots.insert(NormalizedPathKey(project.Root)).second)
                return HubResult<std::vector<HubRecentProject>>::Failure(
                    DuplicateBatchItem(project, "location").Error());
        }

        auto projects = *m_Snapshot;
        for (const auto& incomingProject : incoming)
        {
            auto project = incomingProject;
            const auto rootKey = NormalizedPathKey(project.Root);
            const auto rootOwner = std::ranges::find_if(projects, [&](const HubRecentProject& existing)
                                                        { return NormalizedPathKey(existing.Root) == rootKey; });
            const auto idOwner = std::ranges::find(projects, project.Id, &HubRecentProject::Id);
            if (rootOwner != projects.end() && rootOwner->Id != project.Id)
            {
                return HubResult<std::vector<HubRecentProject>>::Failure(
                    DuplicateBatchItem(project, "location").Error());
            }
            if (idOwner == projects.end())
            {
                projects.push_back(std::move(project));
                continue;
            }

            if (NormalizedPathKey(idOwner->Root) != rootKey)
                continue;
            project.Root = idOwner->Root;
            project.Id = idOwner->Id;
            project.Pinned = project.Pinned || idOwner->Pinned;
            project.AddedUnixSeconds = idOwner->AddedUnixSeconds;
            project.LastOpenedUnixSeconds = std::max(project.LastOpenedUnixSeconds, idOwner->LastOpenedUnixSeconds);
            if (idOwner->PreferredEditorInstallationId)
                project.PreferredEditorInstallationId = idOwner->PreferredEditorInstallationId;
            *idOwner = std::move(project);
        }
        if (projects.size() > MaximumProjects)
        {
            return HubResult<std::vector<HubRecentProject>>::Failure(
                {.Code = HubErrorCode::InvalidArgument,
                 .Message = "The project import would exceed the registry safety limit.",
                 .AffectedItem = "projects"});
        }
        return HubResult<std::vector<HubRecentProject>>::Success(std::move(projects));
    }

    HubStatus HubProjectCatalog::SetPinned(const std::string& projectId, const bool pinned)
    {
        auto projects = *m_Snapshot;
        const auto found = std::ranges::find(projects, projectId, &HubRecentProject::Id);
        if (found == projects.end())
            return HubStatus::Failure({.Code = HubErrorCode::NotFound,
                                       .Message = "The project is no longer in the Hub.",
                                       .AffectedItem = projectId});
        found->Pinned = pinned;
        return Commit(std::move(projects));
    }

    HubStatus HubProjectCatalog::SetPreferredEditor(const std::string& projectId,
                                                    std::optional<std::string> installationId)
    {
        if (installationId && !IsBoundedText(*installationId, 128))
            return HubStatus::Failure({.Code = HubErrorCode::InvalidArgument,
                                       .Message = "The preferred editor identity is invalid.",
                                       .AffectedItem = projectId});
        auto projects = *m_Snapshot;
        const auto found = std::ranges::find(projects, projectId, &HubRecentProject::Id);
        if (found == projects.end())
            return HubStatus::Failure({.Code = HubErrorCode::NotFound,
                                       .Message = "The project is no longer in the Hub.",
                                       .AffectedItem = projectId});
        found->PreferredEditorInstallationId = std::move(installationId);
        return Commit(std::move(projects));
    }

    HubStatus HubProjectCatalog::UpdateCachedMetadata(const std::string& projectId, HubProjectMetadata metadata)
    {
        const std::array updates{HubProjectMetadataUpdate{.ProjectId = projectId, .Metadata = std::move(metadata)}};
        return UpdateCachedMetadataMany(updates);
    }

    HubStatus
    HubProjectCatalog::UpdateCachedMetadataMany(const std::span<const HubProjectMetadataUpdate> metadataUpdates)
    {
        if (metadataUpdates.size() > MaximumProjects)
        {
            return HubStatus::Failure({.Code = HubErrorCode::InvalidArgument,
                                       .Message = "The project metadata update exceeds the registry safety limit.",
                                       .AffectedItem = "projects"});
        }

        auto projects = *m_Snapshot;
        std::set<std::string, std::less<>> updatedIds;
        for (const auto& update : metadataUpdates)
        {
            if (!IsBoundedText(update.ProjectId, 128))
            {
                return HubStatus::Failure({.Code = HubErrorCode::InvalidArgument,
                                           .Message = "A project metadata update has an invalid identity.",
                                           .AffectedItem = update.ProjectId});
            }
            if (!updatedIds.insert(update.ProjectId).second)
            {
                return HubStatus::Failure({.Code = HubErrorCode::DuplicateIdentifier,
                                           .Message = "The project metadata update contains a duplicate identity.",
                                           .AffectedItem = update.ProjectId});
            }
            const auto found = std::ranges::find(projects, update.ProjectId, &HubRecentProject::Id);
            if (found == projects.end())
            {
                return HubStatus::Failure({.Code = HubErrorCode::NotFound,
                                           .Message = "The project is no longer in the Hub.",
                                           .AffectedItem = update.ProjectId});
            }
            found->CachedMetadata = update.Metadata;
            if (const auto status = Validate(*found); !status)
                return status;
        }
        if (metadataUpdates.empty())
            return HubStatus::Success();
        return Commit(std::move(projects));
    }

    HubStatus HubProjectCatalog::Locate(const std::string& projectId, std::filesystem::path newRoot,
                                        const std::string& verifiedProjectId)
    {
        if (projectId != verifiedProjectId)
            return HubStatus::Failure({.Code = HubErrorCode::InvalidArgument,
                                       .Message = "The selected folder belongs to a different project.",
                                       .AffectedItem = projectId});
        auto projects = *m_Snapshot;
        const auto found = std::ranges::find(projects, projectId, &HubRecentProject::Id);
        if (found == projects.end())
            return HubStatus::Failure({.Code = HubErrorCode::NotFound,
                                       .Message = "The project is no longer in the Hub.",
                                       .AffectedItem = projectId});
        found->Root = std::move(newRoot);
        if (const auto status = Validate(*found); !status)
            return status;
        return Commit(std::move(projects));
    }

    HubStatus HubProjectCatalog::Remove(const std::string& projectId)
    {
        auto projects = *m_Snapshot;
        if (std::erase_if(projects, [&](const auto& project) { return project.Id == projectId; }) == 0)
            return HubStatus::Failure({.Code = HubErrorCode::NotFound,
                                       .Message = "The project is no longer in the Hub.",
                                       .AffectedItem = projectId});
        return Commit(std::move(projects));
    }

    HubStatus HubProjectCatalog::RemoveMissing()
    {
        auto projects = *m_Snapshot;
        if (std::erase_if(projects, [](const HubRecentProject& project)
                          { return project.CachedMetadata.Status == HubProjectStatus::Missing; }) == 0)
        {
            return HubStatus::Success();
        }
        return Commit(std::move(projects));
    }

    std::shared_ptr<const std::vector<HubRecentProject>> HubProjectCatalog::Snapshot() const noexcept
    {
        return m_Snapshot;
    }

    bool HubProjectCatalog::MigratedSchemaOne() const noexcept { return m_MigratedSchemaOne; }

    const std::filesystem::path& HubProjectCatalog::Path() const noexcept { return m_Path; }

    HubStatus HubProjectCatalog::RestoreSnapshot(std::shared_ptr<const std::vector<HubRecentProject>> snapshot,
                                                 const bool fileExisted)
    {
        HubStatus status = HubStatus::Success();
        if (fileExisted)
        {
            status = Detail::WriteJsonFileAtomically(m_Path, Serialize(*snapshot));
        }
        else
        {
            std::error_code error;
            (void)std::filesystem::remove(m_Path, error);
            if (error)
            {
                status = HubStatus::Failure({.Code = HubErrorCode::IoWrite,
                                             .Message = "The Hub could not roll back its project import.",
                                             .Retryable = true,
                                             .AffectedItem = "projects",
                                             .TechnicalDetails = error.message()});
            }
        }
        if (status)
            m_Snapshot = std::move(snapshot);
        return status;
    }

    HubStatus HubProjectCatalog::Commit(std::vector<HubRecentProject> projects)
    {
        SortAndTrim(projects);
        if (auto status = Detail::WriteJsonFileAtomically(m_Path, Serialize(projects)); !status)
            return status;
        m_Snapshot = std::make_shared<const std::vector<HubRecentProject>>(std::move(projects));
        return HubStatus::Success();
    }
} // namespace KeireHub
