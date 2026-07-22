#include "KeireInternal/Assets/AssetDatabaseImplementation.h"

namespace Keire
{
    AssetCookResult AssetCooker::Cook(const AssetDatabase& database, const AssetBuildProfile& profile,
                                      const std::filesystem::path& outputDirectory, const std::stop_token cancellation,
                                      AssetOperationProgressCallback progress)
    {
        std::scoped_lock operation(*database.m_Impl->OperationMutex);
        return CookUnlocked(database, profile, outputDirectory, cancellation, std::move(progress));
    }

    AssetCookResult AssetCooker::CookUnlocked(const AssetDatabase& database, const AssetBuildProfile& profile,
                                              const std::filesystem::path& outputDirectory,
                                              const std::stop_token cancellation,
                                              AssetOperationProgressCallback progress)
    {
        ThrowIfOperationCancelled(cancellation);
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
        ReportOperationProgress(progress, AssetOperationPhase::Cooking, 0, records.size());
        std::size_t preparedCount = 0;
        for (const auto& record : records)
        {
            ThrowIfOperationCancelled(cancellation);
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
            ++preparedCount;
            ReportOperationProgress(progress, AssetOperationPhase::Cooking, preparedCount, records.size(),
                                    record.RelativePath);
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
        const auto temporary = Detail::PathWithSuffix(destination, ".tmp-" + AssetId::Generate().ToString());
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
                throw std::runtime_error("Could not create asset pack: " + Detail::PathToUtf8(packPath));
            Detail::WritePackHeader(pack);
            packBytes = Detail::PackHeaderBytes;
            ++result.PackCount;
        };

        try
        {
            for (auto& asset : prepared)
            {
                ThrowIfOperationCancelled(cancellation);
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
            ThrowIfOperationCancelled(cancellation);
            ReportOperationProgress(progress, AssetOperationPhase::Publishing, entries.size(), entries.size(),
                                    destination);
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
                throw std::runtime_error("Could not open cooked asset pack: " + Detail::PathToUtf8(entry.PackPath));
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
