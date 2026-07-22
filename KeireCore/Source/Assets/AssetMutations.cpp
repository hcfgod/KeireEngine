#include "KeireInternal/Assets/AssetDatabaseImplementation.h"

namespace Keire
{
    void AssetDatabase::CreateFolder(const std::filesystem::path& relativePath)
    {
        std::scoped_lock operation(*m_Impl->OperationMutex);
        const auto destination = ConfinedPath(m_Impl->SourceRoot, relativePath);
        if (!std::filesystem::create_directories(destination) && !std::filesystem::is_directory(destination))
            throw std::runtime_error("Could not create asset folder: " + Detail::PathToUtf8(destination));
    }

    AssetId AssetDatabase::CreateAsset(const std::filesystem::path& relativePath,
                                       const AssetImporterRegistration& importer,
                                       const std::span<const std::byte> sourceBytes,
                                       const AssetImportSettings& requestedSettings)
    {
        std::scoped_lock operation(*m_Impl->OperationMutex);
        const auto registered = m_Impl->Importers.find(importer.Name);
        if (registered == m_Impl->Importers.end() || registered->second.Version != importer.Version ||
            registered->second.Type != importer.Type)
            throw std::invalid_argument("Asset creation requires an importer registered with this database.");
        const auto destination = ConfinedPath(m_Impl->SourceRoot, relativePath);
        const auto metadata = Detail::PathWithSuffix(destination, ".keiremeta");
        if (std::filesystem::exists(destination) || std::filesystem::exists(metadata))
            throw std::runtime_error("Asset creation destination already exists.");
        const auto extension = LowerExtension(destination);
        if (std::ranges::find(importer.Extensions, extension) == importer.Extensions.end())
            throw std::invalid_argument("Asset creation path does not use an importer-supported extension.");
        if (sourceBytes.size() > m_Impl->Specification.MaximumSourceBytes)
            throw std::invalid_argument("Asset creation source exceeds the configured maximum size.");
        const auto settings = m_Impl->NormalizeSettings(registered->second, requestedSettings);
        const auto id = AssetId::Generate();
        AssetSourceRecord validationRecord;
        validationRecord.Id = id;
        validationRecord.Type = importer.Type;
        validationRecord.RelativePath = std::filesystem::relative(destination, m_Impl->SourceRoot).lexically_normal();
        validationRecord.Importer = importer.Name;
        validationRecord.ImporterVersion = importer.Version;
        validationRecord.ImportSettings = settings;
        const auto validationMetadata = Detail::PathWithSuffix(metadata, ".validate.tmp");
        validationRecord.MetadataPath = validationMetadata;
        WriteMetadata(validationMetadata, id, importer.Type, importer.Name, importer.Version, settings);
        AssetImportOutput validated;
        try
        {
            validated = m_Impl->ImportSource(validationRecord, sourceBytes);
        }
        catch (...)
        {
            std::error_code ignored;
            std::filesystem::remove(validationMetadata, ignored);
            throw;
        }
        std::error_code ignored;
        std::filesystem::remove(validationMetadata, ignored);
        std::filesystem::create_directories(destination.parent_path());
        try
        {
            Detail::WriteFileAtomically(destination, sourceBytes);
            WriteMetadata(metadata, id, importer.Type, importer.Name, importer.Version, settings);
        }
        catch (...)
        {
            std::error_code cleanupError;
            std::filesystem::remove(destination, cleanupError);
            std::filesystem::remove(metadata, cleanupError);
            throw;
        }
        AssetSourceRecord record;
        try
        {
            record = ReadMetadata(m_Impl->SourceRoot, destination, m_Impl->Specification.MaximumSourceBytes, true,
                                  &registered->second);
            m_Impl->PublishRecord(record, m_Impl->ReadSignature(destination, metadata));
        }
        catch (...)
        {
            std::error_code cleanupError;
            std::filesystem::remove(destination, cleanupError);
            std::filesystem::remove(metadata, cleanupError);
            throw;
        }
        m_Impl->StoreValidatedImport(record, std::move(validated));
        return id;
    }

    void AssetDatabase::Rename(const AssetId id, std::string newName)
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
        const auto source = m_Impl->SourceRoot / record->RelativePath;
        const auto destination = ConfinedPath(m_Impl->SourceRoot, relativeDestination);
        if (source == destination)
            return;
        const auto sourceMetadata = record->MetadataPath;
        const auto destinationMetadata = Detail::PathWithSuffix(destination, ".keiremeta");
        if (std::filesystem::exists(destination) || std::filesystem::exists(destinationMetadata))
            throw std::runtime_error("Asset move destination already exists.");
        std::filesystem::create_directories(destination.parent_path());
        Detail::RenamePathWithRetry(source, destination);
        try
        {
            Detail::RenamePathWithRetry(sourceMetadata, destinationMetadata);
        }
        catch (...)
        {
            std::error_code ignored;
            (void)Detail::TryRenamePathWithRetry(destination, source, ignored);
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
            std::error_code ignored;
            (void)Detail::TryRenamePathWithRetry(destinationMetadata, sourceMetadata, ignored);
            (void)Detail::TryRenamePathWithRetry(destination, source, ignored);
            throw;
        }
    }

    AssetId AssetDatabase::Duplicate(const AssetId id, const std::filesystem::path& destination)
    {
        std::scoped_lock operation(*m_Impl->OperationMutex);
        const auto record = Find(id);
        if (!record)
            throw std::invalid_argument("Cannot duplicate an unknown asset ID.");
        const auto target = ConfinedPath(m_Impl->SourceRoot, destination);
        const auto metadata = Detail::PathWithSuffix(target, ".keiremeta");
        if (std::filesystem::exists(target) || std::filesystem::exists(metadata))
            throw std::runtime_error("Asset duplicate destination already exists.");
        std::filesystem::create_directories(target.parent_path());
        std::filesystem::copy_file(m_Impl->SourceRoot / record->RelativePath, target);
        const auto newId = AssetId::Generate();
        try
        {
            WriteMetadata(metadata, newId, record->Type, record->Importer, record->ImporterVersion);
        }
        catch (...)
        {
            std::error_code ignored;
            std::filesystem::remove(target, ignored);
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
        if (std::filesystem::exists(destination))
            throw std::runtime_error("Asset folder move destination already exists.");
        std::filesystem::create_directories(destination.parent_path());
        Detail::RenamePathWithRetry(source, destination);
        try
        {
            (void)RefreshUnlocked();
        }
        catch (...)
        {
            std::error_code ignored;
            (void)Detail::TryRenamePathWithRetry(destination, source, ignored);
            throw;
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
        if (std::filesystem::exists(destination))
            throw std::runtime_error("Asset folder duplicate destination already exists.");
        try
        {
            std::filesystem::create_directories(destination.parent_path());
            std::filesystem::copy(source, destination, std::filesystem::copy_options::recursive);
            std::error_code error;
            for (std::filesystem::recursive_directory_iterator iterator(destination, error), end;
                 !error && iterator != end; iterator.increment(error))
            {
                if (iterator->is_symlink())
                    throw std::runtime_error("Asset folders containing symbolic links cannot be duplicated.");
                if (iterator->is_regular_file() && iterator->path().extension() == ".keiremeta")
                    std::filesystem::remove(iterator->path());
            }
            if (error)
                throw std::runtime_error("Could not enumerate the duplicated asset folder.");
            (void)RefreshUnlocked();
        }
        catch (...)
        {
            std::error_code ignored;
            std::filesystem::remove_all(destination, ignored);
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
        const auto transaction = AssetId::Generate();
        const auto root = m_Impl->Specification.ProjectRoot / "Library" / "Trash" / transaction.ToString();
        std::filesystem::create_directories(root);
        const auto source = m_Impl->SourceRoot / record->RelativePath;
        const auto destination = root / source.filename();
        const auto destinationMetadata = root / record->MetadataPath.filename();
        Detail::RenamePathWithRetry(source, destination);
        try
        {
            Detail::RenamePathWithRetry(record->MetadataPath, destinationMetadata);
            const std::array assets{id};
            WriteTrashManifest(root, transaction, record->RelativePath, assets, false);
        }
        catch (...)
        {
            std::error_code ignored;
            (void)Detail::TryRenamePathWithRetry(destinationMetadata, record->MetadataPath, ignored);
            (void)Detail::TryRenamePathWithRetry(destination, source, ignored);
            std::filesystem::remove_all(root, ignored);
            throw;
        }
        (void)RefreshUnlocked();
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
        const auto root = m_Impl->Specification.ProjectRoot / "Library" / "Trash" / transaction.ToString();
        std::filesystem::create_directories(root);
        const auto destination = root / source.filename();
        Detail::RenamePathWithRetry(source, destination);
        try
        {
            WriteTrashManifest(root, transaction, normalized, assets, true);
        }
        catch (...)
        {
            std::error_code ignored;
            (void)Detail::TryRenamePathWithRetry(destination, source, ignored);
            std::filesystem::remove_all(root, ignored);
            throw;
        }
        (void)RefreshUnlocked();
        return {AssetTrashId(transaction), normalized, root, std::move(assets), true};
    }

    std::vector<AssetTrashRecord> AssetDatabase::TrashRecords() const
    {
        std::scoped_lock operation(*m_Impl->OperationMutex);
        const auto trashRoot = m_Impl->Specification.ProjectRoot / "Library" / "Trash";
        std::vector<AssetTrashRecord> result;
        std::error_code error;
        for (std::filesystem::directory_iterator iterator(trashRoot, error), end; !error && iterator != end;
             iterator.increment(error))
        {
            if (!iterator->is_directory())
                continue;
            const auto parsed = ReadTrashManifest(iterator->path());
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
        const auto root = m_Impl->Specification.ProjectRoot / "Library" / "Trash" / id.ToString();
        const auto record = ReadTrashManifest(root);
        if (record.Id.ToString() != id.ToString())
            throw std::runtime_error("Asset trash identity does not match its manifest.");
        const auto destination = ConfinedPath(m_Impl->SourceRoot, record.OriginalPath);
        const auto source = root / record.OriginalPath.filename();
        if (std::filesystem::exists(destination))
            throw std::runtime_error("Asset trash restore destination already exists.");
        std::filesystem::create_directories(destination.parent_path());
        std::filesystem::path sourceMetadata;
        std::filesystem::path destinationMetadata;
        if (record.Folder)
            Detail::RenamePathWithRetry(source, destination);
        else
        {
            sourceMetadata = Detail::PathWithSuffix(root / record.OriginalPath.filename(), ".keiremeta");
            destinationMetadata = Detail::PathWithSuffix(destination, ".keiremeta");
            if (std::filesystem::exists(destinationMetadata))
                throw std::runtime_error("Asset trash restore metadata destination already exists.");
            Detail::RenamePathWithRetry(source, destination);
            try
            {
                Detail::RenamePathWithRetry(sourceMetadata, destinationMetadata);
            }
            catch (...)
            {
                std::error_code ignored;
                (void)Detail::TryRenamePathWithRetry(destination, source, ignored);
                throw;
            }
        }
        try
        {
            (void)RefreshUnlocked();
        }
        catch (...)
        {
            std::error_code ignored;
            if (!record.Folder)
                (void)Detail::TryRenamePathWithRetry(destinationMetadata, sourceMetadata, ignored);
            (void)Detail::TryRenamePathWithRetry(destination, source, ignored);
            throw;
        }
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
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
        (void)ReadTrashManifest(root);
        std::error_code error;
        std::filesystem::remove_all(root, error);
        if (error)
            throw std::runtime_error("Could not permanently remove the asset trash entry: " + error.message());
    }

    std::filesystem::path AssetDatabase::MoveToTrash(const AssetId id) { return TrashAsset(id).TrashPath; }

    const AssetDatabaseSpecification& AssetDatabase::Specification() const noexcept { return m_Impl->Specification; }

} // namespace Keire
