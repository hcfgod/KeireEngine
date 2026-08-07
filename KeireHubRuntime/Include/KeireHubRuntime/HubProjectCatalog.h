#pragma once

#include "KeireHubRuntime/HubError.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace KeireHub
{
    class HubController;

    enum class HubProjectStatus
    {
        Unknown,
        Ready,
        UpgradeAvailable,
        Missing,
        Invalid,
        Locked,
        RecoveryRequired,
        UnsupportedSchema,
        MissingEditor
    };

    struct HubProjectMetadata final
    {
        std::optional<std::uint64_t> CreatedUnixSeconds;
        std::optional<std::uint64_t> ModifiedUnixSeconds;
        std::optional<std::uint64_t> SizeBytes;
        std::optional<std::string> CreatedWithEngineVersion;
        std::optional<std::string> LastSavedWithEngineVersion;
        std::optional<std::string> MinimumEngineVersion;
        std::optional<std::uint32_t> ProjectSchemaVersion;
        HubProjectStatus Status = HubProjectStatus::Unknown;
    };

    struct HubRecentProject final
    {
        std::string Id;
        std::filesystem::path Root;
        std::string Name;
        std::uint64_t AddedUnixSeconds = 0;
        std::uint64_t LastOpenedUnixSeconds = 0;
        bool Pinned = false;
        std::optional<std::string> PreferredEditorInstallationId;
        HubProjectMetadata CachedMetadata;
    };

    struct HubProjectMetadataUpdate final
    {
        std::string ProjectId;
        HubProjectMetadata Metadata;
    };

    class HubProjectCatalog final
    {
      public:
        static constexpr std::uint32_t CurrentSchemaVersion = 2;

        explicit HubProjectCatalog(std::filesystem::path registryPath);

        [[nodiscard]] HubStatus Load();
        [[nodiscard]] HubStatus Upsert(HubRecentProject project);
        [[nodiscard]] HubStatus UpsertMany(std::span<const HubRecentProject> projects);
        [[nodiscard]] HubStatus SetPinned(const std::string& projectId, bool pinned);
        [[nodiscard]] HubStatus SetPreferredEditor(const std::string& projectId,
                                                   std::optional<std::string> installationId);
        [[nodiscard]] HubStatus UpdateCachedMetadata(const std::string& projectId, HubProjectMetadata metadata);
        [[nodiscard]] HubStatus UpdateCachedMetadataMany(std::span<const HubProjectMetadataUpdate> metadataUpdates);
        [[nodiscard]] HubStatus Locate(const std::string& projectId, std::filesystem::path newRoot,
                                       const std::string& verifiedProjectId);
        [[nodiscard]] HubStatus Remove(const std::string& projectId);
        [[nodiscard]] HubStatus RemoveMissing();

        [[nodiscard]] std::shared_ptr<const std::vector<HubRecentProject>> Snapshot() const noexcept;
        [[nodiscard]] bool MigratedSchemaOne() const noexcept;
        [[nodiscard]] const std::filesystem::path& Path() const noexcept;

      private:
        friend class HubController;

        [[nodiscard]] HubResult<std::vector<HubRecentProject>>
        PrepareUpsertMany(std::span<const HubRecentProject> projects) const;
        [[nodiscard]] HubStatus RestoreSnapshot(std::shared_ptr<const std::vector<HubRecentProject>> snapshot,
                                                bool fileExisted);
        [[nodiscard]] HubStatus Commit(std::vector<HubRecentProject> projects);

        std::filesystem::path m_Path;
        std::shared_ptr<const std::vector<HubRecentProject>> m_Snapshot;
        bool m_MigratedSchemaOne = false;
    };
} // namespace KeireHub
