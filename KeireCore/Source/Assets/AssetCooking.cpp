#include "KeireInternal/Assets/AssetDatabaseImplementation.h"

#include "Keire/Animation/AnimationSystem.h"
#include "Keire/Animation/ProceduralMotion.h"
#include "Keire/Animation/RiggingSystem.h"
#include "Keire/Assets/RenderingAssets.h"
#include "Keire/Audio/AudioAssets.h"
#include "Keire/Jobs/JobSystem.h"
#include "Keire/Scripting/ManagedDataAsset.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <exception>
#include <mutex>
#include <string>
#include <thread>
#include <type_traits>

namespace Keire
{
    namespace
    {
        template <typename Unsigned>
        [[nodiscard]] Unsigned ReadStreamValue(const std::span<const std::byte> bytes, std::size_t& cursor)
        {
            static_assert(std::is_unsigned_v<Unsigned>);
            if (cursor > bytes.size() || bytes.size() - cursor < sizeof(Unsigned))
                throw std::runtime_error("Cooked asset stream metadata is truncated.");
            Unsigned value = 0;
            for (std::size_t index = 0; index < sizeof(Unsigned); ++index)
                value |= static_cast<Unsigned>(std::to_integer<std::uint8_t>(bytes[cursor++])) << (index * 8U);
            return value;
        }

        [[nodiscard]] Detail::CatalogSegment MakeSegment(const AssetStreamSegmentKind kind, const std::uint32_t segment,
                                                         const std::uint64_t offset, const std::uint64_t bytes,
                                                         const float windowStart = 0.0F, const float windowEnd = 0.0F)
        {
            return {static_cast<std::uint8_t>(kind), segment, offset, bytes, windowStart, windowEnd};
        }

        [[nodiscard]] std::vector<Detail::CatalogSegment> BuildStreamSegments(const AssetTypeId type,
                                                                              const std::span<const std::byte> bytes,
                                                                              const std::size_t streamPageBytes)
        {
            if (bytes.empty())
                return {};

            if (type == Texture2DAsset::StaticType())
            {
                constexpr char Magic[] = "KEIRETEX";
                if (bytes.size() < 12U || std::memcmp(bytes.data(), Magic, 8U) != 0)
                    throw std::runtime_error("Texture stream metadata has an invalid signature.");
                std::size_t cursor = 8U;
                const auto version = ReadStreamValue<std::uint32_t>(bytes, cursor);
                if (version < 1U || version > 3U)
                    throw std::runtime_error("Texture stream metadata has an unsupported version.");
                const std::size_t settingsBytes = version == 1U ? 10U : version == 2U ? 11U : 13U;
                if (cursor > bytes.size() || bytes.size() - cursor < settingsBytes)
                    throw std::runtime_error("Texture stream metadata is truncated.");
                cursor += settingsBytes;
                static_cast<void>(ReadStreamValue<std::uint32_t>(bytes, cursor));
                const auto mipCount = ReadStreamValue<std::uint32_t>(bytes, cursor);
                if (mipCount == 0U || mipCount > 15U)
                    throw std::runtime_error("Texture stream metadata contains an invalid mip count.");

                std::vector<Detail::CatalogSegment> mips;
                mips.reserve(mipCount);
                const auto metadataBytes = cursor;
                for (std::uint32_t mip = 0; mip < mipCount; ++mip)
                {
                    const auto offset = cursor;
                    static_cast<void>(ReadStreamValue<std::uint32_t>(bytes, cursor));
                    static_cast<void>(ReadStreamValue<std::uint32_t>(bytes, cursor));
                    const auto payloadBytes = ReadStreamValue<std::uint64_t>(bytes, cursor);
                    if (payloadBytes > bytes.size() - cursor)
                        throw std::runtime_error("Texture stream mip extends beyond its payload.");
                    cursor += static_cast<std::size_t>(payloadBytes);
                    mips.push_back(MakeSegment(AssetStreamSegmentKind::TextureMip, mip, offset, cursor - offset));
                }
                if (cursor != bytes.size())
                    throw std::runtime_error("Texture stream metadata contains trailing data.");

                std::vector<Detail::CatalogSegment> result;
                result.reserve(mips.size() + 1U);
                result.push_back(MakeSegment(AssetStreamSegmentKind::Metadata, 0, 0, metadataBytes));
                for (auto mip = mips.rbegin(); mip != mips.rend(); ++mip)
                    result.push_back(*mip);
                return result;
            }

            if (type == MeshAsset::StaticType())
            {
                const auto mesh = MeshAsset::Decode(bytes);
                constexpr std::size_t SerializedVertexBytes = 72U;
                const auto vertexBytes = mesh->Vertices().size() * SerializedVertexBytes;
                const auto indexBytes = mesh->Indices().size_bytes();
                if (vertexBytes > bytes.size() || indexBytes > bytes.size() - vertexBytes)
                    throw std::runtime_error("Mesh stream payload size is invalid.");
                const auto vertexOffset = bytes.size() - vertexBytes - indexBytes;
                const auto indexOffset = vertexOffset + vertexBytes;
                std::vector<Detail::CatalogSegment> result;
                result.reserve(mesh->Lods().size() + 1U);
                result.push_back(MakeSegment(AssetStreamSegmentKind::Metadata, 0, 0, vertexOffset));
                for (std::size_t reverse = mesh->Lods().size(); reverse > 0; --reverse)
                {
                    const auto lodIndex = reverse - 1U;
                    const auto& lod = mesh->Lods()[lodIndex];
                    std::uint64_t lastIndex = 0;
                    for (std::uint32_t submeshIndex = 0; submeshIndex < lod.SubmeshCount; ++submeshIndex)
                    {
                        const auto& submesh = mesh->Submeshes()[lod.FirstSubmesh + submeshIndex];
                        lastIndex =
                            std::max(lastIndex, static_cast<std::uint64_t>(submesh.FirstIndex) + submesh.IndexCount);
                    }
                    const auto end = indexOffset + lastIndex * sizeof(std::uint32_t);
                    if (end > bytes.size() || end <= vertexOffset)
                        throw std::runtime_error("Mesh LOD stream range is invalid.");
                    result.push_back(MakeSegment(AssetStreamSegmentKind::MeshLod, static_cast<std::uint32_t>(lodIndex),
                                                 vertexOffset, end - vertexOffset));
                }
                return result;
            }

            if (type == AudioClipAsset::StaticType())
            {
                const auto audio = AudioClipAsset::Decode(bytes);
                std::size_t versionCursor = 8U;
                const auto version = ReadStreamValue<std::uint32_t>(bytes, versionCursor);
                const std::size_t headerBytes = version == 1U ? 28U : 40U;
                if (bytes.size() <= headerBytes)
                    throw std::runtime_error("Audio stream payload does not contain sample data.");
                const auto payloadBytes = bytes.size() - headerBytes;
                const auto pageBytes = std::max<std::size_t>(4096U, streamPageBytes);
                const auto pageCount = (payloadBytes + pageBytes - 1U) / pageBytes;
                std::vector<Detail::CatalogSegment> result;
                result.reserve(pageCount + 1U);
                result.push_back(MakeSegment(AssetStreamSegmentKind::Metadata, 0, 0, headerBytes));
                for (std::size_t page = 0; page < pageCount; ++page)
                {
                    const auto offset = page * pageBytes;
                    const auto count = std::min(pageBytes, payloadBytes - offset);
                    const auto start =
                        audio->DurationSeconds() * static_cast<float>(offset) / static_cast<float>(payloadBytes);
                    const auto end = audio->DurationSeconds() * static_cast<float>(offset + count) /
                                     static_cast<float>(payloadBytes);
                    result.push_back(MakeSegment(AssetStreamSegmentKind::AudioPage, static_cast<std::uint32_t>(page),
                                                 headerBytes + offset, count, start, end));
                }
                return result;
            }

            if (type == AnimationClipAsset::StaticType())
            {
                const auto animation = AnimationClipAsset::Decode(bytes);
                return {MakeSegment(AssetStreamSegmentKind::AnimationWindow, 0, 0, bytes.size(), 0.0F,
                                    animation->Duration())};
            }

            return {MakeSegment(AssetStreamSegmentKind::Data, 0, 0, bytes.size())};
        }
    } // namespace

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
                                              AssetOperationProgressCallback progress,
                                              const std::span<const AssetId> sourceAssets,
                                              const std::span<const AssetId> replacedAssets)
    {
        ThrowIfOperationCancelled(cancellation);
        if (profile.Name.empty() || profile.CompressionLevel < ZSTD_minCLevel() ||
            profile.CompressionLevel > ZSTD_maxCLevel() || profile.MaximumPackBytes <= Detail::PackHeaderBytes)
            throw std::invalid_argument("Asset build profile contains invalid compression or shard settings.");
        if (profile.StreamPageBytes < 4096U || profile.StreamPageBytes > 16U * 1024U * 1024U)
            throw std::invalid_argument("Asset stream page size must be in the range 4 KiB..16 MiB.");
        const bool incremental = !sourceAssets.empty();
        if (incremental && profile.Strict)
            throw std::invalid_argument("Strict asset builds require a complete cook.");
        const auto destination = std::filesystem::absolute(outputDirectory).lexically_normal();
        std::unordered_set<AssetId> selectedSources(sourceAssets.begin(), sourceAssets.end());
        std::vector<Detail::CatalogEntry> previousEntries;
        std::unordered_map<AssetId, AssetTypeId> availableTypes;
        if (incremental)
        {
            const auto previous = Detail::LoadCatalog(destination / "catalog.json");
            previousEntries = std::move(previous.Entries);
            for (auto& entry : previousEntries)
            {
                const auto relative = entry.PackPath.lexically_relative(destination).lexically_normal();
                if (relative.empty() || relative.is_absolute() ||
                    relative.native().starts_with(std::filesystem::path("..").native()))
                    throw std::runtime_error("Existing asset pack escapes the development catalog directory.");
                entry.PackPath = relative;
                availableTypes.insert_or_assign(entry.Id, entry.Type);
            }
        }
        auto jobs = database.m_Impl->Specification.Jobs;
        if (!jobs)
        {
            JobSystemSpecification jobSpecification;
            jobSpecification.WorkerCount =
                std::clamp<std::size_t>(std::max(1U, std::thread::hardware_concurrency()), 1, 8);
            jobSpecification.BlockingWorkerCount = 1;
            jobs = CreateRef<JobSystem>(jobSpecification);
        }
        auto cookingScope = jobs->CreateScope("Asset cooking");
        const auto records = database.Records();
        struct PreparedAsset final
        {
            AssetId Id;
            AssetTypeId Type;
            const AssetSourceRecord* Record = nullptr;
            AssetImportOutput Imported;
            std::vector<AssetId> Dependencies;
        };
        std::vector<PreparedAsset> prepared;
        prepared.reserve(incremental ? selectedSources.size() : records.size());
        std::unordered_map<AssetId, std::size_t> indices;
        const auto cookingTotal = incremental ? selectedSources.size() : records.size();
        ReportOperationProgress(progress, AssetOperationPhase::Cooking, 0, cookingTotal);
        std::size_t preparedCount = 0;
        for (const auto& record : records)
        {
            ThrowIfOperationCancelled(cancellation);
            if (incremental && !selectedSources.contains(record.Id))
                continue;
            if (profile.Strict && record.Importer != ImporterName(record.Type) &&
                !database.m_Impl->FindImporter(record))
                throw std::runtime_error("Strict cooking rejected an unsupported importer: " + record.Importer);
            AssetImportOutput imported;
            if (auto cached = database.m_Impl->TakeCookInput(record))
                imported = std::move(*cached);
            else if (auto restored = database.m_Impl->RestoreCachedImport(record))
                imported = std::move(*restored);
            else
                imported = database.m_Impl->Import(record);
            auto dependencies = record.Dependencies;
            for (const auto dependency : imported.AssetDependencies)
            {
                if (std::ranges::find(dependencies, dependency) == dependencies.end())
                    dependencies.push_back(dependency);
            }
            std::ranges::sort(dependencies);
            const auto parentIndex = prepared.size();
            if (!indices.emplace(record.Id, parentIndex).second)
                throw std::runtime_error("Asset import produced a duplicate catalog identity: " + record.Id.ToString());
            prepared.push_back({record.Id, record.Type, &record, std::move(imported), std::move(dependencies)});
            for (const auto& subAsset : prepared[parentIndex].Imported.SubAssets)
            {
                if (!indices.emplace(subAsset.Id, prepared.size()).second)
                    throw std::runtime_error("Generated subasset identity collides with another catalog entry: " +
                                             subAsset.Id.ToString());
                AssetImportOutput generated;
                generated.Bytes = subAsset.Bytes;
                generated.AssetDependencies = subAsset.AssetDependencies;
                generated.Metadata = subAsset.Metadata;
                prepared.push_back(
                    {subAsset.Id, subAsset.Type, nullptr, std::move(generated), subAsset.AssetDependencies});
            }
            ++preparedCount;
            ReportOperationProgress(progress, AssetOperationPhase::Cooking, preparedCount, cookingTotal,
                                    record.RelativePath);
        }
        for (const auto& asset : prepared)
            availableTypes.insert_or_assign(asset.Id, asset.Type);
        if (profile.Strict)
        {
            std::vector<std::pair<AssetId, Ref<ManagedDataAsset>>> managedData;
            std::vector<ManagedDataCookAsset> validationAssets;
            validationAssets.reserve(prepared.size());
            const auto requireReference = [&indices, &prepared](const PreparedAsset& owner, const AssetId reference,
                                                                const AssetTypeId expectedType,
                                                                const std::string_view description)
            {
                const auto found = indices.find(reference);
                if (!reference || found == indices.end() || prepared[found->second].Type != expectedType)
                {
                    throw std::runtime_error("Strict cooking rejected " + std::string(description) +
                                             " with a missing or incorrectly typed reference: " + reference.ToString());
                }
                if (std::ranges::find(owner.Dependencies, reference) == owner.Dependencies.end())
                {
                    throw std::runtime_error("Strict cooking rejected " + std::string(description) +
                                             " because its dependency was not declared: " + reference.ToString());
                }
                return found->second;
            };
            for (const auto& asset : prepared)
            {
                std::optional<ManagedTypeId> managedType;
                if (asset.Type == ManagedDataAsset::StaticType())
                {
                    auto decoded = ManagedDataAsset::Decode(asset.Imported.Bytes);
                    managedType = decoded->Definition().ManagedType;
                    managedData.emplace_back(asset.Id, std::move(decoded));
                }
                else if (asset.Type == RigDefinitionAsset::StaticType())
                {
                    static_cast<void>(RigDefinitionAsset::Decode(asset.Imported.Bytes));
                }
                else if (asset.Type == ProceduralMotionProfileAsset::StaticType())
                {
                    static_cast<void>(ProceduralMotionProfileAsset::Decode(asset.Imported.Bytes));
                }
                else if (asset.Type == AnimationClipAsset::StaticType())
                {
                    const auto clip = AnimationClipAsset::Decode(asset.Imported.Bytes);
                    requireReference(asset, clip->Skeleton(), SkeletonAsset::StaticType(), "an animation clip");
                }
                else if (asset.Type == SkinnedMeshAsset::StaticType())
                {
                    const auto skinned = SkinnedMeshAsset::Decode(asset.Imported.Bytes);
                    const auto meshIndex =
                        requireReference(asset, skinned->Mesh(), MeshAsset::StaticType(), "a skinned mesh");
                    const auto skeletonIndex =
                        requireReference(asset, skinned->Skeleton(), SkeletonAsset::StaticType(), "a skinned mesh");
                    const auto mesh = MeshAsset::Decode(prepared[meshIndex].Imported.Bytes);
                    const auto skeleton = SkeletonAsset::Decode(prepared[skeletonIndex].Imported.Bytes);
                    const auto influenceCount =
                        skinned->Influences8().empty() ? skinned->Influences().size() : skinned->Influences8().size();
                    if (influenceCount != mesh->Vertices().size())
                    {
                        throw std::runtime_error(
                            "Strict cooking rejected a skinned mesh whose influence count does not match its mesh.");
                    }
                    for (const auto& influence : skinned->Influences())
                    {
                        for (std::size_t index = 0; index < influence.Bones.size(); ++index)
                        {
                            if (influence.Weights[index] > 0.0F && influence.Bones[index] >= skeleton->Bones().size())
                            {
                                throw std::runtime_error(
                                    "Strict cooking rejected a skinned mesh with an out-of-range bone influence.");
                            }
                        }
                    }
                    for (const auto& influence : skinned->Influences8())
                    {
                        for (std::size_t index = 0; index < influence.Count; ++index)
                        {
                            if (influence.Bones[index] >= skeleton->Bones().size())
                            {
                                throw std::runtime_error(
                                    "Strict cooking rejected a skinned mesh with an out-of-range bone influence.");
                            }
                        }
                    }
                }
                validationAssets.push_back({asset.Id, asset.Type, managedType});
            }
            if (!managedData.empty())
            {
                if (!profile.ManagedTypeDiscoveryComplete)
                {
                    throw std::runtime_error(
                        "Strict cooking requires managed runtime compilation and type discovery before validating "
                        "managed data assets.");
                }
                const auto managedTypes = DecodeManagedAssetTypeCatalog(profile.ManagedTypeCatalog);
                for (const auto& [asset, data] : managedData)
                    ValidateManagedDataForCook(asset, data->Definition(), managedTypes, validationAssets);
            }
        }
        if (!profile.Strict)
        {
            for (auto& asset : prepared)
                std::erase_if(asset.Dependencies, [&availableTypes](const AssetId dependency)
                              { return !availableTypes.contains(dependency); });
        }
        for (auto& asset : prepared)
        {
            if (asset.Type != MaterialAsset::StaticType())
                continue;
            const auto material = MaterialAsset::Decode(asset.Imported.Bytes);
            if (!material->Definition().Shader)
                throw std::runtime_error("Material asset must reference a shader: " + asset.Id.ToString());
            const auto shaderIndex = indices.find(material->Definition().Shader);
            if (shaderIndex == indices.end())
            {
                const auto existing = availableTypes.find(material->Definition().Shader);
                if (existing == availableTypes.end() || existing->second != ShaderAsset::StaticType())
                    throw std::runtime_error("Material shader dependency is missing or has the wrong type: " +
                                             material->Definition().Shader.ToString());
                continue;
            }
            if (prepared[shaderIndex->second].Type != ShaderAsset::StaticType())
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
                    const auto existing = availableTypes.find(texture);
                    if (existing != availableTypes.end() && existing->second == Texture2DAsset::StaticType())
                        continue;
                    if (profile.Strict)
                        throw std::runtime_error("Material texture property '" + property.Name +
                                                 "' is missing or has the wrong asset type.");
                    std::erase(asset.Dependencies, texture);
                    continue;
                }
                if (prepared[textureIndex->second].Type != Texture2DAsset::StaticType())
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
            for (const auto& asset : prepared)
                included.insert(asset.Id);
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
        auto temporaryToken = AssetId::Generate().ToString();
        std::erase(temporaryToken, '-');
        temporaryToken.resize(20);
        const auto temporary = Detail::PathWithSuffix(destination, ".tmp-" + temporaryToken);
        std::filesystem::create_directories(temporary);

        AssetCookResult result;
        std::vector<Detail::CatalogEntry> entries;
        if (incremental)
        {
            std::unordered_set<AssetId> replaced(replacedAssets.begin(), replacedAssets.end());
            for (const auto& asset : prepared)
                replaced.insert(asset.Id);
            for (auto& entry : previousEntries)
                if (!replaced.contains(entry.Id))
                    entries.push_back(std::move(entry));
        }
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
                if (!included.contains(asset.Id))
                    continue;
                auto bytes = std::move(asset.Imported.Bytes);
                if (asset.Record)
                    if (const auto* importer = database.m_Impl->FindImporter(*asset.Record); importer && importer->Cook)
                        bytes = importer->Cook(bytes, profile.Target);
                struct PreparedPage final
                {
                    std::size_t UncompressedOffset = 0;
                    std::size_t UncompressedBytes = 0;
                    std::vector<std::byte> Compressed;
                    Detail::Sha256Digest Digest{};
                };
                std::vector<PreparedPage> pages;
                std::uint64_t compressedBytes = 0;
                if (bytes.empty())
                {
                    auto compressed = Compress(bytes, profile.CompressionLevel);
                    compressedBytes = compressed.size();
                    pages.push_back({0, 0, std::move(compressed), Detail::Sha256(bytes)});
                }
                else
                {
                    const auto pageCount = (bytes.size() + profile.StreamPageBytes - 1U) / profile.StreamPageBytes;
                    pages.resize(pageCount);
                    std::atomic_size_t nextPage = 0;
                    std::atomic_bool failed = false;
                    std::exception_ptr failure;
                    std::mutex failureMutex;
                    const auto availableWorkers = std::max(1U, std::thread::hardware_concurrency());
                    const auto workerCount =
                        std::min<std::size_t>({pageCount, static_cast<std::size_t>(availableWorkers), 8U});
                    const auto preparePages = [&]
                    {
                        try
                        {
                            while (!failed.load(std::memory_order_relaxed))
                            {
                                const auto pageIndex = nextPage.fetch_add(1, std::memory_order_relaxed);
                                if (pageIndex >= pageCount)
                                    return;
                                ThrowIfOperationCancelled(cancellation);
                                const auto offset = pageIndex * profile.StreamPageBytes;
                                const auto count = std::min(profile.StreamPageBytes, bytes.size() - offset);
                                const auto source = std::span(bytes).subspan(offset, count);
                                pages[pageIndex] = {offset, count, Compress(source, profile.CompressionLevel),
                                                    Detail::Sha256(source)};
                            }
                        }
                        catch (...)
                        {
                            std::scoped_lock lock(failureMutex);
                            if (!failure)
                                failure = std::current_exception();
                            failed.store(true, std::memory_order_relaxed);
                        }
                    };
                    if (workerCount == 1)
                    {
                        preparePages();
                    }
                    else
                    {
                        std::vector<JobHandle> workers;
                        workers.reserve(workerCount);
                        for (std::size_t worker = 0; worker < workerCount; ++worker)
                        {
                            JobDescription description;
                            description.Name = "Compress asset pages";
                            description.Domain = JobDomain::Tooling;
                            workers.push_back(
                                cookingScope->Submit(std::move(description), [&](JobContext&) { preparePages(); }));
                        }
                        for (const auto& worker : workers)
                        {
                            (void)worker.Wait();
                            worker.RethrowIfFailed();
                        }
                    }
                    if (failure)
                        std::rethrow_exception(failure);
                    for (const auto& page : pages)
                    {
                        const auto& compressed = page.Compressed;
                        if (compressed.size() > std::numeric_limits<std::uint64_t>::max() - compressedBytes)
                            throw std::runtime_error("Compressed streamed asset size overflowed.");
                        compressedBytes += compressed.size();
                    }
                }
                if (compressedBytes > profile.MaximumPackBytes - Detail::PackHeaderBytes)
                    throw std::runtime_error("Compressed asset exceeds the build profile's maximum pack size.");
                if (!pack.is_open() ||
                    (packBytes > Detail::PackHeaderBytes && compressedBytes > profile.MaximumPackBytes - packBytes))
                    openPack();
                Detail::CatalogEntry entry;
                entry.Id = asset.Id;
                entry.Type = asset.Type;
                entry.PackPath = packPath.filename();
                entry.Offset = packBytes;
                entry.CompressedBytes = compressedBytes;
                entry.UncompressedBytes = bytes.size();
                entry.StreamLayoutVersion = 1;
                entry.Digest = Detail::Sha256(bytes);
                entry.Segments = BuildStreamSegments(asset.Type, bytes, profile.StreamPageBytes);
                entry.Dependencies = std::move(asset.Dependencies);
                entry.Metadata = asset.Imported.Metadata;
                for (const auto& page : pages)
                {
                    if (page.UncompressedBytes != 0)
                        entry.Pages.push_back({packBytes, page.Compressed.size(), page.UncompressedOffset,
                                               page.UncompressedBytes, page.Digest});
                    if (!page.Compressed.empty())
                        pack.write(reinterpret_cast<const char*>(page.Compressed.data()),
                                   static_cast<std::streamsize>(page.Compressed.size()));
                    packBytes += page.Compressed.size();
                }
                if (!pack)
                    throw std::runtime_error("Could not write asset pack payload.");
                result.UncompressedBytes += bytes.size();
                result.CompressedBytes += compressedBytes;
                entries.push_back(std::move(entry));
            }
            if (pack.is_open())
                pack.close();
            const Json buildProfile{{"schemaVersion", 1},
                                    {"name", profile.Name},
                                    {"target", static_cast<std::uint8_t>(profile.Target)},
                                    {"compression", "zstd"},
                                    {"compressionLevel", profile.CompressionLevel},
                                    {"streamPageBytes", profile.StreamPageBytes},
                                    {"maximumPackBytes", profile.MaximumPackBytes},
                                    {"strict", profile.Strict}};
            ValidateDependencies(entries);
            const auto catalogPath = temporary / "catalog.json";
            Detail::WriteCatalog(catalogPath, entries);
            auto generationSeed = ReadSource(catalogPath, 64U * 1024U * 1024U);
            const auto profileIdentity = buildProfile.dump();
            const auto profileBytes = std::as_bytes(std::span(profileIdentity));
            generationSeed.insert(generationSeed.end(), profileBytes.begin(), profileBytes.end());
            const auto generation = Detail::DigestToString(Detail::Sha256(generationSeed));
            for (std::size_t packIndex = 0; packIndex < result.PackCount; ++packIndex)
            {
                const auto previousName = std::filesystem::path("content-" + std::to_string(packIndex) + ".keirepak");
                const auto publishedName =
                    std::filesystem::path("content-" + generation + "-" + std::to_string(packIndex) + ".keirepak");
                Detail::RenamePathWithRetry(temporary / previousName, temporary / publishedName);
                for (auto& entry : entries)
                    if (entry.PackPath == previousName)
                        entry.PackPath = publishedName;
            }
            Detail::WriteCatalog(catalogPath, entries);
            Detail::WriteTextFileAtomically(temporary / "build-profile.json", buildProfile.dump(2) + '\n');
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
            std::vector<std::byte> bytes(static_cast<std::size_t>(entry.UncompressedBytes));
            if (entry.Pages.empty())
            {
                pack.seekg(static_cast<std::streamoff>(entry.Offset), std::ios::beg);
                std::vector<std::byte> compressed(static_cast<std::size_t>(entry.CompressedBytes));
                if (!compressed.empty() && !pack.read(reinterpret_cast<char*>(compressed.data()),
                                                      static_cast<std::streamsize>(compressed.size())))
                    throw std::runtime_error("Could not read cooked asset payload.");
                const auto size = ZSTD_decompress(bytes.data(), bytes.size(), compressed.data(), compressed.size());
                if (ZSTD_isError(size) || size != bytes.size())
                    throw std::runtime_error("Cooked asset payload failed decompression.");
            }
            else
            {
                for (const auto& page : entry.Pages)
                {
                    pack.seekg(static_cast<std::streamoff>(page.Offset), std::ios::beg);
                    std::vector<std::byte> compressed(static_cast<std::size_t>(page.CompressedBytes));
                    if (!pack.read(reinterpret_cast<char*>(compressed.data()),
                                   static_cast<std::streamsize>(compressed.size())))
                        throw std::runtime_error("Could not read cooked asset stream page.");
                    auto destination = std::span(bytes).subspan(static_cast<std::size_t>(page.UncompressedOffset),
                                                                static_cast<std::size_t>(page.UncompressedBytes));
                    const auto size =
                        ZSTD_decompress(destination.data(), destination.size(), compressed.data(), compressed.size());
                    if (ZSTD_isError(size) || size != destination.size() || Detail::Sha256(destination) != page.Digest)
                        throw std::runtime_error("Cooked asset stream page failed decompression or integrity.");
                }
            }
            if (Detail::Sha256(bytes) != entry.Digest)
                throw std::runtime_error("Cooked asset payload failed decompression or integrity validation.");
        }
    }
} // namespace Keire
