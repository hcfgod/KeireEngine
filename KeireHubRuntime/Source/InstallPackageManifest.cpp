#include <KeireHubRuntimeInternal/InstallTransactionInternal.h>

#include <KeireHubRuntimeInternal/DistributionEncoding.h>
#include <KeireHubRuntimeInternal/Persistence.h>

#include "KeireHubRuntime/PackageResolver.h"

#include <array>
#include <limits>
#include <ranges>
#include <set>
#include <span>
#include <stdexcept>

namespace KeireHub::Detail
{
    namespace
    {
        constexpr std::size_t MaximumManifestBytes = std::size_t{16U} * 1024U * 1024U;
        constexpr std::size_t MaximumManifestDepth = 32;
        constexpr std::size_t MaximumInventoryFiles = 100000;

        [[nodiscard]] HubError ManifestError(std::string message, std::string affectedItem = {},
                                             std::string details = {})
        {
            return {.Code = HubErrorCode::PackageManifestInvalid,
                    .Message = std::move(message),
                    .AffectedItem = std::move(affectedItem),
                    .TechnicalDetails = std::move(details)};
        }

        [[nodiscard]] std::string Fingerprint(const Json& document)
        {
            auto payload = document;
            payload.erase("manifestFingerprint");
            payload.erase("installedSizeBytes");
            const auto canonical = payload.dump(-1, ' ', false, Json::error_handler_t::strict);
            return Sha256Hex(std::as_bytes(std::span(canonical.data(), canonical.size())));
        }
    } // namespace

    HubResult<InstallerPackageManifest> ReadInstallerPackageManifest(const std::filesystem::path& sourceRoot,
                                                                     const InstallProduct product,
                                                                     const bool allowUnknownFiles)
    {
        if (const auto status = ValidateInstallTree(sourceRoot, false); !status)
            return HubResult<InstallerPackageManifest>::Failure(status.Error());

        const auto manifestName = std::string(ToString(product)) + "-package.json";
        const auto manifestPath = sourceRoot / manifestName;
        auto bytes = ReadTextFile(manifestPath, MaximumManifestBytes);
        if (!bytes)
            return HubResult<InstallerPackageManifest>::Failure(bytes.Error());
        auto parsed = ParseStrictJson(bytes.Value(), MaximumManifestDepth, HubErrorCode::PackageManifestInvalid,
                                      "The installer package manifest is malformed.", manifestName);
        if (!parsed || !parsed.Value().is_object())
        {
            return parsed ? HubResult<InstallerPackageManifest>::Failure(
                                ManifestError("The installer package manifest must be an object.", manifestName))
                          : HubResult<InstallerPackageManifest>::Failure(parsed.Error());
        }

        try
        {
            const auto& document = parsed.Value();
            if (document.at("schemaVersion").get<std::uint32_t>() != 2U ||
                document.at("artifact").get<std::string>() != ToString(product))
            {
                return HubResult<InstallerPackageManifest>::Failure(
                    ManifestError("The package manifest has the wrong product or schema.", manifestName));
            }
            InstallerPackageManifest manifest{.ProductId = document.at("packageId").get<std::string>(),
                                              .Product = product,
                                              .Version = document.at("version").get<std::string>(),
                                              .BuildIdentity = document.at("commit").get<std::string>(),
                                              .Fingerprint = document.at("manifestFingerprint").get<std::string>(),
                                              .ManifestPath = PathFromUtf8(manifestName)};
            if (!IsBoundedIdentifier(manifest.ProductId) || !SemanticVersion::Parse(manifest.Version) ||
                !IsBoundedIdentifier(manifest.BuildIdentity, 256) || !IsSha256(manifest.Fingerprint) ||
                Fingerprint(document) != manifest.Fingerprint)
            {
                return HubResult<InstallerPackageManifest>::Failure(
                    ManifestError("The package manifest identity is invalid.", manifestName));
            }

            if (!document.contains("inventoryExcludes") || !document.at("inventoryExcludes").is_array() ||
                document.at("inventoryExcludes").size() != 1 ||
                document.at("inventoryExcludes").front().get<std::string>() != manifestName)
            {
                return HubResult<InstallerPackageManifest>::Failure(
                    ManifestError("The package inventory exclusions are invalid.", "inventoryExcludes"));
            }
            const auto& files = document.at("files");
            if (!files.is_array() || files.empty() || files.size() > MaximumInventoryFiles)
            {
                return HubResult<InstallerPackageManifest>::Failure(
                    ManifestError("The package file inventory is missing or too large.", "files"));
            }

            std::set<std::string, std::less<>> paths;
            std::uint64_t totalBytes = bytes.Value().size();
            manifest.Files.reserve(files.size() + 2U);
            for (const auto& value : files)
            {
                InstallOwnedFile file{.Path = PathFromUtf8(value.at("path").get<std::string>()),
                                      .SizeBytes = value.at("sizeBytes").get<std::uint64_t>(),
                                      .Sha256 = value.at("sha256").get<std::string>()};
                const auto key = NormalizedInstallPathKey(file.Path);
                if (!IsSafeRelativePath(file.Path) || file.SizeBytes > MaximumInstallFileBytes ||
                    !IsSha256(file.Sha256) || !paths.insert(key).second ||
                    totalBytes > std::numeric_limits<std::uint64_t>::max() - file.SizeBytes)
                {
                    return HubResult<InstallerPackageManifest>::Failure(
                        ManifestError("The package file inventory is unsafe.", PathToUtf8(file.Path)));
                }
                const auto source = sourceRoot / file.Path;
                std::error_code error;
                const auto status = std::filesystem::symlink_status(source, error);
                if (error || !std::filesystem::is_regular_file(status) || std::filesystem::is_symlink(status) ||
                    std::filesystem::file_size(source, error) != file.SizeBytes || error)
                {
                    return HubResult<InstallerPackageManifest>::Failure(
                        ManifestError("A package file is missing, unsafe, or has the wrong size.",
                                      PathToUtf8(file.Path), error.message()));
                }
                auto digest = HashInstallFile(source);
                if (!digest || digest.Value() != file.Sha256)
                {
                    return HubResult<InstallerPackageManifest>::Failure(
                        digest ? ManifestError("A package file digest does not match.", PathToUtf8(file.Path))
                               : digest.Error());
                }
                totalBytes += file.SizeBytes;
                manifest.Files.push_back(std::move(file));
            }
            if (document.at("installedSizeBytes").get<std::uint64_t>() != totalBytes)
            {
                return HubResult<InstallerPackageManifest>::Failure(
                    ManifestError("The package installed size does not match its inventory.", "installedSizeBytes"));
            }

            std::set<std::string, std::less<>> actualPaths;
            std::error_code error;
            for (std::filesystem::recursive_directory_iterator iterator(sourceRoot, error), end;
                 iterator != end && !error; iterator.increment(error))
            {
                if (!iterator->is_regular_file(error) || error)
                    continue;
                auto relative = std::filesystem::relative(iterator->path(), sourceRoot, error);
                if (error)
                    break;
                actualPaths.insert(NormalizedInstallPathKey(relative));
            }
            if (error)
            {
                return HubResult<InstallerPackageManifest>::Failure(ManifestError(
                    "The package staging tree could not be enumerated.", PathToUtf8(sourceRoot), error.message()));
            }
            paths.insert(NormalizedInstallPathKey(manifest.ManifestPath));
            const std::array installerGeneratedFiles{
                std::string("Uninstall.exe"), std::string(InstallReceiptFileName), std::string(InstallMarkerFileName),
                std::string(".keire-") + std::string(ToString(product)) + "-install"};
            for (const auto& generated : installerGeneratedFiles)
            {
                const auto key = NormalizedInstallPathKey(generated);
                if (actualPaths.contains(key))
                    paths.insert(key);
            }
            const bool missingOwnedFile = std::ranges::any_of(paths, [&actualPaths](const auto& path)
                                                               { return !actualPaths.contains(path); });
            if (missingOwnedFile || (!allowUnknownFiles && actualPaths != paths))
            {
                return HubResult<InstallerPackageManifest>::Failure(
                    ManifestError("The package staging tree contains an undeclared file.", PathToUtf8(sourceRoot)));
            }
            return HubResult<InstallerPackageManifest>::Success(std::move(manifest));
        }
        catch (const std::exception& error)
        {
            return HubResult<InstallerPackageManifest>::Failure(
                ManifestError("The installer package manifest is malformed.", manifestName, error.what()));
        }
    }
} // namespace KeireHub::Detail
