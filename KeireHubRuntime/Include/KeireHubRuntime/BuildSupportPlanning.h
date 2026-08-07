#pragma once

#include "KeireHubRuntime/HubError.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace KeireHub
{
    struct BuildSupportEditorTarget final
    {
        std::string InstallationId;
        std::string EngineVersion;
        std::filesystem::path Root;
        std::filesystem::path AssetToolEntrypoint;
        bool Healthy = false;
        bool Running = false;
        bool HasActiveTask = false;
    };

    struct BuildSupportComponent final
    {
        std::string Id;
        std::string EngineVersion;
        std::string Platform;
        std::string Architecture;
        std::uint64_t ArchiveSizeBytes = 0;
        bool Healthy = false;
        std::string Diagnostic;

        [[nodiscard]] bool operator==(const BuildSupportComponent&) const = default;
    };

    struct BuildSupportFilter final
    {
        std::optional<std::string> Platform;
        std::optional<std::string> Architecture;

        [[nodiscard]] bool operator==(const BuildSupportFilter&) const = default;
    };

    struct BuildSupportSelection final
    {
        BuildSupportEditorTarget Editor;
        BuildSupportFilter Filter;
    };

    enum class BuildSupportOperationKind
    {
        Import,
        Repair,
        Remove
    };

    struct BuildSupportCommandPlan final
    {
        BuildSupportOperationKind Kind = BuildSupportOperationKind::Import;
        std::filesystem::path Executable;
        std::vector<std::string> Arguments;
        std::filesystem::path WorkingDirectory;
        std::filesystem::path StatusPath;
        std::filesystem::path CancelPath;
        std::string ComponentId;
    };

    [[nodiscard]] HubResult<BuildSupportSelection>
    SelectBuildSupportEditor(std::span<const BuildSupportEditorTarget> editors, std::string_view installationId,
                             BuildSupportFilter filter = {});
    [[nodiscard]] HubResult<BuildSupportSelection>
    SelectBuildSupportEditorForTarget(std::span<const BuildSupportEditorTarget> editors, std::string_view platform,
                                      std::string_view architecture);
    [[nodiscard]] std::vector<BuildSupportComponent>
    FilterBuildSupportComponents(std::span<const BuildSupportComponent> components,
                                 const BuildSupportSelection& selection);
    [[nodiscard]] std::size_t CountBuildSupportComponents(std::span<const BuildSupportComponent> components,
                                                          std::string_view engineVersion);
    [[nodiscard]] HubError BuildSupportInventoryFailure(std::string affectedItem, std::string technicalDetails);
    [[nodiscard]] std::string BuildSupportLiveStatusText(BuildSupportOperationKind kind, std::string_view state,
                                                         std::string_view phase, std::string_view errorCode = {});
    [[nodiscard]] HubResult<BuildSupportCommandPlan>
    PlanBuildSupportImport(const BuildSupportSelection& selection, const std::filesystem::path& package,
                           const std::filesystem::path& operationDirectory,
                           const BuildSupportComponent* repairComponent = nullptr);
    [[nodiscard]] HubResult<BuildSupportCommandPlan> PlanBuildSupportRemoval(const BuildSupportSelection& selection,
                                                                             const BuildSupportComponent& component);
} // namespace KeireHub
