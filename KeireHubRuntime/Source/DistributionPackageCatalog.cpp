#include "KeireHubRuntime/DistributionCatalog.h"

#include <KeireHubRuntimeInternal/DistributionEncoding.h>

#include <algorithm>
#include <array>
#include <limits>
#include <ranges>
#include <set>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace KeireHub
{
    namespace
    {
        constexpr std::size_t MaximumCatalogBytes = std::size_t{32U} * 1024U * 1024U;
        constexpr std::size_t MaximumCatalogDepth = 32;
        constexpr std::size_t MaximumPackageCount = 4096;
        constexpr std::size_t MaximumAggregateCollectionItems = 250'000;

        [[nodiscard]] HubError CatalogError(const HubErrorCode code, std::string message, std::string item = {},
                                            std::string details = {})
        {
            return {.Code = code,
                    .Message = std::move(message),
                    .AffectedItem = std::move(item),
                    .TechnicalDetails = std::move(details)};
        }

        template <std::size_t RequiredCount, std::size_t OptionalCount>
        [[nodiscard]] bool HasObjectShape(const Detail::Json& value,
                                          const std::array<std::string_view, RequiredCount>& required,
                                          const std::array<std::string_view, OptionalCount>& optional)
        {
            if (!value.is_object())
                return false;
            for (const auto key : required)
            {
                if (!value.contains(std::string(key)))
                    return false;
            }
            for (const auto& [key, member] : value.items())
            {
                static_cast<void>(member);
                const auto permitted = [&](const auto& keys)
                { return std::ranges::find(keys, std::string_view(key)) != keys.end(); };
                if (!permitted(required) && !permitted(optional))
                    return false;
            }
            return true;
        }

        template <std::size_t RequiredCount>
        [[nodiscard]] bool HasObjectShape(const Detail::Json& value,
                                          const std::array<std::string_view, RequiredCount>& required)
        {
            return HasObjectShape(value, required, std::array<std::string_view, 0>{});
        }

        [[nodiscard]] bool IsHostPlatform(const std::string_view value) noexcept
        {
            return value == "windows" || value == "linux" || value == "macos";
        }

        [[nodiscard]] bool IsHostArchitecture(const std::string_view value) noexcept
        {
            return value == "x86_64" || value == "arm64";
        }

        [[nodiscard]] HubStatus ValidateExpectedIdentity(const DistributionPackageCatalogIdentity& identity)
        {
            if (!Detail::IsDistributionKeyId(identity.KeyId) || identity.Sequence == 0U ||
                identity.Sequence > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) ||
                !Detail::ParseUtcInstant(identity.ExpiresAt) || !Detail::IsDistributionRouteToken(identity.Channel) ||
                !IsHostPlatform(identity.Platform) || !IsHostArchitecture(identity.Architecture))
            {
                return HubStatus::Failure(CatalogError(HubErrorCode::InvalidArgument,
                                                       "The expected package catalog identity is invalid.",
                                                       identity.Channel));
            }
            return HubStatus::Success();
        }

        [[nodiscard]] HubStatus ValidatePackageShape(const Detail::Json& value, std::size_t& aggregateItems)
        {
            constexpr std::array required{std::string_view{"schemaVersion"}, std::string_view{"packageId"},
                                          std::string_view{"version"},       std::string_view{"type"},
                                          std::string_view{"displayName"},   std::string_view{"channel"},
                                          std::string_view{"platform"},      std::string_view{"architecture"},
                                          std::string_view{"artifact"},      std::string_view{"installedSizeBytes"},
                                          std::string_view{"files"},         std::string_view{"signatureKeyId"}};
            constexpr std::array optional{std::string_view{"engineCompatibility"}, std::string_view{"dependencies"},
                                          std::string_view{"conflicts"}, std::string_view{"licenses"}};
            constexpr std::array artifactKeys{std::string_view{"sizeBytes"}, std::string_view{"sha256"}};
            constexpr std::array fileKeys{std::string_view{"path"}, std::string_view{"sizeBytes"},
                                          std::string_view{"sha256"}};
            constexpr std::array fileOptional{std::string_view{"mode"}};
            constexpr std::array relationRequired{std::string_view{"packageId"}};
            constexpr std::array relationOptional{std::string_view{"version"}};

            if (!HasObjectShape(value, required, optional) || !HasObjectShape(value.at("artifact"), artifactKeys) ||
                !value.at("files").is_array())
            {
                return HubStatus::Failure(CatalogError(HubErrorCode::PackageManifestInvalid,
                                                       "A package catalog entry has an unexpected schema."));
            }
            const auto countCollection = [&](const Detail::Json& collection) -> bool
            {
                if (!collection.is_array() || collection.size() > MaximumAggregateCollectionItems ||
                    aggregateItems > MaximumAggregateCollectionItems - collection.size())
                {
                    return false;
                }
                aggregateItems += collection.size();
                return true;
            };
            if (!countCollection(value.at("files")))
            {
                return HubStatus::Failure(CatalogError(HubErrorCode::PackageManifestInvalid,
                                                       "The package catalog inventory exceeds its item limit."));
            }
            for (const auto& file : value.at("files"))
            {
                if (!HasObjectShape(file, fileKeys, fileOptional))
                {
                    return HubStatus::Failure(CatalogError(HubErrorCode::PackageManifestInvalid,
                                                           "A package file entry has an unexpected schema."));
                }
            }
            for (const auto key : {std::string_view{"dependencies"}, std::string_view{"conflicts"}})
            {
                const auto iterator = value.find(std::string(key));
                if (iterator == value.end())
                    continue;
                if (!countCollection(*iterator))
                {
                    return HubStatus::Failure(
                        CatalogError(HubErrorCode::PackageManifestInvalid,
                                     "The package catalog dependency metadata exceeds its item limit."));
                }
                for (const auto& relation : *iterator)
                {
                    if (!HasObjectShape(relation, relationRequired, relationOptional))
                    {
                        return HubStatus::Failure(
                            CatalogError(HubErrorCode::PackageManifestInvalid,
                                         "A package dependency or conflict entry has an unexpected schema."));
                    }
                }
            }
            if (const auto licenses = value.find("licenses"); licenses != value.end() && !countCollection(*licenses))
            {
                return HubStatus::Failure(CatalogError(HubErrorCode::PackageManifestInvalid,
                                                       "The package catalog license metadata exceeds its item limit."));
            }
            return HubStatus::Success();
        }
    } // namespace

    HubResult<DistributionPackageCatalog>
    ParseDistributionPackageCatalog(const std::span<const std::byte> exactBytes,
                                    const DistributionPackageCatalogIdentity& expectedIdentity)
    {
        if (const auto status = ValidateExpectedIdentity(expectedIdentity); !status)
            return HubResult<DistributionPackageCatalog>::Failure(status.Error());
        if (exactBytes.empty() || exactBytes.size() > MaximumCatalogBytes)
        {
            return HubResult<DistributionPackageCatalog>::Failure(
                CatalogError(HubErrorCode::InvalidData, "The signed package catalog is empty or too large.",
                             expectedIdentity.Channel));
        }

        const auto text = std::string_view(reinterpret_cast<const char*>(exactBytes.data()), exactBytes.size());
        auto parsed = Detail::ParseStrictJson(text, MaximumCatalogDepth, HubErrorCode::InvalidData,
                                              "The signed package catalog is malformed.", expectedIdentity.Channel);
        if (!parsed)
            return HubResult<DistributionPackageCatalog>::Failure(parsed.Error());

        try
        {
            constexpr std::array rootKeys{std::string_view{"schemaVersion"}, std::string_view{"keyId"},
                                          std::string_view{"sequence"},      std::string_view{"expiresAt"},
                                          std::string_view{"channel"},       std::string_view{"platform"},
                                          std::string_view{"architecture"},  std::string_view{"packages"}};
            constexpr std::array rootOptional{std::string_view{"minimumSupportedHubVersion"}};
            const auto& root = parsed.Value();
            if (!HasObjectShape(root, rootKeys, rootOptional) || !root.at("schemaVersion").is_number_unsigned() ||
                !root.at("sequence").is_number_unsigned() || !root.at("packages").is_array())
                throw std::invalid_argument("The package catalog header has an unexpected schema.");

            const auto schemaVersion = root.at("schemaVersion").get<std::uint32_t>();
            if (schemaVersion != DistributionPackageCatalog::CurrentSchemaVersion)
            {
                return HubResult<DistributionPackageCatalog>::Failure(
                    CatalogError(HubErrorCode::UnsupportedSchema, "The signed package catalog schema is unsupported.",
                                 expectedIdentity.Channel));
            }

            DistributionPackageCatalog result;
            result.SchemaVersion = schemaVersion;
            result.Identity = {.KeyId = root.at("keyId").get<std::string>(),
                               .Sequence = root.at("sequence").get<std::uint64_t>(),
                               .ExpiresAt = root.at("expiresAt").get<std::string>(),
                               .Channel = root.at("channel").get<std::string>(),
                               .Platform = root.at("platform").get<std::string>(),
                               .Architecture = root.at("architecture").get<std::string>()};
            const auto catalogExpiry = Detail::ParseUtcInstant(result.Identity.ExpiresAt);
            const auto expectedExpiry = Detail::ParseUtcInstant(expectedIdentity.ExpiresAt);
            if (const auto minimum = root.find("minimumSupportedHubVersion"); minimum != root.end())
            {
                if (!minimum->is_string())
                    throw std::invalid_argument("The minimum supported Hub version is invalid.");
                auto version = SemanticVersion::Parse(minimum->get<std::string>());
                if (!version)
                    throw std::invalid_argument("The minimum supported Hub version is invalid.");
                result.MinimumSupportedHubVersion = std::move(version).Value();
            }
            if (result.Identity.KeyId != expectedIdentity.KeyId ||
                result.Identity.Sequence != expectedIdentity.Sequence || !catalogExpiry || !expectedExpiry ||
                *catalogExpiry != *expectedExpiry || result.Identity.Channel != expectedIdentity.Channel ||
                result.Identity.Platform != expectedIdentity.Platform ||
                result.Identity.Architecture != expectedIdentity.Architecture ||
                !Detail::IsDistributionKeyId(result.Identity.KeyId) || result.Identity.Sequence == 0U ||
                result.Identity.Sequence > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
            {
                return HubResult<DistributionPackageCatalog>::Failure(CatalogError(
                    HubErrorCode::CatalogIdentityMismatch,
                    "The signed package catalog identity does not match its verified metadata or endpoint.",
                    expectedIdentity.Channel));
            }

            const auto& packages = root.at("packages");
            if (!packages.is_array() || packages.size() > MaximumPackageCount)
                throw std::invalid_argument("The package catalog collection exceeds its item limit.");

            result.Packages.reserve(packages.size());
            std::size_t aggregateItems = 0;
            std::set<std::pair<std::string, SemanticVersion>> identities;
            for (const auto& value : packages)
            {
                if (const auto status = ValidatePackageShape(value, aggregateItems); !status)
                    return HubResult<DistributionPackageCatalog>::Failure(status.Error());
                auto manifest = ParsePackageManifest(value.dump());
                if (!manifest)
                    return HubResult<DistributionPackageCatalog>::Failure(manifest.Error());
                if (manifest.Value().Channel != result.Identity.Channel)
                {
                    return HubResult<DistributionPackageCatalog>::Failure(
                        CatalogError(HubErrorCode::PackageManifestInvalid,
                                     "A package is published under the wrong release channel.", manifest.Value().Id));
                }
                if ((manifest.Value().Platform != "any" && manifest.Value().Platform != result.Identity.Platform) ||
                    (manifest.Value().Architecture != "any" &&
                     manifest.Value().Architecture != result.Identity.Architecture))
                {
                    return HubResult<DistributionPackageCatalog>::Failure(
                        CatalogError(HubErrorCode::PackageHostIncompatible,
                                     "A package catalog entry does not support this host.", manifest.Value().Id));
                }
                if (!identities.emplace(manifest.Value().Id, manifest.Value().Version).second)
                {
                    return HubResult<DistributionPackageCatalog>::Failure(
                        CatalogError(HubErrorCode::DuplicateIdentifier,
                                     "The package catalog contains a duplicate package version.", manifest.Value().Id));
                }
                result.Packages.push_back(std::move(manifest).Value());
            }

            std::ranges::sort(result.Packages,
                              [](const PackageManifest& left, const PackageManifest& right)
                              {
                                  if (left.Id != right.Id)
                                      return left.Id < right.Id;
                                  return left.Version > right.Version;
                              });
            return HubResult<DistributionPackageCatalog>::Success(std::move(result));
        }
        catch (const std::exception& exception)
        {
            return HubResult<DistributionPackageCatalog>::Failure(
                CatalogError(HubErrorCode::InvalidData, "The signed package catalog is malformed.",
                             expectedIdentity.Channel, exception.what()));
        }
    }
} // namespace KeireHub
