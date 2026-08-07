#pragma once

#include "KeireHubRuntime/HubError.h"
#include "KeireHubRuntime/PackageResolver.h"

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace KeireHub
{
    enum class TemplateCategory
    {
        Core,
        Sample,
        Learning
    };

    struct TemplatePayloadFile final
    {
        std::filesystem::path Path;
        std::uint64_t SizeBytes = 0;
        std::string Sha256;
    };

    struct HubTemplateManifest final
    {
        std::string Id;
        SemanticVersion Version;
        std::string DisplayName;
        std::string Description;
        TemplateCategory Category = TemplateCategory::Core;
        std::vector<std::string> Tags;
        std::filesystem::path Thumbnail;
        std::vector<std::filesystem::path> Screenshots;
        VersionConstraint CompatibleEditors;
        std::uint32_t ProjectSchema = 1;
        std::string PlatformTarget = "desktop";
        std::uint64_t EstimatedSizeBytes = 0;
        std::filesystem::path PayloadRoot;
        std::vector<TemplatePayloadFile> PayloadFiles;
        std::map<std::string, std::string, std::less<>> DefaultProjectConfiguration;
        std::vector<std::string> StarterContent;
        std::vector<PackageRequirement> RequiredPackages;
        std::vector<PackageRequirement> RecommendedPackages;
        std::vector<std::string> LicenseReferences;
        bool Featured = false;
    };

    struct HubTemplateCatalog final
    {
        static constexpr std::uint32_t CurrentSchemaVersion = 1;
        std::vector<HubTemplateManifest> Templates;
    };

    enum class ContentDifficulty
    {
        Beginner,
        Intermediate,
        Advanced,
        Reference
    };

    enum class ContentType
    {
        Article,
        Guide,
        Sample,
        Documentation,
        ExternalLink,
        ReleaseNotes,
        Repository,
        IssueTracker
    };

    struct HubContentItem final
    {
        std::string Id;
        std::string Title;
        std::string Summary;
        ContentDifficulty Difficulty = ContentDifficulty::Beginner;
        std::string Category;
        ContentType Type = ContentType::Article;
        std::optional<std::filesystem::path> LocalPath;
        std::optional<std::string> HttpsUrl;
        std::optional<std::filesystem::path> Thumbnail;
        std::vector<std::string> Tags;
        std::optional<VersionConstraint> RequiredEditorVersion;
        std::optional<PackageRequirement> SamplePackage;
        bool Featured = false;
    };

    struct HubContentCatalog final
    {
        static constexpr std::uint32_t CurrentSchemaVersion = 1;
        std::string Locale = "en-US";
        std::vector<HubContentItem> Learn;
        std::vector<HubContentItem> Resources;
    };

    enum class LicenseScope
    {
        Hub,
        Editor,
        BuildSupport,
        Template,
        Content
    };

    struct HubLicenseEntry final
    {
        std::string Id;
        std::string DisplayName;
        LicenseScope Scope = LicenseScope::Hub;
        std::optional<std::string> PackageId;
        std::optional<std::string> Version;
        std::filesystem::path SourcePath;
        std::optional<std::string> EmbeddedText;
    };

    struct HubLicenseCatalog final
    {
        static constexpr std::uint32_t CurrentSchemaVersion = 1;
        std::vector<HubLicenseEntry> Licenses;
    };

    [[nodiscard]] HubStatus ValidateTemplateManifest(const HubTemplateManifest& manifest);
    [[nodiscard]] HubResult<HubTemplateCatalog> ParseTemplateCatalog(std::string_view document);
    [[nodiscard]] HubResult<HubContentCatalog> ParseContentCatalog(std::string_view document);
    [[nodiscard]] HubResult<HubLicenseCatalog> ParseLicenseCatalog(std::string_view document);
} // namespace KeireHub
