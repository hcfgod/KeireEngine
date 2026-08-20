#include "KeireInternal/Assets/AssetDatabaseImplementation.h"

namespace Keire
{
    ExternalAssetImportResult AssetDatabase::ImportExternal(const std::span<const ExternalAssetImportItem> items,
                                                            const std::stop_token cancellation,
                                                            AssetOperationProgressCallback progress)
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
        const auto transactionRoot = ConfinedPath(
            m_Impl->Specification.ProjectRoot, std::filesystem::path("Library/AssetImport") / receiptValue.ToString());
        const auto stagingRoot = transactionRoot / "staging";
        Json journal{{"schemaVersion", 1}, {"state", "staged"}, {"entries", Json::array()}};
        bool publicationStarted = false;
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
                        destination = parent / Detail::PathFromUtf8(stem + " " + std::to_string(copy) + extension);
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
                    item.PreviousSource = ReadSource(destinationPath, m_Impl->Specification.MaximumSourceBytes);
                    item.PreviousMetadata =
                        ReadSource(Detail::PathWithSuffix(destinationPath, ".keiremeta"), 16U * 1024U * 1024U);
                }
            }
            std::erase_if(planned, [](const PlannedItem& item) { return !item.Id; });
            if (planned.empty())
                throw std::invalid_argument("Every external import item was skipped.");

            std::filesystem::create_directories(stagingRoot);
            ReportOperationProgress(progress, AssetOperationPhase::Staging, 0, planned.size());
            for (std::size_t index = 0; index < planned.size(); ++index)
            {
                throwIfCancelled();
                auto& item = planned[index];
                const auto stagedSource = ConfinedPath(stagingRoot, item.Destination);
                const auto stagedMetadata = Detail::PathWithSuffix(stagedSource, ".keiremeta");
                WriteFileAtomically(stagedSource, item.SourceBytes);
                WriteMetadata(stagedMetadata, item.Id, item.Importer->Type, item.Importer->Name, item.Importer->Version,
                              item.Settings);
                const auto stagedRecord = ReadMetadata(stagingRoot, stagedSource,
                                                       m_Impl->Specification.MaximumSourceBytes, true, item.Importer);
                item.Validated = m_Impl->ImportSource(stagedRecord, item.SourceBytes);
                UpdateMetadataImportOutput(stagedMetadata, item.Validated.PrimaryType.value_or(stagedRecord.Type),
                                           item.Validated.SubAssets);
                if (item.Replaced)
                {
                    const auto prefix = std::to_string(index);
                    WriteFileAtomically(transactionRoot / "before" / (prefix + ".source"), item.PreviousSource);
                    WriteFileAtomically(transactionRoot / "before" / (prefix + ".metadata"), item.PreviousMetadata);
                }
                journal["entries"].push_back(
                    {{"destination", Detail::PathToUtf8(item.Destination)}, {"replaced", item.Replaced}});
                ReportOperationProgress(progress, AssetOperationPhase::Staging, index + 1, planned.size(), item.Source);
            }
            WriteJsonAtomically(transactionRoot / "journal.json", journal);
            throwIfCancelled();

            publicationStarted = true;
            journal["state"] = "publishing";
            WriteJsonAtomically(transactionRoot / "journal.json", journal);
            ReportOperationProgress(progress, AssetOperationPhase::Publishing, 0, planned.size());
            for (std::size_t index = 0; index < planned.size(); ++index)
            {
                const auto& item = planned[index];
                const auto destination = ConfinedPath(m_Impl->SourceRoot, item.Destination);
                WriteFileAtomically(destination, item.SourceBytes);
                WriteFileAtomically(
                    Detail::PathWithSuffix(destination, ".keiremeta"),
                    ReadSource(Detail::PathWithSuffix(ConfinedPath(stagingRoot, item.Destination), ".keiremeta"),
                               16U * 1024U * 1024U));
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

            Json receipt{{"schemaVersion", 1}, {"entries", Json::array()}};
            for (std::size_t index = 0; index < planned.size(); ++index)
            {
                const auto& item = planned[index];
                const auto prefix = std::to_string(index);
                WriteFileAtomically(transactionRoot / (prefix + ".after"), item.SourceBytes);
                WriteFileAtomically(
                    transactionRoot / (prefix + ".after.keiremeta"),
                    ReadSource(Detail::PathWithSuffix(ConfinedPath(stagingRoot, item.Destination), ".keiremeta"),
                               16U * 1024U * 1024U));
                if (item.Replaced)
                {
                    WriteFileAtomically(transactionRoot / (prefix + ".before"), item.PreviousSource);
                    WriteFileAtomically(transactionRoot / (prefix + ".before.keiremeta"), item.PreviousMetadata);
                }
                receipt["entries"].push_back(
                    {{"destination", Detail::PathToUtf8(item.Destination)}, {"replaced", item.Replaced}});
                result.Entries.push_back({item.Id, item.Source, item.Destination, item.Replaced});
            }
            WriteJsonAtomically(transactionRoot / "receipt.json", receipt);
            journal["state"] = "committed";
            WriteJsonAtomically(transactionRoot / "journal.json", journal);
            RemovePathNoThrow(stagingRoot);
            RemovePathNoThrow(transactionRoot / "before");
            result.Receipt = ExternalAssetImportReceiptId(receiptValue);
            ReportOperationProgress(progress, AssetOperationPhase::Completed, planned.size(), planned.size());
            return result;
        }
        catch (...)
        {
            if (publicationStarted)
            {
                try
                {
                    ReportOperationProgress(progress, AssetOperationPhase::RollingBack, 0, planned.size());
                    for (std::size_t index = planned.size(); index > 0; --index)
                    {
                        const auto& item = planned[index - 1];
                        const auto destination = ConfinedPath(m_Impl->SourceRoot, item.Destination);
                        const auto metadata = Detail::PathWithSuffix(destination, ".keiremeta");
                        if (item.Replaced)
                        {
                            WriteFileAtomically(destination, item.PreviousSource);
                            WriteFileAtomically(metadata, item.PreviousMetadata);
                        }
                        else
                        {
                            RemovePathNoThrow(destination);
                            RemovePathNoThrow(metadata);
                        }
                    }
                    (void)RefreshUnlocked();
                }
                catch (...)
                {
                }
            }
            RemovePathNoThrow(transactionRoot);
            throw;
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
        const auto receiptRoot = ConfinedPath(m_Impl->Specification.ProjectRoot,
                                              std::filesystem::path("Library/AssetImport") / receipt.ToString());
        const auto manifest = ReadJsonFile(receiptRoot / "receipt.json", 16U * 1024U * 1024U);
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
            ReplayEntry state{.Destination = destination,
                              .Metadata = metadata,
                              .Desired = desired,
                              .Previous = std::filesystem::is_regular_file(destination) &&
                                          std::filesystem::is_regular_file(metadata)};
            if (desired)
            {
                const auto prefix = std::to_string(index) + (applied ? ".after" : ".before");
                state.DesiredSource = ReadSource(receiptRoot / prefix, m_Impl->Specification.MaximumSourceBytes);
                state.DesiredMetadata = ReadSource(receiptRoot / (prefix + ".keiremeta"), 16U * 1024U * 1024U);
            }
            if (state.Previous)
            {
                state.PreviousSource = ReadSource(destination, m_Impl->Specification.MaximumSourceBytes);
                state.PreviousMetadata = ReadSource(metadata, 16U * 1024U * 1024U);
            }
            replay.push_back(std::move(state));
            ++index;
        }

        const auto transactionRoot =
            ConfinedPath(m_Impl->Specification.ProjectRoot,
                         std::filesystem::path("Library/AssetImport") / AssetId::Generate().ToString());
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
                    WriteFileAtomically(transactionRoot / "before" / (prefix + ".source"), state.PreviousSource);
                    WriteFileAtomically(transactionRoot / "before" / (prefix + ".metadata"), state.PreviousMetadata);
                }
                journal["entries"].push_back({{"destination", Detail::PathToUtf8(std::filesystem::relative(
                                                                  state.Destination, m_Impl->SourceRoot))},
                                              {"replaced", state.Previous}});
            }
            WriteJsonAtomically(transactionRoot / "journal.json", journal);
            publicationStarted = true;
            journal["state"] = "publishing";
            WriteJsonAtomically(transactionRoot / "journal.json", journal);

            for (const auto& state : replay)
            {
                if (state.Desired)
                {
                    WriteFileAtomically(state.Destination, state.DesiredSource);
                    WriteFileAtomically(state.Metadata, state.DesiredMetadata);
                }
                else
                {
                    RemovePathNoThrow(state.Destination);
                    RemovePathNoThrow(state.Metadata);
                }
            }
            (void)RefreshUnlocked();
            (void)ImportAllUnlocked(AssetImportPolicy::FailFast, {}, {});
            RemovePathNoThrow(transactionRoot);
        }
        catch (...)
        {
            if (publicationStarted)
            {
                for (auto iterator = replay.rbegin(); iterator != replay.rend(); ++iterator)
                {
                    if (iterator->Previous)
                    {
                        WriteFileAtomically(iterator->Destination, iterator->PreviousSource);
                        WriteFileAtomically(iterator->Metadata, iterator->PreviousMetadata);
                    }
                    else
                    {
                        RemovePathNoThrow(iterator->Destination);
                        RemovePathNoThrow(iterator->Metadata);
                    }
                }
                (void)RefreshUnlocked();
            }
            RemovePathNoThrow(transactionRoot);
            throw;
        }
    }

} // namespace Keire
