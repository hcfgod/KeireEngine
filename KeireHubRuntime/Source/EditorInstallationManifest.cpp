#include "EditorInstallationManifest.h"

#include "DistributionEncoding.h"
#include "Persistence.h"

#include "KeireHubRuntime/PackageResolver.h"

#include <algorithm>
#include <cctype>
#include <limits>
#include <ranges>
#include <set>
#include <span>
#include <stdexcept>
#include <utility>

namespace KeireHub
{
    namespace
    {
        constexpr std::size_t MaximumManifestBytes = 8U * 1024U * 1024U;
        constexpr std::size_t MaximumManifestDepth = 32;
        constexpr std::size_t MaximumInventoryFiles = 100000;
        constexpr std::size_t MaximumEntrypoints = 16;
        constexpr std::uint64_t MaximumInstalledBytes = 1024ULL * 1024ULL * 1024ULL * 1024ULL;

        [[nodiscard]] HubError ManifestError(std::string message, std::string affectedItem,
                                             std::string technicalDetails = {})
        {
            return {.Code = HubErrorCode::EditorManifestInvalid,
                    .Message = std::move(message),
                    .AffectedItem = std::move(affectedItem),
                    .TechnicalDetails = std::move(technicalDetails)};
        }

        [[nodiscard]] HubError InventoryError(std::string message, std::string affectedItem,
                                              std::string technicalDetails = {})
        {
            return {.Code = HubErrorCode::EditorInventoryInvalid,
                    .Message = std::move(message),
                    .AffectedItem = std::move(affectedItem),
                    .TechnicalDetails = std::move(technicalDetails)};
        }

        [[nodiscard]] std::string Fingerprint(const Detail::Json& document)
        {
            auto payload = document;
            payload.erase("manifestFingerprint");
            payload.erase("installedSizeBytes");
            const auto canonical = payload.dump(-1, ' ', false, Detail::Json::error_handler_t::strict);
            return Detail::Sha256Hex(std::as_bytes(std::span(canonical.data(), canonical.size())));
        }

        [[nodiscard]] bool IsEntrypointRole(const std::string_view value) noexcept
        {
            if (value.empty() || value.size() > 64 || std::isalpha(static_cast<unsigned char>(value.front())) == 0)
                return false;
            return std::ranges::all_of(value,
                                       [](const unsigned char character) { return std::isalnum(character) != 0; });
        }

        [[nodiscard]] HubResult<Detail::Json> ParseManifest(const std::string_view document)
        {
            if (document.size() > MaximumManifestBytes)
            {
                return HubResult<Detail::Json>::Failure(
                    ManifestError("The editor package manifest is too large.", "editor-package.json"));
            }
            auto parsed = Detail::ParseStrictJson(document, MaximumManifestDepth, HubErrorCode::EditorManifestInvalid,
                                                  "The editor package manifest is malformed.", "editor-package.json");
            if (!parsed || !parsed.Value().is_object())
            {
                if (!parsed)
                    return parsed;
                return HubResult<Detail::Json>::Failure(
                    ManifestError("The editor package manifest must be an object.", "editor-package.json"));
            }
            return parsed;
        }
    } // namespace

    HubResult<std::string> ComputeEditorPackageManifestFingerprint(const std::string_view document)
    {
        auto parsed = ParseManifest(document);
        if (!parsed)
            return HubResult<std::string>::Failure(parsed.Error());
        try
        {
            return HubResult<std::string>::Success(Fingerprint(parsed.Value()));
        }
        catch (const std::exception& error)
        {
            return HubResult<std::string>::Failure(ManifestError("The editor package manifest cannot be fingerprinted.",
                                                                 "editor-package.json", error.what()));
        }
    }
} // namespace KeireHub

namespace KeireHub::Detail
{
    std::string CanonicalEditorPlatform(const std::string_view value)
    {
        std::string result(value);
        std::ranges::transform(result, result.begin(), [](const unsigned char character)
                               { return static_cast<char>(std::tolower(character)); });
        if (result == "mac" || result == "osx")
            return "macos";
        return result;
    }

    std::string CanonicalEditorArchitecture(const std::string_view value)
    {
        std::string result(value);
        std::ranges::transform(result, result.begin(), [](const unsigned char character)
                               { return static_cast<char>(std::tolower(character)); });
        if (result == "amd64" || result == "x64")
            return "x86_64";
        if (result == "aarch64")
            return "arm64";
        return result;
    }

    std::string NormalizedEditorPathKey(const std::filesystem::path& path)
    {
        auto result = PathToUtf8(path.lexically_normal());
        std::ranges::transform(result, result.begin(), [](const unsigned char character)
                               { return static_cast<char>(std::tolower(character)); });
        return result;
    }

    HubResult<EditorPackageManifest> ReadEditorPackageManifest(const std::filesystem::path& root)
    {
        const auto manifestPath = root / "editor-package.json";
        std::error_code error;
        const auto status = std::filesystem::symlink_status(manifestPath, error);
        if (error || !std::filesystem::is_regular_file(status) || std::filesystem::is_symlink(status))
        {
            return HubResult<EditorPackageManifest>::Failure(ManifestError(
                "The editor package manifest is missing or unsafe.", "editor-package.json", error.message()));
        }
        auto bytes = ReadTextFile(manifestPath, MaximumManifestBytes);
        if (!bytes)
        {
            return HubResult<EditorPackageManifest>::Failure(
                ManifestError("The editor package manifest could not be read.", "editor-package.json",
                              bytes.Error().TechnicalDetails));
        }
        auto parsed = ParseManifest(bytes.Value());
        if (!parsed)
            return HubResult<EditorPackageManifest>::Failure(parsed.Error());
        try
        {
            const auto& document = parsed.Value();
            if (document.at("schemaVersion").get<std::uint32_t>() != 2U ||
                document.at("artifact").get<std::string>() != "editor")
            {
                return HubResult<EditorPackageManifest>::Failure(
                    ManifestError("This is not a supported editor package manifest.", "editor-package.json"));
            }

            EditorPackageManifest result;
            result.PackageId = document.at("packageId").get<std::string>();
            result.Version = document.at("version").get<std::string>();
            if (!SemanticVersion::Parse(result.Version))
                throw std::invalid_argument("The editor version is not semantic.");
            result.Channel = document.at("channel").get<std::string>();
            result.Platform = CanonicalEditorPlatform(document.at("platform").get<std::string>());
            result.Architecture = CanonicalEditorArchitecture(document.at("architecture").get<std::string>());
            result.Fingerprint = document.at("manifestFingerprint").get<std::string>();
            result.InstalledSizeBytes = document.at("installedSizeBytes").get<std::uint64_t>();
            result.BundledDotnetSdk = document.value("bundledDotnetSdk", std::string{});
            result.ManifestSizeBytes = bytes.Value().size();
            if (!IsBoundedIdentifier(result.PackageId) || !IsBoundedIdentifier(result.Channel, 64) ||
                (result.Platform != "windows" && result.Platform != "linux" && result.Platform != "macos") ||
                (result.Architecture != "x86_64" && result.Architecture != "arm64") || !IsSha256(result.Fingerprint) ||
                result.InstalledSizeBytes == 0 || result.InstalledSizeBytes > MaximumInstalledBytes ||
                result.BundledDotnetSdk.size() > 64)
            {
                throw std::invalid_argument("The editor package identity is invalid.");
            }
            const auto calculatedFingerprint = Fingerprint(document);
            if (calculatedFingerprint != result.Fingerprint)
            {
                return HubResult<EditorPackageManifest>::Failure(ManifestError(
                    "The editor package manifest fingerprint does not match its metadata.", "manifestFingerprint"));
            }

            const auto& projectSchema = document.at("projectSchema");
            result.MinimumProjectSchema = projectSchema.at("minimum").get<std::uint32_t>();
            result.MaximumProjectSchema = projectSchema.at("maximum").get<std::uint32_t>();
            if (!projectSchema.is_object() || result.MinimumProjectSchema == 0 ||
                result.MaximumProjectSchema < result.MinimumProjectSchema)
            {
                throw std::invalid_argument("The project schema range is invalid.");
            }

            if (!document.contains("inventoryExcludes"))
            {
                return HubResult<EditorPackageManifest>::Failure(
                    InventoryError("The editor inventory exclusions are missing.", "inventoryExcludes"));
            }
            const auto& inventoryExcludes = document.at("inventoryExcludes");
            if (!inventoryExcludes.is_array() || inventoryExcludes.size() != 1 ||
                inventoryExcludes.front().get<std::string>() != "editor-package.json")
            {
                return HubResult<EditorPackageManifest>::Failure(
                    InventoryError("The editor inventory exclusions are invalid.", "inventoryExcludes"));
            }

            if (!document.contains("files"))
            {
                return HubResult<EditorPackageManifest>::Failure(
                    InventoryError("The editor package file inventory is missing.", "files"));
            }
            const auto& files = document.at("files");
            if (!files.is_array() || files.empty() || files.size() > MaximumInventoryFiles)
            {
                return HubResult<EditorPackageManifest>::Failure(
                    InventoryError("The editor package file inventory is missing or invalid.", "files"));
            }
            std::set<std::string, std::less<>> inventoryPaths;
            std::uint64_t declaredBytes = 0;
            result.Files.reserve(files.size());
            for (const auto& value : files)
            {
                EditorPackageFile file{PathFromUtf8(value.at("path").get<std::string>()),
                                       value.at("sizeBytes").get<std::uint64_t>(),
                                       value.at("sha256").get<std::string>()};
                if (!IsSafeRelativePath(file.Path) || !IsSha256(file.Sha256) ||
                    file.SizeBytes > MaximumInstalledBytes ||
                    declaredBytes > std::numeric_limits<std::uint64_t>::max() - file.SizeBytes ||
                    !inventoryPaths.insert(NormalizedEditorPathKey(file.Path)).second)
                {
                    return HubResult<EditorPackageManifest>::Failure(
                        InventoryError("The editor package file inventory is unsafe.", PathToUtf8(file.Path)));
                }
                declaredBytes += file.SizeBytes;
                result.Files.push_back(std::move(file));
            }
            if (declaredBytes > std::numeric_limits<std::uint64_t>::max() - result.ManifestSizeBytes ||
                declaredBytes + result.ManifestSizeBytes != result.InstalledSizeBytes)
            {
                return HubResult<EditorPackageManifest>::Failure(InventoryError(
                    "The editor package installed size does not match its inventory.", "installedSizeBytes"));
            }

            if (!document.contains("entrypoints"))
            {
                return HubResult<EditorPackageManifest>::Failure(
                    InventoryError("The editor package entrypoints are missing.", "entrypoints"));
            }
            const auto& entrypoints = document.at("entrypoints");
            if (!entrypoints.is_object() || entrypoints.empty() || entrypoints.size() > MaximumEntrypoints ||
                !entrypoints.contains("editor"))
            {
                return HubResult<EditorPackageManifest>::Failure(
                    InventoryError("The editor package entrypoints are missing or invalid.", "entrypoints"));
            }
            std::set<std::string, std::less<>> entrypointPaths;
            result.Entrypoints.reserve(entrypoints.size());
            for (const auto& [role, value] : entrypoints.items())
            {
                if (!IsEntrypointRole(role) || !value.is_string())
                {
                    return HubResult<EditorPackageManifest>::Failure(
                        InventoryError("The editor package contains an invalid entrypoint.", role));
                }
                auto entrypoint = PathFromUtf8(value.get<std::string>());
                const auto key = NormalizedEditorPathKey(entrypoint);
                if (!IsSafeRelativePath(entrypoint) || !inventoryPaths.contains(key) ||
                    !entrypointPaths.insert(key).second)
                {
                    return HubResult<EditorPackageManifest>::Failure(InventoryError(
                        "An editor entrypoint is unsafe or absent from the file inventory.", PathToUtf8(entrypoint)));
                }
                if (role == "editor")
                    result.EditorEntrypoint = entrypoint;
                else if (role == "assetTool")
                    result.AssetToolEntrypoint = entrypoint;
                result.Entrypoints.push_back(std::move(entrypoint));
            }
            return HubResult<EditorPackageManifest>::Success(std::move(result));
        }
        catch (const std::exception& error)
        {
            return HubResult<EditorPackageManifest>::Failure(
                ManifestError("The editor package manifest is malformed.", "editor-package.json", error.what()));
        }
    }
} // namespace KeireHub::Detail
