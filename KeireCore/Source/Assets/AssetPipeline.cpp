#include "KeireInternal/Assets/AssetDatabaseImplementation.h"

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
        : m_Impl(std::make_unique<Impl>(std::move(specification)))
    {
        (void)Refresh();
    }

    AssetDatabase::~AssetDatabase() = default;

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
            const auto expectedMetadata = Detail::PathWithSuffix(source, ".keiremeta").lexically_normal();
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
        database.m_Impl->Records = std::move(records);
        database.m_Impl->Observed = std::move(signatures);
        database.m_Impl->PendingChanges.clear();
        std::erase_if(database.m_Impl->ImportStatuses,
                      [&database](const auto& entry)
                      {
                          return std::ranges::find(database.m_Impl->Records, entry.first, &AssetSourceRecord::Id) ==
                                 database.m_Impl->Records.end();
                      });
        return database.m_Impl->Records.size();
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
        m_Impl->Records = std::move(scanned.Records);
        m_Impl->Observed = std::move(scanned.Signatures);
        m_Impl->PendingChanges.clear();
        std::erase_if(m_Impl->ImportStatuses,
                      [this](const auto& entry)
                      {
                          return std::ranges::find(m_Impl->Records, entry.first, &AssetSourceRecord::Id) ==
                                 m_Impl->Records.end();
                      });
        return m_Impl->Records.size();
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
        const auto found = std::ranges::find(m_Impl->Records, id, &AssetSourceRecord::Id);
        return found == m_Impl->Records.end() ? std::nullopt : std::optional<AssetSourceRecord>(*found);
    }

    std::optional<AssetSourceRecord> AssetDatabase::Find(const std::filesystem::path& relativePath) const
    {
        const auto normalized = relativePath.lexically_normal();
        std::scoped_lock lock(m_Impl->Mutex);
        const auto found = std::ranges::find(m_Impl->Records, normalized, &AssetSourceRecord::RelativePath);
        return found == m_Impl->Records.end() ? std::nullopt : std::optional<AssetSourceRecord>(*found);
    }

    std::vector<AssetId> AssetDatabase::PollChangedAssets()
    {
        std::scoped_lock operation(*m_Impl->OperationMutex);
        auto scanned = m_Impl->Scan(false);
        const auto now = std::chrono::steady_clock::now();
        std::vector<AssetId> ready;
        std::scoped_lock lock(m_Impl->Mutex);
        for (const auto& [id, signature] : scanned.Signatures)
        {
            if (!m_Impl->Observed.contains(id) || m_Impl->Observed[id] != signature)
                m_Impl->PendingChanges[id] = now;
        }
        for (const auto& [id, signature] : m_Impl->Observed)
        {
            if (!scanned.Signatures.contains(id))
                m_Impl->PendingChanges[id] = now;
        }
        for (auto& record : scanned.Records)
        {
            const auto previous = std::ranges::find(m_Impl->Records, record.Id, &AssetSourceRecord::Id);
            if (previous != m_Impl->Records.end())
            {
                record.SourceDigest = previous->SourceDigest;
                record.MetadataDigest = previous->MetadataDigest;
            }
        }
        m_Impl->Observed = std::move(scanned.Signatures);
        m_Impl->Records = std::move(scanned.Records);
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
        std::ranges::sort(ready);
        return ready;
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
        ThrowIfOperationCancelled(cancellation);
        ReportOperationProgress(progress, AssetOperationPhase::Scanning, 0, 0);
        (void)RefreshUnlocked();
        const auto records = Records();
        ReportOperationProgress(progress, AssetOperationPhase::Importing, 0, records.size());
        m_Impl->ResetCookInputs();
        const auto objectRoot = m_Impl->CacheRoot / "Objects";
        std::filesystem::create_directories(objectRoot);
        AssetImportResult result;
        bool failed = false;
        std::size_t completed = 0;
        std::vector<std::pair<AssetId, std::uint32_t>> metadataUpgrades;
        for (const auto& record : records)
        {
            ThrowIfOperationCancelled(cancellation);
            AssetImportStatus status;
            status.Id = record.Id;
            try
            {
                auto validated = m_Impl->TakeValidatedImport(record);
                auto restored = validated ? std::optional<AssetImportOutput>{} : m_Impl->RestoreCachedImport(record);
                auto imported = validated  ? std::move(*validated)
                                : restored ? std::move(*restored)
                                           : m_Impl->Import(record);
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
                    const auto temporary = Detail::PathWithSuffix(object, ".tmp");
                    std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
                    if (!stream ||
                        (!imported.Bytes.empty() && !stream.write(reinterpret_cast<const char*>(imported.Bytes.data()),
                                                                  static_cast<std::streamsize>(imported.Bytes.size()))))
                        throw std::runtime_error("Could not write imported asset cache object.");
                    stream.close();
                    Detail::AtomicReplace(temporary, object);
                    ++result.Imported;
                    status.State = AssetImportState::Imported;
                }
                m_Impl->StoreCookInput(record, std::move(imported));
                if (const auto* importer = m_Impl->FindImporter(record);
                    importer && importer->Version > record.ImporterVersion)
                    metadataUpgrades.emplace_back(record.Id, importer->Version);
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
        for (const auto& [id, version] : metadataUpgrades)
        {
            const auto record = Find(id);
            if (!record || !UpgradeMetadataImporterVersion(record->MetadataPath, record->Importer, version))
                continue;
            auto upgraded = *record;
            upgraded.ImporterVersion = version;
            const auto metadataBytes = ReadSource(upgraded.MetadataPath, 1024U * 1024U);
            upgraded.MetadataDigest = Detail::DigestToString(Detail::Sha256(metadataBytes));
            m_Impl->PublishRecord(std::move(upgraded), m_Impl->ReadSignature(m_Impl->SourceRoot / record->RelativePath,
                                                                             record->MetadataPath));
        }
        try
        {
            ThrowIfOperationCancelled(cancellation);
            const auto cooked = AssetCooker::CookUnlocked(*this, AssetBuildProfile{}, m_Impl->CacheRoot / "Runtime",
                                                          cancellation, progress);
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

} // namespace Keire
