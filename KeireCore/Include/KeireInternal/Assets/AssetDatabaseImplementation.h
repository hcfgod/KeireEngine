#pragma once

#include "Keire/Assets/AssetPipeline.h"
#include "Keire/Assets/RenderingAssets.h"

#include "Keire/Log.h"

#include "KeireInternal/Assets/AssetDatabaseWorkerAccess.h"
#include "KeireInternal/Assets/AssetImportOutputCache.h"
#include "KeireInternal/Assets/AssetInternal.h"
#include "KeireInternal/Assets/AssetWorkerProtocol.h"
#include "KeireInternal/FileSystem.h"

#include <nlohmann/json.hpp>
#include <zstd.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <fstream>
#include <functional>
#include <limits>
#include <mutex>
#include <optional>
#include <ranges>
#include <thread>
#include <unordered_map>
#include <unordered_set>

namespace Keire
{
    namespace Detail
    {
        struct AssetFileSignature final
        {
            std::filesystem::file_time_type Modified{};
            std::uintmax_t Size = 0;
            std::filesystem::file_time_type MetadataModified{};
            std::uintmax_t MetadataSize = 0;

            [[nodiscard]] bool operator==(const AssetFileSignature&) const noexcept = default;
        };
    } // namespace Detail

    namespace
    {
        using Json = nlohmann::json;
        using FileSignature = Detail::AssetFileSignature;

        [[maybe_unused, nodiscard]] std::filesystem::path ConfinedPath(const std::filesystem::path& root,
                                                                       const std::filesystem::path& relative)
        {
            const auto normalized = relative.lexically_normal();
            if (relative.empty() || relative.is_absolute() || normalized.empty() ||
                normalized.native().starts_with(std::filesystem::path("..").native()))
                throw std::invalid_argument("Asset path must be a confined project-relative path.");
            return (root / normalized).lexically_normal();
        }

        [[maybe_unused, nodiscard]] std::vector<std::byte> ReadSource(const std::filesystem::path& path,
                                                                      const std::size_t maximum)
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

        [[maybe_unused]] void WriteFileAtomically(const std::filesystem::path& path,
                                                  const std::span<const std::byte> bytes,
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

        [[maybe_unused]] void WriteJsonAtomically(const std::filesystem::path& path, const Json& value)
        {
            const auto serialized = value.dump(2) + '\n';
            WriteFileAtomically(path, std::as_bytes(std::span(serialized.data(), serialized.size())),
                                ".json-write.tmp");
        }

        [[maybe_unused, nodiscard]] Json ReadJsonFile(const std::filesystem::path& path, const std::size_t maximum)
        {
            const auto bytes = ReadSource(path, maximum);
            return Json::parse(reinterpret_cast<const char*>(bytes.data()),
                               reinterpret_cast<const char*>(bytes.data() + bytes.size()));
        }

        [[maybe_unused, nodiscard]] std::string LowerExtension(const std::filesystem::path& path)
        {
            auto extension = Detail::PathToUtf8(path.extension());
            std::ranges::transform(extension, extension.begin(),
                                   [](const unsigned char value) { return static_cast<char>(std::tolower(value)); });
            return extension;
        }

        [[maybe_unused, nodiscard]] AssetId DeriveSubAssetId(const AssetId parent, const std::string_view key)
        {
            if (!parent || key.empty())
                throw std::invalid_argument("Generated subassets require a valid parent and non-empty key.");
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
        }

        [[maybe_unused, nodiscard]] AssetTypeId InferType(const std::filesystem::path& path)
        {
            static const std::unordered_set<std::string> TextExtensions{
                ".txt",  ".md",  ".json", ".xml",  ".yaml", ".yml",  ".csv",  ".ini",
                ".toml", ".lua", ".glsl", ".hlsl", ".vert", ".frag", ".comp", ".cs"};
            return TextExtensions.contains(LowerExtension(path)) ? TextAsset::StaticType() : BinaryAsset::StaticType();
        }

        [[maybe_unused, nodiscard]] std::string ImporterName(const AssetTypeId type)
        {
            return type == TextAsset::StaticType() ? "Keire.Text" : "Keire.Binary";
        }

        [[maybe_unused]] void LogImportDiagnostic(const AssetSourceRecord& record,
                                                  const AssetImportDiagnostic& diagnostic) noexcept
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

        [[maybe_unused, nodiscard]] bool IsWithin(const std::filesystem::path& parent,
                                                  const std::filesystem::path& candidate)
        {
            const auto relative = candidate.lexically_normal().lexically_relative(parent.lexically_normal());
            return !relative.empty() && !relative.is_absolute() &&
                   !relative.native().starts_with(std::filesystem::path("..").native());
        }

        [[maybe_unused, nodiscard]] bool IsSameOrWithin(const std::filesystem::path& parent,
                                                        const std::filesystem::path& candidate)
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

        [[maybe_unused]] void WriteTrashManifest(const std::filesystem::path& root, const AssetId id,
                                                 const std::filesystem::path& originalPath,
                                                 const std::span<const AssetId> assets, const bool folder)
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

        [[maybe_unused, nodiscard]] ParsedTrashRecord ReadTrashManifest(const std::filesystem::path& root)
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

        [[maybe_unused, nodiscard]] Json EncodeImportSettings(const AssetImportSettings& settings)
        {
            Json result = Json::object();
            for (const auto& [key, value] : settings)
                std::visit([&](const auto& typed) { result[key] = typed; }, value);
            return result;
        }

        [[maybe_unused, nodiscard]] AssetImportSettings DecodeImportSettings(const Json& values)
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

        [[maybe_unused]] void WriteMetadata(const std::filesystem::path& path, const AssetId id, const AssetTypeId type,
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

        [[maybe_unused, nodiscard]] bool UpgradeMetadataImporterVersion(const std::filesystem::path& path,
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

        [[maybe_unused]] void UpdateMetadataSubAssets(const std::filesystem::path& path,
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

        [[maybe_unused]] void UpdateMetadataImportOutput(const std::filesystem::path& path, const AssetTypeId type,
                                                         const std::span<const AssetGeneratedSubAsset> generated)
        {
            auto metadata = ReadJsonFile(path, 1024U * 1024U);
            Json subAssets = Json::array();
            for (const auto& subAsset : generated)
                subAssets.push_back(subAsset.Id.ToString());
            const auto encodedType = type.ToString();
            if (!metadata.contains("type") || metadata["type"] != encodedType || !metadata.contains("subAssets") ||
                metadata["subAssets"] != subAssets)
            {
                metadata["type"] = encodedType;
                metadata["subAssets"] = std::move(subAssets);
                WriteJsonAtomically(path, metadata);
            }
        }

        [[maybe_unused]] void UpdateMetadataImportSettings(const std::filesystem::path& path,
                                                           const AssetImportSettings& settings)
        {
            auto metadata = ReadJsonFile(path, 1024U * 1024U);
            const auto encoded = EncodeImportSettings(settings);
            if (!metadata.contains("importSettings") || metadata["importSettings"] != encoded)
            {
                metadata["importSettings"] = encoded;
                WriteJsonAtomically(path, metadata);
            }
        }

        [[maybe_unused]] void IncrementMetadataImportRevision(const std::filesystem::path& path)
        {
            auto metadata = ReadJsonFile(path, 1024U * 1024U);
            const auto current = metadata.value("importRevision", std::uint64_t{0});
            metadata["importRevision"] =
                current == std::numeric_limits<std::uint64_t>::max() ? std::uint64_t{1} : current + 1;
            WriteJsonAtomically(path, metadata);
        }

        [[maybe_unused, nodiscard]] AssetSourceRecord ReadMetadata(const std::filesystem::path& sourceRoot,
                                                                   const std::filesystem::path& source,
                                                                   const std::size_t maximumSourceBytes,
                                                                   const bool digestSource,
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

        [[maybe_unused, nodiscard]] std::vector<std::byte> Compress(const std::span<const std::byte> bytes,
                                                                    const int level)
        {
            std::vector<std::byte> result(ZSTD_compressBound(bytes.size()));
            const auto size = ZSTD_compress(result.data(), result.size(), bytes.data(), bytes.size(), level);
            if (ZSTD_isError(size))
                throw std::runtime_error(std::string("Zstandard compression failed: ") + ZSTD_getErrorName(size));
            result.resize(size);
            return result;
        }

        [[maybe_unused, nodiscard]] std::filesystem::path
        DirectoryPublicationJournal(const std::filesystem::path& destination)
        {
            return Detail::PathWithSuffix(destination, ".publish.json");
        }

        [[maybe_unused]] void RecoverDirectoryPublication(const std::filesystem::path& requestedDestination)
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
                error.clear();
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

        [[maybe_unused]] void ReplaceDirectory(const std::filesystem::path& requestedTemporary,
                                               const std::filesystem::path& requestedDestination)
        {
            const auto temporary = std::filesystem::absolute(requestedTemporary).lexically_normal();
            const auto destination = std::filesystem::absolute(requestedDestination).lexically_normal();
            RecoverDirectoryPublication(destination);
            const auto backup = Detail::PathWithSuffix(destination, ".bak");
            std::error_code error;
            std::filesystem::remove_all(backup, error);
            error.clear();
            if (!std::filesystem::is_directory(temporary, error) || error)
                throw std::runtime_error("Prepared asset catalog directory is missing: " +
                                         Detail::PathToUtf8(temporary));
            if (std::filesystem::exists(destination, error))
            {
                if (error || std::filesystem::is_symlink(destination, error) ||
                    !std::filesystem::is_directory(destination, error))
                    throw std::runtime_error("Asset catalog destination is not a regular directory: " +
                                             Detail::PathToUtf8(destination));
            }
            else
            {
                if (error)
                    throw std::runtime_error("Could not inspect the asset catalog destination: " + error.message());
                std::filesystem::create_directories(destination);
            }

            const auto preparedCatalogPath = temporary / "catalog.json";
            const auto preparedCatalog = Detail::LoadCatalog(preparedCatalogPath);
            std::unordered_set<std::filesystem::path> activePacks;
            for (const auto& entry : preparedCatalog.Entries)
            {
                const auto relative = entry.PackPath.lexically_relative(temporary).lexically_normal();
                if (relative.empty() || relative.is_absolute() ||
                    relative.native().starts_with(std::filesystem::path("..").native()))
                    throw std::runtime_error("Prepared asset pack escapes its publication directory.");
                activePacks.insert(relative);
            }

            for (const auto& relative : activePacks)
            {
                const auto source = temporary / relative;
                const auto target = destination / relative;
                const bool sourceExists = std::filesystem::exists(source, error);
                if (error)
                    throw std::runtime_error("Could not inspect the prepared asset pack: " + error.message());
                if (!sourceExists)
                {
                    if (!std::filesystem::is_regular_file(target, error) || error ||
                        std::filesystem::is_symlink(target, error))
                        throw std::runtime_error("Reused asset pack is missing or invalid: " +
                                                 Detail::PathToUtf8(target));
                    continue;
                }
                if (!std::filesystem::is_regular_file(source, error) || error ||
                    std::filesystem::is_symlink(source, error))
                    throw std::runtime_error("Prepared asset pack is invalid: " + Detail::PathToUtf8(source));
                std::filesystem::create_directories(target.parent_path());
                if (std::filesystem::exists(target, error))
                {
                    if (error || !std::filesystem::is_regular_file(target, error) ||
                        std::filesystem::file_size(source, error) != std::filesystem::file_size(target, error) || error)
                        throw std::runtime_error("Content-addressed asset pack collision: " +
                                                 Detail::PathToUtf8(target));
                    continue;
                }
                if (error)
                    throw std::runtime_error("Could not inspect the asset pack destination: " + error.message());
                Detail::RenamePathWithRetry(source, target);
            }

            std::vector<std::filesystem::path> auxiliaryFiles;
            for (std::filesystem::recursive_directory_iterator iterator(temporary), end; iterator != end; ++iterator)
            {
                if (iterator->is_symlink())
                    throw std::runtime_error("Prepared asset catalogs may not contain symbolic links.");
                if (!iterator->is_regular_file())
                    continue;
                const auto relative = iterator->path().lexically_relative(temporary).lexically_normal();
                if (relative == std::filesystem::path("catalog.json") || activePacks.contains(relative))
                    continue;
                auxiliaryFiles.push_back(relative);
            }
            const std::unordered_set<std::filesystem::path> activeAuxiliary(auxiliaryFiles.begin(),
                                                                            auxiliaryFiles.end());
            for (const auto& relative : auxiliaryFiles)
            {
                const auto source = temporary / relative;
                const auto target = destination / relative;
                WriteFileAtomically(target, ReadSource(source, 64U * 1024U * 1024U));
            }

            WriteFileAtomically(destination / "catalog.json", ReadSource(preparedCatalogPath, 64U * 1024U * 1024U));

            constexpr auto retirementGrace = std::chrono::minutes(10);
            std::vector<std::filesystem::path> retiredPacks;
            std::vector<std::filesystem::path> retiredAuxiliary;
            const auto now = std::filesystem::file_time_type::clock::now();
            for (std::filesystem::recursive_directory_iterator iterator(destination, error), end;
                 !error && iterator != end; iterator.increment(error))
            {
                if (!iterator->is_regular_file(error))
                    continue;
                const auto relative = iterator->path().lexically_relative(destination).lexically_normal();
                if (iterator->path().extension() != ".keirepak")
                {
                    if (relative != std::filesystem::path("catalog.json") && !activeAuxiliary.contains(relative))
                        retiredAuxiliary.push_back(iterator->path());
                    continue;
                }
                if (activePacks.contains(relative))
                    continue;
                const auto modified = iterator->last_write_time(error);
                if (!error && now - modified >= retirementGrace)
                    retiredPacks.push_back(iterator->path());
                error.clear();
            }
            for (const auto& retired : retiredPacks)
            {
                std::filesystem::remove(retired, error);
                error.clear();
            }
            for (const auto& retired : retiredAuxiliary)
            {
                std::filesystem::remove(retired, error);
                error.clear();
            }
            std::filesystem::remove_all(temporary, error);
        }

        [[maybe_unused]] void ValidateDependencies(const std::span<const Detail::CatalogEntry> entries)
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

        [[maybe_unused]] void ThrowIfOperationCancelled(const std::stop_token cancellation)
        {
            if (cancellation.stop_requested())
                throw AssetOperationCancelled();
        }

        [[maybe_unused]] void ReportOperationProgress(const AssetOperationProgressCallback& callback,
                                                      const AssetOperationPhase phase, const std::size_t completed,
                                                      const std::size_t total, std::filesystem::path currentPath = {})
        {
            if (callback)
                callback({phase, completed, total, std::move(currentPath)});
        }

        [[maybe_unused]] void RemovePathNoThrow(const std::filesystem::path& path) noexcept
        {
            std::error_code ignored;
            std::filesystem::remove_all(path, ignored);
        }
    } // namespace

    class AssetDatabase::Impl final
    {
      public:
        struct RecordIndices final
        {
            std::unordered_map<AssetId, std::size_t> ById;
            std::unordered_map<std::string, std::size_t> ByPath;
        };

        explicit Impl(AssetDatabaseSpecification specification) : Specification(std::move(specification))
        {
            if (Specification.MaximumSourceBytes == 0 || Specification.ChangeDebounce.count() < 0 ||
                Specification.ChangeMonitorInterval.count() <= 0)
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
            const auto compatible =
                importer.Type == record.Type ||
                std::ranges::find(importer.CompatibleTypes, record.Type) != importer.CompatibleTypes.end();
            return importer.Version >= record.ImporterVersion && compatible ? &importer : nullptr;
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
            { return DeriveSubAssetId(parent, key); };
            context.ResolveSubAssetIdFor = [](const AssetId parent, const std::string_view key)
            { return DeriveSubAssetId(parent, key); };
            context.ResolveAssetSource = [this,
                                          current = AssetImportSource{record.Id, record.Type, record.RelativePath}](
                                             const AssetId asset) -> std::optional<AssetImportSource>
            {
                if (asset == current.Id)
                    return current;
                const auto found = std::ranges::find(Records, asset, &AssetSourceRecord::Id);
                if (found == Records.end())
                    return std::nullopt;
                return AssetImportSource{found->Id, found->Type, found->RelativePath};
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

        void ValidateImportOutput(const AssetImporterRegistration* importer, const AssetImportOutput& result) const
        {
            if (result.Bytes.size() > Specification.MaximumSourceBytes)
                throw std::runtime_error("Imported asset exceeds the configured maximum size.");
            if (result.PrimaryType)
            {
                if (!*result.PrimaryType)
                    throw std::runtime_error("Contextual importer returned an invalid primary asset type.");
                if (!importer || (*result.PrimaryType != importer->Type &&
                                  std::ranges::find(importer->CompatibleTypes, *result.PrimaryType) ==
                                      importer->CompatibleTypes.end()))
                    throw std::runtime_error("Contextual importer returned an undeclared primary asset type.");
            }
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
            const auto* importer = FindImporter(record);
            if (importer)
            {
                if (importer->ContextualImport)
                {
                    auto context = CreateImportContext(record);
                    const auto readProjectFile = context.ReadProjectFile;
                    std::vector<AssetSourceDependency> observedDependencies;
                    context.ReadProjectFile =
                        [readProjectFile, &observedDependencies](const std::filesystem::path& path)
                    {
                        const auto normalized = path.lexically_normal();
                        auto bytes = readProjectFile(normalized);
                        const auto digest = Detail::DigestToString(Detail::Sha256(bytes));
                        const auto existing =
                            std::ranges::find(observedDependencies, normalized, &AssetSourceDependency::RelativePath);
                        if (existing == observedDependencies.end())
                            observedDependencies.push_back({normalized, digest});
                        else
                            existing->Digest = digest;
                        return bytes;
                    };
                    result = importer->ContextualImport(context, source);
                    for (auto& dependency : observedDependencies)
                    {
                        const auto existing = std::ranges::find(result.SourceDependencies, dependency.RelativePath,
                                                                &AssetSourceDependency::RelativePath);
                        if (existing == result.SourceDependencies.end())
                            result.SourceDependencies.push_back(std::move(dependency));
                        else
                            existing->Digest = std::move(dependency.Digest);
                    }
                    std::ranges::sort(result.SourceDependencies, {}, &AssetSourceDependency::RelativePath);
                }
                else
                    result.Bytes = importer->Import(source);
            }
            else if ((record.Importer == "Keire.Text" && record.Type == TextAsset::StaticType()) ||
                     (record.Importer == "Keire.Binary" && record.Type == BinaryAsset::StaticType()))
                result.Bytes.assign(source.begin(), source.end());
            else
                throw std::runtime_error("No compatible importer is registered for asset: " +
                                         Detail::PathToUtf8(record.RelativePath));
            ValidateImportOutput(importer, result);
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

        [[nodiscard]] static std::filesystem::path ImportOutputPath(const std::filesystem::path& object,
                                                                    const AssetId asset)
        {
            const auto identity = asset.ToString() + '\n' + Detail::PathToUtf8(object.filename());
            const auto digest = Detail::DigestToString(Detail::Sha256(std::as_bytes(std::span(identity))));
            return object.parent_path().parent_path() / "ImportOutputs" / (digest + ".cbor");
        }

        [[nodiscard]] bool RefreshSourceDependencies(std::vector<AssetSourceDependency>& dependencies) const
        {
            try
            {
                for (auto& dependency : dependencies)
                {
                    const auto path = ConfinedPath(Specification.ProjectRoot, dependency.RelativePath);
                    if (std::filesystem::is_symlink(path))
                        return false;
                    dependency.Digest = Detail::DigestToString(Detail::Sha256File(
                        path, std::min(Specification.MaximumSourceBytes, std::size_t{64U} * 1024U * 1024U)));
                }
                return true;
            }
            catch (const std::exception&)
            {
                return false;
            }
        }

        void StoreCachedImport(const AssetSourceRecord& record, const AssetImportOutput& output) const
        {
            try
            {
                const auto object = ObjectPath(record, ImportDigest(record, output));
                const auto encoded = Json::to_cbor(Detail::EncodeCachedImportOutput(output));
                constexpr std::size_t maximumCacheDocumentBytes = std::size_t{512U} * 1024U * 1024U;
                if (encoded.size() > maximumCacheDocumentBytes)
                {
                    KEIRE_CORE_WARN("Skipping oversized import-output cache for '{}'.",
                                    Detail::PathToUtf8(record.RelativePath));
                    return;
                }
                Detail::WriteFileAtomically(ImportOutputPath(object, record.Id), std::as_bytes(std::span(encoded)));
            }
            catch (const std::exception& error)
            {
                KEIRE_CORE_WARN("Could not persist import-output cache for '{}': {}",
                                Detail::PathToUtf8(record.RelativePath), error.what());
            }
        }

        [[nodiscard]] std::optional<AssetImportOutput> RestoreCachedImport(const AssetSourceRecord& record) const
        {
            auto sourceDependencies = record.SourceDependencies;
            if (!RefreshSourceDependencies(sourceDependencies))
                return std::nullopt;
            AssetImportOutput priorOutput;
            priorOutput.SourceDependencies = sourceDependencies;
            priorOutput.AssetDependencies = record.Dependencies;
            const auto object = ObjectPath(record, ImportDigest(record, priorOutput));
            if (!std::filesystem::is_regular_file(object))
                return std::nullopt;
            const auto* importer = FindImporter(record);
            if (importer && importer->RestoreCachedOutput && record.SourceDependencies.empty() &&
                record.SubAssets.empty())
            {
                auto restored = importer->RestoreCachedOutput(ReadSource(object, Specification.MaximumSourceBytes));
                ValidateImportOutput(importer, restored);
                if (!restored.SourceDependencies.empty())
                    throw std::logic_error("A dependency-free cached importer restored source dependencies.");
                return restored;
            }
            const auto outputPath = ImportOutputPath(object, record.Id);
            if (std::filesystem::is_regular_file(outputPath))
            {
                try
                {
                    constexpr std::size_t maximumCacheDocumentBytes = std::size_t{512U} * 1024U * 1024U;
                    const auto encoded = ReadSource(outputPath, maximumCacheDocumentBytes);
                    auto restored = Detail::DecodeCachedImportOutput(
                        Json::from_cbor(reinterpret_cast<const std::uint8_t*>(encoded.data()),
                                        reinterpret_cast<const std::uint8_t*>(encoded.data() + encoded.size())),
                        ReadSource(object, Specification.MaximumSourceBytes));
                    if (!RefreshSourceDependencies(restored.SourceDependencies) ||
                        ObjectPath(record, ImportDigest(record, restored)) != object)
                    {
                        return std::nullopt;
                    }
                    ValidateImportOutput(importer, restored);
                    return restored;
                }
                catch (const std::exception& error)
                {
                    KEIRE_CORE_WARN("Ignoring invalid import-output cache for '{}': {}",
                                    Detail::PathToUtf8(record.RelativePath), error.what());
                    return std::nullopt;
                }
            }
            return std::nullopt;
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
            std::vector<std::filesystem::path> sources;
            std::error_code error;
            for (std::filesystem::recursive_directory_iterator
                     iterator(SourceRoot, std::filesystem::directory_options::skip_permission_denied, error),
                 end;
                 iterator != end; iterator.increment(error))
            {
                if (error)
                    throw std::runtime_error("Could not enumerate the asset source directory.");
                if (!iterator->is_regular_file() || iterator->path().extension() == ".keiremeta" ||
                    Detail::IsTransientFile(iterator->path()))
                    continue;
                sources.push_back(iterator->path());
            }
            std::ranges::sort(sources, [](const auto& left, const auto& right)
                              { return Detail::PathToUtf8(left) < Detail::PathToUtf8(right); });
            sources.erase(std::unique(sources.begin(), sources.end()), sources.end());

            std::unordered_set<AssetId> identities;
            std::unordered_set<std::string> paths;
            for (const auto& source : sources)
            {
                auto record = ReadMetadata(SourceRoot, source, Specification.MaximumSourceBytes, digestSources,
                                           InferImporter(source));
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
                                          FileSignature{std::filesystem::last_write_time(source),
                                                        std::filesystem::file_size(source),
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

        struct MonitoredScan final
        {
            ScanResult Result;
            std::uint64_t SourceRevision = 0;
        };

        void StartChangeMonitor()
        {
            std::scoped_lock lock(ChangeMonitorMutex);
            if (ChangeMonitor.joinable())
                return;
            ChangeMonitor = std::jthread(
                [this](const std::stop_token stop)
                {
                    std::unique_lock monitorLock(ChangeMonitorMutex);
                    while (!stop.stop_requested())
                    {
                        (void)ChangeMonitorCondition.wait_for(monitorLock, stop, Specification.ChangeMonitorInterval,
                                                              [this] { return ChangeMonitorRequested; });
                        if (stop.stop_requested())
                            break;
                        ChangeMonitorRequested = false;
                        monitorLock.unlock();

                        const auto revision = SourceRevision.load(std::memory_order_acquire);
                        try
                        {
                            ScanResult scanned;
                            {
                                // Directory iterators retain native directory handles on Windows. Serialize the
                                // reconciliation walk with asset transactions so moves, trash operations, and
                                // atomic publications never race an open monitor iterator.
                                std::scoped_lock operation(*OperationMutex);
                                scanned = Scan(false);
                            }
                            monitorLock.lock();
                            if (revision == SourceRevision.load(std::memory_order_acquire))
                            {
                                PublishedChangeScan = MonitoredScan{std::move(scanned), revision};
                                PublishedScans.fetch_add(1, std::memory_order_relaxed);
                            }
                            monitorLock.unlock();
                        }
                        catch (...)
                        {
                            FailedScans.fetch_add(1, std::memory_order_relaxed);
                        }
                        monitorLock.lock();
                    }
                });
        }

        void RequestChangeMonitorScan() noexcept
        {
            {
                std::scoped_lock lock(ChangeMonitorMutex);
                ChangeMonitorRequested = true;
            }
            ChangeMonitorCondition.notify_one();
        }

        [[nodiscard]] std::optional<MonitoredScan> TakeChangeMonitorScan()
        {
            std::scoped_lock lock(ChangeMonitorMutex);
            return std::exchange(PublishedChangeScan, std::nullopt);
        }

        void PublishRecord(AssetSourceRecord record, const FileSignature& signature)
        {
            std::scoped_lock lock(Mutex);
            const auto id = record.Id;
            auto candidate = Records;
            const auto existing = IdIndex.find(record.Id);
            if (existing == IdIndex.end())
                candidate.push_back(std::move(record));
            else
                candidate[existing->second] = std::move(record);
            std::ranges::sort(candidate, [](const auto& left, const auto& right) { return left.Id < right.Id; });
            ReplaceRecords(std::move(candidate));
            Observed.insert_or_assign(id, signature);
            PendingChanges.erase(id);
            SourceRevision.fetch_add(1, std::memory_order_release);
            RequestChangeMonitorScan();
        }

        void RemoveRecords(const std::span<const AssetId> identities)
        {
            std::scoped_lock lock(Mutex);
            const auto contains = [&](const AssetId id)
            { return std::ranges::find(identities, id) != identities.end(); };
            auto candidate = Records;
            std::erase_if(candidate, [&](const AssetSourceRecord& record) { return contains(record.Id); });
            ReplaceRecords(std::move(candidate));
            for (const auto id : identities)
            {
                Observed.erase(id);
                PendingChanges.erase(id);
                ImportStatuses.erase(id);
                ValidatedImports.erase(id);
                CookInputs.erase(id);
            }
            SourceRevision.fetch_add(1, std::memory_order_release);
            RequestChangeMonitorScan();
        }

        [[nodiscard]] static std::string CanonicalPathKey(const std::filesystem::path& path)
        {
            return Detail::PathToUtf8(path.lexically_normal());
        }

        [[nodiscard]] static std::string PortablePathKey(const std::filesystem::path& path)
        {
            auto result = CanonicalPathKey(path);
            std::ranges::transform(result, result.begin(), [](const unsigned char character)
                                   { return static_cast<char>(std::tolower(character)); });
            return result;
        }

        [[nodiscard]] static RecordIndices BuildIndices(const std::vector<AssetSourceRecord>& records)
        {
            RecordIndices result;
            result.ById.reserve(records.size());
            result.ByPath.reserve(records.size());
            std::unordered_map<std::string, std::string> portablePaths;
            portablePaths.reserve(records.size());
            for (std::size_t index = 0; index < records.size(); ++index)
            {
                const auto& record = records[index];
                const auto path = CanonicalPathKey(record.RelativePath);
                if (!result.ById.emplace(record.Id, index).second)
                    throw std::runtime_error("Asset identity is registered more than once: " + record.Id.ToString());
                if (!result.ByPath.emplace(path, index).second)
                    throw std::runtime_error("Asset path is registered more than once: " + path);
                const auto portable = PortablePathKey(record.RelativePath);
                const auto [found, inserted] = portablePaths.emplace(portable, path);
                if (!inserted && found->second != path)
                    throw std::runtime_error("Asset paths differ only by case and are not portable: " + found->second +
                                             " and " + path);
            }
            return result;
        }

        void ReplaceRecords(std::vector<AssetSourceRecord> records)
        {
            auto indices = BuildIndices(records);
            Records = std::move(records);
            IdIndex = std::move(indices.ById);
            PathIndex = std::move(indices.ByPath);
        }

        AssetDatabaseSpecification Specification;
        std::filesystem::path SourceRoot;
        std::filesystem::path CacheRoot;
        std::vector<AssetSourceRecord> Records;
        std::unordered_map<AssetId, std::size_t> IdIndex;
        std::unordered_map<std::string, std::size_t> PathIndex;
        std::unordered_map<AssetId, FileSignature> Observed;
        std::unordered_map<AssetId, std::chrono::steady_clock::time_point> PendingChanges;
        std::unordered_map<std::string, AssetImporterRegistration> Importers;
        std::unordered_map<std::string, std::string> Extensions;
        std::unordered_map<AssetId, AssetImportStatus> ImportStatuses;
        std::unordered_map<AssetId, PreparedImport> ValidatedImports;
        std::unordered_map<AssetId, PreparedImport> CookInputs;
        std::unique_ptr<Detail::InterprocessMutex> OperationMutex;
        mutable std::mutex Mutex;
        std::atomic<std::uint64_t> SourceRevision{1};
        std::atomic<std::uint64_t> PublishedScans{0};
        std::atomic<std::uint64_t> FailedScans{0};
        mutable std::mutex ChangeMonitorMutex;
        std::condition_variable_any ChangeMonitorCondition;
        std::optional<MonitoredScan> PublishedChangeScan;
        bool ChangeMonitorRequested = false;
        std::jthread ChangeMonitor;
    };

} // namespace Keire
