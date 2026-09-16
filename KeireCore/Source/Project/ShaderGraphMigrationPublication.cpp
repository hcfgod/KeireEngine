#include "Keire/Project/ShaderGraphMigration.h"

#include "Keire/Assets/AssetPipeline.h"
#include "Keire/Assets/RenderingAssets.h"
#include "KeireInternal/Assets/AssetInternal.h"
#include "KeireInternal/Assets/AssetWorkerProtocol.h"
#include "KeireInternal/FileSystem.h"
#include "KeireInternal/ProjectFileTransaction.h"

#include <algorithm>
#include <array>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <utility>

namespace Keire
{
    namespace
    {
        constexpr std::size_t MaximumFileBytes = 32U * 1024U * 1024U;
        constexpr std::size_t MaximumSnapshotBytes = 512U * 1024U * 1024U;
        const std::filesystem::path RuntimeDirectory = "Library/AssetCache/Runtime";
        using Snapshot = std::map<std::filesystem::path, Detail::Sha256Digest>;

        void CheckCancellation(const std::stop_token cancellation)
        {
            if (cancellation.stop_requested())
                throw std::runtime_error("Material migration cancelled before publication.");
        }

        [[nodiscard]] std::vector<std::filesystem::path> SnapshotPaths(const Detail::AnchoredFileSystem& fs)
        {
            std::vector<std::filesystem::path> paths;
            const std::array roots{std::filesystem::path("Assets"), std::filesystem::path("Packages"),
                                   std::filesystem::path("ProjectSettings"), RuntimeDirectory};
            for (const auto& root : roots)
            {
                if (!fs.Exists(root))
                    continue;
                for (const auto& entry : std::filesystem::recursive_directory_iterator(fs.Root() / root))
                {
                    if (entry.is_symlink())
                        throw std::runtime_error("Material migration snapshots cannot contain symbolic links.");
                    if (entry.is_directory())
                        continue;
                    const auto relative = entry.path().lexically_relative(fs.Root());
                    if (!fs.IsRegularFile(relative))
                        throw std::runtime_error("Material migration snapshots require regular files.");
                    paths.push_back(relative);
                    if (paths.size() > 4096)
                        throw std::runtime_error("Material migration snapshot exceeds 4096 files.");
                }
            }
            std::ranges::sort(paths);
            return paths;
        }

        // This owner only removes entries beneath its uniquely created, anchored preparation directory.
        struct Preparation
        {
            std::filesystem::path Root;
            Preparation()
                : Root(Detail::CanonicalExistingPath(std::filesystem::temp_directory_path()) /
                       ("KeireUpgrade-" + AssetId::Generate().ToString()))
            {
                // Validation needs no same-volume rename. Keep its scratch tree out of the already-deep project
                // path; the durable backups and publication journal remain inside the actual project.
                if (!std::filesystem::create_directory(Root))
                    throw std::runtime_error("Material migration preparation directory already exists.");
            }
            ~Preparation()
            {
                try
                {
                    const Detail::AnchoredFileSystem fs(Root, Detail::AnchoredRootPolicy::RejectLink);
                    std::vector<std::filesystem::path> entries;
                    for (const auto& entry : std::filesystem::recursive_directory_iterator(Root))
                        entries.push_back(entry.path().lexically_relative(Root));
                    std::ranges::sort(entries, std::greater<>{});
                    for (const auto& entry : entries)
                        fs.Remove(entry);
                    const Detail::AnchoredFileSystem parent(Root.parent_path());
                    parent.Remove(Root.filename());
                }
                catch (...)
                {
                    // Failed preparation cleanup remains isolated and must not hide validation/publication failures.
                }
            }
        };

        void ValidateMaterialIdentities(const Detail::AnchoredFileSystem& original,
                                        const Detail::CatalogData& converted)
        {
            const auto catalog = RuntimeDirectory / "catalog.json";
            if (!original.Exists(catalog))
                return;
            for (const auto& previous : Detail::LoadCatalog(original.Root() / catalog).Entries)
                if (previous.Type == MaterialAsset::StaticType())
                {
                    const auto found = std::ranges::find(converted.Entries, previous.Id, &Detail::CatalogEntry::Id);
                    if (found == converted.Entries.end() || found->Type != previous.Type)
                        throw std::runtime_error("Material migration changed an existing runtime material identity.");
                }
        }
    } // namespace

    ShaderGraphMigrationReport ApplyShaderGraphMigration(const std::filesystem::path& projectRoot,
                                                         const ShaderGraphMigrationReport& reviewed,
                                                         const AssetDatabaseSpecification& specification,
                                                         const std::stop_token cancellation)
    {
        CheckCancellation(cancellation);
        const Detail::AnchoredFileSystem original(projectRoot);
        original.CreateDirectories("Library/AssetOperations");
        Detail::InterprocessMutex operationMutex(original.Root() / "Library/AssetOperations/project.lock");
        std::scoped_lock operationLock(operationMutex);
        if (std::filesystem::absolute(specification.ProjectRoot).lexically_normal() != original.Root() ||
            specification.SourceDirectory != "Assets" || specification.CacheDirectory != "Library/AssetCache")
            throw std::invalid_argument("Validated material migration requires the matching standard project layout.");
        if (Detail::HasPendingMaterialMigration(original.Root()))
            throw std::runtime_error("Recover the interrupted material migration before preparing another upgrade.");
        const auto current = InspectShaderGraphMigration(original.Root());
        if (reviewed.ReviewFingerprint.empty() || reviewed.ReviewFingerprint != current.ReviewFingerprint ||
            !current.CanApply())
            throw std::runtime_error("Material migration review is stale or contains unresolved diagnostics.");
        if (current.PendingCount() == 0)
            return current;

        const Preparation preparation;
        const Detail::AnchoredFileSystem staged(preparation.Root, Detail::AnchoredRootPolicy::RejectLink);
        staged.CreateDirectories("Assets");
        Snapshot snapshot;
        std::size_t totalBytes = 0;
        for (const auto& path : SnapshotPaths(original))
        {
            CheckCancellation(cancellation);
            const auto bytes = original.Read(path, MaximumFileBytes);
            totalBytes += bytes.size();
            if (totalBytes > MaximumSnapshotBytes)
                throw std::runtime_error("Material migration snapshot exceeds 512 MiB.");
            snapshot.emplace(path, Detail::Sha256(bytes));
            staged.CreateDirectories(path.parent_path());
            staged.WriteFileAtomically(path, bytes, false);
        }
        auto report = ApplyShaderGraphMigration(preparation.Root, reviewed);
        auto stagedSpecification = specification;
        stagedSpecification.ProjectRoot = preparation.Root;
        std::vector<AssetId> materials;
        for (const auto& item : report.Items)
            if (item.Disposition == ShaderGraphMigrationDisposition::Migrate)
                materials.push_back(item.SourceAsset);
        {
            const auto database = CreateRef<AssetDatabase>(std::move(stagedSpecification));
            const auto imported = database->ImportAll(AssetImportPolicy::FailFast, cancellation);
            CheckCancellation(cancellation);
            AssetCooker::Validate(imported.CatalogPath);
            ValidateMaterialIdentities(original, Detail::LoadCatalog(imported.CatalogPath));
            auto records = database->Records();
            for (const auto material : materials)
                if (std::ranges::find(records, material, &AssetSourceRecord::Id) == records.end())
                    throw std::runtime_error("Material migration lost a source identity during validation.");
            for (auto& record : records)
                record.MetadataPath =
                    original.Root() / "Assets" / Detail::PathWithSuffix(record.RelativePath, ".keiremeta");
            Detail::WriteAssetSourceIndex(preparation.Root / RuntimeDirectory / "source-index.json", records);
        }
        CheckCancellation(cancellation);

        // Reject changes to any preparation input, including dependencies and the last-good catalog.
        const auto currentPaths = SnapshotPaths(original);
        if (currentPaths.size() != snapshot.size())
            throw std::runtime_error("Project files changed while material migration was validating.");
        for (const auto& path : currentPaths)
        {
            const auto found = snapshot.find(path);
            if (found == snapshot.end() || Detail::Sha256(original.Read(path, MaximumFileBytes)) != found->second)
                throw std::runtime_error("Project inputs changed while material migration was validating.");
        }
        std::vector<Detail::ProjectFileReplacement> files;
        for (const auto& path : SnapshotPaths(staged))
        {
            auto contents = staged.Read(path, MaximumFileBytes);
            const auto found = snapshot.find(path);
            if (found != snapshot.end() && Detail::Sha256(contents) == found->second)
                continue;
            std::optional<std::vector<std::byte>> previous;
            if (found != snapshot.end())
            {
                previous = original.Read(path, MaximumFileBytes);
                if (Detail::Sha256(*previous) != found->second)
                    throw std::runtime_error("Project inputs changed before material migration publication.");
            }
            files.push_back({path, std::move(previous), std::move(contents)});
        }
        // The catalog is the final visible runtime switch; recovery restores all preceding source/index changes.
        const auto catalog = RuntimeDirectory / "catalog.json";
        std::stable_partition(files.begin(), files.end(),
                              [&catalog](const auto& file) { return file.Destination != catalog; });
        CheckCancellation(cancellation);
        Detail::PublishMaterialMigrationFiles(original.Root(), files);
        return report;
    }
} // namespace Keire
