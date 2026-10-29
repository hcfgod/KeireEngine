#include "KeireInternal/Assets/AssetDatabaseImplementation.h"

#include <algorithm>
#include <chrono>
#include <memory>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace Keire
{
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
            if (record.Importer != imported.Importer || record.ImporterVersion != imported.ImporterVersion ||
                record.MetadataDigest != imported.MetadataDigest)
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
                      [this](const auto& entry) { return !m_Impl->IdIndex.contains(entry.first); });
        return m_Impl->Records.size();
    }

    void AssetDatabase::RefreshAssetsUnlocked(const std::span<const AssetId> assets)
    {
        std::vector<AssetSourceRecord> owners;
        owners.reserve(assets.size());
        {
            std::scoped_lock lock(m_Impl->Mutex);
            std::unordered_set<AssetId> selected;
            selected.reserve(assets.size());
            std::unordered_map<AssetId, std::size_t> subAssetOwners;
            bool indexedSubAssets = false;
            for (const auto asset : assets)
            {
                const auto primary = m_Impl->IdIndex.find(asset);
                auto owner = m_Impl->Records.end();
                if (primary != m_Impl->IdIndex.end())
                    owner = m_Impl->Records.begin() + primary->second;
                else
                {
                    if (!indexedSubAssets)
                    {
                        for (std::size_t index = 0; index < m_Impl->Records.size(); ++index)
                            for (const auto child : m_Impl->Records[index].SubAssets)
                                subAssetOwners.try_emplace(child, index);
                        indexedSubAssets = true;
                    }
                    if (const auto child = subAssetOwners.find(asset); child != subAssetOwners.end())
                        owner = m_Impl->Records.begin() + child->second;
                }
                if (owner == m_Impl->Records.end())
                    throw std::invalid_argument("Targeted asset import identity is not in the source database: " +
                                                asset.ToString());
                if (selected.insert(owner->Id).second)
                    owners.push_back(*owner);
            }
        }

        for (const auto& previous : owners)
        {
            const auto source = ConfinedPath(m_Impl->SourceRoot, previous.RelativePath);
            auto refreshed =
                ReadMetadata(*m_Impl->SourceFiles, previous.RelativePath, m_Impl->Specification.MaximumSourceBytes,
                             true, m_Impl->InferImporter(source));
            if (refreshed.Id != previous.Id)
                throw std::runtime_error("Targeted source refresh changed the stable asset identity.");
            if (refreshed.Importer == previous.Importer && refreshed.ImporterVersion == previous.ImporterVersion &&
                refreshed.MetadataDigest == previous.MetadataDigest)
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
                        if (!monitored->DigestsVerified)
                        {
                            record.SourceDigest = previous->SourceDigest;
                        }
                        if (record.SourceDigest != previous->SourceDigest ||
                            record.MetadataDigest != previous->MetadataDigest)
                        {
                            m_Impl->PendingChanges.try_emplace(record.Id, now);
                        }
                        if (record.MetadataDigest == previous->MetadataDigest &&
                            record.Importer == previous->Importer &&
                            record.ImporterVersion == previous->ImporterVersion)
                        {
                            // Discovery has no imported dependency graph or derived display/bounds data. Retain the
                            // last imported values across idle/source-only scans, but never across a metadata change.
                            record.Dependencies = previous->Dependencies;
                            record.SourceDependencies = previous->SourceDependencies;
                            record.Metadata = previous->Metadata;
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
        const auto operationStarted = std::chrono::steady_clock::now();
        const auto elapsedMilliseconds = [](const auto started)
        { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count(); };
        ThrowIfOperationCancelled(cancellation);
        ReportOperationProgress(progress, AssetOperationPhase::Scanning, 0, 0);
        if (refreshSources)
            (void)RefreshUnlocked();
        else
            RefreshAssetsUnlocked(assets);
        auto allRecords = Records();
        auto records = assets.empty() ? std::move(allRecords) : std::vector<AssetSourceRecord>{};
        std::vector<AssetId> sourceAssets;
        std::vector<AssetId> replacedAssets;
        std::unordered_set<AssetId> selected;
        std::unordered_set<AssetId> availableAssets;
        std::unordered_map<AssetId, std::size_t> sourceOwners;
        // Small interactive requests avoid constructing an index; large dependency expansions remain bounded.
        std::size_t ownerLookups = assets.size() >= 32 ? 32 : 0;
        const auto findSourceOwner = [&](const AssetId asset) -> const AssetSourceRecord*
        {
            if (ownerLookups++ < 32)
            {
                const auto owner =
                    std::ranges::find_if(allRecords,
                                         [asset](const AssetSourceRecord& record)
                                         {
                                             return record.Id == asset || std::ranges::find(record.SubAssets, asset) !=
                                                                              record.SubAssets.end();
                                         });
                return owner == allRecords.end() ? nullptr : &*owner;
            }
            if (sourceOwners.empty())
            {
                sourceOwners.reserve(allRecords.size());
                for (std::size_t index = 0; index < allRecords.size(); ++index)
                {
                    sourceOwners.try_emplace(allRecords[index].Id, index);
                    for (const auto child : allRecords[index].SubAssets)
                        sourceOwners.try_emplace(child, index);
                }
            }
            const auto owner = sourceOwners.find(asset);
            return owner == sourceOwners.end() ? nullptr : &allRecords[owner->second];
        };
        if (!assets.empty())
        {
            const auto previousCatalog = m_Impl->CacheRoot / "Runtime" / "catalog.json";
            if (!std::filesystem::is_regular_file(previousCatalog))
                return ImportAssetsUnlocked({}, policy, cancellation, std::move(progress), refreshSources);

            for (const auto& entry : Detail::LoadCatalog(previousCatalog).Entries)
                availableAssets.insert(entry.Id);
            std::unordered_map<std::string, std::vector<std::size_t>> dependents;
            for (std::size_t index = 0; index < allRecords.size(); ++index)
            {
                for (const auto& dependency : allRecords[index].SourceDependencies)
                    dependents[dependency.RelativePath.lexically_normal().generic_string()].push_back(index);
            }
            std::vector<std::string> affectedSources;
            const auto sourcePrefix = std::filesystem::relative(m_Impl->SourceRoot, m_Impl->Specification.ProjectRoot);
            const auto addAffectedSource = [&affectedSources, &sourcePrefix](const AssetSourceRecord& record)
            { affectedSources.push_back((sourcePrefix / record.RelativePath).lexically_normal().generic_string()); };
            for (const auto asset : assets)
            {
                const auto* owner = findSourceOwner(asset);
                if (!owner)
                    throw std::invalid_argument("Targeted asset import identity is not in the source database: " +
                                                asset.ToString());
                if (selected.insert(owner->Id).second)
                    addAffectedSource(*owner);
            }
            for (std::size_t next = 0; next < affectedSources.size(); ++next)
            {
                const auto found = dependents.find(affectedSources[next]);
                if (found == dependents.end())
                    continue;
                for (const auto index : found->second)
                {
                    const auto& record = allRecords[index];
                    if (selected.insert(record.Id).second)
                        addAffectedSource(record);
                }
            }
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
        KEIRE_CORE_INFO("Asset import scan and selection completed (assets={}, elapsed_ms={:.3f}).", records.size(),
                        elapsedMilliseconds(operationStarted));
        m_Impl->ResetCookInputs();
        m_Impl->CacheFiles->CreateDirectories("Objects");
        AssetImportResult result;
        bool failed = false;
        std::size_t completed = 0;
        std::vector<std::pair<AssetSourceRecord, std::uint32_t>> metadataUpgrades;
        std::vector<AssetId> successfulSourceAssets;
        std::vector<AssetId> successfulReplacedAssets;
        successfulSourceAssets.reserve(records.size());
        for (std::size_t next = 0; next < records.size(); ++next)
        {
            const auto assetStarted = std::chrono::steady_clock::now();
            auto record = records[next];
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
                const bool reusedValidatedImport = validated.has_value();
                auto imported = validated  ? std::move(*validated)
                                : restored ? std::move(*restored)
                                           : m_Impl->Import(record);
                const auto preparationMilliseconds = elapsedMilliseconds(assetStarted);
                if (preparationMilliseconds >= 100.0)
                {
                    KEIRE_CORE_INFO("Slow asset preparation (asset={}, source={}, elapsed_ms={:.3f}).",
                                    record.RelativePath.generic_string(),
                                    reusedValidatedImport ? "validated"
                                    : restoredFromCache   ? "cache"
                                                          : "importer",
                                    preparationMilliseconds);
                }
                const auto effectiveType = imported.PrimaryType.value_or(record.Type);
                UpdateMetadataImportOutput(*m_Impl->SourceFiles,
                                           record.MetadataPath.lexically_relative(m_Impl->SourceRoot), effectiveType,
                                           imported.SubAssets, imported.AssetDependencies);
                record.Type = effectiveType;
                const auto metadataBytes = m_Impl->SourceFiles->Read(
                    record.MetadataPath.lexically_relative(m_Impl->SourceRoot), 16ULL * 1024ULL * 1024U);
                record.MetadataDigest = Detail::DigestToString(Detail::Sha256(metadataBytes));
                status.Diagnostics = imported.Diagnostics;
                for (const auto& diagnostic : status.Diagnostics)
                    LogImportDiagnostic(record, diagnostic);
                {
                    std::scoped_lock lock(m_Impl->Mutex);
                    const auto storedIndex = m_Impl->IdIndex.find(record.Id);
                    if (storedIndex != m_Impl->IdIndex.end())
                    {
                        auto* stored = &m_Impl->Records[storedIndex->second];
                        stored->SourceDependencies = imported.SourceDependencies;
                        stored->Metadata = imported.Metadata;
                        stored->Type = effectiveType;
                        stored->MetadataDigest = record.MetadataDigest;
                        stored->SubAssets.clear();
                        stored->SubAssets.reserve(imported.SubAssets.size());
                        for (const auto& subAsset : imported.SubAssets)
                            stored->SubAssets.push_back(subAsset.Id);
                        stored->Dependencies = imported.AssetDependencies;
                        std::ranges::sort(stored->Dependencies);
                        const auto duplicate = std::ranges::unique(stored->Dependencies);
                        stored->Dependencies.erase(duplicate.begin(), duplicate.end());
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
                if (!assets.empty())
                {
                    // Catalog presence does not establish importer compatibility. Follow current owners too,
                    // because an unchanged material may reference a shader requiring migration.
                    const auto includeDependencies = [&](const std::span<const AssetId> dependencies)
                    {
                        std::vector<AssetId> pending(dependencies.begin(), dependencies.end());
                        std::unordered_set<AssetId> visited;
                        for (std::size_t dependencyIndex = 0; dependencyIndex < pending.size(); ++dependencyIndex)
                        {
                            const auto dependency = pending[dependencyIndex];
                            if (!visited.insert(dependency).second)
                            {
                                continue;
                            }
                            const auto* owner = findSourceOwner(dependency);
                            if (!owner)
                            {
                                continue;
                            }
                            const auto& source = *owner;
                            const auto* importer = m_Impl->FindImporter(source);
                            const auto importState = ImportStatus(source.Id).State;
                            const bool requiresRefresh = (importer && importer->Version != source.ImporterVersion) ||
                                                         (importState != AssetImportState::Imported &&
                                                          importState != AssetImportState::CacheHit);
                            if (availableAssets.contains(dependency) && !requiresRefresh)
                            {
                                pending.insert(pending.end(), source.Dependencies.begin(), source.Dependencies.end());
                                continue;
                            }
                            if (!selected.insert(source.Id).second)
                            {
                                continue;
                            }
                            records.push_back(source);
                            sourceAssets.push_back(source.Id);
                            replacedAssets.push_back(source.Id);
                            replacedAssets.insert(replacedAssets.end(), source.SubAssets.begin(),
                                                  source.SubAssets.end());
                        }
                    };
                    includeDependencies(record.Dependencies);
                    includeDependencies(imported.AssetDependencies);
                    for (const auto& subAsset : imported.SubAssets)
                        includeDependencies(subAsset.AssetDependencies);
                }
                m_Impl->StoreCookInput(record, std::move(imported));
                successfulSourceAssets.push_back(record.Id);
                successfulReplacedAssets.push_back(record.Id);
                successfulReplacedAssets.insert(successfulReplacedAssets.end(), record.SubAssets.begin(),
                                                record.SubAssets.end());
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
        const auto publicationFailed = [&](const std::string& diagnostic)
        {
            // Import preparation can upgrade metadata before publication. Persist the failed publication
            // alongside it so a restarted editor cannot mistake old catalog bytes for the upgraded artifacts.
            result.CatalogPath.clear();
            std::scoped_lock lock(m_Impl->Mutex);
            for (auto& status : result.Statuses)
            {
                if (status.State == AssetImportState::Imported || status.State == AssetImportState::CacheHit)
                {
                    status.State = AssetImportState::Failed;
                    status.Diagnostics.push_back(
                        {AssetDiagnosticSeverity::Error, {}, 0, 0, "Asset catalog publication failed: " + diagnostic});
                    m_Impl->ImportStatuses[status.Id] = status;
                }
            }
        };
        if (failed)
        {
            const auto previous = m_Impl->CacheRoot / "Runtime" / "catalog.json";
            if (!std::filesystem::is_regular_file(previous))
                return result;
            result.CatalogPath = previous;
            if (successfulSourceAssets.empty())
                return result;
            try
            {
                ThrowIfOperationCancelled(cancellation);
                const auto cooked =
                    AssetCooker::CookUnlocked(*this, AssetBuildProfile{}, m_Impl->CacheRoot / "Runtime", cancellation,
                                              progress, successfulSourceAssets, successfulReplacedAssets);
                result.CatalogPath = cooked.CatalogPath;
            }
            catch (const AssetOperationCancelled&)
            {
                throw;
            }
            catch (const std::exception& error)
            {
                KEIRE_CORE_ERROR("Successful best-effort imports could not be merged into the last-good catalog: {}",
                                 error.what());
                publicationFailed(error.what());
            }
            return result;
        }
        try
        {
            ThrowIfOperationCancelled(cancellation);
            const auto catalogStarted = std::chrono::steady_clock::now();
            const auto cooked = AssetCooker::CookUnlocked(*this, AssetBuildProfile{}, m_Impl->CacheRoot / "Runtime",
                                                          cancellation, progress, sourceAssets, replacedAssets);
            result.CatalogPath = cooked.CatalogPath;
            KEIRE_CORE_INFO("Asset catalog published (elapsed_ms={:.3f}).", elapsedMilliseconds(catalogStarted));
        }
        catch (const AssetOperationCancelled&)
        {
            throw;
        }
        catch (const std::exception& error)
        {
            publicationFailed(error.what());
            if (policy == AssetImportPolicy::FailFast)
                throw;
            KEIRE_CORE_ERROR("Asset catalog validation failed; retaining the last-good catalog: {}", error.what());
        }
        ReportOperationProgress(progress, AssetOperationPhase::Completed, records.size(), records.size());
        KEIRE_CORE_INFO("Asset import completed (assets={}, imported={}, cache_hits={}, elapsed_ms={:.3f}).",
                        records.size(), result.Imported, result.CacheHits, elapsedMilliseconds(operationStarted));
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
