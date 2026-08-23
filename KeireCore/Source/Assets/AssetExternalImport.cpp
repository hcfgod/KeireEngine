#include "KeireInternal/Assets/AssetDatabaseImplementation.h"

namespace Keire
{
    void AssetDatabase::Impl::RecoverInterruptedExternalImports()
    {
        const auto transactions = Specification.ProjectRoot / "Library/AssetImport";
        std::error_code error;
        if (!std::filesystem::is_directory(transactions, error))
            return;
        for (std::filesystem::directory_iterator iterator(transactions, error), end; !error && iterator != end;
             iterator.increment(error))
        {
            if (!iterator->is_directory(error))
                continue;
            const auto journalPath = iterator->path() / "journal.json";
            if (!std::filesystem::is_regular_file(journalPath, error))
                continue;
            const auto transactionRelative = iterator->path().lexically_relative(Specification.ProjectRoot);
            const auto journal =
                ReadJsonFile(*ProjectFiles, transactionRelative / "journal.json", 16ULL * 1024ULL * 1024U);
            if (journal.value("schemaVersion", 0) != 1 || !journal.contains("state"))
                throw std::runtime_error("External asset import journal is invalid: " +
                                         Detail::PathToUtf8(journalPath));
            const auto state = journal.at("state").get<std::string>();
            if (state == "committed")
            {
                if (!ProjectFiles->Exists(transactionRelative / "receipt.json"))
                    RemoveProjectTree(transactionRelative);
                continue;
            }
            if (state == "staged")
            {
                RemoveProjectTree(transactionRelative);
                continue;
            }
            if (state != "publishing")
                throw std::runtime_error("External asset import journal has an unknown state: " + state);
            if (!journal.contains("entries") || !journal["entries"].is_array())
                throw std::runtime_error("Publishing asset journal has no recovery entries.");
            for (std::size_t index = journal["entries"].size(); index > 0; --index)
            {
                const auto& entry = journal["entries"][index - 1];
                const auto relativeDestination = Detail::PathFromUtf8(entry.at("destination").get<std::string>());
                (void)ConfinedPath(SourceRoot, relativeDestination);
                (void)ConfinedMetadataPath(SourceRoot, relativeDestination);
                if (entry.value("replaced", false))
                {
                    const auto prefix = std::to_string(index - 1);
                    SourceFiles->WriteFileAtomically(
                        relativeDestination, ProjectFiles->Read(transactionRelative / "before" / (prefix + ".source"),
                                                                Specification.MaximumSourceBytes));
                    SourceFiles->WriteFileAtomically(
                        Detail::PathWithSuffix(relativeDestination, ".keiremeta"),
                        ProjectFiles->Read(transactionRelative / "before" / (prefix + ".metadata"),
                                           16ULL * 1024ULL * 1024U));
                }
                else
                {
                    if (SourceFiles->Exists(relativeDestination))
                        SourceFiles->Remove(relativeDestination);
                    const auto metadataRelative = Detail::PathWithSuffix(relativeDestination, ".keiremeta");
                    if (SourceFiles->Exists(metadataRelative))
                        SourceFiles->Remove(metadataRelative);
                }
            }
            PendingExternalImportRecovery.push_back(transactionRelative);
        }
        if (error)
            throw std::runtime_error("Could not recover external asset import transactions: " + error.message());
    }

    void AssetDatabase::Impl::CompleteExternalImportRecovery(AssetDatabase& database)
    {
        if (PendingExternalImportRecovery.empty())
            return;
        std::scoped_lock operation(*OperationMutex);
        (void)database.ImportAllUnlocked(AssetImportPolicy::FailFast, {}, {});
        for (const auto& transaction : PendingExternalImportRecovery)
            RemoveProjectTree(transaction);
        PendingExternalImportRecovery.clear();
    }

    ExternalAssetImportResult AssetDatabase::ImportExternal(const std::span<const ExternalAssetImportItem> items,
                                                            const std::stop_token cancellation,
                                                            const AssetOperationProgressCallback& progress)
    {
        std::scoped_lock operation(*m_Impl->OperationMutex);
        const auto throwIfCancelled = [&cancellation] { ThrowIfOperationCancelled(cancellation); };
        struct PlannedItem final
        {
            std::filesystem::path Source;
            std::filesystem::path Destination;
            AssetImportSettings Settings;
            ExternalAssetConflictPolicy Conflict = ExternalAssetConflictPolicy::UniqueName;
            const AssetImporterRegistration* Importer = nullptr;
            AssetId Id;
            bool Replaced = false;
            std::vector<std::byte> SourceBytes;
            std::vector<std::byte> PreviousSource;
            std::vector<std::byte> PreviousMetadata;
            AssetImportOutput Validated;
        };
        std::vector<PlannedItem> planned;
        ReportOperationProgress(progress, AssetOperationPhase::Preflight, 0, items.size());
        std::size_t preflightCompleted = 0;
        for (const auto& item : items)
        {
            throwIfCancelled();
            if (item.SourcePath.empty())
                throw std::invalid_argument("External import source paths must not be empty.");
            const auto source = std::filesystem::absolute(item.SourcePath).lexically_normal();
            if (std::filesystem::is_symlink(source))
                throw std::invalid_argument("External imports do not follow symbolic links: " +
                                            Detail::PathToUtf8(source));
            if (std::filesystem::is_directory(source))
            {
                std::error_code error;
                for (std::filesystem::recursive_directory_iterator
                         iterator(source, std::filesystem::directory_options::skip_permission_denied, error),
                     end;
                     !error && iterator != end; iterator.increment(error))
                {
                    throwIfCancelled();
                    if (iterator->is_symlink(error))
                    {
                        if (iterator->is_directory(error))
                            iterator.disable_recursion_pending();
                        throw std::invalid_argument("External imports do not follow symbolic links: " +
                                                    Detail::PathToUtf8(iterator->path()));
                    }
                    if (iterator->path().extension() == ".keiremeta")
                        throw std::invalid_argument("External imports do not accept asset identity metadata: " +
                                                    Detail::PathToUtf8(iterator->path()));
                    if (!iterator->is_regular_file(error))
                        continue;
                    if (!m_Impl->InferImporter(iterator->path()))
                        continue;
                    const auto relative = std::filesystem::relative(iterator->path(), source, error);
                    if (error)
                        throw std::runtime_error("Could not resolve a dropped directory entry.");
                    planned.push_back(
                        {iterator->path(), item.RelativeDestination / relative, item.Settings, item.Conflict});
                }
                if (error)
                    throw std::runtime_error("Could not enumerate dropped directory: " + error.message());
                ++preflightCompleted;
                ReportOperationProgress(progress, AssetOperationPhase::Preflight, preflightCompleted, items.size(),
                                        source);
                continue;
            }
            if (!std::filesystem::is_regular_file(source))
                throw std::invalid_argument("External import source is not a regular file: " +
                                            Detail::PathToUtf8(source));
            planned.push_back({source, item.RelativeDestination.empty() ? source.filename() : item.RelativeDestination,
                               item.Settings, item.Conflict});
            ++preflightCompleted;
            ReportOperationProgress(progress, AssetOperationPhase::Preflight, preflightCompleted, items.size(), source);
        }
        if (planned.empty())
            throw std::invalid_argument("No supported asset files were found in the external import.");

        ExternalAssetImportResult result;
        const auto receiptValue = AssetId::Generate();
        const auto transactionRelative = std::filesystem::path("Library/AssetImport") / receiptValue.ToString();
        const auto transactionRoot = ConfinedPath(m_Impl->Specification.ProjectRoot, transactionRelative);
        const auto stagingRoot = transactionRoot / "staging";
        Json journal{{"schemaVersion", 1}, {"state", "staged"}, {"entries", Json::array()}};
        bool publicationStarted = false;
        std::unique_ptr<Detail::AnchoredFileSystem> stagingFiles;
        try
        {
            std::unordered_set<std::string> reservedDestinations;
            for (auto& item : planned)
            {
                throwIfCancelled();
                item.Importer = m_Impl->InferImporter(item.Source);
                if (!item.Importer)
                    throw std::invalid_argument("No importer supports: " + Detail::PathToUtf8(item.Source));
                if (item.Settings.empty() && item.Importer->SuggestImportSettings)
                    item.Settings = item.Importer->SuggestImportSettings(item.Source, item.Settings);
                item.Settings = m_Impl->NormalizeSettings(*item.Importer, item.Settings);
                auto destination = item.Destination.lexically_normal();
                auto existing = Find(destination);
                if (existing && item.Conflict == ExternalAssetConflictPolicy::Skip)
                    continue;
                auto destinationKey = Detail::PathToUtf8(destination);
                if ((existing || reservedDestinations.contains(destinationKey)) &&
                    item.Conflict == ExternalAssetConflictPolicy::UniqueName)
                {
                    const auto parent = destination.parent_path();
                    const auto stem = Detail::PathToUtf8(destination.stem());
                    const auto extension = Detail::PathToUtf8(destination.extension());
                    for (std::size_t copy = 2; existing || reservedDestinations.contains(destinationKey); ++copy)
                    {
                        auto candidate = stem;
                        candidate += ' ';
                        candidate += std::to_string(copy);
                        candidate += extension;
                        destination = parent / Detail::PathFromUtf8(candidate);
                        existing = Find(destination);
                        destinationKey = Detail::PathToUtf8(destination);
                    }
                }
                if (reservedDestinations.contains(destinationKey))
                    throw std::invalid_argument("External import batch contains conflicting destinations: " +
                                                destinationKey);
                reservedDestinations.insert(destinationKey);
                if (existing && (existing->Importer != item.Importer->Name || existing->Type != item.Importer->Type))
                    throw std::invalid_argument("Replace requires a destination with the same importer and type.");
                item.Destination = destination;
                item.Replaced = static_cast<bool>(existing);
                item.Id = existing ? existing->Id : AssetId::Generate();
                item.SourceBytes = ReadSource(item.Source, m_Impl->Specification.MaximumSourceBytes);
                if (existing)
                {
                    const auto destinationPath = ConfinedPath(m_Impl->SourceRoot, destination);
                    item.PreviousSource =
                        m_Impl->SourceFiles->Read(destination, m_Impl->Specification.MaximumSourceBytes);
                    item.PreviousMetadata = m_Impl->SourceFiles->Read(Detail::PathWithSuffix(destination, ".keiremeta"),
                                                                      16ULL * 1024ULL * 1024U);
                }
            }
            std::erase_if(planned, [](const PlannedItem& item) { return !item.Id; });
            if (planned.empty())
                throw std::invalid_argument("Every external import item was skipped.");

            m_Impl->ProjectFiles->CreateDirectories(transactionRelative / "staging");
            stagingFiles = std::make_unique<Detail::AnchoredFileSystem>(stagingRoot);
            ReportOperationProgress(progress, AssetOperationPhase::Staging, 0, planned.size());
            for (std::size_t index = 0; index < planned.size(); ++index)
            {
                throwIfCancelled();
                auto& item = planned[index];
                const auto stagedSource = ConfinedPath(stagingRoot, item.Destination);
                const auto stagedMetadata = Detail::PathWithSuffix(stagedSource, ".keiremeta");
                stagingFiles->WriteFileAtomically(item.Destination, item.SourceBytes);
                WriteMetadata(*stagingFiles, Detail::PathWithSuffix(item.Destination, ".keiremeta"), item.Id,
                              item.Importer->Type, item.Importer->Name, item.Importer->Version, item.Settings);
                const auto stagedRecord = ReadMetadata(*stagingFiles, item.Destination,
                                                       m_Impl->Specification.MaximumSourceBytes, true, item.Importer);
                item.Validated = m_Impl->ImportSource(stagedRecord, item.SourceBytes, stagingRoot);
                UpdateMetadataImportOutput(*stagingFiles, Detail::PathWithSuffix(item.Destination, ".keiremeta"),
                                           item.Validated.PrimaryType.value_or(stagedRecord.Type),
                                           item.Validated.SubAssets);
                if (item.Replaced)
                {
                    const auto prefix = std::to_string(index);
                    m_Impl->ProjectFiles->WriteFileAtomically(transactionRelative / "before" / (prefix + ".source"),
                                                              item.PreviousSource);
                    m_Impl->ProjectFiles->WriteFileAtomically(transactionRelative / "before" / (prefix + ".metadata"),
                                                              item.PreviousMetadata);
                }
                journal["entries"].push_back(
                    {{"destination", Detail::PathToUtf8(item.Destination)}, {"replaced", item.Replaced}});
                ReportOperationProgress(progress, AssetOperationPhase::Staging, index + 1, planned.size(), item.Source);
            }
            WriteJsonAtomically(*m_Impl->ProjectFiles, transactionRelative / "journal.json", journal);
            throwIfCancelled();

            Json receipt{{"schemaVersion", 1}, {"entries", Json::array()}};
            for (std::size_t index = 0; index < planned.size(); ++index)
            {
                const auto& item = planned[index];
                const auto prefix = std::to_string(index);
                m_Impl->ProjectFiles->WriteFileAtomically(transactionRelative / (prefix + ".after"), item.SourceBytes);
                m_Impl->ProjectFiles->WriteFileAtomically(
                    transactionRelative / (prefix + ".after.keiremeta"),
                    stagingFiles->Read(Detail::PathWithSuffix(item.Destination, ".keiremeta"),
                                       16ULL * 1024ULL * 1024U));
                if (item.Replaced)
                {
                    m_Impl->ProjectFiles->WriteFileAtomically(transactionRelative / (prefix + ".before"),
                                                              item.PreviousSource);
                    m_Impl->ProjectFiles->WriteFileAtomically(transactionRelative / (prefix + ".before.keiremeta"),
                                                              item.PreviousMetadata);
                }
                receipt["entries"].push_back(
                    {{"destination", Detail::PathToUtf8(item.Destination)}, {"replaced", item.Replaced}});
                result.Entries.push_back({item.Id, item.Source, item.Destination, item.Replaced});
            }
            WriteJsonAtomically(*m_Impl->ProjectFiles, transactionRelative / "receipt.json", receipt);
            result.Receipt = ExternalAssetImportReceiptId(receiptValue);

            publicationStarted = true;
            journal["state"] = "publishing";
            WriteJsonAtomically(*m_Impl->ProjectFiles, transactionRelative / "journal.json", journal);
            ReportOperationProgress(progress, AssetOperationPhase::Publishing, 0, planned.size());
            for (std::size_t index = 0; index < planned.size(); ++index)
            {
                const auto& item = planned[index];
                const auto destination = ConfinedPath(m_Impl->SourceRoot, item.Destination);
                m_Impl->SourceFiles->WriteFileAtomically(item.Destination, item.SourceBytes);
                m_Impl->SourceFiles->WriteFileAtomically(
                    Detail::PathWithSuffix(item.Destination, ".keiremeta"),
                    stagingFiles->Read(Detail::PathWithSuffix(item.Destination, ".keiremeta"),
                                       16ULL * 1024ULL * 1024U));
                ReportOperationProgress(progress, AssetOperationPhase::Publishing, index + 1, planned.size(),
                                        item.Destination);
            }
            (void)RefreshUnlocked();
            std::vector<AssetId> importedAssets;
            importedAssets.reserve(planned.size());
            for (auto& item : planned)
            {
                if (const auto record = Find(item.Id))
                {
                    m_Impl->StoreValidatedImport(*record, std::move(item.Validated));
                    importedAssets.push_back(item.Id);
                }
            }
            if (importedAssets.size() != planned.size())
                throw std::logic_error("Published external imports must remain present in the source database.");
            result.Import =
                ImportAssetsUnlocked(importedAssets, AssetImportPolicy::FailFast, cancellation, progress, false);
            ReportOperationProgress(progress, AssetOperationPhase::Completed, planned.size(), planned.size());
            journal["state"] = "committed";
            WriteJsonAtomically(*m_Impl->ProjectFiles, transactionRelative / "journal.json", journal);
            stagingFiles.reset();
            try
            {
                m_Impl->RemoveProjectTree(transactionRelative / "staging");
                if (m_Impl->ProjectFiles->Exists(transactionRelative / "before"))
                    m_Impl->RemoveProjectTree(transactionRelative / "before");
            }
            catch (const std::exception& error)
            {
                KEIRE_CORE_WARN("Could not retire committed external import staging data: {}", error.what());
            }
            return result;
        }
        catch (...)
        {
            const auto failure = std::current_exception();
            stagingFiles.reset();
            bool rollbackCompleted = !publicationStarted;
            if (publicationStarted)
            {
                try
                {
                    ReportOperationProgress(progress, AssetOperationPhase::RollingBack, 0, planned.size());
                }
                catch (const std::exception& error)
                {
                    KEIRE_CORE_WARN("External import rollback progress callback failed: {}", error.what());
                }
                catch (...)
                {
                    KEIRE_CORE_WARN("External import rollback progress callback failed with a non-standard error.");
                }
                try
                {
                    for (std::size_t index = planned.size(); index > 0; --index)
                    {
                        const auto& item = planned[index - 1];
                        const auto destination = ConfinedPath(m_Impl->SourceRoot, item.Destination);
                        const auto metadata = Detail::PathWithSuffix(destination, ".keiremeta");
                        if (item.Replaced)
                        {
                            m_Impl->SourceFiles->WriteFileAtomically(item.Destination, item.PreviousSource);
                            m_Impl->SourceFiles->WriteFileAtomically(
                                Detail::PathWithSuffix(item.Destination, ".keiremeta"), item.PreviousMetadata);
                        }
                        else
                        {
                            if (m_Impl->SourceFiles->Exists(item.Destination))
                                m_Impl->SourceFiles->Remove(item.Destination);
                            const auto metadataRelative = Detail::PathWithSuffix(item.Destination, ".keiremeta");
                            if (m_Impl->SourceFiles->Exists(metadataRelative))
                                m_Impl->SourceFiles->Remove(metadataRelative);
                        }
                    }
                    (void)RefreshUnlocked();
                    (void)ImportAllUnlocked(AssetImportPolicy::FailFast, {}, {});
                    rollbackCompleted = true;
                }
                catch (const std::exception& error)
                {
                    KEIRE_CORE_ERROR(
                        "External import rollback could not republish the restored runtime catalog; the recovery "
                        "journal was retained: {}",
                        error.what());
                }
                catch (...)
                {
                    KEIRE_CORE_ERROR("External import rollback could not republish the restored runtime catalog; "
                                     "the recovery journal was retained after a non-standard error.");
                }
            }
            if (rollbackCompleted)
            {
                try
                {
                    m_Impl->RemoveProjectTree(transactionRelative);
                }
                catch (const std::exception& error)
                {
                    KEIRE_CORE_WARN("Could not retire a rolled-back external import transaction: {}", error.what());
                }
            }
            std::rethrow_exception(failure);
        }
    }

    void AssetDatabase::UndoExternalImport(const ExternalAssetImportReceiptId receipt)
    {
        ApplyExternalImportReceipt(receipt, false);
    }

    void AssetDatabase::RedoExternalImport(const ExternalAssetImportReceiptId receipt)
    {
        ApplyExternalImportReceipt(receipt, true);
    }

    void AssetDatabase::ApplyExternalImportReceipt(const ExternalAssetImportReceiptId receipt, const bool applied)
    {
        std::scoped_lock operation(*m_Impl->OperationMutex);
        if (!receipt)
            throw std::invalid_argument("External import receipt is invalid.");
        const auto receiptRelative = std::filesystem::path("Library/AssetImport") / receipt.ToString();
        (void)ConfinedPath(m_Impl->Specification.ProjectRoot, receiptRelative);
        const auto manifest =
            ReadJsonFile(*m_Impl->ProjectFiles, receiptRelative / "receipt.json", 16ULL * 1024ULL * 1024U);
        if (manifest.value("schemaVersion", 0) != 1 || !manifest.contains("entries") || !manifest["entries"].is_array())
            throw std::runtime_error("External import receipt is invalid.");

        struct ReplayEntry final
        {
            std::filesystem::path Destination;
            std::filesystem::path Metadata;
            std::vector<std::byte> DesiredSource;
            std::vector<std::byte> DesiredMetadata;
            std::vector<std::byte> PreviousSource;
            std::vector<std::byte> PreviousMetadata;
            bool Desired = false;
            bool Previous = false;
        };
        std::vector<ReplayEntry> replay;
        replay.reserve(manifest["entries"].size());
        std::size_t index = 0;
        for (const auto& entry : manifest["entries"])
        {
            const auto destination =
                ConfinedPath(m_Impl->SourceRoot, Detail::PathFromUtf8(entry.at("destination").get<std::string>()));
            const auto metadata = Detail::PathWithSuffix(destination, ".keiremeta");
            const bool desired = applied || entry.value("replaced", false);
            ReplayEntry state{
                .Destination = destination,
                .Metadata = metadata,
                .Desired = desired,
                .Previous = m_Impl->SourceFiles->IsRegularFile(destination.lexically_relative(m_Impl->SourceRoot)) &&
                            m_Impl->SourceFiles->IsRegularFile(metadata.lexically_relative(m_Impl->SourceRoot))};
            if (desired)
            {
                const auto prefix = std::to_string(index) + (applied ? ".after" : ".before");
                state.DesiredSource =
                    m_Impl->ProjectFiles->Read(receiptRelative / prefix, m_Impl->Specification.MaximumSourceBytes);
                state.DesiredMetadata =
                    m_Impl->ProjectFiles->Read(receiptRelative / (prefix + ".keiremeta"), 16ULL * 1024ULL * 1024U);
            }
            if (state.Previous)
            {
                state.PreviousSource = m_Impl->SourceFiles->Read(destination.lexically_relative(m_Impl->SourceRoot),
                                                                 m_Impl->Specification.MaximumSourceBytes);
                state.PreviousMetadata =
                    m_Impl->SourceFiles->Read(metadata.lexically_relative(m_Impl->SourceRoot), 16ULL * 1024ULL * 1024U);
            }
            replay.push_back(std::move(state));
            ++index;
        }

        const auto transactionRelative = std::filesystem::path("Library/AssetImport") / AssetId::Generate().ToString();
        (void)ConfinedPath(m_Impl->Specification.ProjectRoot, transactionRelative);
        Json journal{{"schemaVersion", 1}, {"state", "staged"}, {"entries", Json::array()}};
        bool publicationStarted = false;
        try
        {
            for (index = 0; index < replay.size(); ++index)
            {
                const auto& state = replay[index];
                if (state.Previous)
                {
                    const auto prefix = std::to_string(index);
                    m_Impl->ProjectFiles->WriteFileAtomically(transactionRelative / "before" / (prefix + ".source"),
                                                              state.PreviousSource);
                    m_Impl->ProjectFiles->WriteFileAtomically(transactionRelative / "before" / (prefix + ".metadata"),
                                                              state.PreviousMetadata);
                }
                journal["entries"].push_back({{"destination", Detail::PathToUtf8(std::filesystem::relative(
                                                                  state.Destination, m_Impl->SourceRoot))},
                                              {"replaced", state.Previous}});
            }
            WriteJsonAtomically(*m_Impl->ProjectFiles, transactionRelative / "journal.json", journal);
            publicationStarted = true;
            journal["state"] = "publishing";
            WriteJsonAtomically(*m_Impl->ProjectFiles, transactionRelative / "journal.json", journal);

            for (const auto& state : replay)
            {
                if (state.Desired)
                {
                    m_Impl->SourceFiles->WriteFileAtomically(state.Destination.lexically_relative(m_Impl->SourceRoot),
                                                             state.DesiredSource);
                    m_Impl->SourceFiles->WriteFileAtomically(state.Metadata.lexically_relative(m_Impl->SourceRoot),
                                                             state.DesiredMetadata);
                }
                else
                {
                    const auto destinationRelative = state.Destination.lexically_relative(m_Impl->SourceRoot);
                    const auto metadataRelative = state.Metadata.lexically_relative(m_Impl->SourceRoot);
                    if (m_Impl->SourceFiles->Exists(destinationRelative))
                        m_Impl->SourceFiles->Remove(destinationRelative);
                    if (m_Impl->SourceFiles->Exists(metadataRelative))
                        m_Impl->SourceFiles->Remove(metadataRelative);
                }
            }
            (void)RefreshUnlocked();
            (void)ImportAllUnlocked(AssetImportPolicy::FailFast, {}, {});
            journal["state"] = "committed";
            WriteJsonAtomically(*m_Impl->ProjectFiles, transactionRelative / "journal.json", journal);
            try
            {
                m_Impl->RemoveProjectTree(transactionRelative);
            }
            catch (const std::exception& error)
            {
                KEIRE_CORE_WARN("Could not retire a committed external import replay transaction: {}", error.what());
            }
        }
        catch (...)
        {
            const auto failure = std::current_exception();
            bool rollbackCompleted = !publicationStarted;
            if (publicationStarted)
            {
                try
                {
                    for (auto iterator = replay.rbegin(); iterator != replay.rend(); ++iterator)
                    {
                        if (iterator->Previous)
                        {
                            m_Impl->SourceFiles->WriteFileAtomically(
                                iterator->Destination.lexically_relative(m_Impl->SourceRoot), iterator->PreviousSource);
                            m_Impl->SourceFiles->WriteFileAtomically(
                                iterator->Metadata.lexically_relative(m_Impl->SourceRoot), iterator->PreviousMetadata);
                        }
                        else
                        {
                            const auto destinationRelative =
                                iterator->Destination.lexically_relative(m_Impl->SourceRoot);
                            const auto metadataRelative = iterator->Metadata.lexically_relative(m_Impl->SourceRoot);
                            if (m_Impl->SourceFiles->Exists(destinationRelative))
                                m_Impl->SourceFiles->Remove(destinationRelative);
                            if (m_Impl->SourceFiles->Exists(metadataRelative))
                                m_Impl->SourceFiles->Remove(metadataRelative);
                        }
                    }
                    (void)RefreshUnlocked();
                    (void)ImportAllUnlocked(AssetImportPolicy::FailFast, {}, {});
                    rollbackCompleted = true;
                }
                catch (const std::exception& error)
                {
                    KEIRE_CORE_ERROR("External import replay rollback could not republish the restored runtime "
                                     "catalog; the recovery journal was retained: {}",
                                     error.what());
                }
                catch (...)
                {
                    KEIRE_CORE_ERROR("External import replay rollback could not republish the restored runtime "
                                     "catalog; the recovery journal was retained after a non-standard error.");
                }
            }
            if (rollbackCompleted)
            {
                try
                {
                    m_Impl->RemoveProjectTree(transactionRelative);
                }
                catch (const std::exception& error)
                {
                    KEIRE_CORE_WARN("Could not retire a rolled-back external import replay transaction: {}",
                                    error.what());
                }
            }
            std::rethrow_exception(failure);
        }
    }

} // namespace Keire
