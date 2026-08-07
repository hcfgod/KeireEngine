#pragma once

#include "KeireHubRuntime/CatalogModels.h"
#include "KeireHubRuntime/ProjectMetadataScanner.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace KeireHub
{
    struct HubTemplatePackageUiRecord final
    {
        std::string PackageId;
        std::string VersionConstraint;

        [[nodiscard]] bool operator==(const HubTemplatePackageUiRecord&) const noexcept = default;
    };

    struct HubTemplateArtworkUiRecord final
    {
        std::filesystem::path DeclaredPath;
        std::filesystem::path ResolvedPath;
        ProjectThumbnailImage Image;
        bool Available = false;

        [[nodiscard]] bool operator==(const HubTemplateArtworkUiRecord&) const noexcept = default;
    };

    struct HubTemplateUiRecord final
    {
        std::string Id;
        std::string Version;
        std::string Name;
        std::string Description;
        std::string Category;
        std::vector<std::string> Tags;
        HubTemplateArtworkUiRecord Thumbnail;
        std::vector<HubTemplateArtworkUiRecord> Screenshots;
        std::string CompatibleEditors;
        std::uint32_t ProjectSchema = 1;
        std::string PlatformTarget;
        std::uint64_t EstimatedBytes = 0;
        std::vector<std::string> StarterContent;
        std::vector<HubTemplatePackageUiRecord> RequiredPackages;
        std::vector<HubTemplatePackageUiRecord> RecommendedPackages;
        std::vector<std::string> LicenseReferences;
        bool Featured = false;
    };

    struct HubTemplateEditorCompatibilityInput final
    {
        std::string Version;
        std::uint32_t MinimumProjectSchema = 1;
        std::uint32_t MaximumProjectSchema = 1;
        bool Healthy = false;
        bool HasEntrypoint = false;
        bool HasAssetToolEntrypoint = false;
    };

    enum class HubTemplateCompatibilityStatus
    {
        Compatible,
        EditorUnavailable,
        InvalidEditorVersion,
        EditorVersionUnsupported,
        ProjectSchemaUnsupported,
        PlatformUnsupported
    };

    struct HubTemplateCompatibility final
    {
        HubTemplateCompatibilityStatus Status = HubTemplateCompatibilityStatus::EditorUnavailable;
        std::string Label;

        [[nodiscard]] bool Compatible() const noexcept { return Status == HubTemplateCompatibilityStatus::Compatible; }
    };

    enum class HubTemplateCategoryFilter
    {
        All,
        Core,
        Sample,
        Learning
    };

    struct HubTemplateBrowserQuery final
    {
        std::string_view Search;
        HubTemplateCategoryFilter Category = HubTemplateCategoryFilter::All;
    };

    [[nodiscard]] HubTemplateUiRecord MakeHubTemplateUiRecord(const HubTemplateManifest& manifest,
                                                              const std::filesystem::path& templatesRoot);
    [[nodiscard]] HubTemplateCompatibility
    EvaluateTemplateCompatibility(const HubTemplateUiRecord& item, const HubTemplateEditorCompatibilityInput& editor,
                                  std::string_view platformTarget = "desktop");
    [[nodiscard]] std::size_t CountCompatibleEditors(const HubTemplateUiRecord& item,
                                                     std::span<const HubTemplateEditorCompatibilityInput> editors,
                                                     std::string_view platformTarget = "desktop");
    [[nodiscard]] bool MatchesTemplateQuery(const HubTemplateUiRecord& item, const HubTemplateBrowserQuery& query);
    [[nodiscard]] std::vector<std::size_t> QueryTemplateIndices(std::span<const HubTemplateUiRecord> items,
                                                                const HubTemplateBrowserQuery& query);
} // namespace KeireHub
