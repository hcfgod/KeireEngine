#pragma once

#include "KeireHubRuntime/HubError.h"
#include "KeireHubRuntime/PackageResolver.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>

namespace KeireHub
{
    enum class PackagePublishPhase
    {
        Prepared,
        BackupMoved,
        Published
    };

    struct PackagePublishPaths final
    {
        std::filesystem::path AllowedParent;
        std::filesystem::path StagingRoot;
        std::filesystem::path Destination;
        std::filesystem::path BackupRoot;
        std::filesystem::path LockRoot;
        std::filesystem::path Journal;
    };

    struct PackagePublishJournal final
    {
        static constexpr std::uint32_t CurrentSchemaVersion = 1;

        std::uint32_t SchemaVersion = CurrentSchemaVersion;
        std::string OperationId;
        std::string ManifestSha256;
        PackagePublishPaths Paths;
        bool ReplacesExisting = false;
        PackagePublishPhase Phase = PackagePublishPhase::Prepared;
    };

    enum class PackagePublishDestinationPolicy
    {
        Detect,
        RequireAbsent,
        RequireExisting
    };

    struct PackagePublishOptions final
    {
        PackagePublishDestinationPolicy DestinationPolicy = PackagePublishDestinationPolicy::Detect;
        std::function<HubStatus(const PackagePublishJournal&)> AuthorizeMutation;
    };

    [[nodiscard]] HubResult<PackagePublishPaths> PlanPackagePublish(const std::filesystem::path& allowedParent,
                                                                    const std::filesystem::path& destination,
                                                                    std::string operationId);
    [[nodiscard]] HubResult<PackagePublishJournal> PreparePackagePublish(const PackagePublishPaths& paths,
                                                                         const PackageManifest& manifest,
                                                                         std::string operationId,
                                                                         const PackagePublishOptions& options = {});
    [[nodiscard]] HubResult<PackagePublishJournal> LoadPackagePublishJournal(const std::filesystem::path& allowedParent,
                                                                             const std::filesystem::path& journal);
    [[nodiscard]] HubStatus ContinuePackagePublish(PackagePublishJournal journal, const PackageManifest& manifest,
                                                   const PackagePublishOptions& options = {});
    [[nodiscard]] HubStatus PublishStagedPackage(const PackagePublishPaths& paths, const PackageManifest& manifest,
                                                 std::string operationId, const PackagePublishOptions& options = {});
    [[nodiscard]] HubStatus RecoverPackagePublish(const std::filesystem::path& allowedParent,
                                                  const std::filesystem::path& journal, const PackageManifest& manifest,
                                                  std::string operationId, const PackagePublishOptions& options = {});
} // namespace KeireHub
