#include "KeireInternal/Assets/AssetDatabaseImplementation.h"

#include <algorithm>
#include <memory>

namespace Keire
{
    AssetOperationCancelled::AssetOperationCancelled() : std::runtime_error("Asset operation was cancelled.") {}

    ExternalAssetImportReceiptId ExternalAssetImportReceiptId::Parse(const std::string_view value)
    {
        return ExternalAssetImportReceiptId(AssetId::Parse(value));
    }

    std::string ExternalAssetImportReceiptId::ToString() const { return m_Value.ToString(); }

    AssetTrashId AssetTrashId::Parse(const std::string_view value) { return AssetTrashId(AssetId::Parse(value)); }

    std::string AssetTrashId::ToString() const { return m_Value.ToString(); }

    AssetDatabase::AssetDatabase(AssetDatabaseSpecification specification)
        : AssetDatabase(std::move(specification), Initialization::Full)
    {
    }

    AssetDatabase::AssetDatabase(AssetDatabaseSpecification specification, const Initialization initialization)
        : m_Impl(std::make_unique<Impl>(std::move(specification)))
    {
        if (initialization == Initialization::Full)
        {
            (void)Refresh();
            m_Impl->CompleteExternalImportRecovery(*this);
            m_Impl->StartChangeMonitor();
        }
    }

    AssetDatabase::~AssetDatabase() = default;

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
        WriteAssetSourceIndex(path, database.Records());
    }

    std::size_t Detail::AssetDatabaseWorkerAccess::ReloadSourceIndex(AssetDatabase& database,
                                                                     const std::filesystem::path& path)
    {
        auto records = ReadAssetSourceIndex(path);
        std::unordered_set<AssetId> identities;
        std::unordered_set<std::string> relativePaths;
        std::unordered_map<AssetId, FileSignature> signatures;
        signatures.reserve(records.size());
        for (auto& record : records)
        {
            record.RelativePath = record.RelativePath.lexically_normal();
            const auto source = ConfinedPath(database.m_Impl->SourceRoot, record.RelativePath);
            const auto expectedMetadata = ConfinedMetadataPath(database.m_Impl->SourceRoot, record.RelativePath);
            if (!record.Id || !record.Type || record.RelativePath.empty() || record.RelativePath.is_absolute() ||
                !identities.insert(record.Id).second ||
                !relativePaths.insert(record.RelativePath.generic_string()).second ||
                record.MetadataPath.lexically_normal() != expectedMetadata ||
                !std::filesystem::is_regular_file(source) || !std::filesystem::is_regular_file(expectedMetadata))
                throw std::runtime_error("Published asset source index contains an invalid record.");
            signatures.emplace(record.Id, database.m_Impl->ReadSignature(source, expectedMetadata));
        }
        std::ranges::sort(records, [](const auto& left, const auto& right) { return left.Id < right.Id; });

        std::scoped_lock operation(*database.m_Impl->OperationMutex);
        std::scoped_lock lock(database.m_Impl->Mutex);
        database.m_Impl->ReplaceRecords(std::move(records));
        database.m_Impl->Observed = std::move(signatures);
        database.m_Impl->PendingChanges.clear();
        database.m_Impl->SourceRevision.fetch_add(1, std::memory_order_release);
        database.m_Impl->RequestChangeMonitorDigestVerification();
        std::erase_if(database.m_Impl->ImportStatuses,
                      [&database](const auto& entry)
                      {
                          return std::ranges::find(database.m_Impl->Records, entry.first, &AssetSourceRecord::Id) ==
                                 database.m_Impl->Records.end();
                      });
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

    std::size_t AssetDatabase::Refresh()
    {
        std::scoped_lock operation(*m_Impl->OperationMutex);
        return RefreshUnlocked();
    }

    std::size_t AssetDatabase::RefreshUnlocked()
    {
        auto scanned = m_Impl->Scan();
        std::scoped_lock lock(m_Impl->Mutex);
        for (auto& record : scanned.Records)
        {
            const auto previous = m_Impl->IdIndex.find(record.Id);
            if (previous == m_Impl->IdIndex.end())
                continue;
            const auto& imported = m_Impl->Records[previous->second];
            if (record.Importer != imported.Importer || record.ImporterVersion != imported.ImporterVersion)
                continue;
            record.Dependencies = imported.Dependencies;
            record.SourceDependencies = imported.SourceDependencies;
            record.Metadata = imported.Metadata;
        }
        m_Impl->ReplaceRecords(std::move(scanned.Records));
        m_Impl->Observed = std::move(scanned.Signatures);
        m_Impl->PendingChanges.clear();
        m_Impl->SourceRevision.fetch_add(1, std::memory_order_release);
        m_Impl->RequestChangeMonitorScan();
        std::erase_if(m_Impl->ImportStatuses,
                      [this](const auto& entry)
                      {
                          return std::ranges::find(m_Impl->Records, entry.first, &AssetSourceRecord::Id) ==
                                 m_Impl->Records.end();
                      });
        return m_Impl->Records.size();
    }

    void AssetDatabase::RefreshAssetsUnlocked(const std::span<const AssetId> assets)
    {
        const auto records = Records();
        std::vector<AssetSourceRecord> owners;
        owners.reserve(assets.size());
        for (const auto asset : assets)
        {
            const auto owner = std::ranges::find_if(
                records, [asset](const AssetSourceRecord& record)
                { return record.Id == asset || std::ranges::find(record.SubAssets, asset) != record.SubAssets.end(); });
            if (owner == records.end())
                throw std::invalid_argument("Targeted asset import identity is not in the source database: " +
                                            asset.ToString());
            if (std::ranges::find(owners, owner->Id, &AssetSourceRecord::Id) == owners.end())
                owners.push_back(*owner);
        }

        for (const auto& previous : owners)
        {
            const auto source = ConfinedPath(m_Impl->SourceRoot, previous.RelativePath);
            auto refreshed =
                ReadMetadata(*m_Impl->SourceFiles, previous.RelativePath, m_Impl->Specification.MaximumSourceBytes,
                             true, m_Impl->InferImporter(source));
            if (refreshed.Id != previous.Id)
                throw std::runtime_error("Targeted source refresh changed the stable asset identity.");
            if (refreshed.Importer == previous.Importer && refreshed.ImporterVersion == previous.ImporterVersion)
            {
                refreshed.Dependencies = previous.Dependencies;
                refreshed.SourceDependencies = previous.SourceDependencies;
                refreshed.Metadata = previous.Metadata;
            }
            m_Impl->PublishRecord(
                std::move(refreshed),
                m_Impl->ReadSignature(source, ConfinedMetadataPath(m_Impl->SourceRoot, previous.RelativePath)));
        }
    }

    std::vector<AssetSourceRecord> AssetDatabase::Records() const
    {
        // Queries return value snapshots and must remain available while a background import stages its next
        // publication. The records mutex protects each snapshot without making editor frames wait for the
        // operation-wide transaction lock.
        std::scoped_lock lock(m_Impl->Mutex);
        return m_Impl->Records;
    }

    std::optional<AssetSourceRecord> AssetDatabase::Find(const AssetId id) const
    {
        std::scoped_lock lock(m_Impl->Mutex);
        const auto found = m_Impl->IdIndex.find(id);
        return found == m_Impl->IdIndex.end() ? std::nullopt
                                              : std::optional<AssetSourceRecord>(m_Impl->Records[found->second]);
    }

    std::optional<AssetSourceRecord> AssetDatabase::Find(const std::filesystem::path& relativePath) const
    {
        const auto normalized = m_Impl->CanonicalPathKey(relativePath);
        std::scoped_lock lock(m_Impl->Mutex);
        const auto found = m_Impl->PathIndex.find(normalized);
        return found == m_Impl->PathIndex.end() ? std::nullopt
                                                : std::optional<AssetSourceRecord>(m_Impl->Records[found->second]);
    }

    std::vector<AssetId> AssetDatabase::PollChangedAssets()
    {
        m_Impl->RequestChangeMonitorScan();
        auto monitored = m_Impl->TakeChangeMonitorScan();
        const auto now = std::chrono::steady_clock::now();
        std::vector<AssetId> ready;
        {
            std::scoped_lock lock(m_Impl->Mutex);
            if (monitored && monitored->SourceRevision == m_Impl->SourceRevision.load(std::memory_order_acquire))
            {
                auto& scanned = monitored->Result;
                for (const auto& [id, signature] : scanned.Signatures)
                {
                    const auto previous = m_Impl->Observed.find(id);
                    if (previous == m_Impl->Observed.end() || previous->second != signature)
                        m_Impl->PendingChanges.try_emplace(id, now);
                }
                for (const auto& [id, signature] : m_Impl->Observed)
                    if (!scanned.Signatures.contains(id))
                        m_Impl->PendingChanges.try_emplace(id, now);
                for (auto& record : scanned.Records)
                {
                    const auto previous = std::ranges::find(m_Impl->Records, record.Id, &AssetSourceRecord::Id);
                    if (previous != m_Impl->Records.end())
                    {
                        if (monitored->DigestsVerified)
                        {
                            if (record.SourceDigest != previous->SourceDigest ||
                                record.MetadataDigest != previous->MetadataDigest)
                            {
                                m_Impl->PendingChanges.try_emplace(record.Id, now);
                            }
                        }
                        else
                        {
                            record.SourceDigest = previous->SourceDigest;
                            record.MetadataDigest = previous->MetadataDigest;
                        }
                    }
                }
                m_Impl->Observed = std::move(scanned.Signatures);
                m_Impl->ReplaceRecords(std::move(scanned.Records));
            }
            for (auto iterator = m_Impl->PendingChanges.begin(); iterator != m_Impl->PendingChanges.end();)
            {
                if (now - iterator->second >= m_Impl->Specification.ChangeDebounce)
                {
                    ready.push_back(iterator->first);
                    iterator = m_Impl->PendingChanges.erase(iterator);
                }
                else
                    ++iterator;
            }
        }

        std::ranges::sort(ready);
        return ready;
    }

    AssetChangeMonitorStatistics AssetDatabase::ChangeMonitorStatistics() const noexcept
    {
        AssetChangeMonitorStatistics result;
        result.PublishedScans = m_Impl->PublishedScans.load(std::memory_order_relaxed);
        result.FailedScans = m_Impl->FailedScans.load(std::memory_order_relaxed);
        {
            std::scoped_lock lock(m_Impl->ChangeMonitorMutex);
            result.ScanPending = m_Impl->ChangeMonitorRequested || m_Impl->PublishedChangeScan.has_value();
        }
        return result;
    }

    AssetImportResult AssetDatabase::ImportAll() { return ImportAll(AssetImportPolicy::FailFast, {}, {}); }

    AssetImportResult AssetDatabase::ImportAll(const AssetImportPolicy policy) { return ImportAll(policy, {}, {}); }

    AssetImportResult AssetDatabase::ImportAll(const AssetImportPolicy policy, const std::stop_token cancellation,
                                               AssetOperationProgressCallback progress)
    {
        std::scoped_lock operation(*m_Impl->OperationMutex);
        return ImportAllUnlocked(policy, cancellation, std::move(progress));
    }

    AssetImportResult AssetDatabase::ImportAllUnlocked(const AssetImportPolicy policy,
                                                       const std::stop_token cancellation,
                                                       AssetOperationProgressCallback progress)
    {
        return ImportAssetsUnlocked({}, policy, cancellation, std::move(progress), true);
    }

    AssetImportResult AssetDatabase::ImportAssets(const std::span<const AssetId> assets, const AssetImportPolicy policy,
                                                  const std::stop_token cancellation,
                                                  AssetOperationProgressCallback progress)
    {
        if (assets.empty())
            throw std::invalid_argument("Targeted asset import requires at least one asset identity.");
        std::scoped_lock operation(*m_Impl->OperationMutex);
        return ImportAssetsUnlocked(assets, policy, cancellation, std::move(progress), true);
    }

    AssetImportResult AssetDatabase::ImportAssetsUnlocked(const std::span<const AssetId> assets,
                                                          const AssetImportPolicy policy,
                                                          const std::stop_token cancellation,
                                                          AssetOperationProgressCallback progress,
                                                          const bool refreshSources)
    {
        ThrowIfOperationCancelled(cancellation);
        ReportOperationProgress(progress, AssetOperationPhase::Scanning, 0, 0);
        if (refreshSources)
            (void)RefreshUnlocked();
        else
            RefreshAssetsUnlocked(assets);
        const auto allRecords = Records();
        auto records = allRecords;
        std::vector<AssetId> sourceAssets;
        std::vector<AssetId> replacedAssets;
        if (!assets.empty())
        {
            const auto previousCatalog = m_Impl->CacheRoot / "Runtime" / "catalog.json";
            if (!std::filesystem::is_regular_file(previousCatalog))
                return ImportAssetsUnlocked({}, policy, cancellation, std::move(progress), refreshSources);

            std::unordered_set<AssetId> selected;
            std::unordered_set<std::string> affectedSources;
            const auto sourcePrefix = std::filesystem::relative(m_Impl->SourceRoot, m_Impl->Specification.ProjectRoot);
            const auto addAffectedSource = [&affectedSources, &sourcePrefix](const AssetSourceRecord& record)
            { affectedSources.insert((sourcePrefix / record.RelativePath).lexically_normal().generic_string()); };
            for (const auto asset : assets)
            {
                const auto owner =
                    std::ranges::find_if(allRecords,
                                         [asset](const AssetSourceRecord& record)
                                         {
                                             return record.Id == asset || std::ranges::find(record.SubAssets, asset) !=
                                                                              record.SubAssets.end();
                                         });
                if (owner == allRecords.end())
                    throw std::invalid_argument("Targeted asset import identity is not in the source database: " +
                                                asset.ToString());
                selected.insert(owner->Id);
                addAffectedSource(*owner);
            }
            bool expanded = true;
            while (expanded)
            {
                expanded = false;
                for (const auto& record : allRecords)
                {
                    if (selected.contains(record.Id) ||
                        std::ranges::none_of(record.SourceDependencies,
                                             [&affectedSources](const AssetSourceDependency& dependency)
                                             {
                                                 return affectedSources.contains(
                                                     dependency.RelativePath.lexically_normal().generic_string());
                                             }))
                        continue;
                    selected.insert(record.Id);
                    addAffectedSource(record);
                    expanded = true;
                }
            }
            records.clear();
            for (const auto& record : allRecords)
            {
                if (!selected.contains(record.Id))
                    continue;
                records.push_back(record);
                sourceAssets.push_back(record.Id);
                replacedAssets.push_back(record.Id);
                replacedAssets.insert(replacedAssets.end(), record.SubAssets.begin(), record.SubAssets.end());
            }
        }
        ReportOperationProgress(progress, AssetOperationPhase::Importing, 0, records.size());
        m_Impl->ResetCookInputs();
        m_Impl->CacheFiles->CreateDirectories("Objects");
        AssetImportResult result;
        bool failed = false;
        std::size_t completed = 0;
        std::vector<std::pair<AssetSourceRecord, std::uint32_t>> metadataUpgrades;
        for (auto record : records)
        {
            ThrowIfOperationCancelled(cancellation);
            AssetImportStatus status;
            status.Id = record.Id;
            try
            {
                (void)ConfinedPath(m_Impl->SourceRoot, record.RelativePath);
                record.MetadataPath = ConfinedMetadataPath(m_Impl->SourceRoot, record.RelativePath);
                auto validated = m_Impl->TakeValidatedImport(record);
                auto restored = validated ? std::optional<AssetImportOutput>{} : m_Impl->RestoreCachedImport(record);
                const bool restoredFromCache = restored.has_value();
                auto imported = validated  ? std::move(*validated)
                                : restored ? std::move(*restored)
                                           : m_Impl->Import(record);
                const auto effectiveType = imported.PrimaryType.value_or(record.Type);
                UpdateMetadataImportOutput(*m_Impl->SourceFiles,
                                           record.MetadataPath.lexically_relative(m_Impl->SourceRoot), effectiveType,
                                           imported.SubAssets);
                record.Type = effectiveType;
                const auto metadataBytes = m_Impl->SourceFiles->Read(
                    record.MetadataPath.lexically_relative(m_Impl->SourceRoot), 16ULL * 1024ULL * 1024U);
                record.MetadataDigest = Detail::DigestToString(Detail::Sha256(metadataBytes));
                status.Diagnostics = imported.Diagnostics;
                for (const auto& diagnostic : status.Diagnostics)
                    LogImportDiagnostic(record, diagnostic);
                {
                    std::scoped_lock lock(m_Impl->Mutex);
                    const auto stored = std::ranges::find(m_Impl->Records, record.Id, &AssetSourceRecord::Id);
                    if (stored != m_Impl->Records.end())
                    {
                        stored->SourceDependencies = imported.SourceDependencies;
                        stored->Metadata = imported.Metadata;
                        stored->Type = effectiveType;
                        stored->MetadataDigest = record.MetadataDigest;
                        stored->SubAssets.clear();
                        stored->SubAssets.reserve(imported.SubAssets.size());
                        for (const auto& subAsset : imported.SubAssets)
                            stored->SubAssets.push_back(subAsset.Id);
                        for (const auto dependency : imported.AssetDependencies)
                        {
                            if (std::ranges::find(stored->Dependencies, dependency) == stored->Dependencies.end())
                                stored->Dependencies.push_back(dependency);
                        }
                        std::ranges::sort(stored->Dependencies);
                    }
                }
                const auto object = m_Impl->ObjectPath(record, m_Impl->ImportDigest(record, imported));
                if (std::filesystem::exists(object))
                {
                    ++result.CacheHits;
                    status.State = AssetImportState::CacheHit;
                }
                else
                {
                    m_Impl->CacheFiles->WriteFileAtomically(object.lexically_relative(m_Impl->CacheRoot),
                                                            imported.Bytes);
                    ++result.Imported;
                    status.State = AssetImportState::Imported;
                }
                if (!restoredFromCache)
                    m_Impl->StoreCachedImport(record, imported);
                m_Impl->StoreCookInput(record, std::move(imported));
                if (const auto* importer = m_Impl->FindImporter(record);
                    importer && importer->Version > record.ImporterVersion)
                    metadataUpgrades.emplace_back(record, importer->Version);
            }
            catch (const std::exception& error)
            {
                failed = true;
                status.State = AssetImportState::Failed;
                status.Diagnostics.push_back({AssetDiagnosticSeverity::Error, record.RelativePath, 0, 0, error.what()});
                LogImportDiagnostic(record, status.Diagnostics.back());
                {
                    std::scoped_lock lock(m_Impl->Mutex);
                    m_Impl->ImportStatuses[record.Id] = status;
                }
                result.Statuses.push_back(std::move(status));
                if (policy == AssetImportPolicy::FailFast)
                    throw;
                continue;
            }
            {
                std::scoped_lock lock(m_Impl->Mutex);
                m_Impl->ImportStatuses[record.Id] = status;
            }
            result.Statuses.push_back(std::move(status));
            ++completed;
            ReportOperationProgress(progress, AssetOperationPhase::Importing, completed, records.size(),
                                    record.RelativePath);
        }
        if (failed)
        {
            const auto previous = m_Impl->CacheRoot / "Runtime" / "catalog.json";
            if (std::filesystem::is_regular_file(previous))
                result.CatalogPath = previous;
            return result;
        }
        for (const auto& [previous, version] : metadataUpgrades)
        {
            const auto record = Find(previous.Id);
            if (!record)
                continue;
            const auto source = ConfinedPath(m_Impl->SourceRoot, record->RelativePath);
            const auto metadata = ConfinedMetadataPath(m_Impl->SourceRoot, record->RelativePath);
            if (!UpgradeMetadataImporterVersion(*m_Impl->SourceFiles, metadata.lexically_relative(m_Impl->SourceRoot),
                                                record->Importer, version))
                continue;
            auto upgraded = *record;
            upgraded.MetadataPath = metadata;
            upgraded.ImporterVersion = version;
            const auto metadataBytes = m_Impl->SourceFiles->Read(
                upgraded.MetadataPath.lexically_relative(m_Impl->SourceRoot), 1024ULL * 1024U);
            upgraded.MetadataDigest = Detail::DigestToString(Detail::Sha256(metadataBytes));
            auto imported = m_Impl->TakeCookInput(previous);
            if (!imported)
                throw std::logic_error(
                    "A successfully imported asset lost its prepared output during metadata upgrade.");
            const auto object = m_Impl->ObjectPath(upgraded, m_Impl->ImportDigest(upgraded, *imported));
            if (!std::filesystem::exists(object))
                m_Impl->CacheFiles->WriteFileAtomically(object.lexically_relative(m_Impl->CacheRoot), imported->Bytes);
            m_Impl->StoreCachedImport(upgraded, *imported);
            m_Impl->StoreCookInput(upgraded, std::move(*imported));
            m_Impl->PublishRecord(std::move(upgraded), m_Impl->ReadSignature(source, metadata));
        }
        try
        {
            ThrowIfOperationCancelled(cancellation);
            const auto cooked = AssetCooker::CookUnlocked(*this, AssetBuildProfile{}, m_Impl->CacheRoot / "Runtime",
                                                          cancellation, progress, sourceAssets, replacedAssets);
            result.CatalogPath = cooked.CatalogPath;
        }
        catch (const AssetOperationCancelled&)
        {
            throw;
        }
        catch (const std::exception& error)
        {
            if (policy == AssetImportPolicy::FailFast)
                throw;
            KEIRE_CORE_ERROR("Asset catalog validation failed; retaining the last-good catalog: {}", error.what());
            const auto previous = m_Impl->CacheRoot / "Runtime" / "catalog.json";
            if (std::filesystem::is_regular_file(previous))
                result.CatalogPath = previous;
        }
        ReportOperationProgress(progress, AssetOperationPhase::Completed, records.size(), records.size());
        return result;
    }

    AssetImportStatus AssetDatabase::ImportStatus(const AssetId id) const
    {
        std::scoped_lock lock(m_Impl->Mutex);
        const auto found = m_Impl->ImportStatuses.find(id);
        return found == m_Impl->ImportStatuses.end() ? AssetImportStatus{.Id = id} : found->second;
    }

    std::optional<AssetImporterRegistration> AssetDatabase::FindImporterForPath(const std::filesystem::path& path) const
    {
        const auto* importer = m_Impl->InferImporter(path);
        return importer ? std::optional<AssetImporterRegistration>(*importer) : std::nullopt;
    }

    AssetImporterRegistration CreateTextAssetImporter()
    {
        AssetImporterRegistration result;
        result.Name = "Keire.Text";
        result.Version = 1;
        result.Type = TextAsset::StaticType();
        result.Extensions = {".cs"};
        result.Import = [](const std::span<const std::byte> bytes)
        { return std::vector<std::byte>(bytes.begin(), bytes.end()); };
        return result;
    }

} // namespace Keire
