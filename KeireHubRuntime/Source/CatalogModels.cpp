#include "KeireHubRuntime/CatalogModels.h"

#include "Persistence.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <limits>
#include <map>
#include <ranges>
#include <set>

namespace KeireHub
{
    namespace
    {
        constexpr std::size_t MaximumCatalogBytes = 16 * 1024 * 1024;
        constexpr std::size_t MaximumCatalogItems = 1024;
        constexpr std::size_t MaximumTags = 32;
        constexpr std::size_t MaximumPackages = 64;
        constexpr std::uint64_t MaximumTemplatePayloadBytes = 16ULL * 1024ULL * 1024ULL * 1024ULL;

        [[nodiscard]] std::optional<TemplateCategory> ParseTemplateCategory(const std::string_view value) noexcept
        {
            if (value == "core")
                return TemplateCategory::Core;
            if (value == "sample")
                return TemplateCategory::Sample;
            if (value == "learning")
                return TemplateCategory::Learning;
            return std::nullopt;
        }

        [[nodiscard]] std::optional<ContentDifficulty> ParseDifficulty(const std::string_view value) noexcept
        {
            if (value == "beginner")
                return ContentDifficulty::Beginner;
            if (value == "intermediate")
                return ContentDifficulty::Intermediate;
            if (value == "advanced")
                return ContentDifficulty::Advanced;
            if (value == "reference")
                return ContentDifficulty::Reference;
            return std::nullopt;
        }

        [[nodiscard]] std::optional<ContentType> ParseContentType(const std::string_view value) noexcept
        {
            constexpr std::array<std::pair<std::string_view, ContentType>, 8> values{
                std::pair{"article", ContentType::Article},
                std::pair{"guide", ContentType::Guide},
                std::pair{"sample", ContentType::Sample},
                std::pair{"documentation", ContentType::Documentation},
                std::pair{"externalLink", ContentType::ExternalLink},
                std::pair{"releaseNotes", ContentType::ReleaseNotes},
                std::pair{"repository", ContentType::Repository},
                std::pair{"issueTracker", ContentType::IssueTracker}};
            for (const auto& [name, type] : values)
            {
                if (name == value)
                    return type;
            }
            return std::nullopt;
        }

        [[nodiscard]] std::optional<LicenseScope> ParseLicenseScope(const std::string_view value) noexcept
        {
            if (value == "hub")
                return LicenseScope::Hub;
            if (value == "editor")
                return LicenseScope::Editor;
            if (value == "buildSupport")
                return LicenseScope::BuildSupport;
            if (value == "template")
                return LicenseScope::Template;
            if (value == "content")
                return LicenseScope::Content;
            return std::nullopt;
        }

        [[nodiscard]] bool IsText(const std::string_view value, const std::size_t maximum,
                                  const bool allowEmpty = false) noexcept
        {
            return (allowEmpty || !value.empty()) && value.size() <= maximum &&
                   std::ranges::none_of(value,
                                        [](const unsigned char character)
                                        {
                                            return character == 0 || (character < 0x20 && character != '\n' &&
                                                                      character != '\r' && character != '\t');
                                        });
        }

        [[nodiscard]] bool IsHttpsUrl(const std::string_view value) noexcept
        {
            if (!value.starts_with("https://") || value.size() > 2048)
                return false;
            const auto authorityEnd = value.find('/', 8);
            const auto authority =
                value.substr(8, authorityEnd == std::string_view::npos ? value.size() - 8 : authorityEnd - 8);
            return !authority.empty() && authority.find('@') == std::string_view::npos &&
                   std::ranges::none_of(value, [](const unsigned char character)
                                        { return character <= 0x20 || character == '\\'; });
        }

        [[nodiscard]] HubResult<PackageRequirement> ParseRequirement(const Detail::Json& value)
        {
            try
            {
                auto versions = VersionConstraint::Parse(value.value("version", "*"));
                if (!versions)
                    return HubResult<PackageRequirement>::Failure(versions.Error());
                PackageRequirement requirement{value.at("packageId").get<std::string>(), std::move(versions).Value()};
                if (!Detail::IsBoundedIdentifier(requirement.PackageId))
                    throw std::invalid_argument("Invalid package identity.");
                return HubResult<PackageRequirement>::Success(std::move(requirement));
            }
            catch (const std::exception& error)
            {
                return HubResult<PackageRequirement>::Failure({.Code = HubErrorCode::InvalidData,
                                                               .Message = "A catalog package requirement is malformed.",
                                                               .TechnicalDetails = error.what()});
            }
        }

        [[nodiscard]] HubStatus ValidateTags(const std::vector<std::string>& tags, const std::string& affectedItem)
        {
            if (tags.size() > MaximumTags)
                return HubStatus::Failure({.Code = HubErrorCode::InvalidData,
                                           .Message = "A catalog item contains too many tags.",
                                           .AffectedItem = affectedItem});
            std::set<std::string, std::less<>> unique;
            for (const auto& tag : tags)
            {
                if (!IsText(tag, 64) || !unique.insert(tag).second)
                    return HubStatus::Failure({.Code = HubErrorCode::InvalidData,
                                               .Message = "A catalog item contains invalid or duplicate tags.",
                                               .AffectedItem = affectedItem});
            }
            return HubStatus::Success();
        }

        [[nodiscard]] HubStatus ValidateContent(const HubContentItem& item)
        {
            if (!Detail::IsBoundedIdentifier(item.Id) || !IsText(item.Title, 256) || !IsText(item.Summary, 4096) ||
                !IsText(item.Category, 128) || item.LocalPath.has_value() == item.HttpsUrl.has_value() ||
                (item.LocalPath && !Detail::IsSafeRelativePath(*item.LocalPath)) ||
                (item.HttpsUrl && !IsHttpsUrl(*item.HttpsUrl)) ||
                (item.Thumbnail && !Detail::IsSafeRelativePath(*item.Thumbnail)))
            {
                return HubStatus::Failure({.Code = HubErrorCode::InvalidData,
                                           .Message = "A content catalog item is invalid.",
                                           .AffectedItem = item.Id});
            }
            if (const auto status = ValidateTags(item.Tags, item.Id); !status)
                return status;
            return HubStatus::Success();
        }
    } // namespace

    HubStatus ValidateTemplateManifest(const HubTemplateManifest& manifest)
    {
        if (manifest.Category < TemplateCategory::Core || manifest.Category > TemplateCategory::Learning ||
            !Detail::IsBoundedIdentifier(manifest.Id) || !IsText(manifest.DisplayName, 256) ||
            !IsText(manifest.Description, 8192) || !Detail::IsSafeRelativePath(manifest.Thumbnail) ||
            manifest.Screenshots.size() > 16 || manifest.ProjectSchema == 0 ||
            !Detail::IsBoundedIdentifier(manifest.PlatformTarget, 64) || manifest.EstimatedSizeBytes == 0 ||
            manifest.EstimatedSizeBytes > MaximumTemplatePayloadBytes ||
            !Detail::IsSafeRelativePath(manifest.PayloadRoot) || manifest.PayloadFiles.size() > 100000 ||
            manifest.DefaultProjectConfiguration.size() > 64 || manifest.StarterContent.size() > 256 ||
            manifest.RequiredPackages.size() > MaximumPackages ||
            manifest.RecommendedPackages.size() > MaximumPackages || manifest.LicenseReferences.size() > 128)
        {
            return HubStatus::Failure({.Code = HubErrorCode::InvalidData,
                                       .Message = "A template manifest is invalid.",
                                       .AffectedItem = manifest.Id});
        }
        if (const auto status = ValidateTags(manifest.Tags, manifest.Id); !status)
            return status;
        for (const auto& screenshot : manifest.Screenshots)
        {
            if (!Detail::IsSafeRelativePath(screenshot))
                return HubStatus::Failure({.Code = HubErrorCode::InvalidData,
                                           .Message = "A template screenshot path is unsafe.",
                                           .AffectedItem = manifest.Id});
        }
        for (const auto& [key, value] : manifest.DefaultProjectConfiguration)
        {
            if (!Detail::IsBoundedIdentifier(key, 128) || !IsText(value, 2048, true))
                return HubStatus::Failure({.Code = HubErrorCode::InvalidData,
                                           .Message = "A template project setting is invalid.",
                                           .AffectedItem = manifest.Id});
        }
        std::set<std::string, std::less<>> payloadPaths;
        std::map<std::string, std::string, std::less<>> payloadPathSpellings;
        std::uint64_t payloadBytes = 0;
        for (const auto& file : manifest.PayloadFiles)
        {
            if (!Detail::IsSafeRelativePath(file.Path) || !Detail::IsSha256(file.Sha256) ||
                file.SizeBytes > MaximumTemplatePayloadBytes ||
                payloadBytes > MaximumTemplatePayloadBytes - file.SizeBytes)
            {
                return HubStatus::Failure({.Code = HubErrorCode::InvalidData,
                                           .Message = "A template payload file is invalid.",
                                           .AffectedItem = manifest.Id});
            }
            const auto normalized = file.Path.lexically_normal();
            auto key = Detail::PathToUtf8(normalized);
            std::ranges::transform(key, key.begin(),
                                   [](const unsigned char value) { return static_cast<char>(std::tolower(value)); });
            bool componentCollision = false;
            std::filesystem::path prefix;
            for (const auto& component : normalized)
            {
                prefix /= component;
                const auto spelling = Detail::PathToUtf8(prefix);
                auto folded = spelling;
                std::ranges::transform(folded, folded.begin(), [](const unsigned char value)
                                       { return static_cast<char>(std::tolower(value)); });
                const auto [existing, inserted] = payloadPathSpellings.emplace(std::move(folded), spelling);
                if (!inserted && existing->second != spelling)
                {
                    componentCollision = true;
                    break;
                }
            }
            if (key == "projectsettings/project.keireproject" || key == "library" || key.starts_with("library/") ||
                key == ".git" || key.starts_with(".git/") || key == ".vs" || key.starts_with(".vs/") || key == "bin" ||
                key.starts_with("bin/") || key == "obj" || key.starts_with("obj/") || componentCollision ||
                !payloadPaths.insert(std::move(key)).second)
            {
                return HubStatus::Failure({.Code = HubErrorCode::InvalidData,
                                           .Message = "A template payload path is reserved or collides by case.",
                                           .AffectedItem = manifest.Id});
            }
            payloadBytes += file.SizeBytes;
        }
        if (payloadBytes > manifest.EstimatedSizeBytes)
            return HubStatus::Failure({.Code = HubErrorCode::InvalidData,
                                       .Message = "A template payload exceeds its estimated size.",
                                       .AffectedItem = manifest.Id});
        for (const auto& path : manifest.StarterContent)
        {
            if (!Detail::IsSafeRelativePath(Detail::PathFromUtf8(path)))
                return HubStatus::Failure({.Code = HubErrorCode::InvalidData,
                                           .Message = "A template starter-content path is unsafe.",
                                           .AffectedItem = manifest.Id});
        }
        std::set<std::string, std::less<>> packageIds;
        const auto validatePackages = [&](const std::vector<PackageRequirement>& requirements) -> HubStatus
        {
            for (const auto& requirement : requirements)
            {
                if (!Detail::IsBoundedIdentifier(requirement.PackageId) ||
                    !packageIds.insert(requirement.PackageId).second)
                {
                    return HubStatus::Failure({.Code = HubErrorCode::InvalidData,
                                               .Message = "A template package requirement is invalid or duplicated.",
                                               .AffectedItem = manifest.Id});
                }
            }
            return HubStatus::Success();
        };
        if (auto status = validatePackages(manifest.RequiredPackages); !status)
            return status;
        if (auto status = validatePackages(manifest.RecommendedPackages); !status)
            return status;
        for (const auto& license : manifest.LicenseReferences)
        {
            if (!Detail::IsSafeRelativePath(Detail::PathFromUtf8(license)))
                return HubStatus::Failure({.Code = HubErrorCode::InvalidData,
                                           .Message = "A template license path is unsafe.",
                                           .AffectedItem = manifest.Id});
        }
        return HubStatus::Success();
    }

    HubResult<HubTemplateCatalog> ParseTemplateCatalog(const std::string_view document)
    {
        if (document.size() > MaximumCatalogBytes)
            return HubResult<HubTemplateCatalog>::Failure(
                {.Code = HubErrorCode::InvalidData, .Message = "The template catalog is too large."});
        try
        {
            const auto json = Detail::Json::parse(document);
            if (json.at("schemaVersion").get<std::uint32_t>() != HubTemplateCatalog::CurrentSchemaVersion)
                return HubResult<HubTemplateCatalog>::Failure(
                    {.Code = HubErrorCode::UnsupportedSchema,
                     .Message = "The template catalog schema is unsupported."});
            const auto& values = json.at("templates");
            if (!values.is_array() || values.size() > MaximumCatalogItems)
                throw std::invalid_argument("Invalid template collection.");
            HubTemplateCatalog result;
            std::set<std::pair<std::string, SemanticVersion>> identities;
            for (const auto& value : values)
            {
                HubTemplateManifest manifest;
                manifest.Id = value.at("id").get<std::string>();
                auto version = SemanticVersion::Parse(value.at("version").get<std::string>());
                auto compatibility = VersionConstraint::Parse(value.at("compatibleEditors").get<std::string>());
                const auto category = ParseTemplateCategory(value.at("category").get<std::string>());
                if (!version || !compatibility || !category)
                    throw std::invalid_argument("Invalid template version, compatibility, or category.");
                manifest.Version = std::move(version).Value();
                manifest.CompatibleEditors = std::move(compatibility).Value();
                manifest.Category = *category;
                manifest.DisplayName = value.at("displayName").get<std::string>();
                manifest.Description = value.at("description").get<std::string>();
                manifest.Tags = value.value("tags", std::vector<std::string>{});
                manifest.Thumbnail = Detail::PathFromUtf8(value.at("thumbnail").get<std::string>());
                for (const auto& screenshot : value.value("screenshots", std::vector<std::string>{}))
                    manifest.Screenshots.push_back(Detail::PathFromUtf8(screenshot));
                manifest.ProjectSchema = value.at("projectSchema").get<std::uint32_t>();
                manifest.PlatformTarget = value.at("platformTarget").get<std::string>();
                manifest.EstimatedSizeBytes = value.at("estimatedSizeBytes").get<std::uint64_t>();
                manifest.PayloadRoot = Detail::PathFromUtf8(value.at("payloadRoot").get<std::string>());
                for (const auto& file : value.value("payloadFiles", Detail::Json::array()))
                {
                    manifest.PayloadFiles.push_back({Detail::PathFromUtf8(file.at("path").get<std::string>()),
                                                     file.at("sizeBytes").get<std::uint64_t>(),
                                                     file.at("sha256").get<std::string>()});
                }
                if (value.contains("defaultProjectConfiguration"))
                {
                    manifest.DefaultProjectConfiguration =
                        value.at("defaultProjectConfiguration").get<std::map<std::string, std::string, std::less<>>>();
                }
                manifest.StarterContent = value.value("starterContent", std::vector<std::string>{});
                for (const auto& requirement : value.value("requiredPackages", Detail::Json::array()))
                {
                    auto parsed = ParseRequirement(requirement);
                    if (!parsed)
                        throw std::invalid_argument(parsed.Error().Message);
                    manifest.RequiredPackages.push_back(std::move(parsed).Value());
                }
                for (const auto& requirement : value.value("recommendedPackages", Detail::Json::array()))
                {
                    auto parsed = ParseRequirement(requirement);
                    if (!parsed)
                        throw std::invalid_argument(parsed.Error().Message);
                    manifest.RecommendedPackages.push_back(std::move(parsed).Value());
                }
                manifest.LicenseReferences = value.value("licenses", std::vector<std::string>{});
                manifest.Featured = value.value("featured", false);
                if (const auto status = ValidateTemplateManifest(manifest); !status)
                    return HubResult<HubTemplateCatalog>::Failure(status.Error());
                if (!identities.emplace(manifest.Id, manifest.Version).second)
                    throw std::invalid_argument("Duplicate template identity and version.");
                result.Templates.push_back(std::move(manifest));
            }
            return HubResult<HubTemplateCatalog>::Success(std::move(result));
        }
        catch (const std::exception& error)
        {
            return HubResult<HubTemplateCatalog>::Failure({.Code = HubErrorCode::InvalidData,
                                                           .Message = "The template catalog is malformed.",
                                                           .TechnicalDetails = error.what()});
        }
    }

    HubResult<HubContentCatalog> ParseContentCatalog(const std::string_view document)
    {
        if (document.size() > MaximumCatalogBytes)
            return HubResult<HubContentCatalog>::Failure(
                {.Code = HubErrorCode::InvalidData, .Message = "The content catalog is too large."});
        try
        {
            const auto json = Detail::Json::parse(document);
            if (json.at("schemaVersion").get<std::uint32_t>() != HubContentCatalog::CurrentSchemaVersion)
                return HubResult<HubContentCatalog>::Failure(
                    {.Code = HubErrorCode::UnsupportedSchema, .Message = "The content catalog schema is unsupported."});
            HubContentCatalog result;
            result.Locale = json.at("locale").get<std::string>();
            if (!IsText(result.Locale, 32))
                throw std::invalid_argument("Invalid content locale.");
            std::set<std::string, std::less<>> identities;
            const auto parseCollection = [&](const Detail::Json& values,
                                             std::vector<HubContentItem>& output) -> HubStatus
            {
                if (!values.is_array() || values.size() > MaximumCatalogItems)
                    return HubStatus::Failure(
                        {.Code = HubErrorCode::InvalidData, .Message = "A content catalog collection is invalid."});
                for (const auto& value : values)
                {
                    HubContentItem item;
                    item.Id = value.at("id").get<std::string>();
                    item.Title = value.at("title").get<std::string>();
                    item.Summary = value.at("summary").get<std::string>();
                    const auto difficulty = ParseDifficulty(value.at("difficulty").get<std::string>());
                    const auto type = ParseContentType(value.at("type").get<std::string>());
                    if (!difficulty || !type)
                        return HubStatus::Failure({.Code = HubErrorCode::InvalidData,
                                                   .Message = "A content item uses an unknown type or difficulty.",
                                                   .AffectedItem = item.Id});
                    item.Difficulty = *difficulty;
                    item.Type = *type;
                    item.Category = value.at("category").get<std::string>();
                    if (value.contains("localPath"))
                        item.LocalPath = Detail::PathFromUtf8(value.at("localPath").get<std::string>());
                    if (value.contains("url"))
                        item.HttpsUrl = value.at("url").get<std::string>();
                    if (value.contains("thumbnail"))
                        item.Thumbnail = Detail::PathFromUtf8(value.at("thumbnail").get<std::string>());
                    item.Tags = value.value("tags", std::vector<std::string>{});
                    if (value.contains("requiredEditorVersion"))
                    {
                        auto constraint =
                            VersionConstraint::Parse(value.at("requiredEditorVersion").get<std::string>());
                        if (!constraint)
                            return HubStatus::Failure(constraint.Error());
                        item.RequiredEditorVersion = std::move(constraint).Value();
                    }
                    if (value.contains("samplePackage"))
                    {
                        auto requirement = ParseRequirement(value.at("samplePackage"));
                        if (!requirement)
                            return HubStatus::Failure(requirement.Error());
                        item.SamplePackage = std::move(requirement).Value();
                    }
                    item.Featured = value.value("featured", false);
                    if (const auto status = ValidateContent(item); !status)
                        return status;
                    if (!identities.insert(item.Id).second)
                        return HubStatus::Failure({.Code = HubErrorCode::DuplicateIdentifier,
                                                   .Message = "The content catalog contains a duplicate identity.",
                                                   .AffectedItem = item.Id});
                    output.push_back(std::move(item));
                }
                return HubStatus::Success();
            };
            if (auto status = parseCollection(json.value("learn", Detail::Json::array()), result.Learn); !status)
                return HubResult<HubContentCatalog>::Failure(status.Error());
            if (auto status = parseCollection(json.value("resources", Detail::Json::array()), result.Resources);
                !status)
                return HubResult<HubContentCatalog>::Failure(status.Error());
            return HubResult<HubContentCatalog>::Success(std::move(result));
        }
        catch (const std::exception& error)
        {
            return HubResult<HubContentCatalog>::Failure({.Code = HubErrorCode::InvalidData,
                                                          .Message = "The content catalog is malformed.",
                                                          .TechnicalDetails = error.what()});
        }
    }

    HubResult<HubLicenseCatalog> ParseLicenseCatalog(const std::string_view document)
    {
        if (document.size() > MaximumCatalogBytes)
            return HubResult<HubLicenseCatalog>::Failure(
                {.Code = HubErrorCode::InvalidData, .Message = "The license catalog is too large."});
        try
        {
            const auto json = Detail::Json::parse(document);
            if (json.at("schemaVersion").get<std::uint32_t>() != HubLicenseCatalog::CurrentSchemaVersion)
                return HubResult<HubLicenseCatalog>::Failure(
                    {.Code = HubErrorCode::UnsupportedSchema, .Message = "The license catalog schema is unsupported."});
            const auto& values = json.at("licenses");
            if (!values.is_array() || values.size() > MaximumCatalogItems)
                throw std::invalid_argument("Invalid license collection.");
            HubLicenseCatalog result;
            std::set<std::string, std::less<>> identities;
            for (const auto& value : values)
            {
                HubLicenseEntry entry;
                entry.Id = value.at("id").get<std::string>();
                entry.DisplayName = value.at("displayName").get<std::string>();
                const auto scope = ParseLicenseScope(value.at("scope").get<std::string>());
                if (!scope)
                    throw std::invalid_argument("Unknown license scope.");
                entry.Scope = *scope;
                if (value.contains("packageId"))
                    entry.PackageId = value.at("packageId").get<std::string>();
                if (value.contains("version"))
                    entry.Version = value.at("version").get<std::string>();
                entry.SourcePath = Detail::PathFromUtf8(value.at("sourcePath").get<std::string>());
                if (value.contains("embeddedText"))
                    entry.EmbeddedText = value.at("embeddedText").get<std::string>();
                if (!Detail::IsBoundedIdentifier(entry.Id) || !IsText(entry.DisplayName, 256) ||
                    !Detail::IsSafeRelativePath(entry.SourcePath) ||
                    (entry.PackageId && !Detail::IsBoundedIdentifier(*entry.PackageId)) ||
                    (entry.Version && !IsText(*entry.Version, 128)) ||
                    (entry.EmbeddedText && !IsText(*entry.EmbeddedText, 1024 * 1024, true)) ||
                    !identities.insert(entry.Id).second)
                {
                    throw std::invalid_argument("Invalid or duplicate license entry.");
                }
                result.Licenses.push_back(std::move(entry));
            }
            return HubResult<HubLicenseCatalog>::Success(std::move(result));
        }
        catch (const std::exception& error)
        {
            return HubResult<HubLicenseCatalog>::Failure({.Code = HubErrorCode::InvalidData,
                                                          .Message = "The license catalog is malformed.",
                                                          .TechnicalDetails = error.what()});
        }
    }
} // namespace KeireHub
