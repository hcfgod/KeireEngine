#include "KeireInternal/Rendering/RenderBackendInternal.h"

#include "Keire/BuiltinSkinningShaders.h"
#include "Keire/Log.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <ranges>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

namespace Keire::RenderBackend
{
    bool RenderSharedState::EnsureSkinningPipeline()
    {
        if (SkinningPipelineAttempted)
            return SkinningPipeline != nullptr;
        SkinningPipelineAttempted = true;

        const auto formats = SDL_GetGPUShaderFormats(Device);
        const unsigned char* code = nullptr;
        std::size_t codeSize = 0;
        SDL_GPUShaderFormat format = SDL_GPU_SHADERFORMAT_INVALID;
        if ((formats & SDL_GPU_SHADERFORMAT_DXIL) != 0)
        {
            code = ::Keire::Detail::BuiltinSkinningComputeDxil;
            codeSize = sizeof(::Keire::Detail::BuiltinSkinningComputeDxil);
            format = SDL_GPU_SHADERFORMAT_DXIL;
        }
        else if ((formats & SDL_GPU_SHADERFORMAT_SPIRV) != 0)
        {
            code = ::Keire::Detail::BuiltinSkinningComputeSpirv;
            codeSize = sizeof(::Keire::Detail::BuiltinSkinningComputeSpirv);
            format = SDL_GPU_SHADERFORMAT_SPIRV;
        }
        else if ((formats & SDL_GPU_SHADERFORMAT_MSL) != 0)
        {
            code = ::Keire::Detail::BuiltinSkinningComputeMsl;
            codeSize = sizeof(::Keire::Detail::BuiltinSkinningComputeMsl);
            format = SDL_GPU_SHADERFORMAT_MSL;
        }
        if (!code)
            return false;

        SDL_GPUComputePipelineCreateInfo createInfo{};
        createInfo.code = code;
        createInfo.code_size = codeSize;
        createInfo.entrypoint = "CSMain";
        createInfo.format = format;
        createInfo.num_readonly_storage_buffers = 3;
        createInfo.num_readwrite_storage_buffers = 2;
        createInfo.num_uniform_buffers = 1;
        createInfo.threadcount_x = 64;
        createInfo.threadcount_y = 1;
        createInfo.threadcount_z = 1;
        SkinningPipeline = SDL_CreateGPUComputePipeline(Device, &createInfo);
        return SkinningPipeline != nullptr;
    }

    void RenderSharedState::PrepareSkinning(SDL_GPUCommandBuffer* commands, SceneRenderPacket& packet)
    {
        struct alignas(16) GpuSkinInfluence
        {
            std::array<std::uint32_t, 4> Bones0{};
            std::array<std::uint32_t, 4> Bones1{};
            std::array<float, 4> Weights0{};
            std::array<float, 4> Weights1{};
        };
        struct alignas(16) GpuSkinMatrix
        {
            std::array<float, 4> Column0{};
            std::array<float, 4> Column1{};
            std::array<float, 4> Column2{};
            std::array<float, 4> Column3{};
        };
        struct SkinDispatch
        {
            std::uint32_t VertexCount = 0;
            std::uint32_t InfluenceCount = 4;
            std::uint32_t SkinningMode = 0;
            std::uint32_t Padding = 0;
        };
        static_assert(sizeof(GpuSkinInfluence) == 64);
        static_assert(alignof(GpuSkinInfluence) == 16);
        static_assert(sizeof(GpuSkinMatrix) == 64);
        static_assert(alignof(GpuSkinMatrix) == 16);
        static_assert(sizeof(SkinDispatch) == 16);
        static_assert(sizeof(GpuMeshVertex) == 96);
        static_assert(sizeof(GpuRenderVertex) == 48);

        const auto createOutput = [this](const std::uint32_t vertexCount)
        {
            const auto assetBytes = static_cast<std::uint64_t>(vertexCount) * sizeof(GpuMeshVertex);
            const auto builtinBytes = static_cast<std::uint64_t>(vertexCount) * sizeof(GpuRenderVertex);
            if (assetBytes > std::numeric_limits<std::uint32_t>::max() ||
                builtinBytes > std::numeric_limits<std::uint32_t>::max())
            {
                throw std::invalid_argument("Skinned mesh output exceeds SDL's 32-bit buffer limit.");
            }

            GpuSkinOutputResources result;
            const auto createBuffer = [this](const std::uint32_t bytes)
            {
                SDL_GPUBufferCreateInfo createInfo{};
                createInfo.usage = SDL_GPU_BUFFERUSAGE_VERTEX | SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE;
                createInfo.size = bytes;
                auto* buffer = SDL_CreateGPUBuffer(Device, &createInfo);
                if (!buffer)
                    throw std::runtime_error("SDL_CreateGPUBuffer(skin cache) failed: " + LastSdlError());
                return buffer;
            };
            try
            {
                result.AssetVertices = createBuffer(static_cast<std::uint32_t>(assetBytes));
                result.BuiltinVertices = createBuffer(static_cast<std::uint32_t>(builtinBytes));
                return result;
            }
            catch (...)
            {
                if (result.BuiltinVertices)
                    SDL_ReleaseGPUBuffer(Device, result.BuiltinVertices);
                if (result.AssetVertices)
                    SDL_ReleaseGPUBuffer(Device, result.AssetVertices);
                throw;
            }
        };

        for (auto& item : packet.DrawItems)
        {
            item.SkinnedAssetVertices = nullptr;
            item.SkinnedBuiltinVertices = nullptr;
            if (!Assets || !item.Skin || item.SkinPalette.empty())
                continue;

            auto [cacheIterator, inserted] = SkinCache.try_emplace(item.Skin);
            (void)inserted;
            auto& cache = cacheIterator->second;
            cache.LastRequestedFrame = Statistics.Frame;

            const auto skinHandle = Assets->Load<SkinnedMeshAsset>(item.Skin, AssetPriority::High);
            const auto skin = skinHandle.TryGetLoaded();
            if (!skin)
                continue;
            const auto meshHandle = Assets->Load<MeshAsset>(skin->Mesh(), AssetPriority::High);
            const auto meshAsset = meshHandle.TryGetLoaded();
            if (!meshAsset || skinHandle.Revision() == 0 || meshHandle.Revision() == 0)
                continue;

            auto dependencyStamp = std::uint64_t{1469598103934665603ULL};
            dependencyStamp = HashDependencyStamp(dependencyStamp, item.Skin);
            dependencyStamp = HashDependencyStamp(dependencyStamp, skinHandle.Revision());
            dependencyStamp = HashDependencyStamp(dependencyStamp, skin->Mesh());
            dependencyStamp = HashDependencyStamp(dependencyStamp, meshHandle.Revision());
            if (dependencyStamp != cache.LastAttemptedDependencyStamp)
            {
                cache.LastAttemptedDependencyStamp = dependencyStamp;
                bool valid = skin->Influences8().size() == meshAsset->Vertices().size() &&
                             !meshAsset->Vertices().empty() &&
                             meshAsset->Vertices().size() <= std::numeric_limits<std::uint32_t>::max();
                std::uint32_t maximumBoneIndex = 0;
                if (valid)
                {
                    for (const auto& influence : skin->Influences8())
                    {
                        if (influence.Count == 0 || influence.Count > influence.Bones.size())
                        {
                            valid = false;
                            break;
                        }
                        for (std::size_t index = 0; index < influence.Count; ++index)
                        {
                            if (!std::isfinite(influence.Weights[index]) || influence.Weights[index] < 0.0F)
                            {
                                valid = false;
                                break;
                            }
                            maximumBoneIndex =
                                std::max(maximumBoneIndex, static_cast<std::uint32_t>(influence.Bones[index]));
                        }
                        if (!valid)
                            break;
                    }
                }

                try
                {
                    GpuSkinResources replacement;
                    replacement.Valid = valid;
                    if (valid)
                    {
                        replacement.VertexCount = static_cast<std::uint32_t>(meshAsset->Vertices().size());
                        replacement.MaximumBoneIndex = maximumBoneIndex;
                        replacement.MaximumInfluences = skin->MaximumInfluences();
                        const auto* driver = SDL_GetGPUDeviceDriver(Device);
                        if (driver && SupportsComputeSkinning(driver, skin->Method()) && EnsureSkinningPipeline())
                        {
                            std::vector<GpuSkinInfluence> influences(skin->Influences8().size());
                            for (std::size_t vertex = 0; vertex < skin->Influences8().size(); ++vertex)
                            {
                                const auto& source = skin->Influences8()[vertex];
                                for (std::size_t influence = 0; influence < source.Count; ++influence)
                                {
                                    if (influence < 4)
                                    {
                                        influences[vertex].Bones0[influence] = source.Bones[influence];
                                        influences[vertex].Weights0[influence] = source.Weights[influence];
                                    }
                                    else
                                    {
                                        influences[vertex].Bones1[influence - 4] = source.Bones[influence];
                                        influences[vertex].Weights1[influence - 4] = source.Weights[influence];
                                    }
                                }
                            }
                            replacement.Influences = UploadBuffer(commands, std::as_bytes(std::span(influences)),
                                                                  SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ);
                        }
                    }
                    Retire(std::exchange(cache.Resources, std::move(replacement)));
                    cache.Skin = skin;
                    cache.Mesh = meshAsset;
                    cache.LoadedDependencyStamp = dependencyStamp;
                    ++SkinningStaticBuilds;
                }
                catch (const std::exception& error)
                {
                    KEIRE_CORE_ERROR("Skin GPU cache rebuild failed for id={} dependency={}: {}", item.Skin.ToString(),
                                     dependencyStamp, error.what());
                }
            }

            if (cache.LoadedDependencyStamp != dependencyStamp || !cache.Skin || !cache.Mesh ||
                !cache.Resources.Valid || cache.Skin->Mesh() != item.Mesh ||
                cache.Skin->Skeleton() != item.SkinSkeleton ||
                cache.Resources.VertexCount != cache.Mesh->Vertices().size() ||
                cache.Resources.MaximumBoneIndex >= item.SkinPalette.size() ||
                !std::ranges::all_of(item.SkinPalette, [](const Matrix4& matrix) { return Math::IsFinite(matrix); }))
            {
                continue;
            }

            item.Skinning = cache.Skin->Method();
            const auto& mesh = ResolveMesh(item.Mesh);
            if (mesh.Empty() || !mesh.AssetVertices)
                continue;

            const auto useCompute = cache.Resources.Influences && SkinningPipeline;
            if (!useCompute)
            {
                std::vector<MeshVertex> deformed(cache.Mesh->Vertices().size());
                SkinMeshCpu(cache.Mesh->Vertices(), cache.Skin->Influences8(), item.SkinPalette, item.Skinning,
                            deformed);
                const auto& sourceBounds = cache.Mesh->Bounds();
                const auto sourceMagnitude =
                    std::max({1.0F, std::abs(sourceBounds.Minimum.X), std::abs(sourceBounds.Minimum.Y),
                              std::abs(sourceBounds.Minimum.Z), std::abs(sourceBounds.Maximum.X),
                              std::abs(sourceBounds.Maximum.Y), std::abs(sourceBounds.Maximum.Z)});
                const auto maximumCoordinate = sourceMagnitude * 8.0F;
                const auto validDeformation =
                    std::ranges::all_of(deformed,
                                        [maximumCoordinate](const MeshVertex& vertex)
                                        {
                                            return Math::IsFinite(vertex.Position) && Math::IsFinite(vertex.Normal) &&
                                                   Math::IsFinite(vertex.Tangent) &&
                                                   std::abs(vertex.Position.X) <= maximumCoordinate &&
                                                   std::abs(vertex.Position.Y) <= maximumCoordinate &&
                                                   std::abs(vertex.Position.Z) <= maximumCoordinate;
                                        });
                if (!validDeformation)
                    continue;
                std::vector<RenderVertex> builtinVertices;
                builtinVertices.reserve(deformed.size());
                for (const auto& vertex : deformed)
                {
                    builtinVertices.push_back(
                        {vertex.Position,
                         {vertex.VertexColor.Red, vertex.VertexColor.Green, vertex.VertexColor.Blue},
                         vertex.Normal});
                }
                item.SkinnedAssetVertices = UploadMeshVertexBuffer(commands, deformed);
                item.SkinnedBuiltinVertices = UploadVertexBuffer(commands, builtinVertices);
                FrameTransientBuffers.push_back(item.SkinnedAssetVertices);
                FrameTransientBuffers.push_back(item.SkinnedBuiltinVertices);
                continue;
            }

            std::vector<GpuSkinMatrix> palette;
            palette.reserve(item.SkinPalette.size());
            for (const auto& matrix : item.SkinPalette)
            {
                const auto& elements = matrix.Elements;
                palette.push_back({
                    {elements[0], elements[1], elements[2], elements[3]},
                    {elements[4], elements[5], elements[6], elements[7]},
                    {elements[8], elements[9], elements[10], elements[11]},
                    {elements[12], elements[13], elements[14], elements[15]},
                });
            }
            auto* paletteBuffer =
                UploadBuffer(commands, std::as_bytes(std::span(palette)), SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ);
            FrameTransientBuffers.push_back(paletteBuffer);

            const GpuSkinInstanceKey instanceKey{packet.Scene, item.Entity};
            auto [instanceIterator, instanceInserted] = cache.Resources.Instances.try_emplace(instanceKey);
            auto& instance = instanceIterator->second;
            if (instanceInserted)
                instance.Outputs.resize(Specification.MaximumFramesInFlight);
            instance.LastPreparedFrame = Statistics.Frame;
            auto& output =
                instance.Outputs[SkinningOutputSlot(ActiveGpuSubmissionSerial, Specification.MaximumFramesInFlight)];
            if (output.Empty())
            {
                output = createOutput(cache.Resources.VertexCount);
                ++SkinningOutputBuilds;
            }
            item.SkinnedAssetVertices = output.AssetVertices;
            item.SkinnedBuiltinVertices = output.BuiltinVertices;

            const std::array writeBindings{SDL_GPUStorageBufferReadWriteBinding{item.SkinnedAssetVertices, false},
                                           SDL_GPUStorageBufferReadWriteBinding{item.SkinnedBuiltinVertices, false}};
            auto* pass = SDL_BeginGPUComputePass(commands, nullptr, 0, writeBindings.data(),
                                                 static_cast<std::uint32_t>(writeBindings.size()));
            if (!pass)
                throw std::runtime_error("SDL_BeginGPUComputePass(skin cache) failed: " + LastSdlError());
            SDL_BindGPUComputePipeline(pass, SkinningPipeline);
            const std::array readBindings{mesh.AssetVertices, cache.Resources.Influences, paletteBuffer};
            SDL_BindGPUComputeStorageBuffers(pass, 0, readBindings.data(),
                                             static_cast<std::uint32_t>(readBindings.size()));
            const SkinDispatch dispatch{cache.Resources.VertexCount, cache.Resources.MaximumInfluences};
            SDL_PushGPUComputeUniformData(commands, 0, &dispatch, sizeof(dispatch));
            SDL_DispatchGPUCompute(pass, (dispatch.VertexCount + 63U) / 64U, 1, 1);
            SDL_EndGPUComputePass(pass);
        }
    }

} // namespace Keire::RenderBackend
