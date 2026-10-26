#include "KeireInternal/Assets/AssetDatabaseImplementation.h"

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <mutex>
#include <span>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace Keire
{
    Ref<AssetDatabase> Detail::AssetDatabaseWorkerAccess::CreateFromSourceIndex(
        AssetDatabaseSpecification specification, const std::filesystem::path& path, const bool startChangeMonitor)
    {
        auto database =
            CreateRef<AssetDatabase>(std::move(specification), AssetDatabase::Initialization::PublishedSourceIndex);
        (void)ReloadSourceIndex(*database, path);
        database->m_Impl->CompleteExternalImportRecovery(*database);
        if (startChangeMonitor)
            database->m_Impl->StartChangeMonitor();
        return database;
    }

    void Detail::AssetDatabaseWorkerAccess::PublishSourceIndex(const AssetDatabase& database,
                                                               const std::filesystem::path& path)
    {
        const auto records = database.Records();
        std::vector<AssetImportStatus> statuses;
        statuses.reserve(records.size());
        for (const auto& record : records)
        {
            auto status = database.ImportStatus(record.Id);
            if (status.State != AssetImportState::NotImported)
                statuses.push_back(std::move(status));
        }
        WriteAssetSourceIndex(path, records, statuses);
    }

    void Detail::AssetDatabaseWorkerAccess::ApplyImportStatuses(AssetDatabase& database,
                                                                const std::span<const AssetImportStatus> statuses)
    {
        std::scoped_lock lock(database.m_Impl->Mutex);
        auto updated = database.m_Impl->ImportStatuses;
        for (const auto& status : statuses)
        {
            if (!status.Id || status.State > AssetImportState::Failed || !database.m_Impl->IdIndex.contains(status.Id))
                throw std::invalid_argument("Asset worker import status does not match the current source index.");
            updated.insert_or_assign(status.Id, status);
        }
        database.m_Impl->ImportStatuses.swap(updated);
    }

    std::size_t Detail::AssetDatabaseWorkerAccess::ReloadSourceIndex(AssetDatabase& database,
                                                                     const std::filesystem::path& path)
    {
        std::vector<AssetImportStatus> persistedStatuses;
        auto records = ReadAssetSourceIndex(path, &persistedStatuses);
        std::unordered_set<AssetId> identities;
        std::unordered_set<std::string> relativePaths;
        std::unordered_map<AssetId, FileSignature> signatures;
        signatures.reserve(records.size());
        for (auto& record : records)
        {
            record.RelativePath = record.RelativePath.lexically_normal();
            const auto metadataRelative = Detail::PathWithSuffix(record.RelativePath, ".keiremeta");
            if (!record.Id || !record.Type || record.RelativePath.empty() || record.RelativePath.is_absolute() ||
                !identities.insert(record.Id).second ||
                !relativePaths.insert(record.RelativePath.generic_string()).second ||
                record.MetadataPath.lexically_normal() != database.m_Impl->SourceRoot / metadataRelative)
                throw std::runtime_error("Published asset source index contains an invalid record.");
            // Anchored signatures also reject escaped paths, redirected files, and non-regular files.
            const auto sourceSignature = database.m_Impl->SourceFiles->Signature(record.RelativePath);
            const auto metadataSignature = database.m_Impl->SourceFiles->Signature(metadataRelative);
            signatures.emplace(record.Id, FileSignature{sourceSignature.Modified, sourceSignature.Size,
                                                        metadataSignature.Modified, metadataSignature.Size});
        }
        std::unordered_set<AssetId> statusIdentities;
        for (const auto& status : persistedStatuses)
        {
            if (!identities.contains(status.Id) || !statusIdentities.insert(status.Id).second)
                throw std::runtime_error("Published asset source index contains an "
                                         "invalid import status identity.");
        }
        std::ranges::sort(records, [](const auto& left, const auto& right) { return left.Id < right.Id; });

        std::scoped_lock operation(*database.m_Impl->OperationMutex);
        std::scoped_lock lock(database.m_Impl->Mutex);
        auto updatedStatuses = database.m_Impl->ImportStatuses;
        for (auto& status : persistedStatuses)
            updatedStatuses.insert_or_assign(status.Id, std::move(status));
        std::erase_if(updatedStatuses, [&identities](const auto& entry) { return !identities.contains(entry.first); });
        database.m_Impl->ReplaceRecords(std::move(records));
        database.m_Impl->ImportStatuses.swap(updatedStatuses);
        database.m_Impl->Observed = std::move(signatures);
        database.m_Impl->PendingChanges.clear();
        database.m_Impl->SourceRevision.fetch_add(1, std::memory_order_release);
        database.m_Impl->RequestChangeMonitorDigestVerification();
        std::erase_if(database.m_Impl->ImportStatuses,
                      [&database](const auto& entry) { return !database.m_Impl->IdIndex.contains(entry.first); });
        return database.m_Impl->Records.size();
    }

    AssetImportResult Detail::AssetDatabaseWorkerAccess::ImportAssetsFromSourceIndex(
        AssetDatabase& database, const std::span<const AssetId> assets, const AssetImportPolicy policy,
        const std::stop_token cancellation, AssetOperationProgressCallback progress)
    {
        if (assets.empty())
            throw std::invalid_argument("Indexed targeted asset import requires at least one asset identity.");
        std::scoped_lock operation(*database.m_Impl->OperationMutex);
        return database.ImportAssetsUnlocked(assets, policy, cancellation, std::move(progress), false);
    }

    AssetImportResult Detail::AssetDatabaseWorkerAccess::ImportAssetsFromSourceIndexOrRescan(
        Ref<AssetDatabase>& database, AssetDatabaseSpecification fallbackSpecification,
        const std::span<const AssetId> assets, const AssetImportPolicy policy, bool& rescanned,
        const std::stop_token cancellation, AssetOperationProgressCallback progress)
    {
        rescanned = false;
        const auto records = database->Records();
        const bool missingTarget =
            std::ranges::any_of(assets,
                                [&](const AssetId asset)
                                {
                                    return std::ranges::none_of(records,
                                                                [asset](const AssetSourceRecord& record)
                                                                {
                                                                    return record.Id == asset ||
                                                                           std::ranges::find(record.SubAssets, asset) !=
                                                                               record.SubAssets.end();
                                                                });
                                });
        if (!missingTarget)
            return ImportAssetsFromSourceIndex(*database, assets, policy, cancellation, progress);
        database = CreateRef<AssetDatabase>(std::move(fallbackSpecification));
        rescanned = true;
        return database->ImportAssets(assets, policy, cancellation, std::move(progress));
    }
} // namespace Keire
