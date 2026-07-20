#include "Keire/Assets/AssetPipeline.h"

#include "Keire/Log.h"

#include "KeireInternal/Assets/AssetInternal.h"

#include <nlohmann/json.hpp>
#include <zstd.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
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
            std::filesystem::file_time_type MetadataModified{};
            std::uintmax_t MetadataSize = 0;

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

        void LogImportDiagnostic(const AssetSourceRecord& record, const AssetImportDiagnostic& diagnostic) noexcept
        {
            try
            {
                const auto source = diagnostic.RelativePath.empty() ? record.RelativePath : diagnostic.RelativePath;
                auto location = source.generic_string();
                if (diagnostic.Line != 0)
                {
                    location += ':' + std::to_string(diagnostic.Line);
                    if (diagnostic.Column != 0)
                        location += ':' + std::to_string(diagnostic.Column);
                }
                switch (diagnostic.Severity)
                {
                case AssetDiagnosticSeverity::Information:
                    KEIRE_CORE_INFO("Asset import diagnostic for '{}' (id={}, importer={}) at {}: {}",
                                    record.RelativePath.generic_string(), record.Id.ToString(), record.Importer,
                                    location, diagnostic.Message);
                    break;
                case AssetDiagnosticSeverity::Warning:
                    KEIRE_CORE_WARN("Asset import warning for '{}' (id={}, importer={}) at {}: {}",
                                    record.RelativePath.generic_string(), record.Id.ToString(), record.Importer,
                                    location, diagnostic.Message);
                    break;
                case AssetDiagnosticSeverity::Error:
                    KEIRE_CORE_ERROR("Asset import failed for '{}' (id={}, importer={}) at {}: {}",
                                     record.RelativePath.generic_string(), record.Id.ToString(), record.Importer,
                                     location, diagnostic.Message);
                    break;
                }
            }
            catch (...)
            {
                std::fprintf(stderr, "Asset import diagnostic logging failed for %s: %s\n",
                             record.RelativePath.generic_string().c_str(), diagnostic.Message.c_str());
            }
        }

        [[nodiscard]] bool IsWithin(const std::filesystem::path& parent, const std::filesystem::path& candidate)
        {
            const auto relative = candidate.lexically_normal().lexically_relative(parent.lexically_normal());
            return !relative.empty() && !relative.is_absolute() && !relative.generic_string().starts_with("..");
        }

        [[nodiscard]] bool IsSameOrWithin(const std::filesystem::path& parent, const std::filesystem::path& candidate)
        {
            return parent.lexically_normal() == candidate.lexically_normal() || IsWithin(parent, candidate);
        }

        struct ParsedTrashRecord final
        {
            AssetId Id;
            std::filesystem::path OriginalPath;
            std::vector<AssetId> Assets;
            bool Folder = false;
        };

        void WriteTrashManifest(const std::filesystem::path& root, const AssetId id,
                                const std::filesystem::path& originalPath, const std::span<const AssetId> assets,
                                const bool folder)
        {
            Json assetIds = Json::array();
            for (const auto asset : assets)
                assetIds.push_back(asset.ToString());
            const Json manifest{{"schemaVersion", 1},
                                {"id", id.ToString()},
                                {"originalPath", originalPath.generic_string()},
                                {"folder", folder},
                                {"assets", std::move(assetIds)}};
            const auto destination = root / "trash.json";
            const auto temporary = destination.string() + ".tmp";
            std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
            if (!stream)
                throw std::runtime_error("Could not create asset trash manifest.");
            stream << manifest.dump(2) << '\n';
            stream.close();
            if (!stream)
                throw std::runtime_error("Could not write asset trash manifest.");
            Detail::AtomicReplace(temporary, destination);
        }

        [[nodiscard]] ParsedTrashRecord ReadTrashManifest(const std::filesystem::path& root)
        {
            const auto path = root / "trash.json";
            std::error_code error;
            if (!std::filesystem::is_regular_file(path, error) ||
                std::filesystem::file_size(path, error) > 1024U * 1024U)
                throw std::runtime_error("Asset trash manifest is missing or exceeds the 1 MiB limit.");
            std::ifstream stream(path, std::ios::binary);
            Json manifest;
            stream >> manifest;
            if (!stream || !manifest.is_object() || manifest.value("schemaVersion", 0) != 1)
                throw std::runtime_error("Asset trash manifest has an unsupported schema.");
            ParsedTrashRecord result;
            result.Id = AssetId::Parse(manifest.at("id").get<std::string>());
            result.OriginalPath = manifest.at("originalPath").get<std::string>();
            result.Folder = manifest.at("folder").get<bool>();
            const auto normalized = result.OriginalPath.lexically_normal();
            if (!result.Id || result.OriginalPath.empty() || result.OriginalPath.is_absolute() ||
                normalized.generic_string().starts_with(".."))
                throw std::runtime_error("Asset trash manifest contains an invalid identity or source path.");
            for (const auto& asset : manifest.at("assets"))
            {
                const auto id = AssetId::Parse(asset.get<std::string>());
                if (!id)
                    throw std::runtime_error("Asset trash manifest contains an invalid asset identity.");
                result.Assets.push_back(id);
            }
            return result;
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
                const auto metadataBytes = ReadSource(record.MetadataPath, 1024U * 1024U);
                record.MetadataDigest = Detail::DigestToString(Detail::Sha256(metadataBytes));
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
                    (!importer.Import && !importer.ContextualImport))
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
            return importer.Version >= record.ImporterVersion && importer.Type == record.Type ? &importer : nullptr;
        }

        [[nodiscard]] AssetImportContext CreateImportContext(const AssetSourceRecord& record) const
        {
            AssetImportContext context;
            context.ProjectRoot = Specification.ProjectRoot;
            context.SourceRoot = SourceRoot;
            context.SourcePath = SourceRoot / record.RelativePath;
            context.MetadataPath = record.MetadataPath;
            context.RelativePath = record.RelativePath;
            context.MaximumDependencyBytes =
                std::min(Specification.MaximumSourceBytes, std::size_t{64U * 1024U * 1024U});
            context.ReadProjectFile = [root = Specification.ProjectRoot,
                                       maximum = context.MaximumDependencyBytes](const std::filesystem::path& relative)
            {
                const auto path = ConfinedPath(root, relative);
                if (std::filesystem::is_symlink(path))
                    throw std::runtime_error("Asset dependencies may not be symbolic links.");
                return ReadSource(path, maximum);
            };
            return context;
        }

        [[nodiscard]] AssetImportOutput Import(const AssetSourceRecord& record) const
        {
            const auto source = ReadSource(SourceRoot / record.RelativePath, Specification.MaximumSourceBytes);
            AssetImportOutput result;
            if (const auto* importer = FindImporter(record))
            {
                if (importer->ContextualImport)
                    result = importer->ContextualImport(CreateImportContext(record), source);
                else
                    result.Bytes = importer->Import(source);
            }
            else if ((record.Importer == "Keire.Text" && record.Type == TextAsset::StaticType()) ||
                     (record.Importer == "Keire.Binary" && record.Type == BinaryAsset::StaticType()))
                result.Bytes = source;
            else
                throw std::runtime_error("No compatible importer is registered for asset: " +
                                         record.RelativePath.generic_string());

            if (result.Bytes.size() > Specification.MaximumSourceBytes)
                throw std::runtime_error("Imported asset exceeds the configured maximum size.");
            std::unordered_set<std::string> dependencies;
            for (const auto& dependency : result.SourceDependencies)
            {
                const auto normalized = dependency.RelativePath.lexically_normal();
                const auto comparable = normalized.generic_string();
                if (dependency.RelativePath.empty() || dependency.RelativePath.is_absolute() ||
                    comparable.starts_with("..") || dependency.Digest.size() != 64 ||
                    !dependencies.insert(comparable).second)
                    throw std::runtime_error("Contextual importer returned an invalid source dependency record.");
            }
            std::unordered_set<AssetId> assetDependencies;
            for (const auto dependency : result.AssetDependencies)
            {
                if (!dependency || !assetDependencies.insert(dependency).second)
                    throw std::runtime_error("Contextual importer returned an invalid asset dependency record.");
            }
            if (result.Diagnostics.size() > 4096)
                throw std::runtime_error("Contextual importer returned too many diagnostics.");
            for (const auto& diagnostic : result.Diagnostics)
            {
                const auto normalized = diagnostic.RelativePath.lexically_normal();
                if (diagnostic.Message.empty() || diagnostic.Message.size() > 16U * 1024U ||
                    diagnostic.RelativePath.is_absolute() || normalized.generic_string().starts_with(".."))
                    throw std::runtime_error("Contextual importer returned an invalid diagnostic.");
            }
            return result;
        }

        [[nodiscard]] std::string ImportDigest(const AssetSourceRecord& record, const AssetImportOutput& imported) const
        {
            std::string input = record.SourceDigest + "\nmetadata=" + record.MetadataDigest;
            auto dependencies = imported.SourceDependencies;
            std::ranges::sort(dependencies, {}, &AssetSourceDependency::RelativePath);
            for (const auto& dependency : dependencies)
            {
                input.push_back('\n');
                input += dependency.RelativePath.generic_string();
                input.push_back('=');
                input += dependency.Digest;
            }
            return Detail::DigestToString(Detail::Sha256(std::as_bytes(std::span(input))));
        }

        [[nodiscard]] std::filesystem::path ObjectPath(const AssetSourceRecord& record,
                                                       const std::string_view importDigest) const
        {
            const auto* importer = FindImporter(record);
            const auto effectiveVersion = importer ? importer->Version : record.ImporterVersion;
            return CacheRoot / "Objects" /
                   (std::string(importDigest) + "-" + record.Type.ToString() + "-" + std::to_string(effectiveVersion) +
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
                result.Signatures.emplace(record.Id,
                                          FileSignature{iterator->last_write_time(), iterator->file_size(),
                                                        std::filesystem::last_write_time(record.MetadataPath),
                                                        std::filesystem::file_size(record.MetadataPath)});
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
        std::unordered_map<AssetId, AssetImportStatus> ImportStatuses;
        mutable std::mutex Mutex;
    };

    std::string AssetTrashId::ToString() const { return m_Value.ToString(); }

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

    AssetImportResult AssetDatabase::ImportAll() { return ImportAll(AssetImportPolicy::FailFast); }

    AssetImportResult AssetDatabase::ImportAll(const AssetImportPolicy policy)
    {
        (void)Refresh();
        const auto records = Records();
        const auto objectRoot = m_Impl->CacheRoot / "Objects";
        std::filesystem::create_directories(objectRoot);
        AssetImportResult result;
        bool failed = false;
        for (const auto& record : records)
        {
            AssetImportStatus status;
            status.Id = record.Id;
            try
            {
                const auto imported = m_Impl->Import(record);
                status.Diagnostics = imported.Diagnostics;
                for (const auto& diagnostic : status.Diagnostics)
                    LogImportDiagnostic(record, diagnostic);
                {
                    std::scoped_lock lock(m_Impl->Mutex);
                    const auto stored = std::ranges::find(m_Impl->Records, record.Id, &AssetSourceRecord::Id);
                    if (stored != m_Impl->Records.end())
                    {
                        stored->SourceDependencies = imported.SourceDependencies;
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
                    const auto temporary = object.string() + ".tmp";
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
        }
        if (failed)
        {
            const auto previous = m_Impl->CacheRoot / "Runtime" / "catalog.json";
            if (std::filesystem::is_regular_file(previous))
                result.CatalogPath = previous;
            return result;
        }
        const auto cooked = AssetCooker::Cook(*this, AssetBuildProfile{}, m_Impl->CacheRoot / "Runtime");
        result.CatalogPath = cooked.CatalogPath;
        return result;
    }

    AssetImportStatus AssetDatabase::ImportStatus(const AssetId id) const
    {
        std::scoped_lock lock(m_Impl->Mutex);
        const auto found = m_Impl->ImportStatuses.find(id);
        return found == m_Impl->ImportStatuses.end() ? AssetImportStatus{.Id = id} : found->second;
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
        AssetSourceRecord validationRecord;
        validationRecord.RelativePath = std::filesystem::relative(destination, m_Impl->SourceRoot).lexically_normal();
        validationRecord.MetadataPath = metadata;
        if (registered->second.ContextualImport)
            (void)registered->second.ContextualImport(m_Impl->CreateImportContext(validationRecord), sourceBytes);
        else
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
        MoveAsset(id, record->RelativePath.parent_path() / newName);
    }

    void AssetDatabase::MoveAsset(const AssetId id, const std::filesystem::path& relativeDestination)
    {
        const auto record = Find(id);
        if (!record)
            throw std::invalid_argument("Cannot move an unknown asset ID.");
        const auto source = m_Impl->SourceRoot / record->RelativePath;
        const auto destination = ConfinedPath(m_Impl->SourceRoot, relativeDestination);
        if (source == destination)
            return;
        const auto sourceMetadata = record->MetadataPath;
        const auto destinationMetadata = std::filesystem::path(destination.string() + ".keiremeta");
        if (std::filesystem::exists(destination) || std::filesystem::exists(destinationMetadata))
            throw std::runtime_error("Asset move destination already exists.");
        std::filesystem::create_directories(destination.parent_path());
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
        try
        {
            (void)Refresh();
        }
        catch (...)
        {
            std::error_code ignored;
            std::filesystem::rename(destinationMetadata, sourceMetadata, ignored);
            std::filesystem::rename(destination, source, ignored);
            throw;
        }
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

    void AssetDatabase::MoveFolder(const std::filesystem::path& relativeSource,
                                   const std::filesystem::path& relativeDestination)
    {
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
        std::filesystem::rename(source, destination);
        try
        {
            (void)Refresh();
        }
        catch (...)
        {
            std::error_code ignored;
            std::filesystem::rename(destination, source, ignored);
            throw;
        }
    }

    std::vector<AssetId> AssetDatabase::DuplicateFolder(const std::filesystem::path& relativeSource,
                                                        const std::filesystem::path& relativeDestination)
    {
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
            (void)Refresh();
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
        const auto record = Find(id);
        if (!record)
            throw std::invalid_argument("Cannot trash an unknown asset ID.");
        const auto transaction = AssetId::Generate();
        const auto root = m_Impl->Specification.ProjectRoot / "Library" / "Trash" / transaction.ToString();
        std::filesystem::create_directories(root);
        const auto source = m_Impl->SourceRoot / record->RelativePath;
        const auto destination = root / source.filename();
        const auto destinationMetadata = root / record->MetadataPath.filename();
        std::filesystem::rename(source, destination);
        try
        {
            std::filesystem::rename(record->MetadataPath, destinationMetadata);
            const std::array assets{id};
            WriteTrashManifest(root, transaction, record->RelativePath, assets, false);
        }
        catch (...)
        {
            std::error_code ignored;
            std::filesystem::rename(destinationMetadata, record->MetadataPath, ignored);
            std::filesystem::rename(destination, source, ignored);
            std::filesystem::remove_all(root, ignored);
            throw;
        }
        (void)Refresh();
        return {AssetTrashId(transaction), record->RelativePath, root, {id}, false};
    }

    AssetTrashRecord AssetDatabase::TrashFolder(const std::filesystem::path& relativePath)
    {
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
        std::filesystem::rename(source, destination);
        try
        {
            WriteTrashManifest(root, transaction, normalized, assets, true);
        }
        catch (...)
        {
            std::error_code ignored;
            std::filesystem::rename(destination, source, ignored);
            std::filesystem::remove_all(root, ignored);
            throw;
        }
        (void)Refresh();
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
            std::filesystem::rename(source, destination);
        else
        {
            sourceMetadata = root / (record.OriginalPath.filename().string() + ".keiremeta");
            destinationMetadata = std::filesystem::path(destination.string() + ".keiremeta");
            if (std::filesystem::exists(destinationMetadata))
                throw std::runtime_error("Asset trash restore metadata destination already exists.");
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
        }
        try
        {
            (void)Refresh();
        }
        catch (...)
        {
            std::error_code ignored;
            if (!record.Folder)
                std::filesystem::rename(destinationMetadata, sourceMetadata, ignored);
            std::filesystem::rename(destination, source, ignored);
            throw;
        }
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
    }

    void AssetDatabase::PermanentlyDeleteTrash(const AssetTrashId id)
    {
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

    AssetCookResult AssetCooker::Cook(const AssetDatabase& database, const AssetBuildProfile& profile,
                                      const std::filesystem::path& outputDirectory)
    {
        if (profile.Name.empty() || profile.CompressionLevel < ZSTD_minCLevel() ||
            profile.CompressionLevel > ZSTD_maxCLevel() || profile.MaximumPackBytes <= Detail::PackHeaderBytes)
            throw std::invalid_argument("Asset build profile contains invalid compression or shard settings.");
        const auto records = database.Records();
        struct PreparedAsset final
        {
            const AssetSourceRecord* Record = nullptr;
            AssetImportOutput Imported;
            std::vector<AssetId> Dependencies;
        };
        std::vector<PreparedAsset> prepared;
        prepared.reserve(records.size());
        std::unordered_map<AssetId, std::size_t> indices;
        for (const auto& record : records)
        {
            if (profile.Strict && record.Importer != ImporterName(record.Type) &&
                !database.m_Impl->FindImporter(record))
                throw std::runtime_error("Strict cooking rejected an unsupported importer: " + record.Importer);
            auto imported = database.m_Impl->Import(record);
            auto dependencies = record.Dependencies;
            for (const auto dependency : imported.AssetDependencies)
            {
                if (std::ranges::find(dependencies, dependency) == dependencies.end())
                    dependencies.push_back(dependency);
            }
            std::ranges::sort(dependencies);
            indices.emplace(record.Id, prepared.size());
            prepared.push_back({&record, std::move(imported), std::move(dependencies)});
        }
        std::unordered_set<AssetId> included;
        if (profile.Roots.empty())
        {
            for (const auto& record : records)
                included.insert(record.Id);
        }
        else
        {
            std::vector<AssetId> pending = profile.Roots;
            while (!pending.empty())
            {
                const auto id = pending.back();
                pending.pop_back();
                if (!id || !included.insert(id).second)
                    continue;
                const auto found = indices.find(id);
                if (found == indices.end())
                    throw std::runtime_error("Cook root or dependency is missing from the asset database: " +
                                             id.ToString());
                const auto& dependencies = prepared[found->second].Dependencies;
                pending.insert(pending.end(), dependencies.begin(), dependencies.end());
            }
        }
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
            for (auto& asset : prepared)
            {
                const auto& record = *asset.Record;
                if (!included.contains(record.Id))
                    continue;
                auto bytes = std::move(asset.Imported.Bytes);
                if (const auto* importer = database.m_Impl->FindImporter(record); importer && importer->Cook)
                    bytes = importer->Cook(bytes, profile.Target);
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
                entry.Dependencies = std::move(asset.Dependencies);
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
                                    {"target", static_cast<std::uint8_t>(profile.Target)},
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
