#include "KeireHubRuntime/LicenseCatalog.h"

#include <KeireHubRuntimeInternal/DistributionEncoding.h>
#include <KeireHubRuntimeInternal/LocalCatalogSupport.h>
#include <KeireHubRuntimeInternal/Persistence.h>

#include <algorithm>
#include <set>
#include <span>
#include <tuple>
#include <utility>

namespace KeireHub
{
    namespace
    {
        constexpr std::size_t MaximumLicenseCatalogBytes = std::size_t{4} * 1024 * 1024;
        constexpr std::size_t MaximumLicenseBytes = std::size_t{2} * 1024 * 1024;
        constexpr std::size_t MaximumLicenseTextBytes = std::size_t{32} * 1024 * 1024;
        constexpr std::size_t MaximumLicenses = 512;

        [[nodiscard]] HubError LicenseError(const HubErrorCode code, std::string message,
                                            const std::filesystem::path& affected, std::string details = {})
        {
            return {.Code = code,
                    .Message = std::move(message),
                    .AffectedItem = Detail::PathToUtf8(affected.filename()),
                    .TechnicalDetails = std::move(details)};
        }

        [[nodiscard]] std::string ScopeGroup(const HubLicenseEntry& entry)
        {
            std::string group;
            switch (entry.Scope)
            {
            case LicenseScope::Hub:
                group = "Hub";
                break;
            case LicenseScope::Editor:
                group = "Editor";
                break;
            case LicenseScope::BuildSupport:
                group = "Build Support";
                break;
            case LicenseScope::Template:
                group = "Template";
                break;
            case LicenseScope::Content:
                group = "Content";
                break;
            }
            if (entry.PackageId)
                group += " · " + *entry.PackageId;
            if (entry.Version)
                group += " " + *entry.Version;
            return group;
        }

        [[nodiscard]] std::string PathKey(const std::filesystem::path& path)
        {
            auto key = Detail::PathToUtf8(path.lexically_normal());
#if defined(_WIN32)
            std::ranges::transform(key, key.begin(), [](const unsigned char value)
                                   { return static_cast<char>(value >= 'A' && value <= 'Z' ? value + 32 : value); });
#endif
            return key;
        }

        [[nodiscard]] HubStatus
        AddResolvedLicense(ResolvedLicenseEntry entry, std::set<std::string, std::less<>>& paths,
                           std::set<std::string, std::less<>>& identities, std::size_t& totalTextBytes,
                           std::vector<ResolvedLicenseEntry>& destination, const bool deduplicatePath = true)
        {
            const auto pathKey = PathKey(entry.SourcePath);
            if ((deduplicatePath && paths.contains(pathKey)) || identities.contains(entry.Id))
                return HubStatus::Success();
            if (destination.size() >= MaximumLicenses || entry.Text.size() > MaximumLicenseBytes ||
                totalTextBytes > MaximumLicenseTextBytes - entry.Text.size())
            {
                return HubStatus::Failure({.Code = HubErrorCode::InvalidData,
                                           .Message = "The local license collection exceeds its allowed limit.",
                                           .AffectedItem = entry.Id});
            }
            totalTextBytes += entry.Text.size();
            paths.insert(pathKey);
            identities.insert(entry.Id);
            destination.push_back(std::move(entry));
            return HubStatus::Success();
        }

        [[nodiscard]] std::pair<LicenseScope, std::string_view> PackageLicenseGroup(const PackageKind kind) noexcept
        {
            switch (kind)
            {
            case PackageKind::HubInstaller:
                return {LicenseScope::Hub, "Hub"};
            case PackageKind::Editor:
                return {LicenseScope::Editor, "Editor"};
            case PackageKind::BuildSupport:
                return {LicenseScope::BuildSupport, "Build Support"};
            case PackageKind::Template:
                return {LicenseScope::Template, "Template"};
            case PackageKind::LearningContent:
                return {LicenseScope::Content, "Content"};
            case PackageKind::Toolchain:
                return {LicenseScope::BuildSupport, "Toolchain"};
            }
            return {LicenseScope::Hub, "Package"};
        }

        [[nodiscard]] HubError InstalledLicenseError(const HubErrorCode code, std::string message,
                                                     const std::string& installationId,
                                                     const std::filesystem::path& path = {})
        {
            return {.Code = code,
                    .Message = std::move(message),
                    .AffectedItem = installationId,
                    .TechnicalDetails = path.empty() ? std::string{} : Detail::PathToUtf8(path)};
        }

        [[nodiscard]] HubStatus AddCatalogSource(const LicenseCatalogSource& source,
                                                 std::set<std::string, std::less<>>& paths,
                                                 std::set<std::string, std::less<>>& identities,
                                                 std::size_t& totalTextBytes,
                                                 std::vector<ResolvedLicenseEntry>& destination)
        {
            auto document = Detail::ReadJsonFile(source.CatalogPath, MaximumLicenseCatalogBytes);
            if (!document)
                return HubStatus::Failure(document.Error());
            try
            {
                const auto schema = document.Value().at("schemaVersion").get<std::uint32_t>();
                if (schema != HubLicenseCatalog::CurrentSchemaVersion)
                {
                    return HubStatus::Failure(LicenseError(HubErrorCode::UnsupportedSchema,
                                                           "The local license catalog schema is unsupported.",
                                                           source.CatalogPath));
                }
                const auto& values = document.Value().at("licenses");
                if (!values.is_array() || values.size() > MaximumLicenses)
                    throw std::invalid_argument("Invalid license collection.");

                for (const auto& value : values)
                {
                    Detail::Json licenses = Detail::Json::array();
                    licenses.push_back(value);
                    Detail::Json single{{"schemaVersion", schema}, {"licenses", std::move(licenses)}};
                    auto parsed = ParseLicenseCatalog(single.dump());
                    if (!parsed)
                        continue;
                    auto metadata = std::move(parsed.Value().Licenses.front());
                    auto resolved = Detail::ResolveConfinedRegularFile(source.ContentRoot, metadata.SourcePath);
                    if (!resolved)
                        continue;
                    auto text = Detail::ReadBoundedCatalogText(*resolved, MaximumLicenseBytes);
                    if (!text || text->empty())
                        continue;
                    ResolvedLicenseEntry entry{.Id = metadata.Id,
                                               .DisplayName = metadata.DisplayName,
                                               .Group = ScopeGroup(metadata),
                                               .Scope = metadata.Scope,
                                               .PackageId = std::move(metadata.PackageId),
                                               .Version = std::move(metadata.Version),
                                               .SourcePath = std::move(*resolved),
                                               .Text = std::move(*text)};
                    if (auto status =
                            AddResolvedLicense(std::move(entry), paths, identities, totalTextBytes, destination);
                        !status)
                    {
                        return status;
                    }
                }
                return HubStatus::Success();
            }
            catch (const std::exception& error)
            {
                return HubStatus::Failure(LicenseError(HubErrorCode::InvalidData,
                                                       "The local license catalog is malformed.", source.CatalogPath,
                                                       error.what()));
            }
        }

        [[nodiscard]] HubStatus AddFallback(const std::filesystem::path& root, const std::filesystem::path& relative,
                                            std::string id, std::string name, std::string group,
                                            std::set<std::string, std::less<>>& paths,
                                            std::set<std::string, std::less<>>& identities, std::size_t& totalTextBytes,
                                            std::vector<ResolvedLicenseEntry>& destination, const bool required)
        {
            auto resolved = Detail::ResolveConfinedRegularFile(root, relative);
            if (!resolved)
            {
                return required ? HubStatus::Failure(LicenseError(
                                      HubErrorCode::IoRead, "A required Hub license file is unavailable.", relative))
                                : HubStatus::Success();
            }
            auto text = Detail::ReadBoundedCatalogText(*resolved, MaximumLicenseBytes);
            if (!text || text->empty())
            {
                return required ? HubStatus::Failure(LicenseError(HubErrorCode::InvalidData,
                                                                  "A required Hub license file is invalid.", relative))
                                : HubStatus::Success();
            }
            return AddResolvedLicense({.Id = std::move(id),
                                       .DisplayName = std::move(name),
                                       .Group = std::move(group),
                                       .Scope = LicenseScope::Hub,
                                       .SourcePath = std::move(*resolved),
                                       .Text = std::move(*text)},
                                      paths, identities, totalTextBytes, destination);
        }
    } // namespace

    LicenseCatalog::LicenseCatalog(std::filesystem::path hubRoot, std::vector<LicenseCatalogSource> sources)
        : m_HubRoot(std::move(hubRoot)), m_Sources(std::move(sources)),
          m_Snapshot(std::make_shared<const std::vector<ResolvedLicenseEntry>>())
    {
    }

    HubStatus LicenseCatalog::Load()
    {
        std::vector<ResolvedLicenseEntry> next;
        std::set<std::string, std::less<>> paths;
        std::set<std::string, std::less<>> identities;
        std::size_t totalTextBytes = 0;

        for (const auto& source : m_Sources)
        {
            if (auto status = AddCatalogSource(source, paths, identities, totalTextBytes, next); !status)
                return status;
        }
        if (auto status = AddFallback(m_HubRoot, "LICENSE.txt", "hub.keire-mit", "Kéire MIT License", "Hub", paths,
                                      identities, totalTextBytes, next, true);
            !status)
        {
            return status;
        }
        if (auto status =
                AddFallback(m_HubRoot, "THIRD_PARTY_NOTICES.md", "hub.third-party-notices", "Third-party notices",
                            "Hub dependencies", paths, identities, totalTextBytes, next, true);
            !status)
        {
            return status;
        }

        constexpr std::pair<std::string_view, std::string_view> fontLicenses[]{
            {"content/Fonts/Inter-OFL.txt", "Inter typography license"},
            {"content/Fonts/Material-Symbols-Apache-2.0.txt", "Material Symbols license"},
            {"KeireHubContent/Fonts/Inter-OFL.txt", "Inter typography license"},
            {"KeireHubContent/Fonts/Material-Symbols-Apache-2.0.txt", "Material Symbols license"}};
        std::size_t optionalIndex = 0;
        for (const auto& [path, name] : fontLicenses)
        {
            if (auto status =
                    AddFallback(m_HubRoot, path, "hub.font." + std::to_string(optionalIndex++), std::string(name),
                                "Hub dependencies", paths, identities, totalTextBytes, next, false);
                !status)
            {
                return status;
            }
        }

        const auto thirdParty = m_HubRoot / "third-party" / "licenses";
        std::error_code error;
        if (std::filesystem::is_directory(thirdParty, error) && !error)
        {
            std::vector<std::filesystem::path> files;
            for (std::filesystem::directory_iterator iterator(thirdParty, error), end;
                 !error && iterator != end && files.size() <= MaximumLicenses; iterator.increment(error))
            {
                const auto status = iterator->symlink_status(error);
                if (!error && status.type() == std::filesystem::file_type::regular)
                    files.push_back(iterator->path().filename());
            }
            if (error || files.size() > MaximumLicenses)
            {
                return HubStatus::Failure(LicenseError(HubErrorCode::IoRead,
                                                       "The packaged license directory could not be enumerated.",
                                                       thirdParty, error.message()));
            }
            std::ranges::sort(files, {}, [](const auto& path) { return Detail::PathToUtf8(path); });
            for (const auto& file : files)
            {
                const auto relative = std::filesystem::path("third-party") / "licenses" / file;
                if (auto status = AddFallback(m_HubRoot, relative, "hub.dependency." + std::to_string(optionalIndex++),
                                              file.stem().string(), "Hub dependencies", paths, identities,
                                              totalTextBytes, next, false);
                    !status)
                {
                    return status;
                }
            }
        }

        std::ranges::sort(next,
                          [](const auto& left, const auto& right)
                          {
                              return std::tie(left.Group, left.DisplayName, left.Id) <
                                     std::tie(right.Group, right.DisplayName, right.Id);
                          });
        m_Snapshot = std::make_shared<const std::vector<ResolvedLicenseEntry>>(std::move(next));
        return HubStatus::Success();
    }

    std::shared_ptr<const std::vector<ResolvedLicenseEntry>> LicenseCatalog::Snapshot() const noexcept
    {
        return m_Snapshot;
    }

    std::vector<std::size_t> LicenseCatalog::Search(const std::string_view query) const
    {
        std::vector<std::size_t> result;
        for (std::size_t index = 0; index < m_Snapshot->size(); ++index)
        {
            const auto& license = (*m_Snapshot)[index];
            if (Detail::ContainsCaseInsensitive(license.DisplayName, query) ||
                Detail::ContainsCaseInsensitive(license.Group, query) ||
                (license.PackageId && Detail::ContainsCaseInsensitive(*license.PackageId, query)) ||
                Detail::ContainsCaseInsensitive(license.Text, query))
            {
                result.push_back(index);
            }
        }
        return result;
    }

    const std::filesystem::path& LicenseCatalog::HubRoot() const noexcept { return m_HubRoot; }

    HubResult<std::vector<ResolvedLicenseEntry>>
    ResolveInstalledPackageLicenses(const std::span<const EditorInstallation> installations)
    {
        if (installations.size() > 128U)
        {
            return HubResult<std::vector<ResolvedLicenseEntry>>::Failure(
                {.Code = HubErrorCode::InvalidData,
                 .Message = "The installed editor license collection exceeds its allowed limit."});
        }

        std::vector<ResolvedLicenseEntry> result;
        std::set<std::string, std::less<>> paths;
        std::set<std::string, std::less<>> identities;
        std::size_t totalTextBytes = 0;
        for (const auto& installation : installations)
        {
            if (installation.Ownership != InstallationOwnership::Managed || installation.InstalledPackages.empty())
                continue;
            if (!installation.Root.is_absolute())
            {
                return HubResult<std::vector<ResolvedLicenseEntry>>::Failure(InstalledLicenseError(
                    HubErrorCode::UnsafeInstallRoot, "An installed editor license root is unsafe.", installation.Id));
            }
            auto receipt = ReadPackageInstallReceipt(installation.Root);
            if (!receipt || receipt.Value().DocumentSha256 != installation.PackageReceiptSha256 ||
                receipt.Value().AggregateIdentitySha256 != installation.PackageTreeIdentity)
            {
                return HubResult<std::vector<ResolvedLicenseEntry>>::Failure(InstalledLicenseError(
                    HubErrorCode::EditorInventoryInvalid,
                    "The installed package license receipt no longer matches this editor.", installation.Id));
            }
            for (const auto& package : receipt.Value().Packages)
            {
                const auto [scope, groupName] = PackageLicenseGroup(package.Kind);
                for (std::size_t index = 0; index < package.LicenseReferences.size(); ++index)
                {
                    const auto relative = Detail::PathFromUtf8(package.LicenseReferences[index]);
                    const auto declared = std::ranges::find(package.Files, relative, &PackageFile::Path);
                    if (declared == package.Files.end() || declared->SizeBytes > MaximumLicenseBytes)
                    {
                        return HubResult<std::vector<ResolvedLicenseEntry>>::Failure(InstalledLicenseError(
                            HubErrorCode::EditorInventoryInvalid,
                            "An installed package license is not present in its verified inventory.", installation.Id,
                            relative));
                    }
                    const auto resolved = Detail::ResolveConfinedRegularFile(installation.Root, relative);
                    if (!resolved)
                    {
                        return HubResult<std::vector<ResolvedLicenseEntry>>::Failure(InstalledLicenseError(
                            HubErrorCode::EditorInventoryInvalid,
                            "An installed package license file is missing or unsafe.", installation.Id, relative));
                    }
                    auto text = Detail::ReadBoundedCatalogText(*resolved, MaximumLicenseBytes);
                    if (!text || text->size() != declared->SizeBytes ||
                        Detail::Sha256Hex(std::as_bytes(std::span(*text))) != declared->Sha256)
                    {
                        return HubResult<std::vector<ResolvedLicenseEntry>>::Failure(
                            InstalledLicenseError(HubErrorCode::EditorInventoryInvalid,
                                                  "An installed package license file failed integrity verification.",
                                                  installation.Id, relative));
                    }

                    const auto version = package.Version.ToString();
                    ResolvedLicenseEntry entry{.Id = "installed-license:" + installation.Id + ':' + package.Id + ':' +
                                                     std::to_string(index),
                                               .DisplayName = package.Id + " - " + relative.filename().string(),
                                               .Group = std::string(groupName) + " - " + package.Id + ' ' + version,
                                               .Scope = scope,
                                               .PackageId = package.Id,
                                               .Version = version,
                                               .SourcePath = *resolved,
                                               .Text = std::move(*text)};
                    if (auto status =
                            AddResolvedLicense(std::move(entry), paths, identities, totalTextBytes, result, false);
                        !status)
                    {
                        return HubResult<std::vector<ResolvedLicenseEntry>>::Failure(status.Error());
                    }
                }
            }
        }
        std::ranges::sort(result,
                          [](const auto& left, const auto& right)
                          {
                              return std::tie(left.Group, left.DisplayName, left.Id) <
                                     std::tie(right.Group, right.DisplayName, right.Id);
                          });
        return HubResult<std::vector<ResolvedLicenseEntry>>::Success(std::move(result));
    }
} // namespace KeireHub
