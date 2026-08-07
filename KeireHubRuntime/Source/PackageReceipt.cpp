#include "KeireHubRuntime/PackageReceipt.h"

#include "DistributionEncoding.h"
#include "Persistence.h"

#include <algorithm>
#include <array>
#include <functional>
#include <limits>
#include <optional>
#include <ranges>
#include <set>
#include <span>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <utility>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace KeireHub
{
    namespace
    {
        constexpr std::size_t MaximumReceiptBytes = 8U * 1024U * 1024U;
        constexpr std::size_t MaximumReceiptDepth = 32U;
        constexpr std::size_t MaximumPackages = 256U;
        constexpr std::size_t MaximumPackageFiles = 32766U;
        constexpr std::uint64_t MaximumInstalledBytes = 64ULL * 1024ULL * 1024ULL * 1024ULL;

        [[nodiscard]] HubError ReceiptError(const HubErrorCode code, std::string message, std::string details = {})
        {
            return {.Code = code,
                    .Message = std::move(message),
                    .AffectedItem = PackageInstallReceiptFileName,
                    .TechnicalDetails = std::move(details)};
        }

        [[nodiscard]] std::string_view KindText(const PackageKind kind) noexcept
        {
            constexpr std::array names{"hubInstaller", "editor",          "buildSupport",
                                       "template",     "learningContent", "toolchain"};
            const auto index = static_cast<std::size_t>(kind);
            return index < names.size() ? names[index] : std::string_view{};
        }

        [[nodiscard]] std::optional<PackageKind> ParseKind(const std::string_view value) noexcept
        {
            constexpr std::array values{PackageKind::HubInstaller,    PackageKind::Editor,
                                        PackageKind::BuildSupport,    PackageKind::Template,
                                        PackageKind::LearningContent, PackageKind::Toolchain};
            const auto found = std::ranges::find(values, value, KindText);
            return found == values.end() ? std::nullopt : std::optional(*found);
        }

        [[nodiscard]] std::filesystem::path NativeIoPath(const std::filesystem::path& path)
        {
#if defined(_WIN32)
            auto value = std::filesystem::absolute(path).lexically_normal().native();
            if (value.starts_with(LR"(\\?\)"))
                return value;
            if (value.starts_with(LR"(\\)"))
                return LR"(\\?\UNC\)" + value.substr(2);
            return LR"(\\?\)" + value;
#else
            return path;
#endif
        }

        [[nodiscard]] bool HasUnsafeLinkAncestor(const std::filesystem::path& path)
        {
            auto current = path.lexically_normal();
            while (!current.empty())
            {
#if defined(_WIN32)
                const auto attributes = GetFileAttributesW(NativeIoPath(current).c_str());
                if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
                    return true;
                if (attributes == INVALID_FILE_ATTRIBUTES)
                {
                    const auto failure = GetLastError();
                    if (failure != ERROR_FILE_NOT_FOUND && failure != ERROR_PATH_NOT_FOUND)
                        return true;
                }
#else
                std::error_code error;
                const auto status = std::filesystem::symlink_status(current, error);
                if (!error && std::filesystem::is_symlink(status))
                    return true;
                if (error && error != std::errc::no_such_file_or_directory)
                    return true;
#endif
                if (current == current.root_path())
                    break;
                const auto parent = current.parent_path();
                if (parent == current)
                    break;
                current = parent;
            }
            return false;
        }

        [[nodiscard]] bool IsRegularFileWithoutLinks(const std::filesystem::path& path)
        {
            if (HasUnsafeLinkAncestor(path))
                return false;
            std::error_code error;
            const auto status = std::filesystem::symlink_status(NativeIoPath(path), error);
            return !error && status.type() == std::filesystem::file_type::regular;
        }

        [[nodiscard]] bool HasExactShape(const Detail::Json& value, const std::span<const std::string_view> keys)
        {
            return value.is_object() && value.size() == keys.size() &&
                   std::ranges::all_of(keys, [&](const auto key) { return value.contains(std::string(key)); });
        }

        [[nodiscard]] Detail::Json EncodeRecord(const InstalledPackageRecord& record)
        {
            Detail::Json dependencies = Detail::Json::array();
            for (const auto& dependency : record.Dependencies)
            {
                dependencies.push_back(
                    {{"packageId", dependency.PackageId}, {"versions", dependency.Versions.ToString()}});
            }
            Detail::Json files = Detail::Json::array();
            for (const auto& file : record.Files)
            {
                files.push_back({{"path", Detail::PathToUtf8(file.Path)},
                                 {"sizeBytes", file.SizeBytes},
                                 {"sha256", file.Sha256},
                                 {"mode", file.Mode}});
            }
            return {{"id", record.Id},
                    {"version", record.Version.ToString()},
                    {"kind", KindText(record.Kind)},
                    {"artifact", {{"sizeBytes", record.ArtifactSizeBytes}, {"sha256", record.ArtifactSha256}}},
                    {"installedSizeBytes", record.InstalledSizeBytes},
                    {"dependencies", std::move(dependencies)},
                    {"files", std::move(files)},
                    {"licenses", record.LicenseReferences}};
        }
    } // namespace

    HubStatus ValidatePackageInstallReceipt(const PackageInstallReceipt& receipt)
    {
        if (receipt.SchemaVersion != PackageInstallReceipt::CurrentSchemaVersion ||
            (!receipt.DocumentSha256.empty() && !Detail::IsSha256(receipt.DocumentSha256)) ||
            !Detail::IsSha256(receipt.AggregateIdentitySha256) || receipt.AggregateInstalledSizeBytes == 0 ||
            receipt.AggregateInstalledSizeBytes > MaximumInstalledBytes || receipt.Packages.empty() ||
            receipt.Packages.size() > MaximumPackages || receipt.Packages.front().Kind != PackageKind::Editor)
        {
            return HubStatus::Failure(
                ReceiptError(HubErrorCode::PackageManifestInvalid, "The installed-package receipt is invalid."));
        }
        std::set<std::string, std::less<>> packageIds;
        std::uint64_t total = 0;
        std::size_t editorCount = 0;
        std::size_t fileCount = 0;
        std::vector<PackageFile> aggregateFiles;
        for (const auto& package : receipt.Packages)
        {
            editorCount += package.Kind == PackageKind::Editor ? 1U : 0U;
            if (!Detail::IsBoundedIdentifier(package.Id) || !packageIds.insert(package.Id).second ||
                package.Kind < PackageKind::HubInstaller || package.Kind > PackageKind::Toolchain ||
                package.ArtifactSizeBytes == 0 || !Detail::IsSha256(package.ArtifactSha256) ||
                package.InstalledSizeBytes == 0 || package.InstalledSizeBytes > MaximumInstalledBytes ||
                total > MaximumInstalledBytes - package.InstalledSizeBytes || package.Dependencies.size() > 128U ||
                package.Files.empty() || package.Files.size() > MaximumPackageFiles ||
                fileCount > MaximumPackageFiles - package.Files.size() || package.LicenseReferences.size() > 128U)
            {
                return HubStatus::Failure(ReceiptError(HubErrorCode::PackageManifestInvalid,
                                                       "An installed-package receipt entry is invalid."));
            }
            total += package.InstalledSizeBytes;
            fileCount += package.Files.size();
            PackageManifest source{.Id = package.Id,
                                   .Version = package.Version,
                                   .Kind = package.Kind,
                                   .DisplayName = package.Id,
                                   .Channel = "receipt",
                                   .Platform = "any",
                                   .Architecture = "any",
                                   .Dependencies = package.Dependencies,
                                   .ArtifactSizeBytes = package.ArtifactSizeBytes,
                                   .ArtifactSha256 = package.ArtifactSha256,
                                   .InstalledSizeBytes = package.InstalledSizeBytes,
                                   .Files = package.Files,
                                   .LicenseReferences = package.LicenseReferences,
                                   .SignatureKeyId = "receipt-source"};
            if (const auto status = ValidatePackageManifest(source); !status)
                return status;
            aggregateFiles.insert(aggregateFiles.end(), package.Files.begin(), package.Files.end());
            std::set<std::string, std::less<>> dependencies;
            for (const auto& dependency : package.Dependencies)
            {
                if (!Detail::IsBoundedIdentifier(dependency.PackageId) || dependency.PackageId == package.Id ||
                    !dependencies.insert(dependency.PackageId).second)
                {
                    return HubStatus::Failure(ReceiptError(HubErrorCode::PackageManifestInvalid,
                                                           "An installed-package dependency is invalid."));
                }
            }
            for (const auto& license : package.LicenseReferences)
            {
                const auto path = Detail::PathFromUtf8(license);
                if (!Detail::IsSafeRelativePath(path) ||
                    std::ranges::none_of(package.Files, [&](const PackageFile& file) { return file.Path == path; }))
                {
                    return HubStatus::Failure(ReceiptError(HubErrorCode::PackageManifestInvalid,
                                                           "An installed-package license reference is not declared."));
                }
            }
        }
        if (editorCount != 1U || total != receipt.AggregateInstalledSizeBytes)
        {
            return HubStatus::Failure(
                ReceiptError(HubErrorCode::PackageManifestInvalid, "The installed-package aggregate is inconsistent."));
        }
        for (const auto& package : receipt.Packages)
        {
            for (const auto& dependency : package.Dependencies)
            {
                const auto found =
                    std::ranges::find(receipt.Packages, dependency.PackageId, &InstalledPackageRecord::Id);
                if (found == receipt.Packages.end() || !dependency.Versions.Matches(found->Version))
                {
                    return HubStatus::Failure(
                        ReceiptError(HubErrorCode::PackageMissingDependency,
                                     "The installed-package receipt is not a complete compatible dependency closure."));
                }
            }
        }
        std::vector<std::uint8_t> visit(receipt.Packages.size(), 0U);
        std::function<bool(std::size_t)> hasCycle = [&](const std::size_t index)
        {
            if (visit[index] == 1U)
                return true;
            if (visit[index] == 2U)
                return false;
            visit[index] = 1U;
            for (const auto& dependency : receipt.Packages[index].Dependencies)
            {
                const auto found =
                    std::ranges::find(receipt.Packages, dependency.PackageId, &InstalledPackageRecord::Id);
                if (hasCycle(static_cast<std::size_t>(found - receipt.Packages.begin())))
                    return true;
            }
            visit[index] = 2U;
            return false;
        };
        if (std::ranges::any_of(std::views::iota(std::size_t{0}, receipt.Packages.size()), hasCycle))
        {
            return HubStatus::Failure(ReceiptError(HubErrorCode::PackageDependencyCycle,
                                                   "The installed-package receipt contains a dependency cycle."));
        }
        auto aggregateVersion = SemanticVersion::Parse("1.0.0");
        if (!aggregateVersion)
            return HubStatus::Failure(aggregateVersion.Error());
        PackageManifest aggregate{.Id = "keire.install-receipt",
                                  .Version = std::move(aggregateVersion).Value(),
                                  .Kind = PackageKind::Toolchain,
                                  .DisplayName = "Installed package inventory",
                                  .Channel = "receipt",
                                  .Platform = "any",
                                  .Architecture = "any",
                                  .ArtifactSizeBytes = total,
                                  .ArtifactSha256 = receipt.AggregateIdentitySha256,
                                  .InstalledSizeBytes = total,
                                  .Files = std::move(aggregateFiles),
                                  .SignatureKeyId = "receipt-aggregate"};
        if (const auto status = ValidatePackageManifest(aggregate); !status)
            return status;
        return HubStatus::Success();
    }

    HubResult<std::string> EncodePackageInstallReceipt(const PackageInstallReceipt& receipt)
    {
        if (const auto status = ValidatePackageInstallReceipt(receipt); !status)
            return HubResult<std::string>::Failure(status.Error());
        Detail::Json packages = Detail::Json::array();
        for (const auto& package : receipt.Packages)
            packages.push_back(EncodeRecord(package));
        const auto result = Detail::Json{{"schemaVersion", receipt.SchemaVersion},
                                         {"aggregateIdentitySha256", receipt.AggregateIdentitySha256},
                                         {"aggregateInstalledSizeBytes", receipt.AggregateInstalledSizeBytes},
                                         {"packages", std::move(packages)}}
                                .dump(2) +
                            '\n';
        if (result.size() > MaximumReceiptBytes)
            return HubResult<std::string>::Failure(
                ReceiptError(HubErrorCode::PackageManifestInvalid, "The installed-package receipt is too large."));
        return HubResult<std::string>::Success(result);
    }

    HubResult<PackageInstallReceipt> ReadPackageInstallReceipt(const std::filesystem::path& installationRoot)
    {
        const auto receiptPath = installationRoot.lexically_normal() / PackageInstallReceiptFileName;
        if (!installationRoot.is_absolute() || !IsRegularFileWithoutLinks(receiptPath))
        {
            return HubResult<PackageInstallReceipt>::Failure(
                ReceiptError(HubErrorCode::NotFound, "The installed-package receipt is missing or unsafe."));
        }
        auto text = Detail::ReadTextFile(receiptPath, MaximumReceiptBytes);
        if (!text)
            return HubResult<PackageInstallReceipt>::Failure(text.Error());
        auto parsed =
            Detail::ParseStrictJson(text.Value(), MaximumReceiptDepth, HubErrorCode::InvalidData,
                                    "The installed-package receipt is malformed.", PackageInstallReceiptFileName);
        if (!parsed)
            return HubResult<PackageInstallReceipt>::Failure(parsed.Error());
        try
        {
            constexpr std::array rootKeys{
                std::string_view{"schemaVersion"}, std::string_view{"aggregateIdentitySha256"},
                std::string_view{"aggregateInstalledSizeBytes"}, std::string_view{"packages"}};
            constexpr std::array packageKeys{std::string_view{"id"},
                                             std::string_view{"version"},
                                             std::string_view{"kind"},
                                             std::string_view{"artifact"},
                                             std::string_view{"installedSizeBytes"},
                                             std::string_view{"dependencies"},
                                             std::string_view{"files"},
                                             std::string_view{"licenses"}};
            constexpr std::array artifactKeys{std::string_view{"sizeBytes"}, std::string_view{"sha256"}};
            constexpr std::array dependencyKeys{std::string_view{"packageId"}, std::string_view{"versions"}};
            constexpr std::array fileKeys{std::string_view{"path"}, std::string_view{"sizeBytes"},
                                          std::string_view{"sha256"}, std::string_view{"mode"}};
            if (!HasExactShape(parsed.Value(), rootKeys) || !parsed.Value().at("packages").is_array() ||
                parsed.Value().at("packages").size() > MaximumPackages)
            {
                throw std::invalid_argument("The receipt root has an unexpected shape.");
            }
            PackageInstallReceipt result{
                .SchemaVersion = parsed.Value().at("schemaVersion").get<std::uint32_t>(),
                .DocumentSha256 = Detail::Sha256Hex(std::as_bytes(std::span(text.Value()))),
                .AggregateIdentitySha256 = parsed.Value().at("aggregateIdentitySha256").get<std::string>(),
                .AggregateInstalledSizeBytes = parsed.Value().at("aggregateInstalledSizeBytes").get<std::uint64_t>()};
            for (const auto& value : parsed.Value().at("packages"))
            {
                if (!HasExactShape(value, packageKeys) || !HasExactShape(value.at("artifact"), artifactKeys) ||
                    !value.at("dependencies").is_array() || !value.at("files").is_array() ||
                    !value.at("licenses").is_array())
                {
                    throw std::invalid_argument("A receipt package has an unexpected shape.");
                }
                auto version = SemanticVersion::Parse(value.at("version").get<std::string>());
                const auto kind = ParseKind(value.at("kind").get<std::string>());
                if (!version || !kind)
                    throw std::invalid_argument("A receipt package identity is invalid.");
                InstalledPackageRecord record{
                    .Id = value.at("id").get<std::string>(),
                    .Version = std::move(version).Value(),
                    .Kind = *kind,
                    .ArtifactSizeBytes = value.at("artifact").at("sizeBytes").get<std::uint64_t>(),
                    .ArtifactSha256 = value.at("artifact").at("sha256").get<std::string>(),
                    .InstalledSizeBytes = value.at("installedSizeBytes").get<std::uint64_t>(),
                    .LicenseReferences = value.at("licenses").get<std::vector<std::string>>()};
                for (const auto& dependency : value.at("dependencies"))
                {
                    if (!HasExactShape(dependency, dependencyKeys))
                        throw std::invalid_argument("A receipt dependency has an unexpected shape.");
                    auto versions = VersionConstraint::Parse(dependency.at("versions").get<std::string>());
                    if (!versions)
                        throw std::invalid_argument("A receipt dependency version is invalid.");
                    record.Dependencies.push_back({.PackageId = dependency.at("packageId").get<std::string>(),
                                                   .Versions = std::move(versions).Value()});
                }
                for (const auto& file : value.at("files"))
                {
                    if (!HasExactShape(file, fileKeys))
                        throw std::invalid_argument("A receipt file has an unexpected shape.");
                    record.Files.push_back({.Path = Detail::PathFromUtf8(file.at("path").get<std::string>()),
                                            .SizeBytes = file.at("sizeBytes").get<std::uint64_t>(),
                                            .Sha256 = file.at("sha256").get<std::string>(),
                                            .Mode = file.at("mode").get<std::uint32_t>()});
                }
                result.Packages.push_back(std::move(record));
            }
            if (const auto status = ValidatePackageInstallReceipt(result); !status)
                return HubResult<PackageInstallReceipt>::Failure(status.Error());
            return HubResult<PackageInstallReceipt>::Success(std::move(result));
        }
        catch (const std::exception& error)
        {
            return HubResult<PackageInstallReceipt>::Failure(
                ReceiptError(HubErrorCode::InvalidData, "The installed-package receipt is invalid.", error.what()));
        }
    }
} // namespace KeireHub
