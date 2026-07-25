#pragma once

#include "Keire/Assets/AssetPipeline.h"
#include "Keire/Assets/RenderingAssets.h"

#include "Keire/Log.h"

#include "KeireInternal/Assets/AssetDatabaseWorkerAccess.h"
#include "KeireInternal/Assets/AssetInternal.h"
#include "KeireInternal/Assets/AssetWorkerProtocol.h"
#include "KeireInternal/FileSystem.h"

#include <nlohmann/json.hpp>
#include <zstd.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
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
                normalized.native().starts_with(std::filesystem::path("..").native()))
                throw std::invalid_argument("Asset path must be a confined project-relative path.");
            return (root / normalized).lexically_normal();
        }

        [[nodiscard]] std::vector<std::byte> ReadSource(const std::filesystem::path& path, const std::size_t maximum)
        {
            std::error_code error;
            const auto size = std::filesystem::file_size(path, error);
            if (error)
                throw std::runtime_error("Could not inspect asset source: " + Detail::PathToUtf8(path));
            if (size > maximum || size > static_cast<std::uintmax_t>(std::numeric_limits<std::size_t>::max()))
                throw std::runtime_error("Asset source exceeds the configured maximum size: " +
                                         Detail::PathToUtf8(path));
            std::vector<std::byte> bytes(static_cast<std::size_t>(size));
            std::ifstream stream(path, std::ios::binary);
            if (!stream || (!bytes.empty() && !stream.read(reinterpret_cast<char*>(bytes.data()),
                                                           static_cast<std::streamsize>(bytes.size()))))
                throw std::runtime_error("Could not read asset source: " + Detail::PathToUtf8(path));
            return bytes;
        }

        void WriteFileAtomically(const std::filesystem::path& path, const std::span<const std::byte> bytes,
                                 const std::string_view temporarySuffix = ".asset-operation.tmp")
        {
            std::filesystem::create_directories(path.parent_path());
            const auto temporary = Detail::PathWithSuffix(path, temporarySuffix);
            std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
            if (!stream || (!bytes.empty() && !stream.write(reinterpret_cast<const char*>(bytes.data()),
                                                            static_cast<std::streamsize>(bytes.size()))))
                throw std::runtime_error("Could not write asset operation data: " + Detail::PathToUtf8(path));
            stream.close();
            if (!stream)
                throw std::runtime_error("Could not finish asset operation data: " + Detail::PathToUtf8(path));
            Detail::AtomicReplace(temporary, path);
        }

        void WriteJsonAtomically(const std::filesystem::path& path, const Json& value)
        {
            const auto serialized = value.dump(2) + '\n';
            WriteFileAtomically(path, std::as_bytes(std::span(serialized.data(), serialized.size())),
                                ".json-write.tmp");
        }

        [[nodiscard]] Json ReadJsonFile(const std::filesystem::path& path, const std::size_t maximum)
        {
            const auto bytes = ReadSource(path, maximum);
            return Json::parse(reinterpret_cast<const char*>(bytes.data()),
                               reinterpret_cast<const char*>(bytes.data() + bytes.size()));
        }

        [[nodiscard]] std::string LowerExtension(const std::filesystem::path& path)
        {
            auto extension = Detail::PathToUtf8(path.extension());
            std::ranges::transform(extension, extension.begin(),
                                   [](const unsigned char value) { return static_cast<char>(std::tolower(value)); });
            return extension;
        }

        [[nodiscard]] AssetTypeId InferType(const std::filesystem::path& path)
        {
            static const std::unordered_set<std::string> TextExtensions{
                ".txt",  ".md",  ".json", ".xml",  ".yaml", ".yml",  ".csv",  ".ini",
                ".toml", ".lua", ".glsl", ".hlsl", ".vert", ".frag", ".comp", ".cs"};
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
                auto location = Detail::PathToUtf8(source);
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
                                    Detail::PathToUtf8(record.RelativePath), record.Id.ToString(), record.Importer,
                                    location, diagnostic.Message);
                    break;
                case AssetDiagnosticSeverity::Warning:
                    KEIRE_CORE_WARN("Asset import warning for '{}' (id={}, importer={}) at {}: {}",
                                    Detail::PathToUtf8(record.RelativePath), record.Id.ToString(), record.Importer,
                                    location, diagnostic.Message);
                    break;
                case AssetDiagnosticSeverity::Error:
                    KEIRE_CORE_ERROR("Asset import failed for '{}' (id={}, importer={}) at {}: {}",
                                     Detail::PathToUtf8(record.RelativePath), record.Id.ToString(), record.Importer,
                                     location, diagnostic.Message);
                    break;
                }
            }
            catch (...)
            {
                std::fprintf(stderr, "Asset import diagnostic logging failed for %s: %s\n",
                             Detail::PathToUtf8(record.RelativePath).c_str(), diagnostic.Message.c_str());
            }
        }

        [[nodiscard]] bool IsWithin(const std::filesystem::path& parent, const std::filesystem::path& candidate)
        {
            const auto relative = candidate.lexically_normal().lexically_relative(parent.lexically_normal());
            return !relative.empty() && !relative.is_absolute() &&
                   !relative.native().starts_with(std::filesystem::path("..").native());
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
                                {"originalPath", Detail::PathToUtf8(originalPath)},
                                {"folder", folder},
                                {"assets", std::move(assetIds)}};
            const auto destination = root / "trash.json";
            const auto temporary = Detail::PathWithSuffix(destination, ".tmp");
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
            result.OriginalPath = Detail::PathFromUtf8(manifest.at("originalPath").get<std::string>());
            result.Folder = manifest.at("folder").get<bool>();
            const auto normalized = result.OriginalPath.lexically_normal();
            if (!result.Id || result.OriginalPath.empty() || result.OriginalPath.is_absolute() ||
                normalized.native().starts_with(std::filesystem::path("..").native()))
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

        [[nodiscard]] Json EncodeImportSettings(const AssetImportSettings& settings)
        {
            Json result = Json::object();
            for (const auto& [key, value] : settings)
                std::visit([&](const auto& typed) { result[key] = typed; }, value);
            return result;
        }

        [[nodiscard]] AssetImportSettings DecodeImportSettings(const Json& values)
        {
            if (!values.is_object())
                throw std::runtime_error("Asset importSettings metadata must be an object.");
            AssetImportSettings result;
            for (auto iterator = values.begin(); iterator != values.end(); ++iterator)
            {
                if (iterator->is_boolean())
                    result.emplace(iterator.key(), iterator->get<bool>());
                else if (iterator->is_number_integer())
                    result.emplace(iterator.key(), iterator->get<std::int64_t>());
                else if (iterator->is_number())
                    result.emplace(iterator.key(), iterator->get<double>());
                else if (iterator->is_string())
                    result.emplace(iterator.key(), iterator->get<std::string>());
                else
                    throw std::runtime_error("Asset import setting values must be scalar.");
            }
            return result;
        }

        void WriteMetadata(const std::filesystem::path& path, const AssetId id, const AssetTypeId type,
                           const std::string_view importer, const std::uint32_t importerVersion,
                           const AssetImportSettings& settings = {})
        {
            Json metadata{{"schemaVersion", 1},
                          {"id", id.ToString()},
                          {"type", type.ToString()},
                          {"importer", importer},
                          {"importerVersion", importerVersion},
                          {"dependencies", Json::array()},
                          {"subAssets", Json::array()}};
            if (!settings.empty())
                metadata["importSettings"] = EncodeImportSettings(settings);
            std::filesystem::create_directories(path.parent_path());
            const auto temporary = Detail::PathWithSuffix(path, ".tmp");
            std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
            if (!stream)
                throw std::runtime_error("Could not create asset metadata: " + Detail::PathToUtf8(path));
            stream << metadata.dump(2) << '\n';
            stream.close();
            if (!stream)
                throw std::runtime_error("Could not write asset metadata: " + Detail::PathToUtf8(path));
            Detail::AtomicReplace(temporary, path);
        }

        [[nodiscard]] bool UpgradeMetadataImporterVersion(const std::filesystem::path& path,
                                                          const std::string_view importer,
                                                          const std::uint32_t importerVersion)
        {
            auto metadata = ReadJsonFile(path, 1024U * 1024U);
            if (!metadata.is_object() || metadata.value("schemaVersion", 0) != 1 ||
                metadata.value("importer", std::string{}) != importer)
                throw std::runtime_error("Asset metadata cannot be upgraded by a different importer: " +
                                         Detail::PathToUtf8(path));
            const auto currentVersion = metadata.value("importerVersion", 0U);
            if (currentVersion >= importerVersion)
                return false;
            metadata["importerVersion"] = importerVersion;
            WriteJsonAtomically(path, metadata);
            return true;
        }

        void UpdateMetadataSubAssets(const std::filesystem::path& path,
                                     const std::span<const AssetGeneratedSubAsset> generated)
        {
            auto metadata = ReadJsonFile(path, 1024U * 1024U);
            Json subAssets = Json::array();
            for (const auto& subAsset : generated)
                subAssets.push_back(subAsset.Id.ToString());
            if (!metadata.contains("subAssets") || metadata["subAssets"] != subAssets)
            {
                metadata["subAssets"] = std::move(subAssets);
                WriteJsonAtomically(path, metadata);
            }
        }

        [[nodiscard]] AssetSourceRecord ReadMetadata(const std::filesystem::path& sourceRoot,
                                                     const std::filesystem::path& source,
                                                     const std::size_t maximumSourceBytes, const bool digestSource,
                                                     const AssetImporterRegistration* inferredImporter)
        {
            AssetSourceRecord record;
            record.RelativePath = std::filesystem::relative(source, sourceRoot).lexically_normal();
            record.MetadataPath = Detail::PathWithSuffix(source, ".keiremeta");
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
                throw std::runtime_error("Could not open asset metadata: " + Detail::PathToUtf8(record.MetadataPath));
            Json metadata;
            stream >> metadata;
            if (!stream || !metadata.is_object() || metadata.value("schemaVersion", 0) != 1)
                throw std::runtime_error("Asset metadata has an unsupported schema: " +
                                         Detail::PathToUtf8(record.MetadataPath));
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
            if (metadata.contains("importSettings"))
                record.ImportSettings = DecodeImportSettings(metadata["importSettings"]);
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

        [[nodiscard]] std::filesystem::path DirectoryPublicationJournal(const std::filesystem::path& destination)
        {
            return Detail::PathWithSuffix(destination, ".publish.json");
        }

        void RecoverDirectoryPublication(const std::filesystem::path& requestedDestination)
        {
            const auto destination = std::filesystem::absolute(requestedDestination).lexically_normal();
            const auto backup = Detail::PathWithSuffix(destination, ".bak");
            const auto journalPath = DirectoryPublicationJournal(destination);
            std::error_code error;
            if (!std::filesystem::is_regular_file(journalPath, error))
            {
                error.clear();
                if (!std::filesystem::exists(destination, error) && std::filesystem::exists(backup, error))
                    Detail::RenamePathWithRetry(backup, destination);
                else if (std::filesystem::exists(destination, error) && std::filesystem::exists(backup, error))
                    std::filesystem::remove_all(backup, error);
                return;
            }

            const auto journal = ReadJsonFile(journalPath, 1024U * 1024U);
            if (journal.value("schemaVersion", 0) != 1 || !journal.contains("temporary") ||
                !journal.contains("destination") || !journal.contains("backup") || !journal.contains("state") ||
                !journal.contains("hadDestination"))
                throw std::runtime_error("Asset catalog publication journal is invalid: " +
                                         Detail::PathToUtf8(journalPath));

            const auto temporary =
                std::filesystem::absolute(Detail::PathFromUtf8(journal.at("temporary").get<std::string>()))
                    .lexically_normal();
            const auto recordedDestination =
                std::filesystem::absolute(Detail::PathFromUtf8(journal.at("destination").get<std::string>()))
                    .lexically_normal();
            const auto recordedBackup =
                std::filesystem::absolute(Detail::PathFromUtf8(journal.at("backup").get<std::string>()))
                    .lexically_normal();
            if (recordedDestination != destination || recordedBackup != backup ||
                temporary.parent_path() != destination.parent_path() ||
                !temporary.filename().native().starts_with(destination.filename().native() +
                                                           std::filesystem::path(".tmp-").native()))
                throw std::runtime_error("Asset catalog publication journal escapes its destination: " +
                                         Detail::PathToUtf8(journalPath));

            const auto state = journal.at("state").get<std::string>();
            const bool hadDestination = journal.at("hadDestination").get<bool>();
            if (state == "published")
            {
                if (!std::filesystem::is_directory(destination, error))
                    throw std::runtime_error("Published asset catalog is missing during recovery: " +
                                             Detail::PathToUtf8(destination));
                std::filesystem::remove_all(backup, error);
                if (error)
                    throw std::runtime_error("Could not retire the previous asset catalog: " + error.message());
            }
            else if (state == "prepared" || state == "backedUp")
            {
                if (std::filesystem::exists(backup, error))
                {
                    std::filesystem::remove_all(destination, error);
                    Detail::RenamePathWithRetry(backup, destination);
                }
                else if (!hadDestination)
                {
                    std::filesystem::remove_all(destination, error);
                }
                else if (!std::filesystem::is_directory(destination, error))
                {
                    throw std::runtime_error("Last-good asset catalog cannot be restored: " +
                                             Detail::PathToUtf8(destination));
                }
            }
            else
            {
                throw std::runtime_error("Asset catalog publication journal has an unknown state: " + state);
            }
            std::filesystem::remove_all(temporary, error);
            if (error)
                throw std::runtime_error("Could not remove an interrupted asset catalog: " + error.message());
            std::filesystem::remove(journalPath, error);
            if (error)
                throw std::runtime_error("Could not finish asset catalog publication recovery: " + error.message());
        }

        void ReplaceDirectory(const std::filesystem::path& requestedTemporary,
                              const std::filesystem::path& requestedDestination)
        {
            const auto temporary = std::filesystem::absolute(requestedTemporary).lexically_normal();
            const auto destination = std::filesystem::absolute(requestedDestination).lexically_normal();
            RecoverDirectoryPublication(destination);
            const auto backup = Detail::PathWithSuffix(destination, ".bak");
            const auto journalPath = DirectoryPublicationJournal(destination);
            std::error_code error;
            std::filesystem::remove_all(backup, error);
            const bool hadDestination = std::filesystem::exists(destination);
            Json journal{{"schemaVersion", 1},
                         {"state", "prepared"},
                         {"temporary", Detail::PathToUtf8(temporary)},
                         {"destination", Detail::PathToUtf8(destination)},
                         {"backup", Detail::PathToUtf8(backup)},
                         {"hadDestination", hadDestination}};
            WriteJsonAtomically(journalPath, journal);
            if (hadDestination)
                Detail::RenamePathWithRetry(destination, backup);
            journal["state"] = "backedUp";
            WriteJsonAtomically(journalPath, journal);
            try
            {
                Detail::RenamePathWithRetry(temporary, destination);
                journal["state"] = "published";
                WriteJsonAtomically(journalPath, journal);
            }
            catch (...)
            {
                RecoverDirectoryPublication(destination);
                throw;
            }
            std::filesystem::remove_all(backup, error);
            if (error)
                throw std::runtime_error("Could not remove the previous asset catalog: " + error.message());
            std::filesystem::remove(journalPath, error);
            if (error)
                throw std::runtime_error("Could not clean an asset catalog publication journal: " + error.message());
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

        void ThrowIfOperationCancelled(const std::stop_token cancellation)
        {
            if (cancellation.stop_requested())
                throw AssetOperationCancelled();
        }

        void ReportOperationProgress(const AssetOperationProgressCallback& callback, const AssetOperationPhase phase,
                                     const std::size_t completed, const std::size_t total,
                                     std::filesystem::path currentPath = {})
        {
            if (callback)
                callback({phase, completed, total, std::move(currentPath)});
        }

        void RemovePathNoThrow(const std::filesystem::path& path) noexcept
        {
            std::error_code ignored;
            std::filesystem::remove_all(path, ignored);
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
            OperationMutex = std::make_unique<Detail::InterprocessMutex>(Specification.ProjectRoot /
                                                                         "Library/AssetOperations/project.lock");
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
            RecoverDirectoryPublication(CacheRoot / "Runtime");
            RecoverInterruptedExternalImports();
        }

        void RecoverInterruptedExternalImports()
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
                const auto journal = ReadJsonFile(journalPath, 16U * 1024U * 1024U);
                if (journal.value("schemaVersion", 0) != 1 || !journal.contains("state"))
                    throw std::runtime_error("External asset import journal is invalid: " +
                                             Detail::PathToUtf8(journalPath));
                const auto state = journal.at("state").get<std::string>();
                if (state == "committed")
                    continue;
                if (state == "publishing")
                {
                    if (!journal.contains("entries") || !journal["entries"].is_array())
                        throw std::runtime_error("Publishing asset journal has no recovery entries.");
                    for (std::size_t index = journal["entries"].size(); index > 0; --index)
                    {
                        const auto& entry = journal["entries"][index - 1];
                        const auto destination =
                            ConfinedPath(SourceRoot, Detail::PathFromUtf8(entry.at("destination").get<std::string>()));
                        const auto metadata = Detail::PathWithSuffix(destination, ".keiremeta");
                        if (entry.value("replaced", false))
                        {
                            const auto prefix = std::to_string(index - 1);
                            WriteFileAtomically(destination,
                                                ReadSource(iterator->path() / "before" / (prefix + ".source"),
                                                           Specification.MaximumSourceBytes));
                            WriteFileAtomically(
                                metadata,
                                ReadSource(iterator->path() / "before" / (prefix + ".metadata"), 16U * 1024U * 1024U));
                        }
                        else
                        {
                            RemovePathNoThrow(destination);
                            RemovePathNoThrow(metadata);
                        }
                    }
                }
                RemovePathNoThrow(iterator->path());
            }
            if (error)
                throw std::runtime_error("Could not recover external asset import transactions: " + error.message());
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
            context.Asset = record.Id;
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
            context.ImportSettings = record.ImportSettings;
            context.ResolveSubAssetId = [parent = record.Id](const std::string_view key)
            {
                if (key.empty())
                    throw std::invalid_argument("Generated subasset keys must not be empty.");
                std::string identity = parent.ToString();
                identity.push_back('\n');
                identity.append(key);
                const auto digest = Detail::Sha256(std::as_bytes(std::span(identity)));
                std::uint64_t high = 0;
                std::uint64_t low = 0;
                for (std::size_t index = 0; index < 8; ++index)
                {
                    high = (high << 8U) | std::to_integer<std::uint8_t>(digest[index]);
                    low = (low << 8U) | std::to_integer<std::uint8_t>(digest[index + 8U]);
                }
                // Preserve the UUID layout used by AssetId::Generate while deriving the payload deterministically.
                high = (high & 0xffffffffffff0fffULL) | 0x0000000000005000ULL;
                low = (low & 0x3fffffffffffffffULL) | 0x8000000000000000ULL;
                return AssetId(high, low);
            };
            return context;
        }

        [[nodiscard]] AssetImportSettings NormalizeSettings(const AssetImporterRegistration& importer,
                                                            const AssetImportSettings& requested) const
        {
            AssetImportSettings result;
            for (const auto& option : importer.ImportOptions)
                result.emplace(option.Key, option.DefaultValue);
            for (const auto& [key, value] : requested)
            {
                const auto descriptor =
                    std::ranges::find(importer.ImportOptions, key, &AssetImportOptionDescriptor::Key);
                if (descriptor == importer.ImportOptions.end())
                    throw std::invalid_argument("Unknown import setting '" + key + "'.");
                const bool typeMatches =
                    (descriptor->Kind == AssetImportOptionKind::Boolean && std::holds_alternative<bool>(value)) ||
                    (descriptor->Kind == AssetImportOptionKind::Integer &&
                     std::holds_alternative<std::int64_t>(value)) ||
                    (descriptor->Kind == AssetImportOptionKind::Scalar && std::holds_alternative<double>(value)) ||
                    (descriptor->Kind == AssetImportOptionKind::Choice && std::holds_alternative<std::string>(value));
                if (!typeMatches)
                    throw std::invalid_argument("Import setting '" + key + "' has the wrong value type.");
                result[key] = value;
            }
            for (const auto& option : importer.ImportOptions)
            {
                const auto& value = result.at(option.Key);
                std::optional<double> numeric;
                if (const auto* integer = std::get_if<std::int64_t>(&value))
                    numeric = static_cast<double>(*integer);
                else if (const auto* scalar = std::get_if<double>(&value))
                    numeric = *scalar;
                if (numeric && ((!std::isfinite(*numeric)) || (option.Minimum && *numeric < *option.Minimum) ||
                                (option.Maximum && *numeric > *option.Maximum)))
                    throw std::invalid_argument("Import setting '" + option.Key + "' is outside its valid range.");
                if (const auto* choice = std::get_if<std::string>(&value);
                    choice && std::ranges::find(option.Choices, *choice) == option.Choices.end())
                    throw std::invalid_argument("Import setting '" + option.Key + "' has an invalid choice.");
            }
            return importer.NormalizeImportSettings ? importer.NormalizeImportSettings(result) : result;
        }

        void ValidateImportOutput(const AssetImportOutput& result) const
        {
            if (result.Bytes.size() > Specification.MaximumSourceBytes)
                throw std::runtime_error("Imported asset exceeds the configured maximum size.");
            std::unordered_set<std::string> dependencies;
            for (const auto& dependency : result.SourceDependencies)
            {
                const auto normalized = dependency.RelativePath.lexically_normal();
                const auto comparable = Detail::PathToUtf8(normalized);
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
            std::unordered_set<AssetId> subAssetIds;
            std::unordered_set<std::string> subAssetKeys;
            for (const auto& subAsset : result.SubAssets)
            {
                if (!subAsset.Id || !subAsset.Type || subAsset.Key.empty() || subAsset.Key.size() > 4096U ||
                    subAsset.Name.empty() || subAsset.Name.size() > 4096U ||
                    subAsset.Bytes.size() > Specification.MaximumSourceBytes ||
                    !subAssetIds.insert(subAsset.Id).second || !subAssetKeys.insert(subAsset.Key).second)
                    throw std::runtime_error("Contextual importer returned an invalid generated subasset.");
                std::unordered_set<AssetId> generatedDependencies;
                for (const auto dependency : subAsset.AssetDependencies)
                    if (!dependency || !generatedDependencies.insert(dependency).second)
                        throw std::runtime_error("Generated subasset contains an invalid dependency record.");
            }
            if (result.Diagnostics.size() > 4096)
                throw std::runtime_error("Contextual importer returned too many diagnostics.");
            for (const auto& diagnostic : result.Diagnostics)
            {
                const auto normalized = diagnostic.RelativePath.lexically_normal();
                if (diagnostic.Message.empty() || diagnostic.Message.size() > 16U * 1024U ||
                    diagnostic.RelativePath.is_absolute() ||
                    normalized.native().starts_with(std::filesystem::path("..").native()))
                    throw std::runtime_error("Contextual importer returned an invalid diagnostic.");
            }
        }

        [[nodiscard]] AssetImportOutput ImportSource(const AssetSourceRecord& record,
                                                     const std::span<const std::byte> source) const
        {
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
                result.Bytes.assign(source.begin(), source.end());
            else
                throw std::runtime_error("No compatible importer is registered for asset: " +
                                         Detail::PathToUtf8(record.RelativePath));
            ValidateImportOutput(result);
            return result;
        }

        [[nodiscard]] AssetImportOutput Import(const AssetSourceRecord& record) const
        {
            const auto source = ReadSource(SourceRoot / record.RelativePath, Specification.MaximumSourceBytes);
            return ImportSource(record, source);
        }

        [[nodiscard]] std::string ImportDigest(const AssetSourceRecord& record, const AssetImportOutput& imported) const
        {
            std::string input = record.SourceDigest + "\nmetadata=" + record.MetadataDigest;
            auto dependencies = imported.SourceDependencies;
            std::ranges::sort(dependencies, {}, &AssetSourceDependency::RelativePath);
            for (const auto& dependency : dependencies)
            {
                input.push_back('\n');
                input += Detail::PathToUtf8(dependency.RelativePath);
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

        [[nodiscard]] std::optional<AssetImportOutput> RestoreCachedImport(const AssetSourceRecord& record) const
        {
            const auto* importer = FindImporter(record);
            if (!importer || !importer->RestoreCachedOutput)
                return std::nullopt;
            const AssetImportOutput dependencyFree;
            const auto object = ObjectPath(record, ImportDigest(record, dependencyFree));
            if (!std::filesystem::is_regular_file(object))
                return std::nullopt;
            auto restored = importer->RestoreCachedOutput(ReadSource(object, Specification.MaximumSourceBytes));
            ValidateImportOutput(restored);
            if (!restored.SourceDependencies.empty())
                throw std::logic_error("A dependency-free cached importer restored source dependencies.");
            return restored;
        }

        struct PreparedImport final
        {
            std::string SourceDigest;
            std::string MetadataDigest;
            AssetImportOutput Output;
        };

        void StoreValidatedImport(const AssetSourceRecord& record, AssetImportOutput output)
        {
            std::scoped_lock lock(Mutex);
            ValidatedImports.insert_or_assign(
                record.Id, PreparedImport{record.SourceDigest, record.MetadataDigest, std::move(output)});
        }

        [[nodiscard]] std::optional<AssetImportOutput> TakeValidatedImport(const AssetSourceRecord& record)
        {
            std::scoped_lock lock(Mutex);
            const auto found = ValidatedImports.find(record.Id);
            if (found == ValidatedImports.end())
                return std::nullopt;
            if (found->second.SourceDigest != record.SourceDigest ||
                found->second.MetadataDigest != record.MetadataDigest)
            {
                ValidatedImports.erase(found);
                return std::nullopt;
            }
            auto output = std::move(found->second.Output);
            ValidatedImports.erase(found);
            return output;
        }

        void ResetCookInputs()
        {
            std::scoped_lock lock(Mutex);
            CookInputs.clear();
        }

        void StoreCookInput(const AssetSourceRecord& record, AssetImportOutput output)
        {
            std::scoped_lock lock(Mutex);
            CookInputs.insert_or_assign(record.Id,
                                        PreparedImport{record.SourceDigest, record.MetadataDigest, std::move(output)});
        }

        [[nodiscard]] std::optional<AssetImportOutput> TakeCookInput(const AssetSourceRecord& record)
        {
            std::scoped_lock lock(Mutex);
            const auto found = CookInputs.find(record.Id);
            if (found == CookInputs.end())
                return std::nullopt;
            if (found->second.SourceDigest != record.SourceDigest ||
                found->second.MetadataDigest != record.MetadataDigest)
            {
                CookInputs.erase(found);
                return std::nullopt;
            }
            auto output = std::move(found->second.Output);
            CookInputs.erase(found);
            return output;
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
                auto comparable = Detail::PathToUtf8(record.RelativePath);
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

        [[nodiscard]] FileSignature ReadSignature(const std::filesystem::path& source,
                                                  const std::filesystem::path& metadata) const
        {
            return {std::filesystem::last_write_time(source), std::filesystem::file_size(source),
                    std::filesystem::last_write_time(metadata), std::filesystem::file_size(metadata)};
        }

        void PublishRecord(AssetSourceRecord record, const FileSignature& signature)
        {
            std::scoped_lock lock(Mutex);
            const auto id = record.Id;
            const auto samePath = std::ranges::find(Records, record.RelativePath, &AssetSourceRecord::RelativePath);
            if (samePath != Records.end() && samePath->Id != record.Id)
                throw std::runtime_error("Asset path is already registered: " +
                                         Detail::PathToUtf8(record.RelativePath));
            const auto existing = std::ranges::find(Records, record.Id, &AssetSourceRecord::Id);
            if (existing == Records.end())
                Records.push_back(std::move(record));
            else
                *existing = std::move(record);
            std::ranges::sort(Records, [](const auto& left, const auto& right) { return left.Id < right.Id; });
            Observed.insert_or_assign(id, signature);
            PendingChanges.erase(id);
        }

        void RemoveRecords(const std::span<const AssetId> identities)
        {
            std::scoped_lock lock(Mutex);
            const auto contains = [&](const AssetId id)
            { return std::ranges::find(identities, id) != identities.end(); };
            std::erase_if(Records, [&](const AssetSourceRecord& record) { return contains(record.Id); });
            for (const auto id : identities)
            {
                Observed.erase(id);
                PendingChanges.erase(id);
                ImportStatuses.erase(id);
                ValidatedImports.erase(id);
                CookInputs.erase(id);
            }
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
        std::unordered_map<AssetId, PreparedImport> ValidatedImports;
        std::unordered_map<AssetId, PreparedImport> CookInputs;
        std::unique_ptr<Detail::InterprocessMutex> OperationMutex;
        mutable std::mutex Mutex;
    };

} // namespace Keire
