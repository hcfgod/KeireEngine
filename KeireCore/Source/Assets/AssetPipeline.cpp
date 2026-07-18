#include "Keire/Assets/AssetPipeline.h"

#include "KeireInternal/Assets/AssetInternal.h"

#include <nlohmann/json.hpp>
#include <zstd.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <fstream>
#include <functional>
#include <limits>
#include <mutex>
#include <ranges>
#include <thread>
#include <unordered_map>
#include <unordered_set>

namespace Keire
{
    namespace
    {
        using Json = nlohmann::json;

        struct FileSignature
        {
            std::filesystem::file_time_type Modified{};
            std::uintmax_t Size = 0;

            [[nodiscard]] bool operator==(const FileSignature&) const noexcept = default;
        };

        [[nodiscard]] std::filesystem::path ConfinedPath(const std::filesystem::path& root,
                                                         const std::filesystem::path& relative)
        {
            const auto normalized = relative.lexically_normal();
            if (relative.empty() || relative.is_absolute() || normalized.empty() ||
                normalized.generic_string().starts_with(".."))
                throw std::invalid_argument("Asset path must be a confined project-relative path.");
            return (root / normalized).lexically_normal();
        }

        [[nodiscard]] std::vector<std::byte> ReadSource(const std::filesystem::path& path, const std::size_t maximum)
        {
            std::error_code error;
            const auto size = std::filesystem::file_size(path, error);
            if (error)
                throw std::runtime_error("Could not inspect asset source: " + path.string());
            if (size > maximum || size > static_cast<std::uintmax_t>(std::numeric_limits<std::size_t>::max()))
                throw std::runtime_error("Asset source exceeds the configured maximum size: " + path.string());
            std::vector<std::byte> bytes(static_cast<std::size_t>(size));
            std::ifstream stream(path, std::ios::binary);
            if (!stream || (!bytes.empty() && !stream.read(reinterpret_cast<char*>(bytes.data()),
                                                           static_cast<std::streamsize>(bytes.size()))))
                throw std::runtime_error("Could not read asset source: " + path.string());
            return bytes;
        }

        [[nodiscard]] std::string LowerExtension(const std::filesystem::path& path)
        {
            auto extension = path.extension().string();
            std::ranges::transform(extension, extension.begin(),
                                   [](const unsigned char value) { return static_cast<char>(std::tolower(value)); });
            return extension;
        }

        [[nodiscard]] AssetTypeId InferType(const std::filesystem::path& path)
        {
            static const std::unordered_set<std::string> TextExtensions{".txt",  ".md",   ".json", ".xml",  ".yaml",
                                                                        ".yml",  ".csv",  ".ini",  ".toml", ".lua",
                                                                        ".glsl", ".hlsl", ".vert", ".frag", ".comp"};
            return TextExtensions.contains(LowerExtension(path)) ? TextAsset::StaticType() : BinaryAsset::StaticType();
        }

        [[nodiscard]] std::string ImporterName(const AssetTypeId type)
        {
            return type == TextAsset::StaticType() ? "Keire.Text" : "Keire.Binary";
        }

        void WriteMetadata(const std::filesystem::path& path, const AssetId id, const AssetTypeId type,
                           const std::string_view importer, const std::uint32_t importerVersion)
        {
            const Json metadata{{"schemaVersion", 1},
                                {"id", id.ToString()},
                                {"type", type.ToString()},
                                {"importer", importer},
                                {"importerVersion", importerVersion},
                                {"dependencies", Json::array()},
                                {"subAssets", Json::array()}};
            std::filesystem::create_directories(path.parent_path());
            const auto temporary = path.string() + ".tmp";
            std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
            if (!stream)
                throw std::runtime_error("Could not create asset metadata: " + path.string());
            stream << metadata.dump(2) << '\n';
            stream.close();
            if (!stream)
                throw std::runtime_error("Could not write asset metadata: " + path.string());
            Detail::AtomicReplace(temporary, path);
        }

        [[nodiscard]] AssetSourceRecord ReadMetadata(const std::filesystem::path& sourceRoot,
                                                     const std::filesystem::path& source,
                                                     const std::size_t maximumSourceBytes, const bool digestSource,
                                                     const AssetImporterRegistration* inferredImporter)
        {
            AssetSourceRecord record;
            record.RelativePath = std::filesystem::relative(source, sourceRoot).lexically_normal();
            record.MetadataPath = std::filesystem::path(source.string() + ".keiremeta");
            if (!std::filesystem::exists(record.MetadataPath))
            {
                const auto type = inferredImporter ? inferredImporter->Type : InferType(source);
                WriteMetadata(record.MetadataPath, AssetId::Generate(), type,
                              inferredImporter ? inferredImporter->Name : ImporterName(type),
                              inferredImporter ? inferredImporter->Version : 1U);
            }
            if (std::filesystem::is_symlink(source) || std::filesystem::is_symlink(record.MetadataPath))
                throw std::runtime_error("Asset sources and metadata may not be symbolic links.");
            if (std::filesystem::file_size(record.MetadataPath) > 1024U * 1024U)
                throw std::runtime_error("Asset metadata exceeds the 1 MiB safety limit.");

            std::ifstream stream(record.MetadataPath, std::ios::binary);
            if (!stream)
                throw std::runtime_error("Could not open asset metadata: " + record.MetadataPath.string());
            Json metadata;
            stream >> metadata;
            if (!stream || !metadata.is_object() || metadata.value("schemaVersion", 0) != 1)
                throw std::runtime_error("Asset metadata has an unsupported schema: " + record.MetadataPath.string());
            record.Id = AssetId::Parse(metadata.at("id").get<std::string>());
            record.Type = AssetTypeId::Parse(metadata.at("type").get<std::string>());
            record.Importer = metadata.at("importer").get<std::string>();
            record.ImporterVersion = metadata.at("importerVersion").get<std::uint32_t>();
            if (!record.Id || !record.Type || record.Importer.empty() || record.ImporterVersion == 0)
                throw std::runtime_error("Asset metadata contains an invalid identity or importer.");
            if (metadata.contains("dependencies"))
            {
                for (const auto& dependency : metadata["dependencies"])
                    record.Dependencies.push_back(AssetId::Parse(dependency.get<std::string>()));
            }
            if (metadata.contains("subAssets"))
            {
                for (const auto& subAsset : metadata["subAssets"])
                    record.SubAssets.push_back(AssetId::Parse(subAsset.get<std::string>()));
            }
            if (digestSource)
            {
                const auto bytes = ReadSource(source, maximumSourceBytes);
                record.SourceDigest = Detail::DigestToString(Detail::Sha256(bytes));
            }
            return record;
        }

        [[nodiscard]] std::vector<std::byte> Compress(const std::span<const std::byte> bytes, const int level)
        {
            std::vector<std::byte> result(ZSTD_compressBound(bytes.size()));
            const auto size = ZSTD_compress(result.data(), result.size(), bytes.data(), bytes.size(), level);
            if (ZSTD_isError(size))
                throw std::runtime_error(std::string("Zstandard compression failed: ") + ZSTD_getErrorName(size));
            result.resize(size);
            return result;
        }

        void ReplaceDirectory(const std::filesystem::path& temporary, const std::filesystem::path& destination)
        {
            const auto backup = std::filesystem::path(destination.string() + ".bak");
            std::error_code error;
            std::filesystem::remove_all(backup, error);
            if (std::filesystem::exists(destination))
            {
                std::filesystem::rename(destination, backup, error);
                if (error)
                    throw std::runtime_error("Could not preserve the previous cooked asset directory.");
            }
            constexpr int maximumPublishAttempts = 8;
            for (int attempt = 0; attempt < maximumPublishAttempts; ++attempt)
            {
                error.clear();
                std::filesystem::rename(temporary, destination, error);
                if (!error || error != std::errc::permission_denied)
                    break;
                std::this_thread::sleep_for(std::chrono::milliseconds(5 * (attempt + 1)));
            }
            if (error)
            {
                std::error_code ignored;
                if (std::filesystem::exists(backup))
                    std::filesystem::rename(backup, destination, ignored);
                throw std::runtime_error("Could not publish the cooked asset directory atomically: " + error.message());
            }
            std::filesystem::remove_all(backup, error);
        }

        void ValidateDependencies(const std::span<const Detail::CatalogEntry> entries)
        {
            std::unordered_map<AssetId, std::size_t> indices;
            for (std::size_t index = 0; index < entries.size(); ++index)
                indices.emplace(entries[index].Id, index);
            std::vector<std::uint8_t> marks(entries.size());
            std::function<void(std::size_t)> visit = [&](const std::size_t index)
            {
                if (marks[index] == 1)
                    throw std::runtime_error("Asset dependency graph contains a cycle.");
                if (marks[index] == 2)
                    return;
                marks[index] = 1;
                for (const auto dependency : entries[index].Dependencies)
                {
                    const auto found = indices.find(dependency);
                    if (found == indices.end())
                        throw std::runtime_error("Asset dependency is missing from the cooked catalog: " +
                                                 dependency.ToString());
                    visit(found->second);
                }
                marks[index] = 2;
            };
            for (std::size_t index = 0; index < entries.size(); ++index)
                visit(index);
        }
    } // namespace

    class AssetDatabase::Impl final
    {
      public:
        explicit Impl(AssetDatabaseSpecification value) : Specification(std::move(value))
        {
            if (Specification.MaximumSourceBytes == 0 || Specification.ChangeDebounce.count() < 0)
                throw std::invalid_argument("Asset database limits must be non-zero and non-negative.");
            Specification.ProjectRoot = std::filesystem::absolute(Specification.ProjectRoot).lexically_normal();
            SourceRoot = ConfinedPath(Specification.ProjectRoot, Specification.SourceDirectory);
            CacheRoot = ConfinedPath(Specification.ProjectRoot, Specification.CacheDirectory);
            for (auto& importer : Specification.Importers)
            {
                if (importer.Name.empty() || importer.Version == 0 || !importer.Type || importer.Extensions.empty() ||
                    !importer.Import)
                    throw std::invalid_argument("Asset importer registration is incomplete.");
                for (auto& extension : importer.Extensions)
                {
                    std::ranges::transform(extension, extension.begin(), [](const unsigned char value)
                                           { return static_cast<char>(std::tolower(value)); });
                    if (extension.empty() || extension.front() != '.' || Extensions.contains(extension))
                        throw std::invalid_argument("Asset importer extension is invalid or duplicated.");
                    Extensions.emplace(extension, importer.Name);
                }
                if (!Importers.emplace(importer.Name, importer).second)
                    throw std::invalid_argument("Asset importer name is duplicated.");
            }
            std::filesystem::create_directories(SourceRoot);
            std::filesystem::create_directories(CacheRoot);
        }

        [[nodiscard]] const AssetImporterRegistration* InferImporter(const std::filesystem::path& path) const
        {
            const auto found = Extensions.find(LowerExtension(path));
            if (found == Extensions.end())
                return nullptr;
            return &Importers.at(found->second);
        }

        [[nodiscard]] const AssetImporterRegistration* FindImporter(const AssetSourceRecord& record) const
        {
            const auto found = Importers.find(record.Importer);
            if (found == Importers.end())
                return nullptr;
            const auto& importer = found->second;
            return importer.Version == record.ImporterVersion && importer.Type == record.Type ? &importer : nullptr;
        }

        [[nodiscard]] std::vector<std::byte> Import(const AssetSourceRecord& record) const
        {
            const auto source = ReadSource(SourceRoot / record.RelativePath, Specification.MaximumSourceBytes);
            if (const auto* importer = FindImporter(record))
                return importer->Import(source);
            if ((record.Importer == "Keire.Text" && record.Type == TextAsset::StaticType()) ||
                (record.Importer == "Keire.Binary" && record.Type == BinaryAsset::StaticType()))
                return source;
            throw std::runtime_error("No compatible importer is registered for asset: " +
                                     record.RelativePath.generic_string());
        }

        [[nodiscard]] std::filesystem::path ObjectPath(const AssetSourceRecord& record) const
        {
            return CacheRoot / "Objects" /
                   (record.SourceDigest + "-" + record.Type.ToString() + "-" + std::to_string(record.ImporterVersion) +
                    ".bin");
        }

        struct ScanResult
        {
            std::vector<AssetSourceRecord> Records;
            std::unordered_map<AssetId, FileSignature> Signatures;
        };

        [[nodiscard]] ScanResult Scan(const bool digestSources = true) const
        {
            ScanResult result;
            std::unordered_set<AssetId> identities;
            std::unordered_set<std::string> paths;
            std::error_code error;
            for (std::filesystem::recursive_directory_iterator
                     iterator(SourceRoot, std::filesystem::directory_options::skip_permission_denied, error),
                 end;
                 iterator != end; iterator.increment(error))
            {
                if (error)
                    throw std::runtime_error("Could not enumerate the asset source directory.");
                if (!iterator->is_regular_file() || iterator->path().extension() == ".keiremeta")
                    continue;
                auto record = ReadMetadata(SourceRoot, iterator->path(), Specification.MaximumSourceBytes,
                                           digestSources, InferImporter(iterator->path()));
                if (!identities.insert(record.Id).second)
                    throw std::runtime_error("Duplicate asset identity detected: " + record.Id.ToString());
                for (const auto subAsset : record.SubAssets)
                {
                    if (!subAsset || !identities.insert(subAsset).second)
                        throw std::runtime_error("Duplicate or invalid subasset identity detected.");
                }
                auto comparable = record.RelativePath.generic_string();
#if defined(_WIN32)
                std::ranges::transform(comparable, comparable.begin(), [](const unsigned char value)
                                       { return static_cast<char>(std::tolower(value)); });
#endif
                if (!paths.insert(comparable).second)
                    throw std::runtime_error("Case-colliding asset paths are not portable: " + comparable);
                result.Signatures.emplace(record.Id, FileSignature{iterator->last_write_time(), iterator->file_size()});
                result.Records.push_back(std::move(record));
            }
            std::ranges::sort(result.Records, [](const auto& left, const auto& right) { return left.Id < right.Id; });
            return result;
        }

        AssetDatabaseSpecification Specification;
        std::filesystem::path SourceRoot;
        std::filesystem::path CacheRoot;
        std::vector<AssetSourceRecord> Records;
        std::unordered_map<AssetId, FileSignature> Observed;
        std::unordered_map<AssetId, std::chrono::steady_clock::time_point> PendingChanges;
        std::unordered_map<std::string, AssetImporterRegistration> Importers;
        std::unordered_map<std::string, std::string> Extensions;
        mutable std::mutex Mutex;
    };

    AssetDatabase::AssetDatabase(AssetDatabaseSpecification specification)
        : m_Impl(std::make_unique<Impl>(std::move(specification)))
    {
        (void)Refresh();
    }

    AssetDatabase::~AssetDatabase() = default;

    std::size_t AssetDatabase::Refresh()
    {
        auto scanned = m_Impl->Scan();
        std::scoped_lock lock(m_Impl->Mutex);
        m_Impl->Records = std::move(scanned.Records);
        m_Impl->Observed = std::move(scanned.Signatures);
        m_Impl->PendingChanges.clear();
        return m_Impl->Records.size();
    }

    std::vector<AssetSourceRecord> AssetDatabase::Records() const
    {
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
                record.SourceDigest = previous->SourceDigest;
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

    AssetImportResult AssetDatabase::ImportAll()
    {
        (void)Refresh();
        const auto records = Records();
        const auto objectRoot = m_Impl->CacheRoot / "Objects";
        std::filesystem::create_directories(objectRoot);
        AssetImportResult result;
        for (const auto& record : records)
        {
            const auto object = m_Impl->ObjectPath(record);
            if (std::filesystem::exists(object))
            {
                ++result.CacheHits;
                continue;
            }
            const auto bytes = m_Impl->Import(record);
            const auto temporary = object.string() + ".tmp";
            std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
            if (!stream || (!bytes.empty() && !stream.write(reinterpret_cast<const char*>(bytes.data()),
                                                            static_cast<std::streamsize>(bytes.size()))))
                throw std::runtime_error("Could not write imported asset cache object.");
            stream.close();
            Detail::AtomicReplace(temporary, object);
            ++result.Imported;
        }
        const auto cooked = AssetCooker::Cook(*this, AssetBuildProfile{}, m_Impl->CacheRoot / "Runtime");
        result.CatalogPath = cooked.CatalogPath;
        return result;
    }

    void AssetDatabase::CreateFolder(const std::filesystem::path& relativePath)
    {
        const auto destination = ConfinedPath(m_Impl->SourceRoot, relativePath);
        if (!std::filesystem::create_directories(destination) && !std::filesystem::is_directory(destination))
            throw std::runtime_error("Could not create asset folder: " + destination.string());
    }

    AssetId AssetDatabase::CreateAsset(const std::filesystem::path& relativePath,
                                       const AssetImporterRegistration& importer,
                                       const std::span<const std::byte> sourceBytes)
    {
        const auto registered = m_Impl->Importers.find(importer.Name);
        if (registered == m_Impl->Importers.end() || registered->second.Version != importer.Version ||
            registered->second.Type != importer.Type)
            throw std::invalid_argument("Asset creation requires an importer registered with this database.");
        const auto destination = ConfinedPath(m_Impl->SourceRoot, relativePath);
        const auto metadata = std::filesystem::path(destination.string() + ".keiremeta");
        if (std::filesystem::exists(destination) || std::filesystem::exists(metadata))
            throw std::runtime_error("Asset creation destination already exists.");
        const auto extension = LowerExtension(destination);
        if (std::ranges::find(importer.Extensions, extension) == importer.Extensions.end())
            throw std::invalid_argument("Asset creation path does not use an importer-supported extension.");
        if (sourceBytes.size() > m_Impl->Specification.MaximumSourceBytes)
            throw std::invalid_argument("Asset creation source exceeds the configured maximum size.");
        (void)registered->second.Import(sourceBytes);
        std::filesystem::create_directories(destination.parent_path());
        const auto temporary = destination.string() + ".tmp";
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        if (!stream || (!sourceBytes.empty() && !stream.write(reinterpret_cast<const char*>(sourceBytes.data()),
                                                              static_cast<std::streamsize>(sourceBytes.size()))))
            throw std::runtime_error("Could not create asset source.");
        stream.close();
        const auto id = AssetId::Generate();
        try
        {
            Detail::AtomicReplace(temporary, destination);
            WriteMetadata(metadata, id, importer.Type, importer.Name, importer.Version);
        }
        catch (...)
        {
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
            std::filesystem::remove(destination, ignored);
            std::filesystem::remove(metadata, ignored);
            throw;
        }
        (void)Refresh();
        return id;
    }

    void AssetDatabase::Rename(const AssetId id, std::string newName)
    {
        if (newName.empty() || newName == "." || newName == ".." || newName.find('/') != std::string::npos ||
            newName.find('\\') != std::string::npos)
            throw std::invalid_argument("Asset name must be one non-empty path component.");
        const auto record = Find(id);
        if (!record)
            throw std::invalid_argument("Cannot rename an unknown asset ID.");
        const auto source = m_Impl->SourceRoot / record->RelativePath;
        const auto destination = source.parent_path() / newName;
        const auto sourceMetadata = record->MetadataPath;
        const auto destinationMetadata = std::filesystem::path(destination.string() + ".keiremeta");
        if (std::filesystem::exists(destination) || std::filesystem::exists(destinationMetadata))
            throw std::runtime_error("Asset rename destination already exists.");
        std::filesystem::rename(source, destination);
        try
        {
            std::filesystem::rename(sourceMetadata, destinationMetadata);
        }
        catch (...)
        {
            std::error_code ignored;
            std::filesystem::rename(destination, source, ignored);
            throw;
        }
        (void)Refresh();
    }

    AssetId AssetDatabase::Duplicate(const AssetId id, const std::filesystem::path& destination)
    {
        const auto record = Find(id);
        if (!record)
            throw std::invalid_argument("Cannot duplicate an unknown asset ID.");
        const auto target = ConfinedPath(m_Impl->SourceRoot, destination);
        const auto metadata = std::filesystem::path(target.string() + ".keiremeta");
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
        (void)Refresh();
        return newId;
    }

    std::filesystem::path AssetDatabase::MoveToTrash(const AssetId id)
    {
        const auto record = Find(id);
        if (!record)
            throw std::invalid_argument("Cannot trash an unknown asset ID.");
        const auto trash = m_Impl->Specification.ProjectRoot / "Library" / "Trash" / id.ToString();
        std::filesystem::create_directories(trash);
        const auto source = m_Impl->SourceRoot / record->RelativePath;
        const auto destination = trash / source.filename();
        const auto destinationMetadata = trash / record->MetadataPath.filename();
        std::filesystem::rename(source, destination);
        try
        {
            std::filesystem::rename(record->MetadataPath, destinationMetadata);
        }
        catch (...)
        {
            std::error_code ignored;
            std::filesystem::rename(destination, source, ignored);
            throw;
        }
        (void)Refresh();
        return trash;
    }

    const AssetDatabaseSpecification& AssetDatabase::Specification() const noexcept { return m_Impl->Specification; }

    AssetCookResult AssetCooker::Cook(const AssetDatabase& database, const AssetBuildProfile& profile,
                                      const std::filesystem::path& outputDirectory)
    {
        if (profile.Name.empty() || profile.CompressionLevel < ZSTD_minCLevel() ||
            profile.CompressionLevel > ZSTD_maxCLevel() || profile.MaximumPackBytes <= Detail::PackHeaderBytes)
            throw std::invalid_argument("Asset build profile contains invalid compression or shard settings.");
        const auto records = database.Records();
        const auto destination = std::filesystem::absolute(outputDirectory).lexically_normal();
        const auto temporary = std::filesystem::path(destination.string() + ".tmp-" + AssetId::Generate().ToString());
        std::filesystem::create_directories(temporary);

        AssetCookResult result;
        std::vector<Detail::CatalogEntry> entries;
        std::ofstream pack;
        std::filesystem::path packPath;
        std::uint64_t packBytes = 0;
        auto openPack = [&]
        {
            if (pack.is_open())
            {
                pack.close();
                if (!pack)
                    throw std::runtime_error("Could not finalize an asset pack.");
            }
            packPath = temporary / ("content-" + std::to_string(result.PackCount) + ".keirepak");
            pack = std::ofstream(packPath, std::ios::binary | std::ios::trunc);
            if (!pack)
                throw std::runtime_error("Could not create asset pack: " + packPath.string());
            Detail::WritePackHeader(pack);
            packBytes = Detail::PackHeaderBytes;
            ++result.PackCount;
        };

        try
        {
            for (const auto& record : records)
            {
                if (profile.Strict && record.Importer != ImporterName(record.Type) &&
                    !database.m_Impl->FindImporter(record))
                    throw std::runtime_error("Strict cooking rejected an unsupported importer: " + record.Importer);
                const auto bytes = database.m_Impl->Import(record);
                const auto compressed = Compress(bytes, profile.CompressionLevel);
                if (compressed.size() > profile.MaximumPackBytes - Detail::PackHeaderBytes)
                    throw std::runtime_error("Compressed asset exceeds the build profile's maximum pack size.");
                if (!pack.is_open() ||
                    (packBytes > Detail::PackHeaderBytes && compressed.size() > profile.MaximumPackBytes - packBytes))
                    openPack();
                Detail::CatalogEntry entry;
                entry.Id = record.Id;
                entry.Type = record.Type;
                entry.PackPath = packPath.filename();
                entry.Offset = packBytes;
                entry.CompressedBytes = compressed.size();
                entry.UncompressedBytes = bytes.size();
                entry.Digest = Detail::Sha256(bytes);
                entry.Dependencies = record.Dependencies;
                if (!compressed.empty())
                    pack.write(reinterpret_cast<const char*>(compressed.data()),
                               static_cast<std::streamsize>(compressed.size()));
                if (!pack)
                    throw std::runtime_error("Could not write asset pack payload.");
                packBytes += compressed.size();
                result.UncompressedBytes += bytes.size();
                result.CompressedBytes += compressed.size();
                entries.push_back(std::move(entry));
            }
            if (pack.is_open())
                pack.close();
            ValidateDependencies(entries);
            Detail::WriteCatalog(temporary / "catalog.json", entries);
            const Json buildProfile{{"schemaVersion", 1},
                                    {"name", profile.Name},
                                    {"compression", "zstd"},
                                    {"compressionLevel", profile.CompressionLevel},
                                    {"maximumPackBytes", profile.MaximumPackBytes},
                                    {"strict", profile.Strict}};
            std::ofstream profileStream(temporary / "build-profile.json", std::ios::binary | std::ios::trunc);
            profileStream << buildProfile.dump(2) << '\n';
            profileStream.close();
            if (!profileStream)
                throw std::runtime_error("Could not write the asset build profile.");
            std::filesystem::create_directories(destination.parent_path());
            ReplaceDirectory(temporary, destination);
        }
        catch (...)
        {
            std::error_code ignored;
            std::filesystem::remove_all(temporary, ignored);
            throw;
        }
        result.AssetCount = entries.size();
        result.CatalogPath = destination / "catalog.json";
        return result;
    }

    void AssetCooker::Validate(const std::filesystem::path& catalogPath, const std::size_t maximumAssetBytes)
    {
        const auto catalog = Detail::LoadCatalog(catalogPath);
        ValidateDependencies(catalog.Entries);
        std::unordered_set<AssetId> identities;
        for (const auto& entry : catalog.Entries)
        {
            if (!identities.insert(entry.Id).second || entry.UncompressedBytes > maximumAssetBytes ||
                entry.CompressedBytes > std::numeric_limits<std::size_t>::max() ||
                entry.Offset > static_cast<std::uint64_t>(std::numeric_limits<std::streamoff>::max()))
                throw std::runtime_error("Cooked asset catalog contains an invalid or duplicate entry.");
            std::ifstream pack(entry.PackPath, std::ios::binary);
            if (!pack)
                throw std::runtime_error("Could not open cooked asset pack: " + entry.PackPath.string());
            Detail::ValidatePackHeader(pack, entry.PackPath);
            std::error_code error;
            const auto packSize = std::filesystem::file_size(entry.PackPath, error);
            if (error || entry.Offset > packSize || entry.CompressedBytes > packSize - entry.Offset)
                throw std::runtime_error("Cooked asset payload is outside its pack.");
            pack.seekg(static_cast<std::streamoff>(entry.Offset), std::ios::beg);
            std::vector<std::byte> compressed(static_cast<std::size_t>(entry.CompressedBytes));
            if (!compressed.empty() &&
                !pack.read(reinterpret_cast<char*>(compressed.data()), static_cast<std::streamsize>(compressed.size())))
                throw std::runtime_error("Could not read cooked asset payload.");
            std::vector<std::byte> bytes(static_cast<std::size_t>(entry.UncompressedBytes));
            const auto size = ZSTD_decompress(bytes.data(), bytes.size(), compressed.data(), compressed.size());
            if (ZSTD_isError(size) || size != bytes.size() || Detail::Sha256(bytes) != entry.Digest)
                throw std::runtime_error("Cooked asset payload failed decompression or integrity validation.");
        }
    }
} // namespace Keire
