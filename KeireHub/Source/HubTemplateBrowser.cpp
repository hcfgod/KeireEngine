#include "KeireHub/HubTemplateBrowser.h"

#include <algorithm>
#include <cctype>
#include <iterator>
#include <ranges>
#include <system_error>

namespace KeireHub
{
    namespace
    {
        [[nodiscard]] std::string CategoryLabel(const TemplateCategory category)
        {
            switch (category)
            {
            case TemplateCategory::Core:
                return "Core";
            case TemplateCategory::Sample:
                return "Sample";
            case TemplateCategory::Learning:
                return "Learning";
            }
            return "Template";
        }

        [[nodiscard]] std::string Lower(std::string value)
        {
            std::ranges::transform(value, value.begin(), [](const unsigned char character)
                                   { return static_cast<char>(std::tolower(character)); });
            return value;
        }

        [[nodiscard]] bool IsWithin(const std::filesystem::path& root, const std::filesystem::path& candidate)
        {
            const auto relative = candidate.lexically_relative(root);
            return !relative.empty() && !relative.is_absolute() && *relative.begin() != "..";
        }

        [[nodiscard]] HubTemplateArtworkUiRecord ResolveArtwork(const std::filesystem::path& templatesRoot,
                                                                const std::filesystem::path& declared)
        {
            HubTemplateArtworkUiRecord result{.DeclaredPath = declared, .ResolvedPath = {}, .Available = false};
            constexpr std::uint64_t MaximumArtworkBytes = 8ULL * 1024ULL * 1024ULL;
            std::error_code error;
            const auto root = std::filesystem::weakly_canonical(templatesRoot, error);
            if (error)
                return result;
            const auto candidate = std::filesystem::weakly_canonical(root / declared, error);
            if (error || !IsWithin(root, candidate) || !std::filesystem::is_regular_file(candidate, error) || error)
                return result;
            const auto size = std::filesystem::file_size(candidate, error);
            if (error || size == 0 || size > MaximumArtworkBytes)
                return result;
            std::string details;
            auto image = DecodeHubPngImage(candidate, size, details);
            if (!image)
                return result;
            result.ResolvedPath = candidate;
            result.Image = std::move(*image);
            result.Available = true;
            return result;
        }

        [[nodiscard]] HubTemplatePackageUiRecord PackageRecord(const PackageRequirement& requirement)
        {
            return {.PackageId = requirement.PackageId, .VersionConstraint = requirement.Versions.ToString()};
        }

        [[nodiscard]] bool MatchesCategory(const HubTemplateUiRecord& item,
                                           const HubTemplateCategoryFilter category) noexcept
        {
            switch (category)
            {
            case HubTemplateCategoryFilter::All:
                return true;
            case HubTemplateCategoryFilter::Core:
                return item.Category == "Core";
            case HubTemplateCategoryFilter::Sample:
                return item.Category == "Sample";
            case HubTemplateCategoryFilter::Learning:
                return item.Category == "Learning";
            }
            return false;
        }
    } // namespace

    HubTemplateUiRecord MakeHubTemplateUiRecord(const HubTemplateManifest& manifest,
                                                const std::filesystem::path& templatesRoot)
    {
        HubTemplateUiRecord result{.Id = manifest.Id,
                                   .Version = manifest.Version.ToString(),
                                   .Name = manifest.DisplayName,
                                   .Description = manifest.Description,
                                   .Category = CategoryLabel(manifest.Category),
                                   .Tags = manifest.Tags,
                                   .Thumbnail = ResolveArtwork(templatesRoot, manifest.Thumbnail),
                                   .Screenshots = {},
                                   .CompatibleEditors = manifest.CompatibleEditors.ToString(),
                                   .ProjectSchema = manifest.ProjectSchema,
                                   .PlatformTarget = manifest.PlatformTarget,
                                   .EstimatedBytes = manifest.EstimatedSizeBytes,
                                   .StarterContent = manifest.StarterContent,
                                   .RequiredPackages = {},
                                   .RecommendedPackages = {},
                                   .LicenseReferences = manifest.LicenseReferences,
                                   .Featured = manifest.Featured};
        result.Screenshots.reserve(manifest.Screenshots.size());
        for (const auto& screenshot : manifest.Screenshots)
            result.Screenshots.push_back(ResolveArtwork(templatesRoot, screenshot));
        result.RequiredPackages.reserve(manifest.RequiredPackages.size());
        std::ranges::transform(manifest.RequiredPackages, std::back_inserter(result.RequiredPackages), PackageRecord);
        result.RecommendedPackages.reserve(manifest.RecommendedPackages.size());
        std::ranges::transform(manifest.RecommendedPackages, std::back_inserter(result.RecommendedPackages),
                               PackageRecord);
        return result;
    }

    HubTemplateCompatibility EvaluateTemplateCompatibility(const HubTemplateUiRecord& item,
                                                           const HubTemplateEditorCompatibilityInput& editor,
                                                           const std::string_view platformTarget)
    {
        if (!editor.Healthy || !editor.HasEntrypoint || !editor.HasAssetToolEntrypoint)
            return {.Status = HubTemplateCompatibilityStatus::EditorUnavailable, .Label = "Editor unavailable"};
        const auto version = SemanticVersion::Parse(editor.Version);
        if (!version)
            return {.Status = HubTemplateCompatibilityStatus::InvalidEditorVersion,
                    .Label = "Editor version is invalid"};
        const auto constraint = VersionConstraint::Parse(item.CompatibleEditors);
        if (!constraint || !constraint.Value().Matches(version.Value()))
            return {.Status = HubTemplateCompatibilityStatus::EditorVersionUnsupported,
                    .Label = "Requires editor " + item.CompatibleEditors};
        if (item.ProjectSchema < editor.MinimumProjectSchema || item.ProjectSchema > editor.MaximumProjectSchema)
            return {.Status = HubTemplateCompatibilityStatus::ProjectSchemaUnsupported,
                    .Label = "Requires project schema " + std::to_string(item.ProjectSchema)};
        if (item.PlatformTarget != platformTarget)
            return {.Status = HubTemplateCompatibilityStatus::PlatformUnsupported,
                    .Label = "Targets " + item.PlatformTarget};
        return {.Status = HubTemplateCompatibilityStatus::Compatible, .Label = "Compatible"};
    }

    std::size_t CountCompatibleEditors(const HubTemplateUiRecord& item,
                                       const std::span<const HubTemplateEditorCompatibilityInput> editors,
                                       const std::string_view platformTarget)
    {
        return static_cast<std::size_t>(std::ranges::count_if(
            editors, [&](const auto& editor)
            { return EvaluateTemplateCompatibility(item, editor, platformTarget).Compatible(); }));
    }

    bool MatchesTemplateQuery(const HubTemplateUiRecord& item, const HubTemplateBrowserQuery& query)
    {
        if (!MatchesCategory(item, query.Category))
            return false;
        const auto search = Lower(std::string(query.Search));
        if (search.empty())
            return true;
        std::string haystack = item.Name + ' ' + item.Description + ' ' + item.Category + ' ' + item.Id;
        for (const auto& tag : item.Tags)
            haystack += ' ' + tag;
        return Lower(std::move(haystack)).find(search) != std::string::npos;
    }

    std::vector<std::size_t> QueryTemplateIndices(const std::span<const HubTemplateUiRecord> items,
                                                  const HubTemplateBrowserQuery& query)
    {
        std::vector<std::size_t> result;
        result.reserve(items.size());
        for (std::size_t index = 0; index < items.size(); ++index)
        {
            if (MatchesTemplateQuery(items[index], query))
                result.push_back(index);
        }
        std::ranges::stable_sort(result,
                                 [&](const std::size_t left, const std::size_t right)
                                 {
                                     if (items[left].Featured != items[right].Featured)
                                         return items[left].Featured;
                                     const auto leftName = Lower(items[left].Name);
                                     const auto rightName = Lower(items[right].Name);
                                     if (leftName != rightName)
                                         return leftName < rightName;
                                     if (items[left].Id != items[right].Id)
                                         return items[left].Id < items[right].Id;
                                     return items[left].Version > items[right].Version;
                                 });
        return result;
    }
} // namespace KeireHub
