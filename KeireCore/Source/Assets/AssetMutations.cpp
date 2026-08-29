#include "KeireInternal/Assets/AssetDatabaseImplementation.h"

#include <algorithm>
#include <unordered_map>

namespace Keire
{
    void AssetDatabase::CreateFolder(const std::filesystem::path& relativePath)
    {
        std::scoped_lock operation(*m_Impl->OperationMutex);
        (void)ConfinedPath(m_Impl->SourceRoot, relativePath);
        m_Impl->SourceFiles->CreateDirectories(relativePath);
    }

    AssetId AssetDatabase::CreateAsset(const std::filesystem::path& relativePath,
                                       const AssetImporterRegistration& importer,
                                       const std::span<const std::byte> sourceBytes,
                                       const AssetImportSettings& requestedSettings, const AssetId parentSource)
    {
        std::scoped_lock operation(*m_Impl->OperationMutex);
        return CreateAssetUnlocked(relativePath, importer, sourceBytes, requestedSettings, parentSource);
    }

    AssetId AssetDatabase::CreateAssetUnlocked(const std::filesystem::path& relativePath,
                                               const AssetImporterRegistration& importer,
                                               const std::span<const std::byte> sourceBytes,
                                               const AssetImportSettings& requestedSettings, const AssetId parentSource)
    {
        const auto registered = m_Impl->Importers.find(importer.Name);
        if (registered == m_Impl->Importers.end() || registered->second.Version != importer.Version ||
            registered->second.Type != importer.Type)
            throw std::invalid_argument("Asset creation requires an importer registered with this database.");
        const auto destination = ConfinedPath(m_Impl->SourceRoot, relativePath);
        const auto metadata = ConfinedMetadataPath(m_Impl->SourceRoot, relativePath);
        const auto metadataRelative = Detail::PathWithSuffix(relativePath, ".keiremeta");
        if (m_Impl->SourceFiles->Exists(relativePath) || m_Impl->SourceFiles->Exists(metadataRelative))
            throw std::runtime_error("Asset creation destination already exists.");
        const auto extension = LowerExtension(destination);
        if (std::ranges::find(importer.Extensions, extension) == importer.Extensions.end())
            throw std::invalid_argument("Asset creation path does not use an importer-supported extension.");
        if (sourceBytes.size() > m_Impl->Specification.MaximumSourceBytes)
            throw std::invalid_argument("Asset creation source exceeds the configured maximum size.");
        const auto relativeDestination = std::filesystem::relative(destination, m_Impl->SourceRoot).lexically_normal();
        if (parentSource)
        {
            const auto parent = Find(parentSource);
            if (!parent)
                throw std::invalid_argument("Asset creation parent must be a live source asset.");
            if (parent->RelativePath.parent_path() != relativeDestination.parent_path())
                throw std::invalid_argument("Asset creation parent must be in the same source folder.");
        }
        const auto settings = m_Impl->NormalizeSettings(registered->second, requestedSettings);
        const auto id = AssetId::Generate();
        AssetSourceRecord validationRecord;
        validationRecord.Id = id;
        validationRecord.Type = importer.Type;
        validationRecord.RelativePath = relativeDestination;
        validationRecord.Importer = importer.Name;
        validationRecord.ImporterVersion = importer.Version;
        validationRecord.ImportSettings = settings;
        validationRecord.ParentSource = parentSource;
        const auto validationMetadata = Detail::PathWithSuffix(metadata, ".validate.tmp");
        validationRecord.MetadataPath = validationMetadata;
        WriteMetadata(*m_Impl->SourceFiles, validationMetadata.lexically_relative(m_Impl->SourceRoot), id,
                      importer.Type, importer.Name, importer.Version, settings, parentSource);
        AssetImportOutput validated;
        try
        {
            validated = m_Impl->ImportSource(validationRecord, sourceBytes);
        }
        catch (...)
        {
            try
            {
                m_Impl->SourceFiles->Remove(validationMetadata.lexically_relative(m_Impl->SourceRoot));
            }
            catch (...)
            {
            }
            throw;
        }
        m_Impl->SourceFiles->Remove(validationMetadata.lexically_relative(m_Impl->SourceRoot));
        try
        {
            m_Impl->SourceFiles->WriteFileAtomically(relativeDestination, sourceBytes);
            WriteMetadata(*m_Impl->SourceFiles, metadata.lexically_relative(m_Impl->SourceRoot), id, importer.Type,
                          importer.Name, importer.Version, settings, parentSource);
            UpdateMetadataImportOutput(*m_Impl->SourceFiles, metadata.lexically_relative(m_Impl->SourceRoot),
                                       validated.PrimaryType.value_or(importer.Type), validated.SubAssets);
        }
        catch (...)
        {
            try
            {
                m_Impl->SourceFiles->Remove(relativeDestination);
                m_Impl->SourceFiles->Remove(metadataRelative);
            }
            catch (...)
            {
            }
            throw;
        }
        AssetSourceRecord record;
        try
        {
            record = ReadMetadata(*m_Impl->SourceFiles, relativeDestination, m_Impl->Specification.MaximumSourceBytes,
                                  true, &registered->second);
            m_Impl->PublishRecord(record, m_Impl->ReadSignature(destination, metadata));
        }
        catch (...)
        {
            try
            {
                m_Impl->SourceFiles->Remove(relativeDestination);
                m_Impl->SourceFiles->Remove(metadataRelative);
            }
            catch (...)
            {
            }
            throw;
        }
        m_Impl->StoreValidatedImport(record, std::move(validated));
        return id;
    }

    void AssetDatabase::ReplaceAssetSource(const AssetId id, const std::span<const std::byte> sourceBytes)
    {
        std::scoped_lock operation(*m_Impl->OperationMutex);
        const auto existing = Find(id);
        if (!existing)
            throw std::invalid_argument("Asset source replacement requires a live source asset.");
        if (sourceBytes.size() > m_Impl->Specification.MaximumSourceBytes)
            throw std::invalid_argument("Asset replacement source exceeds the configured maximum size.");
        const auto importer = m_Impl->Importers.find(existing->Importer);
        if (importer == m_Impl->Importers.end() || importer->second.Type != existing->Type ||
            importer->second.Version != existing->ImporterVersion)
            throw std::runtime_error("Asset source replacement requires the source asset's registered importer.");

        const auto destination = ConfinedPath(m_Impl->SourceRoot, existing->RelativePath);
        const auto metadata = ConfinedMetadataPath(m_Impl->SourceRoot, existing->RelativePath);
        const auto originalSource =
            m_Impl->SourceFiles->Read(existing->RelativePath, m_Impl->Specification.MaximumSourceBytes);
        const auto metadataRelative = Detail::PathWithSuffix(existing->RelativePath, ".keiremeta");
        const auto originalMetadata = m_Impl->SourceFiles->Read(metadataRelative, 16ULL * 1024ULL * 1024U);
        auto originalImport = m_Impl->Import(*existing);
        auto validated = m_Impl->ImportSource(*existing, sourceBytes);

        try
        {
            m_Impl->SourceFiles->WriteFileAtomically(existing->RelativePath, sourceBytes);
            UpdateMetadataImportOutput(*m_Impl->SourceFiles, metadata.lexically_relative(m_Impl->SourceRoot),
                                       validated.PrimaryType.value_or(existing->Type), validated.SubAssets);
            auto replacement = ReadMetadata(*m_Impl->SourceFiles, existing->RelativePath,
                                            m_Impl->Specification.MaximumSourceBytes, true, &importer->second);
            if (replacement.Id != id)
                throw std::runtime_error("Asset source replacement changed the stable asset identity.");
            m_Impl->PublishRecord(replacement, m_Impl->ReadSignature(destination, metadata));
            m_Impl->StoreValidatedImport(replacement, std::move(validated));
        }
        catch (...)
        {
            const auto failure = std::current_exception();
            try
            {
                m_Impl->SourceFiles->WriteFileAtomically(existing->RelativePath, originalSource);
                m_Impl->SourceFiles->WriteFileAtomically(metadataRelative, originalMetadata);
                auto restored = ReadMetadata(*m_Impl->SourceFiles, existing->RelativePath,
                                             m_Impl->Specification.MaximumSourceBytes, true, &importer->second);
                m_Impl->PublishRecord(restored, m_Impl->ReadSignature(destination, metadata));
                m_Impl->StoreValidatedImport(restored, std::move(originalImport));
            }
            catch (...)
            {
            }
            std::rethrow_exception(failure);
        }
    }

    void AssetDatabase::SetImportSettings(const AssetId id, const AssetImportSettings& requestedSettings)
    {
        std::scoped_lock operation(*m_Impl->OperationMutex);
        const auto existing = Find(id);
        if (!existing)
            throw std::invalid_argument("Import settings require a live source asset.");
        const auto importer = m_Impl->Importers.find(existing->Importer);
        if (importer == m_Impl->Importers.end() || importer->second.Type != existing->Type ||
            importer->second.Version != existing->ImporterVersion)
        {
            throw std::runtime_error("Import settings require the source asset's registered importer.");
        }

        const auto settings = m_Impl->NormalizeSettings(importer->second, requestedSettings);
        if (settings == existing->ImportSettings)
            return;
        const auto source = ConfinedPath(m_Impl->SourceRoot, existing->RelativePath);
        const auto metadata = ConfinedMetadataPath(m_Impl->SourceRoot, existing->RelativePath);
        const auto metadataRelative = Detail::PathWithSuffix(existing->RelativePath, ".keiremeta");
        const auto originalMetadata = m_Impl->SourceFiles->Read(metadataRelative, 16ULL * 1024ULL * 1024U);
        try
        {
            UpdateMetadataImportSettings(*m_Impl->SourceFiles, metadata.lexically_relative(m_Impl->SourceRoot),
                                         settings);
            auto replacement = ReadMetadata(*m_Impl->SourceFiles, existing->RelativePath,
                                            m_Impl->Specification.MaximumSourceBytes, true, &importer->second);
            if (replacement.Id != id)
                throw std::runtime_error("Import settings changed the stable asset identity.");
            m_Impl->PublishRecord(replacement, m_Impl->ReadSignature(source, metadata));
        }
        catch (...)
        {
            const auto failure = std::current_exception();
            try
            {
                m_Impl->SourceFiles->WriteFileAtomically(metadataRelative, originalMetadata);
                m_Impl->PublishRecord(*existing, m_Impl->ReadSignature(source, metadata));
            }
            catch (...)
            {
            }
            std::rethrow_exception(failure);
        }
    }

    void AssetDatabase::RequestReimport(const AssetId id)
    {
        std::scoped_lock operation(*m_Impl->OperationMutex);
        const auto existing = Find(id);
        if (!existing)
            throw std::invalid_argument("Reimport requires a live source asset.");
        const auto importer = m_Impl->Importers.find(existing->Importer);
        if (importer == m_Impl->Importers.end() || importer->second.Type != existing->Type)
            throw std::runtime_error("Reimport requires the source asset's registered importer.");

        const auto source = ConfinedPath(m_Impl->SourceRoot, existing->RelativePath);
        const auto metadata = ConfinedMetadataPath(m_Impl->SourceRoot, existing->RelativePath);
        const auto metadataRelative = Detail::PathWithSuffix(existing->RelativePath, ".keiremeta");
        const auto originalMetadata = m_Impl->SourceFiles->Read(metadataRelative, 16ULL * 1024ULL * 1024U);
        try
        {
            IncrementMetadataImportRevision(*m_Impl->SourceFiles, metadata.lexically_relative(m_Impl->SourceRoot));
            auto replacement = ReadMetadata(*m_Impl->SourceFiles, existing->RelativePath,
                                            m_Impl->Specification.MaximumSourceBytes, true, &importer->second);
            if (replacement.Id != id)
                throw std::runtime_error("Reimport invalidation changed the stable asset identity.");
            m_Impl->PublishRecord(replacement, m_Impl->ReadSignature(source, metadata));
        }
        catch (...)
        {
            const auto failure = std::current_exception();
            try
            {
                m_Impl->SourceFiles->WriteFileAtomically(metadataRelative, originalMetadata);
                m_Impl->PublishRecord(*existing, m_Impl->ReadSignature(source, metadata));
            }
            catch (...)
            {
            }
            std::rethrow_exception(failure);
        }
    }

    AssetId AssetDatabase::ExtractMaterial(const AssetId model, const AssetId generatedMaterial,
                                           const std::filesystem::path& relativePath)
    {
        std::scoped_lock operation(*m_Impl->OperationMutex);
        const auto record = Find(model);
        if (!record || record->Type != MeshAsset::StaticType())
            throw std::invalid_argument("Material extraction requires a model asset.");
        const auto imported = m_Impl->Import(*record);
        const auto generated = std::ranges::find(imported.SubAssets, generatedMaterial, &AssetGeneratedSubAsset::Id);
        if (generated == imported.SubAssets.end() || generated->Type != MaterialAsset::StaticType())
            throw std::invalid_argument("The selected model does not contain that generated material.");
        const auto definition = MaterialAsset::Decode(generated->Bytes)->Definition();
        const auto materialImporter = m_Impl->Importers.find("Keire.Material");
        if (materialImporter == m_Impl->Importers.end())
            throw std::logic_error("Material extraction requires the Kéire material importer.");
        const auto source = MaterialAsset::EncodeSource(definition);
        return CreateAssetUnlocked(relativePath, materialImporter->second, source, {}, {});
    }

    std::vector<AssetId> AssetDatabase::ExtractMaterials(const AssetId model,
                                                         const std::filesystem::path& relativeDirectory)
    {
        std::scoped_lock operation(*m_Impl->OperationMutex);
        const auto record = Find(model);
        if (!record || record->Type != MeshAsset::StaticType())
            throw std::invalid_argument("Material extraction requires a model asset.");
        const auto imported = m_Impl->Import(*record);
        const auto mesh = MeshAsset::Decode(imported.Bytes);
        const auto materialImporter = m_Impl->Importers.find("Keire.Material");
        if (materialImporter == m_Impl->Importers.end())
            throw std::logic_error("Material extraction requires the Kéire material importer.");

        std::vector<AssetId> result;
        std::vector<std::filesystem::path> createdPaths;
        std::unordered_set<AssetId> extracted;
        const auto destinationExists = [this](const std::filesystem::path& relative)
        {
            const auto destination = ConfinedPath(m_Impl->SourceRoot, relative);
            return m_Impl->SourceFiles->Exists(relative) ||
                   m_Impl->SourceFiles->Exists(Detail::PathWithSuffix(relative, ".keiremeta"));
        };
        try
        {
            for (const auto& slot : mesh->MaterialSlots())
            {
                if (!slot.DefaultMaterial || !extracted.insert(slot.DefaultMaterial).second)
                    continue;
                const auto generated =
                    std::ranges::find(imported.SubAssets, slot.DefaultMaterial, &AssetGeneratedSubAsset::Id);
                if (generated == imported.SubAssets.end() || generated->Type != MaterialAsset::StaticType())
                    continue;
                std::string name = generated->Name.empty() ? slot.Name : generated->Name;
                for (char& character : name)
                    if (static_cast<unsigned char>(character) < 32U ||
                        std::string_view("<>:\"/\\|?*").find(character) != std::string_view::npos)
                        character = '_';
                while (!name.empty() && (name.back() == ' ' || name.back() == '.'))
                    name.pop_back();
                if (name.empty() || name == "." || name == "..")
                    name = "Material";
                auto destination = relativeDirectory / (name + ".keirematerial");
                for (std::size_t copy = 2; destinationExists(destination); ++copy)
                    destination = relativeDirectory / (name + " " + std::to_string(copy) + ".keirematerial");
                const auto definition = MaterialAsset::Decode(generated->Bytes)->Definition();
                const auto source = MaterialAsset::EncodeSource(definition);
                result.push_back(CreateAssetUnlocked(destination, materialImporter->second, source, {}, {}));
                createdPaths.push_back(destination);
            }
        }
        catch (...)
        {
            for (const auto& path : createdPaths)
            {
                try
                {
                    (void)ConfinedPath(m_Impl->SourceRoot, path);
                    m_Impl->SourceFiles->Remove(path);
                    m_Impl->SourceFiles->Remove(Detail::PathWithSuffix(path, ".keiremeta"));
                }
                catch (...)
                {
                }
            }
            (void)RefreshUnlocked();
            throw;
        }
        if (result.empty())
            throw std::runtime_error("The model does not contain extractable generated materials.");
        return result;
    }

    void AssetDatabase::Rename(const AssetId id, const std::string& newName)
    {
        std::scoped_lock operation(*m_Impl->OperationMutex);
        if (newName.empty() || newName == "." || newName == ".." || newName.find('/') != std::string::npos ||
            newName.find('\\') != std::string::npos)
            throw std::invalid_argument("Asset name must be one non-empty path component.");
        const auto record = Find(id);
        if (!record)
            throw std::invalid_argument("Cannot rename an unknown asset ID.");
        MoveAssetUnlocked(id, record->RelativePath.parent_path() / newName);
    }

    void AssetDatabase::MoveAsset(const AssetId id, const std::filesystem::path& relativeDestination)
    {
        std::scoped_lock operation(*m_Impl->OperationMutex);
        MoveAssetUnlocked(id, relativeDestination);
    }

    void AssetDatabase::MoveAssetUnlocked(const AssetId id, const std::filesystem::path& relativeDestination)
    {
        const auto record = Find(id);
        if (!record)
            throw std::invalid_argument("Cannot move an unknown asset ID.");
        const auto source = ConfinedPath(m_Impl->SourceRoot, record->RelativePath);
        const auto destination = ConfinedPath(m_Impl->SourceRoot, relativeDestination);
        if (source == destination)
            return;
        const auto sourceMetadata = ConfinedMetadataPath(m_Impl->SourceRoot, record->RelativePath);
        const auto destinationMetadata = ConfinedMetadataPath(m_Impl->SourceRoot, relativeDestination);
        if (m_Impl->SourceFiles->Exists(relativeDestination) ||
            m_Impl->SourceFiles->Exists(Detail::PathWithSuffix(relativeDestination, ".keiremeta")))
            throw std::runtime_error("Asset move destination already exists.");
        m_Impl->SourceFiles->Rename(record->RelativePath, relativeDestination);
        try
        {
            m_Impl->SourceFiles->Rename(Detail::PathWithSuffix(record->RelativePath, ".keiremeta"),
                                        Detail::PathWithSuffix(relativeDestination, ".keiremeta"));
        }
        catch (...)
        {
            m_Impl->SourceFiles->Rename(relativeDestination, record->RelativePath);
            throw;
        }
        try
        {
            auto moved = *record;
            moved.RelativePath = std::filesystem::relative(destination, m_Impl->SourceRoot).lexically_normal();
            moved.MetadataPath = destinationMetadata;
            m_Impl->PublishRecord(std::move(moved), m_Impl->ReadSignature(destination, destinationMetadata));
        }
        catch (...)
        {
            m_Impl->SourceFiles->Rename(Detail::PathWithSuffix(relativeDestination, ".keiremeta"),
                                        Detail::PathWithSuffix(record->RelativePath, ".keiremeta"));
            m_Impl->SourceFiles->Rename(relativeDestination, record->RelativePath);
            throw;
        }
    }

    AssetId AssetDatabase::Duplicate(const AssetId id, const std::filesystem::path& destination)
    {
        std::scoped_lock operation(*m_Impl->OperationMutex);
        const auto record = Find(id);
        if (!record)
            throw std::invalid_argument("Cannot duplicate an unknown asset ID.");
        const auto source = ConfinedPath(m_Impl->SourceRoot, record->RelativePath);
        const auto target = ConfinedPath(m_Impl->SourceRoot, destination);
        const auto metadata = ConfinedMetadataPath(m_Impl->SourceRoot, destination);
        if (m_Impl->SourceFiles->Exists(destination) ||
            m_Impl->SourceFiles->Exists(Detail::PathWithSuffix(destination, ".keiremeta")))
            throw std::runtime_error("Asset duplicate destination already exists.");
        m_Impl->SourceFiles->Copy(record->RelativePath, destination);
        const auto newId = AssetId::Generate();
        try
        {
            const auto duplicateParent =
                record->RelativePath.parent_path() == destination.parent_path() ? record->ParentSource : AssetId{};
            WriteMetadata(*m_Impl->SourceFiles, metadata.lexically_relative(m_Impl->SourceRoot), newId, record->Type,
                          record->Importer, record->ImporterVersion, record->ImportSettings, duplicateParent);
        }
        catch (...)
        {
            try
            {
                m_Impl->SourceFiles->Remove(destination);
            }
            catch (...)
            {
            }
            throw;
        }
        (void)RefreshUnlocked();
        return newId;
    }

    void AssetDatabase::MoveFolder(const std::filesystem::path& relativeSource,
                                   const std::filesystem::path& relativeDestination)
    {
        std::scoped_lock operation(*m_Impl->OperationMutex);
        const auto source = ConfinedPath(m_Impl->SourceRoot, relativeSource);
        const auto destination = ConfinedPath(m_Impl->SourceRoot, relativeDestination);
        if (source == destination)
            return;
        if (!std::filesystem::is_directory(source) || std::filesystem::is_symlink(source))
            throw std::invalid_argument("Asset folder move requires a regular source directory.");
        if (IsSameOrWithin(source, destination))
            throw std::invalid_argument("An asset folder cannot be moved into itself.");
        if (m_Impl->SourceFiles->Exists(relativeDestination))
            throw std::runtime_error("Asset folder move destination already exists.");
        const auto sourceRelative = std::filesystem::relative(source, m_Impl->SourceRoot).lexically_normal();
        const auto destinationRelative = std::filesystem::relative(destination, m_Impl->SourceRoot).lexically_normal();
        auto candidate = Records();
        std::vector<std::size_t> affected;
        for (std::size_t index = 0; index < candidate.size(); ++index)
        {
            const auto& record = candidate[index];
            if (IsSameOrWithin(sourceRelative, record.RelativePath))
                affected.push_back(index);
        }
        m_Impl->SourceFiles->Rename(sourceRelative, destinationRelative);
        try
        {
            std::unordered_map<AssetId, FileSignature> updatedSignatures;
            updatedSignatures.reserve(affected.size());
            for (const auto index : affected)
            {
                auto& moved = candidate[index];
                moved.RelativePath =
                    (destinationRelative / moved.RelativePath.lexically_relative(sourceRelative)).lexically_normal();
                const auto movedSource = ConfinedPath(m_Impl->SourceRoot, moved.RelativePath);
                const auto movedMetadata = ConfinedMetadataPath(m_Impl->SourceRoot, moved.RelativePath);
                moved.MetadataPath = movedMetadata;
                updatedSignatures.emplace(moved.Id, m_Impl->ReadSignature(movedSource, movedMetadata));
            }
            {
                std::scoped_lock lock(m_Impl->Mutex);
                auto observed = m_Impl->Observed;
                for (const auto& [id, signature] : updatedSignatures)
                    observed.insert_or_assign(id, signature);
                m_Impl->ReplaceRecords(std::move(candidate));
                m_Impl->Observed = std::move(observed);
                for (const auto& [id, signature] : updatedSignatures)
                {
                    (void)signature;
                    m_Impl->PendingChanges.erase(id);
                }
                m_Impl->SourceRevision.fetch_add(1, std::memory_order_release);
            }
            m_Impl->RequestChangeMonitorScan();
        }
        catch (...)
        {
            const auto failure = std::current_exception();
            try
            {
                m_Impl->SourceFiles->Rename(destinationRelative, sourceRelative);
            }
            catch (...)
            {
            }
            std::rethrow_exception(failure);
        }
    }

    std::vector<AssetId> AssetDatabase::DuplicateFolder(const std::filesystem::path& relativeSource,
                                                        const std::filesystem::path& relativeDestination)
    {
        std::scoped_lock operation(*m_Impl->OperationMutex);
        const auto source = ConfinedPath(m_Impl->SourceRoot, relativeSource);
        const auto destination = ConfinedPath(m_Impl->SourceRoot, relativeDestination);
        if (!std::filesystem::is_directory(source) || std::filesystem::is_symlink(source))
            throw std::invalid_argument("Asset folder duplication requires a regular source directory.");
        if (IsSameOrWithin(source, destination))
            throw std::invalid_argument("An asset folder cannot be duplicated into itself.");
        if (m_Impl->SourceFiles->Exists(relativeDestination))
            throw std::runtime_error("Asset folder duplicate destination already exists.");
        try
        {
            std::error_code error;
            m_Impl->SourceFiles->CreateDirectories(relativeDestination);
            for (std::filesystem::recursive_directory_iterator iterator(source, error), end; !error && iterator != end;
                 iterator.increment(error))
            {
                if (iterator->is_symlink())
                    throw std::runtime_error("Asset folders containing symbolic links cannot be duplicated.");
                const auto relative = iterator->path().lexically_relative(source);
                const auto sourceEntry = relativeSource / relative;
                const auto destinationEntry = relativeDestination / relative;
                if (iterator->is_directory())
                    m_Impl->SourceFiles->CreateDirectories(destinationEntry);
                else if (iterator->is_regular_file() && iterator->path().extension() != ".keiremeta")
                    m_Impl->SourceFiles->Copy(sourceEntry, destinationEntry);
            }
            if (error)
                throw std::runtime_error("Could not enumerate the duplicated asset folder.");
            (void)RefreshUnlocked();
        }
        catch (...)
        {
            try
            {
                const auto projectRelative = destination.lexically_relative(m_Impl->Specification.ProjectRoot);
                m_Impl->RemoveProjectTree(projectRelative);
            }
            catch (...)
            {
            }
            throw;
        }
        std::vector<AssetId> result;
        const auto destinationRelative = std::filesystem::relative(destination, m_Impl->SourceRoot).lexically_normal();
        for (const auto& record : Records())
            if (IsSameOrWithin(destinationRelative, record.RelativePath))
                result.push_back(record.Id);
        return result;
    }

    AssetTrashRecord AssetDatabase::TrashAsset(const AssetId id)
    {
        std::scoped_lock operation(*m_Impl->OperationMutex);
        const auto record = Find(id);
        if (!record)
            throw std::invalid_argument("Cannot trash an unknown asset ID.");
        const auto source = ConfinedPath(m_Impl->SourceRoot, record->RelativePath);
        const auto sourceMetadata = ConfinedMetadataPath(m_Impl->SourceRoot, record->RelativePath);
        const auto transaction = AssetId::Generate();
        const auto rootRelative = std::filesystem::path("Library/Trash") / transaction.ToString();
        const auto root = m_Impl->Specification.ProjectRoot / rootRelative;
        m_Impl->ProjectFiles->CreateDirectories(rootRelative);
        const auto destination = root / source.filename();
        const auto destinationMetadata = root / sourceMetadata.filename();
        m_Impl->SourceFiles->RenameTo(record->RelativePath, *m_Impl->ProjectFiles,
                                      rootRelative / record->RelativePath.filename());
        try
        {
            m_Impl->SourceFiles->RenameTo(
                Detail::PathWithSuffix(record->RelativePath, ".keiremeta"), *m_Impl->ProjectFiles,
                rootRelative / Detail::PathWithSuffix(record->RelativePath.filename(), ".keiremeta"));
            const std::array assets{id};
            WriteTrashManifest(*m_Impl->ProjectFiles, rootRelative, transaction, record->RelativePath, assets, false);
        }
        catch (...)
        {
            try
            {
                m_Impl->ProjectFiles->RenameTo(
                    rootRelative / Detail::PathWithSuffix(record->RelativePath.filename(), ".keiremeta"),
                    *m_Impl->SourceFiles, Detail::PathWithSuffix(record->RelativePath, ".keiremeta"));
            }
            catch (...)
            {
            }
            m_Impl->ProjectFiles->RenameTo(rootRelative / record->RelativePath.filename(), *m_Impl->SourceFiles,
                                           record->RelativePath);
            try
            {
                m_Impl->RemoveProjectTree(rootRelative);
            }
            catch (...)
            {
            }
            throw;
        }
        const std::array assets{id};
        m_Impl->RemoveRecords(assets);
        return {AssetTrashId(transaction), record->RelativePath, root, {id}, false};
    }

    AssetTrashRecord AssetDatabase::TrashFolder(const std::filesystem::path& relativePath)
    {
        std::scoped_lock operation(*m_Impl->OperationMutex);
        const auto source = ConfinedPath(m_Impl->SourceRoot, relativePath);
        if (!std::filesystem::is_directory(source) || std::filesystem::is_symlink(source))
            throw std::invalid_argument("Asset folder trash requires a regular source directory.");
        const auto normalized = std::filesystem::relative(source, m_Impl->SourceRoot).lexically_normal();
        std::vector<AssetId> assets;
        for (const auto& record : Records())
            if (IsSameOrWithin(normalized, record.RelativePath))
                assets.push_back(record.Id);
        const auto transaction = AssetId::Generate();
        const auto rootRelative = std::filesystem::path("Library/Trash") / transaction.ToString();
        const auto root = m_Impl->Specification.ProjectRoot / rootRelative;
        m_Impl->ProjectFiles->CreateDirectories(rootRelative);
        const auto destination = root / source.filename();
        m_Impl->SourceFiles->RenameTo(normalized, *m_Impl->ProjectFiles, rootRelative / normalized.filename());
        try
        {
            WriteTrashManifest(*m_Impl->ProjectFiles, rootRelative, transaction, normalized, assets, true);
        }
        catch (...)
        {
            m_Impl->ProjectFiles->RenameTo(rootRelative / normalized.filename(), *m_Impl->SourceFiles, normalized);
            try
            {
                m_Impl->RemoveProjectTree(rootRelative);
            }
            catch (...)
            {
            }
            throw;
        }
        m_Impl->RemoveRecords(assets);
        return {AssetTrashId(transaction), normalized, root, std::move(assets), true};
    }

    std::vector<AssetTrashRecord> AssetDatabase::TrashRecords() const
    {
        const auto trashRoot = m_Impl->Specification.ProjectRoot / "Library" / "Trash";
        std::vector<AssetTrashRecord> result;
        std::error_code error;
        for (std::filesystem::directory_iterator iterator(trashRoot, error), end; !error && iterator != end;
             iterator.increment(error))
        {
            if (!iterator->is_directory())
                continue;
            const auto relativeRoot = iterator->path().lexically_relative(m_Impl->Specification.ProjectRoot);
            const auto parsed = ReadTrashManifest(*m_Impl->ProjectFiles, relativeRoot);
            result.push_back(
                {AssetTrashId(parsed.Id), parsed.OriginalPath, iterator->path(), parsed.Assets, parsed.Folder});
        }
        if (error && error != std::errc::no_such_file_or_directory)
            throw std::runtime_error("Could not enumerate recoverable asset trash.");
        std::ranges::sort(result, {}, &AssetTrashRecord::OriginalPath);
        return result;
    }

    void AssetDatabase::RestoreTrash(const AssetTrashId id)
    {
        std::scoped_lock operation(*m_Impl->OperationMutex);
        if (!id)
            throw std::invalid_argument("Cannot restore an empty asset trash identity.");
        const auto rootRelative = std::filesystem::path("Library/Trash") / id.ToString();
        const auto root = m_Impl->Specification.ProjectRoot / rootRelative;
        const auto record = ReadTrashManifest(*m_Impl->ProjectFiles, rootRelative);
        if (record.Id.ToString() != id.ToString())
            throw std::runtime_error("Asset trash identity does not match its manifest.");
        const auto destination = ConfinedPath(m_Impl->SourceRoot, record.OriginalPath);
        const auto source = root / record.OriginalPath.filename();
        if (m_Impl->SourceFiles->Exists(record.OriginalPath))
        {
            m_Impl->RemoveProjectTree(rootRelative);
            return;
        }
        std::filesystem::path sourceMetadata;
        std::filesystem::path destinationMetadata;
        if (record.Folder)
            m_Impl->ProjectFiles->RenameTo(rootRelative / record.OriginalPath.filename(), *m_Impl->SourceFiles,
                                           record.OriginalPath);
        else
        {
            sourceMetadata = Detail::PathWithSuffix(root / record.OriginalPath.filename(), ".keiremeta");
            destinationMetadata = ConfinedMetadataPath(m_Impl->SourceRoot, record.OriginalPath);
            if (m_Impl->SourceFiles->Exists(Detail::PathWithSuffix(record.OriginalPath, ".keiremeta")))
            {
                m_Impl->RemoveProjectTree(rootRelative);
                return;
            }
            m_Impl->ProjectFiles->RenameTo(rootRelative / record.OriginalPath.filename(), *m_Impl->SourceFiles,
                                           record.OriginalPath);
            try
            {
                m_Impl->ProjectFiles->RenameTo(
                    rootRelative / Detail::PathWithSuffix(record.OriginalPath.filename(), ".keiremeta"),
                    *m_Impl->SourceFiles, Detail::PathWithSuffix(record.OriginalPath, ".keiremeta"));
            }
            catch (...)
            {
                m_Impl->SourceFiles->RenameTo(record.OriginalPath, *m_Impl->ProjectFiles,
                                              rootRelative / record.OriginalPath.filename());
                throw;
            }
        }
        try
        {
            (void)RefreshUnlocked();
        }
        catch (...)
        {
            if (!record.Folder)
                m_Impl->SourceFiles->RenameTo(
                    Detail::PathWithSuffix(record.OriginalPath, ".keiremeta"), *m_Impl->ProjectFiles,
                    rootRelative / Detail::PathWithSuffix(record.OriginalPath.filename(), ".keiremeta"));
            m_Impl->SourceFiles->RenameTo(record.OriginalPath, *m_Impl->ProjectFiles,
                                          rootRelative / record.OriginalPath.filename());
            throw;
        }
        m_Impl->RemoveProjectTree(rootRelative);
    }

    void AssetDatabase::PermanentlyDeleteTrash(const AssetTrashId id)
    {
        std::scoped_lock operation(*m_Impl->OperationMutex);
        if (!id)
            throw std::invalid_argument("Cannot delete an empty asset trash identity.");
        const auto trashRoot = (m_Impl->Specification.ProjectRoot / "Library" / "Trash").lexically_normal();
        const auto root = (trashRoot / id.ToString()).lexically_normal();
        if (root.parent_path() != trashRoot)
            throw std::logic_error("Asset trash deletion escaped the configured trash directory.");
        const auto relativeRoot = root.lexically_relative(m_Impl->Specification.ProjectRoot);
        (void)ReadTrashManifest(*m_Impl->ProjectFiles, relativeRoot);
        m_Impl->RemoveProjectTree(relativeRoot);
    }

    std::filesystem::path AssetDatabase::MoveToTrash(const AssetId id) { return TrashAsset(id).TrashPath; }

    const AssetDatabaseSpecification& AssetDatabase::Specification() const noexcept { return m_Impl->Specification; }

} // namespace Keire
