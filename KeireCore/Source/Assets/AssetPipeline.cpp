#include "Keire/Assets/AssetPipeline.h"
#include "Keire/Assets/RenderingAssets.h"

#include "Keire/Log.h"

#include "KeireInternal/Assets/AssetInternal.h"

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
            context.ImportSettings = record.ImportSettings;
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
                                         record.RelativePath.generic_string());
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
                throw std::runtime_error("Asset path is already registered: " + record.RelativePath.generic_string());
            const auto existing = std::ranges::find(Records, record.Id, &AssetSourceRecord::Id);
            if (existing == Records.end())
                Records.push_back(std::move(record));
            else
                *existing = std::move(record);
            std::ranges::sort(Records, [](const auto& left, const auto& right) { return left.Id < right.Id; });
            Observed.insert_or_assign(id, signature);
            PendingChanges.erase(id);
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
        mutable std::mutex Mutex;
    };

    std::string ExternalAssetImportReceiptId::ToString() const { return m_Value.ToString(); }

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
        m_Impl->ResetCookInputs();
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
                m_Impl->StoreCookInput(record, std::move(imported));
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
        try
        {
            const auto cooked = AssetCooker::Cook(*this, AssetBuildProfile{}, m_Impl->CacheRoot / "Runtime");
            result.CatalogPath = cooked.CatalogPath;
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

    ExternalAssetImportResult AssetDatabase::ImportExternal(const std::span<const ExternalAssetImportItem> items,
                                                            const std::stop_token cancellation)
    {
        const auto throwIfCancelled = [&cancellation]
        {
            if (cancellation.stop_requested())
                throw std::runtime_error("External asset import was cancelled.");
        };
        struct PlannedItem final
        {
            std::filesystem::path Source;
            std::filesystem::path Destination;
            AssetImportSettings Settings;
            ExternalAssetConflictPolicy Conflict = ExternalAssetConflictPolicy::UniqueName;
        };
        std::vector<PlannedItem> planned;
        for (const auto& item : items)
        {
            throwIfCancelled();
            if (item.SourcePath.empty())
                throw std::invalid_argument("External import source paths must not be empty.");
            const auto source = std::filesystem::absolute(item.SourcePath).lexically_normal();
            if (std::filesystem::is_symlink(source))
                throw std::invalid_argument("External imports do not follow symbolic links: " + source.string());
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
                                                    iterator->path().string());
                    }
                    if (!iterator->is_regular_file(error) || iterator->path().extension() == ".keiremeta")
                        continue;
                    if (!m_Impl->InferImporter(iterator->path()))
                        throw std::invalid_argument("No importer supports a dropped directory entry: " +
                                                    iterator->path().string());
                    const auto relative = std::filesystem::relative(iterator->path(), source, error);
                    if (error)
                        throw std::runtime_error("Could not resolve a dropped directory entry.");
                    planned.push_back(
                        {iterator->path(), item.RelativeDestination / relative, item.Settings, item.Conflict});
                }
                if (error)
                    throw std::runtime_error("Could not enumerate dropped directory: " + error.message());
                continue;
            }
            if (!std::filesystem::is_regular_file(source))
                throw std::invalid_argument("External import source is not a regular file: " + source.string());
            planned.push_back({source, item.RelativeDestination.empty() ? source.filename() : item.RelativeDestination,
                               item.Settings, item.Conflict});
        }
        if (planned.empty())
            throw std::invalid_argument("No supported asset files were found in the external import.");

        struct Rollback final
        {
            std::filesystem::path Source;
            std::filesystem::path Metadata;
            std::vector<std::byte> PreviousSource;
            std::vector<std::byte> PreviousMetadata;
            bool Replaced = false;
        };
        std::vector<Rollback> rollback;
        ExternalAssetImportResult result;
        std::filesystem::path receiptRoot;
        const auto writeBytes = [](const std::filesystem::path& path, const std::span<const std::byte> bytes)
        {
            std::filesystem::create_directories(path.parent_path());
            const auto temporary = path.string() + ".external-import.tmp";
            std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
            if (!stream || (!bytes.empty() && !stream.write(reinterpret_cast<const char*>(bytes.data()),
                                                            static_cast<std::streamsize>(bytes.size()))))
                throw std::runtime_error("Could not stage external asset data.");
            stream.close();
            Detail::AtomicReplace(temporary, path);
        };
        try
        {
            for (auto& item : planned)
            {
                throwIfCancelled();
                const auto* importer = m_Impl->InferImporter(item.Source);
                if (!importer)
                    throw std::invalid_argument("No importer supports: " + item.Source.string());
                if (item.Settings.empty() && importer->SuggestImportSettings)
                    item.Settings = importer->SuggestImportSettings(item.Source, item.Settings);
                auto destination = item.Destination.lexically_normal();
                auto absoluteDestination = ConfinedPath(m_Impl->SourceRoot, destination);
                auto existing = Find(destination);
                if (existing && item.Conflict == ExternalAssetConflictPolicy::Skip)
                    continue;
                if (existing && item.Conflict == ExternalAssetConflictPolicy::UniqueName)
                {
                    const auto parent = destination.parent_path();
                    const auto stem = destination.stem().string();
                    const auto extension = destination.extension().string();
                    for (std::size_t copy = 2; existing; ++copy)
                    {
                        destination = parent / (stem + " " + std::to_string(copy) + extension);
                        existing = Find(destination);
                    }
                    absoluteDestination = ConfinedPath(m_Impl->SourceRoot, destination);
                }
                const auto bytes = ReadSource(item.Source, m_Impl->Specification.MaximumSourceBytes);
                if (!existing)
                {
                    const auto id = CreateAsset(destination, *importer, bytes, item.Settings);
                    rollback.push_back(
                        {absoluteDestination, std::filesystem::path(absoluteDestination.string() + ".keiremeta")});
                    result.Entries.push_back({id, item.Source, destination, false});
                    continue;
                }
                if (existing->Importer != importer->Name || existing->Type != importer->Type)
                    throw std::invalid_argument("Replace requires a destination with the same importer and type.");

                const auto metadata = std::filesystem::path(absoluteDestination.string() + ".keiremeta");
                Rollback restore{absoluteDestination, metadata,
                                 ReadSource(absoluteDestination, m_Impl->Specification.MaximumSourceBytes),
                                 ReadSource(metadata, 1024U * 1024U), true};
                const auto settings = m_Impl->NormalizeSettings(*importer, item.Settings);
                AssetSourceRecord validation = *existing;
                validation.ImportSettings = settings;
                validation.Type = importer->Type;
                validation.Importer = importer->Name;
                validation.ImporterVersion = importer->Version;
                const auto validationMetadata = std::filesystem::path(metadata.string() + ".validate.tmp");
                validation.MetadataPath = validationMetadata;
                WriteMetadata(validationMetadata, existing->Id, importer->Type, importer->Name, importer->Version,
                              settings);
                AssetImportOutput validated;
                try
                {
                    validated = m_Impl->ImportSource(validation, bytes);
                }
                catch (...)
                {
                    std::error_code ignored;
                    std::filesystem::remove(validationMetadata, ignored);
                    throw;
                }
                std::error_code ignored;
                std::filesystem::remove(validationMetadata, ignored);
                writeBytes(absoluteDestination, bytes);
                WriteMetadata(metadata, existing->Id, importer->Type, importer->Name, importer->Version, settings);
                rollback.push_back(std::move(restore));
                (void)Refresh();
                if (const auto refreshed = Find(existing->Id))
                    m_Impl->StoreValidatedImport(*refreshed, std::move(validated));
                result.Entries.push_back({existing->Id, item.Source, destination, true});
            }
            throwIfCancelled();
            result.Import = ImportAll(AssetImportPolicy::FailFast);
            throwIfCancelled();
            const auto receiptValue = AssetId::Generate();
            receiptRoot = ConfinedPath(m_Impl->Specification.ProjectRoot,
                                       std::filesystem::path("Library/AssetImport") / receiptValue.ToString());
            std::filesystem::create_directories(receiptRoot);
            Json receipt{{"schemaVersion", 1}, {"entries", Json::array()}};
            for (std::size_t index = 0; index < rollback.size(); ++index)
            {
                const auto& state = rollback[index];
                const auto prefix = std::to_string(index);
                writeBytes(receiptRoot / (prefix + ".after"),
                           ReadSource(state.Source, m_Impl->Specification.MaximumSourceBytes));
                writeBytes(receiptRoot / (prefix + ".after.keiremeta"),
                           ReadSource(state.Metadata, 16U * 1024U * 1024U));
                if (state.Replaced)
                {
                    writeBytes(receiptRoot / (prefix + ".before"), state.PreviousSource);
                    writeBytes(receiptRoot / (prefix + ".before.keiremeta"), state.PreviousMetadata);
                }
                receipt["entries"].push_back(
                    {{"destination", result.Entries[index].RelativeDestination.generic_string()},
                     {"replaced", state.Replaced}});
            }
            const auto manifest = receipt.dump(2);
            writeBytes(receiptRoot / "receipt.json", std::as_bytes(std::span(manifest.data(), manifest.size())));
            result.Receipt = ExternalAssetImportReceiptId(receiptValue);
            return result;
        }
        catch (...)
        {
            for (auto iterator = rollback.rbegin(); iterator != rollback.rend(); ++iterator)
            {
                std::error_code ignored;
                if (iterator->Replaced)
                {
                    try
                    {
                        writeBytes(iterator->Source, iterator->PreviousSource);
                        writeBytes(iterator->Metadata, iterator->PreviousMetadata);
                    }
                    catch (...)
                    {
                    }
                }
                else
                {
                    std::filesystem::remove(iterator->Source, ignored);
                    std::filesystem::remove(iterator->Metadata, ignored);
                }
            }
            (void)Refresh();
            if (!receiptRoot.empty())
            {
                std::error_code ignored;
                std::filesystem::remove_all(receiptRoot, ignored);
            }
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
        if (!receipt)
            throw std::invalid_argument("External import receipt is invalid.");
        const auto receiptRoot = ConfinedPath(m_Impl->Specification.ProjectRoot,
                                              std::filesystem::path("Library/AssetImport") / receipt.ToString());
        const auto manifestBytes = ReadSource(receiptRoot / "receipt.json", 16U * 1024U * 1024U);
        const auto manifest = Json::parse(reinterpret_cast<const char*>(manifestBytes.data()),
                                          reinterpret_cast<const char*>(manifestBytes.data() + manifestBytes.size()));
        if (manifest.value("schemaVersion", 0) != 1 || !manifest.contains("entries") || !manifest["entries"].is_array())
            throw std::runtime_error("External import receipt is invalid.");
        const auto writeBytes = [](const std::filesystem::path& path, const std::span<const std::byte> bytes)
        {
            std::filesystem::create_directories(path.parent_path());
            const auto temporary = path.string() + ".external-import-replay.tmp";
            std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
            if (!stream || (!bytes.empty() && !stream.write(reinterpret_cast<const char*>(bytes.data()),
                                                            static_cast<std::streamsize>(bytes.size()))))
                throw std::runtime_error("Could not replay external asset import receipt.");
            stream.close();
            Detail::AtomicReplace(temporary, path);
        };
        const auto applyEntry = [&](const std::size_t index, const Json& entry)
        {
            const auto destination =
                ConfinedPath(m_Impl->SourceRoot, std::filesystem::path(entry.at("destination").get<std::string>()));
            const auto metadata = std::filesystem::path(destination.string() + ".keiremeta");
            const bool replaced = entry.value("replaced", false);
            if (!applied && !replaced)
            {
                std::error_code error;
                std::filesystem::remove(destination, error);
                if (error)
                    throw std::runtime_error("Could not undo imported asset: " + error.message());
                std::filesystem::remove(metadata, error);
                if (error)
                    throw std::runtime_error("Could not undo imported asset metadata: " + error.message());
                return;
            }
            const auto prefix = std::to_string(index) + (applied ? ".after" : ".before");
            writeBytes(destination, ReadSource(receiptRoot / prefix, m_Impl->Specification.MaximumSourceBytes));
            writeBytes(metadata, ReadSource(receiptRoot / (prefix + ".keiremeta"), 16U * 1024U * 1024U));
        };
        if (applied)
        {
            std::size_t index = 0;
            for (const auto& entry : manifest["entries"])
                applyEntry(index++, entry);
        }
        else
        {
            for (std::size_t index = manifest["entries"].size(); index > 0; --index)
                applyEntry(index - 1, manifest["entries"][index - 1]);
        }
        (void)Refresh();
        (void)ImportAll(AssetImportPolicy::KeepLastGood);
    }

    void AssetDatabase::CreateFolder(const std::filesystem::path& relativePath)
    {
        const auto destination = ConfinedPath(m_Impl->SourceRoot, relativePath);
        if (!std::filesystem::create_directories(destination) && !std::filesystem::is_directory(destination))
            throw std::runtime_error("Could not create asset folder: " + destination.string());
    }

    AssetId AssetDatabase::CreateAsset(const std::filesystem::path& relativePath,
                                       const AssetImporterRegistration& importer,
                                       const std::span<const std::byte> sourceBytes,
                                       const AssetImportSettings& requestedSettings)
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
        const auto settings = m_Impl->NormalizeSettings(registered->second, requestedSettings);
        const auto id = AssetId::Generate();
        AssetSourceRecord validationRecord;
        validationRecord.Id = id;
        validationRecord.Type = importer.Type;
        validationRecord.RelativePath = std::filesystem::relative(destination, m_Impl->SourceRoot).lexically_normal();
        validationRecord.Importer = importer.Name;
        validationRecord.ImporterVersion = importer.Version;
        validationRecord.ImportSettings = settings;
        const auto validationMetadata = std::filesystem::path(metadata.string() + ".validate.tmp");
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
        const auto temporary = destination.string() + ".tmp";
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        if (!stream || (!sourceBytes.empty() && !stream.write(reinterpret_cast<const char*>(sourceBytes.data()),
                                                              static_cast<std::streamsize>(sourceBytes.size()))))
            throw std::runtime_error("Could not create asset source.");
        stream.close();
        try
        {
            Detail::AtomicReplace(temporary, destination);
            WriteMetadata(metadata, id, importer.Type, importer.Name, importer.Version, settings);
        }
        catch (...)
        {
            std::error_code cleanupError;
            std::filesystem::remove(temporary, cleanupError);
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
            auto moved = *record;
            moved.RelativePath = std::filesystem::relative(destination, m_Impl->SourceRoot).lexically_normal();
            moved.MetadataPath = destinationMetadata;
            m_Impl->PublishRecord(std::move(moved), m_Impl->ReadSignature(destination, destinationMetadata));
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
            auto cached = database.m_Impl->TakeCookInput(record);
            auto imported = cached ? std::move(*cached) : database.m_Impl->Import(record);
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
        if (!profile.Strict)
        {
            for (auto& asset : prepared)
                std::erase_if(asset.Dependencies,
                              [&indices](const AssetId dependency) { return !indices.contains(dependency); });
        }
        for (auto& asset : prepared)
        {
            if (asset.Record->Type != MaterialAsset::StaticType())
                continue;
            const auto material = MaterialAsset::Decode(asset.Imported.Bytes);
            if (!material->Definition().Shader)
                throw std::runtime_error("Material asset must reference a shader: " + asset.Record->Id.ToString());
            const auto shaderIndex = indices.find(material->Definition().Shader);
            if (shaderIndex == indices.end() || prepared[shaderIndex->second].Record->Type != ShaderAsset::StaticType())
                throw std::runtime_error("Material shader dependency is missing or has the wrong type: " +
                                         material->Definition().Shader.ToString());
            const auto shader = ShaderAsset::Decode(prepared[shaderIndex->second].Imported.Bytes);
            ValidateMaterialAgainstShader(material->Definition(), shader->Definition());
            for (const auto& property : shader->Definition().Properties)
            {
                if (property.Type != ShaderPropertyType::Texture2D)
                    continue;
                auto texture = property.DefaultTexture;
                if (const auto selected = material->Definition().Properties.find(property.Name);
                    selected != material->Definition().Properties.end())
                {
                    const auto* selectedTexture = std::get_if<AssetId>(&selected->second);
                    if (!selectedTexture)
                        throw std::runtime_error("Material texture property has a non-texture value: " + property.Name);
                    texture = *selectedTexture;
                }
                if (!texture)
                    continue;
                const auto textureIndex = indices.find(texture);
                if (textureIndex == indices.end())
                {
                    if (profile.Strict)
                        throw std::runtime_error("Material texture property '" + property.Name +
                                                 "' is missing or has the wrong asset type.");
                    std::erase(asset.Dependencies, texture);
                    continue;
                }
                if (prepared[textureIndex->second].Record->Type != Texture2DAsset::StaticType())
                    throw std::runtime_error("Material texture property '" + property.Name +
                                             "' is missing or has the wrong asset type.");
                const auto textureAsset = Texture2DAsset::Decode(prepared[textureIndex->second].Imported.Bytes);
                const auto& settings = textureAsset->Settings();
                const bool compatible =
                    property.TextureSemantic == ShaderTextureSemantic::Generic ||
                    ((property.TextureSemantic == ShaderTextureSemantic::BaseColor ||
                      property.TextureSemantic == ShaderTextureSemantic::Emissive) &&
                     settings.Semantic == TextureSemantic::Color && settings.ColorSpace == TextureColorSpace::Srgb) ||
                    (property.TextureSemantic == ShaderTextureSemantic::Normal &&
                     settings.Semantic == TextureSemantic::Normal &&
                     settings.ColorSpace == TextureColorSpace::Linear) ||
                    ((property.TextureSemantic == ShaderTextureSemantic::MetallicRoughness ||
                      property.TextureSemantic == ShaderTextureSemantic::Occlusion ||
                      property.TextureSemantic == ShaderTextureSemantic::Metallic ||
                      property.TextureSemantic == ShaderTextureSemantic::Roughness) &&
                     settings.Semantic == TextureSemantic::Data && settings.ColorSpace == TextureColorSpace::Linear);
                if (!compatible)
                    throw std::runtime_error("Material texture property '" + property.Name +
                                             "' has incompatible semantic or color-space import settings.");
            }
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
                entry.Metadata = asset.Imported.Metadata;
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
