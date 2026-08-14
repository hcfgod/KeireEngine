#include "KeireClient/Editor/AssetPackageAuthoring.h"

#include "KeireClient/Editor/AssetBrowserUtilities.h"

#include "KeireInternal/FileSystem.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <stdexcept>
#include <utility>

namespace KeireEditor
{
    namespace
    {
        class StagingDirectory final
        {
          public:
            explicit StagingDirectory(std::filesystem::path path) : m_Path(std::move(path)) {}

            ~StagingDirectory()
            {
                std::error_code ignored;
                std::filesystem::remove_all(m_Path, ignored);
            }

            StagingDirectory(const StagingDirectory&) = delete;
            StagingDirectory& operator=(const StagingDirectory&) = delete;

            [[nodiscard]] const std::filesystem::path& Path() const noexcept { return m_Path; }

          private:
            std::filesystem::path m_Path;
        };

        [[nodiscard]] std::filesystem::path CanonicalFileInside(const std::filesystem::path& root,
                                                                const std::filesystem::path& candidate)
        {
            const auto path = Keire::Detail::CanonicalExistingPath(candidate);
            if (!SameOrChild(root, path) || !std::filesystem::is_regular_file(path) ||
                std::filesystem::is_symlink(std::filesystem::symlink_status(candidate)))
                throw std::invalid_argument("Asset-package source files must be regular files inside Assets.");
            return path;
        }

        void CopyPayloadFile(const std::filesystem::path& source, const std::filesystem::path& destination)
        {
            std::filesystem::create_directories(destination.parent_path());
            if (!std::filesystem::copy_file(source, destination, std::filesystem::copy_options::none))
                throw std::runtime_error("Asset-package payload staging did not copy a source file.");
        }
    } // namespace

    std::string SuggestedAssetPackageIdentifier(const std::string_view displayName)
    {
        std::string slug;
        bool separator = false;
        for (const char value : displayName)
        {
            const auto character = static_cast<unsigned char>(value);
            if (std::isalnum(character))
            {
                if (separator && !slug.empty())
                    slug.push_back('-');
                slug.push_back(static_cast<char>(std::tolower(character)));
                separator = false;
            }
            else
            {
                separator = true;
            }
            if (slug.size() >= 80)
                break;
        }
        if (slug.empty())
            slug = "package";
        return "com.keire.assets." + slug;
    }

    std::vector<Keire::AssetSourceRecord>
    ResolveAssetPackageRecords(const std::span<const Keire::AssetSourceRecord> records,
                               const AssetPackageSelection& selection)
    {
        if (selection.Assets.empty() == !selection.Folder.has_value())
            throw std::invalid_argument("Choose assets or one folder for the asset package.");

        std::map<Keire::AssetId, const Keire::AssetSourceRecord*> byId;
        for (const auto& record : records)
        {
            if (!record.Id || !byId.emplace(record.Id, &record).second)
                throw std::invalid_argument("The asset source inventory contains duplicate or empty identities.");
        }

        std::set<Keire::AssetId> included;
        if (selection.Folder)
        {
            const auto folder = selection.Folder->lexically_normal();
            if (folder.empty() || folder.is_absolute() || *folder.begin() == "..")
                throw std::invalid_argument("The asset-package folder must be relative to Assets.");
            for (const auto& record : records)
                if (SameOrChild(folder, record.RelativePath))
                    included.emplace(record.Id);
        }
        else
        {
            for (const auto asset : selection.Assets)
            {
                if (!byId.contains(asset))
                    throw std::invalid_argument("A selected asset is no longer present in the project database.");
                included.emplace(asset);
            }
        }
        if (included.empty())
            throw std::invalid_argument("The asset-package selection is empty.");

        std::vector<Keire::AssetId> pending(included.begin(), included.end());
        for (std::size_t index = 0; index < pending.size(); ++index)
        {
            const auto record = byId.at(pending[index]);
            for (const auto dependency : record->Dependencies)
            {
                if (!byId.contains(dependency))
                    throw std::invalid_argument("A selected asset dependency is not present in the project database.");
                if (included.emplace(dependency).second)
                    pending.push_back(dependency);
            }
        }

        std::vector<Keire::AssetSourceRecord> result;
        result.reserve(included.size());
        for (const auto asset : included)
            result.push_back(*byId.at(asset));
        std::ranges::sort(result, {}, [](const auto& record) { return record.RelativePath.generic_string(); });
        return result;
    }

    Keire::AssetPackageArchiveMetadata CreateAssetPackageArchive(const AssetPackageAuthoringRequest& request)
    {
        if (request.ProjectRoot.empty() || request.SourceDirectory.empty() || request.SourceDirectory.is_absolute() ||
            request.Output.empty() || request.Output.extension() != ".keireassetpackage")
            throw std::invalid_argument("Asset-package authoring paths are invalid.");

        const auto projectRoot = Keire::Detail::CanonicalExistingPath(request.ProjectRoot);
        const auto sourceDirectory = request.SourceDirectory.lexically_normal();
        if (sourceDirectory.empty() || sourceDirectory == "." || *sourceDirectory.begin() == "..")
            throw std::invalid_argument("The project Assets root is invalid.");
        const auto sourceRoot = Keire::Detail::CanonicalExistingPath(projectRoot / sourceDirectory);
        if (!SameOrChild(projectRoot, sourceRoot) || !std::filesystem::is_directory(sourceRoot))
            throw std::invalid_argument("The project Assets root is invalid.");

        auto stagingParent = request.StagingParent.empty() ? projectRoot / "Library/AssetPackageExports"
                                                           : std::filesystem::absolute(request.StagingParent);
        std::filesystem::create_directories(stagingParent);
        stagingParent = Keire::Detail::CanonicalExistingPath(stagingParent);
        if (!SameOrChild(projectRoot, stagingParent))
            throw std::invalid_argument("Asset-package staging must remain inside the project.");

        const auto stagingPath = stagingParent / Keire::AssetId::Generate().ToString();
        if (!std::filesystem::create_directory(stagingPath))
            throw std::runtime_error("Asset-package staging could not be created.");
        const StagingDirectory staging(stagingPath);

        auto selected = ResolveAssetPackageRecords(request.Records, request.Selection);
        Keire::AssetPackageManifest manifest;
        manifest.PackageId = request.Draft.PackageId;
        manifest.Version = request.Draft.Version;
        manifest.PublisherId = request.Draft.PublisherId;
        manifest.DisplayName = request.Draft.DisplayName;
        manifest.Summary = request.Draft.Summary;
        manifest.InstallKind = Keire::AssetPackageInstallKind::AssetImport;
        manifest.Compatibility.MinimumEngineVersion = request.Draft.MinimumEngineVersion;

        for (const auto& record : selected)
        {
            if (record.RelativePath.empty() || record.RelativePath.is_absolute() ||
                *record.RelativePath.lexically_normal().begin() == "..")
                throw std::invalid_argument("An asset-package source path is invalid.");
            const auto source = CanonicalFileInside(sourceRoot, sourceRoot / record.RelativePath);
            const auto expectedMetadata = Keire::Detail::PathWithSuffix(source, ".keiremeta");
            const auto metadata = CanonicalFileInside(sourceRoot, record.MetadataPath);
            if (metadata != Keire::Detail::CanonicalExistingPath(expectedMetadata))
                throw std::invalid_argument("An asset-package metadata path does not match its source asset.");

            const auto packagedSource = sourceDirectory / record.RelativePath;
            const auto packagedMetadata = sourceDirectory / metadata.lexically_relative(sourceRoot);
            CopyPayloadFile(source, staging.Path() / packagedSource);
            CopyPayloadFile(metadata, staging.Path() / packagedMetadata);
            auto dependencies = record.Dependencies;
            std::ranges::sort(dependencies);
            dependencies.erase(std::unique(dependencies.begin(), dependencies.end()), dependencies.end());
            manifest.Assets.push_back({.Id = record.Id,
                                       .Type = record.Type,
                                       .SourcePath = packagedSource,
                                       .MetadataPath = packagedMetadata,
                                       .Dependencies = std::move(dependencies)});
        }
        std::ranges::sort(manifest.Assets, {}, [](const auto& asset) { return asset.SourcePath.generic_string(); });
        manifest = Keire::InventoryAssetPackagePayload(std::move(manifest), staging.Path());
        return Keire::WriteAssetPackageArchive(
            {.Manifest = std::move(manifest), .PayloadRoot = staging.Path(), .Output = request.Output});
    }
} // namespace KeireEditor
