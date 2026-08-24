#pragma once

#include "KeireHubRuntime/CatalogModels.h"
#include "KeireHubRuntime/HubError.h"
#include "KeireHubRuntime/PackageResolver.h"
#include "KeireHubRuntime/ProjectSchema.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace KeireHub
{
    [[nodiscard]] bool IsValidProjectName(std::string_view name) noexcept;

    struct TemplateManagerServices final
    {
        std::function<HubResult<std::string>()> GenerateProjectId;
        std::function<HubResult<std::string>()> CurrentUtcTimestamp;
        std::function<HubResult<std::uint64_t>(const std::filesystem::path&)> AvailableSpace;
    };

    struct TemplateCreateRequest final
    {
        std::string TemplateId;
        std::optional<SemanticVersion> TemplateVersion;
        std::string ProjectName;
        std::filesystem::path Destination;
        SemanticVersion EditorVersion;
        std::uint32_t EditorMinimumProjectSchema = 1;
        std::uint32_t EditorMaximumProjectSchema = CurrentProjectSchemaVersion;
        std::string PlatformTarget = "desktop";
        std::string HostPlatform;
        std::string HostArchitecture;
        std::vector<PackageManifest> AvailablePackages;
        std::vector<std::filesystem::path> ForbiddenDestinationRoots;
        std::function<bool()> CancellationRequested;
        std::function<HubStatus(const std::filesystem::path&)> ValidateStagedProject;
    };

    struct TemplateCreationPlan final
    {
        HubTemplateManifest Template;
        PackageResolution RequiredPackages;
        std::filesystem::path Destination;
        std::uint64_t PayloadBytes = 0;
        std::uint64_t RequiredDiskBytes = 0;
    };

    struct TemplateCreationResult final
    {
        std::string ProjectId;
        std::filesystem::path Root;
        std::string TemplateId;
        SemanticVersion TemplateVersion;
        PackageResolution RequiredPackages;
    };

    class TemplateManager final
    {
      public:
        explicit TemplateManager(std::filesystem::path templatesRoot, TemplateManagerServices services = {});

        [[nodiscard]] HubStatus Load();
        [[nodiscard]] HubResult<TemplateCreationPlan> Preflight(const TemplateCreateRequest& request) const;
        [[nodiscard]] HubResult<TemplateCreationResult> CreateProject(const TemplateCreateRequest& request) const;

        [[nodiscard]] std::shared_ptr<const std::vector<HubTemplateManifest>> Snapshot() const noexcept;
        [[nodiscard]] const std::filesystem::path& Root() const noexcept;

      private:
        std::filesystem::path m_Root;
        TemplateManagerServices m_Services;
        std::shared_ptr<const std::vector<HubTemplateManifest>> m_Snapshot;
    };
} // namespace KeireHub
