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

    void RenderSharedState::PrepareSkinning(SDL_GPUCommandBuffer* commands, SceneRenderPacket& packet,
                                            const std::uint64_t surface)
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

        const auto skinBufferSizes = [](const std::uint32_t vertexCount)
        {
            const auto assetBytes = static_cast<std::uint64_t>(vertexCount) * sizeof(GpuMeshVertex);
            const auto builtinBytes = static_cast<std::uint64_t>(vertexCount) * sizeof(GpuRenderVertex);
            if (assetBytes > std::numeric_limits<std::uint32_t>::max() ||
                builtinBytes > std::numeric_limits<std::uint32_t>::max())
            {
                throw std::invalid_argument("Skinned mesh output exceeds SDL's 32-bit buffer limit.");
            }
            return std::pair{static_cast<std::uint32_t>(assetBytes), static_cast<std::uint32_t>(builtinBytes)};
        };
        const auto createSkinBuffer = [this](const std::uint32_t bytes)
        {
            SDL_GPUBufferCreateInfo createInfo{};
            createInfo.usage = SDL_GPU_BUFFERUSAGE_VERTEX | SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE;
            createInfo.size = bytes;
            auto* buffer = SDL_CreateGPUBuffer(Device, &createInfo);
            if (!buffer)
                throw std::runtime_error("SDL_CreateGPUBuffer(skin cache) failed: " + LastSdlError());
            return buffer;
        };
        const auto releasePalette = [this](GpuSkinPaletteResources& palette)
        {
            if (palette.Transfer)
                SDL_ReleaseGPUTransferBuffer(Device, palette.Transfer);
            if (palette.Buffer)
                SDL_ReleaseGPUBuffer(Device, palette.Buffer);
            palette = {};
        };
        const auto createPalette = [this](const std::uint32_t bytes)
        {
            GpuSkinPaletteResources result;
            SDL_GPUBufferCreateInfo bufferInformation{};
            bufferInformation.usage = SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ;
            bufferInformation.size = bytes;
            result.Buffer = SDL_CreateGPUBuffer(Device, &bufferInformation);
            if (!result.Buffer)
                throw std::runtime_error("SDL_CreateGPUBuffer(skin palette) failed: " + LastSdlError());

            SDL_GPUTransferBufferCreateInfo transferInformation{};
            transferInformation.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
            transferInformation.size = bytes;
            result.Transfer = SDL_CreateGPUTransferBuffer(Device, &transferInformation);
            if (!result.Transfer)
            {
                SDL_ReleaseGPUBuffer(Device, result.Buffer);
                throw std::runtime_error("SDL_CreateGPUTransferBuffer(skin palette) failed: " + LastSdlError());
            }
            result.Bytes = bytes;
            return result;
        };
        const auto createOutput = [this, &createPalette, &createSkinBuffer, &releasePalette,
                                   &skinBufferSizes](const std::uint32_t vertexCount, const std::uint32_t paletteBytes)
        {
            const auto [assetBytes, builtinBytes] = skinBufferSizes(vertexCount);

            GpuSkinOutputResources result;
            try
            {
                result.AssetVertices = createSkinBuffer(assetBytes);
                result.BuiltinVertices = createSkinBuffer(builtinBytes);
                result.Palette = createPalette(paletteBytes);
                return result;
            }
            catch (const GpuDeviceLostError&)
            {
                throw;
            }
            catch (const std::exception& error)
            {
                ThrowIfDeviceLost("skinning output-buffer creation", error.what());
                releasePalette(result.Palette);
                if (result.BuiltinVertices)
                    SDL_ReleaseGPUBuffer(Device, result.BuiltinVertices);
                if (result.AssetVertices)
                    SDL_ReleaseGPUBuffer(Device, result.AssetVertices);
                if (result.PreviousBuiltinVertices)
                    SDL_ReleaseGPUBuffer(Device, result.PreviousBuiltinVertices);
                if (result.PreviousAssetVertices)
                    SDL_ReleaseGPUBuffer(Device, result.PreviousAssetVertices);
                throw;
            }
            catch (...)
            {
                releasePalette(result.Palette);
                if (result.BuiltinVertices)
                    SDL_ReleaseGPUBuffer(Device, result.BuiltinVertices);
                if (result.AssetVertices)
                    SDL_ReleaseGPUBuffer(Device, result.AssetVertices);
                if (result.PreviousBuiltinVertices)
                    SDL_ReleaseGPUBuffer(Device, result.PreviousBuiltinVertices);
                if (result.PreviousAssetVertices)
                    SDL_ReleaseGPUBuffer(Device, result.PreviousAssetVertices);
                throw;
            }
        };
        const auto ensurePreviousOutput =
            [this, &createPalette, &createSkinBuffer, &releasePalette, &skinBufferSizes](
                GpuSkinOutputResources& output, const std::uint32_t vertexCount, const std::uint32_t paletteBytes)
        {
            if (output.PreviousAssetVertices && output.PreviousBuiltinVertices && !output.PreviousPalette.Empty())
                return;
            if (output.PreviousAssetVertices || output.PreviousBuiltinVertices || !output.PreviousPalette.Empty())
                throw std::logic_error("Skinned mesh previous-output resources are only partially initialized.");

            const auto [assetBytes, builtinBytes] = skinBufferSizes(vertexCount);
            SDL_GPUBuffer* asset = nullptr;
            SDL_GPUBuffer* builtin = nullptr;
            GpuSkinPaletteResources palette;
            try
            {
                asset = createSkinBuffer(assetBytes);
                builtin = createSkinBuffer(builtinBytes);
                palette = createPalette(paletteBytes);
                output.PreviousAssetVertices = asset;
                output.PreviousBuiltinVertices = builtin;
                output.PreviousPalette = std::exchange(palette, {});
            }
            catch (const GpuDeviceLostError&)
            {
                throw;
            }
            catch (const std::exception& error)
            {
                ThrowIfDeviceLost("skinning previous-output buffer creation", error.what());
                releasePalette(palette);
                if (builtin)
                    SDL_ReleaseGPUBuffer(Device, builtin);
                if (asset)
                    SDL_ReleaseGPUBuffer(Device, asset);
                throw;
            }
            catch (...)
            {
                releasePalette(palette);
                if (builtin)
                    SDL_ReleaseGPUBuffer(Device, builtin);
                if (asset)
                    SDL_ReleaseGPUBuffer(Device, asset);
                throw;
            }
        };

        for (auto& item : packet.DrawItems)
        {
            item.SkinnedAssetVertices = nullptr;
            item.SkinnedBuiltinVertices = nullptr;
            item.PreviousSkinnedAssetVertices = nullptr;
            item.PreviousSkinnedBuiltinVertices = nullptr;
            item.CurrentPoseSubmeshBounds.clear();
            item.BoundsPoseGeneration = 0;
            item.BoundsFrameIndex = 0;
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
                catch (const GpuDeviceLostError&)
                {
                    throw;
                }
                catch (const std::exception& error)
                {
                    ThrowIfDeviceLost("skin GPU cache rebuild", error.what());
                    KEIRE_CORE_ERROR("Skin GPU cache rebuild failed for id={} dependency={}: {}", item.Skin.ToString(),
                                     dependencyStamp, error.what());
                }
            }

            if (cache.LoadedDependencyStamp != dependencyStamp || !cache.Skin || !cache.Mesh ||
                !cache.Resources.Valid || cache.Skin->Mesh() != item.Mesh ||
                cache.Skin->Skeleton() != item.SkinSkeleton ||
                cache.Resources.VertexCount != cache.Mesh->Vertices().size() ||
                cache.Resources.MaximumBoneIndex >= item.SkinPalette.size() ||
                item.PreviousSkinPalette.size() != item.SkinPalette.size() ||
                !std::ranges::all_of(item.SkinPalette, [](const Matrix4& matrix) { return Math::IsFinite(matrix); }) ||
                !std::ranges::all_of(item.PreviousSkinPalette,
                                     [](const Matrix4& matrix) { return Math::IsFinite(matrix); }))
            {
                continue;
            }

            item.Skinning = cache.Skin->Method();
            const auto& mesh = ResolveMesh(item.Mesh);
            if (mesh.Empty() || !mesh.AssetVertices)
                continue;

            std::vector<MeshBounds> currentPoseBounds;
            if (item.Skinning == SkinningMethod::LinearBlend && item.PoseGeneration != 0 &&
                cache.Skin->HasCompleteInfluenceBounds() &&
                cache.Skin->InfluenceBoundsSubmeshCount() == mesh.Submeshes.size())
            {
                try
                {
                    currentPoseBounds = CalculateLinearBlendPoseBounds(
                        cache.Skin->InfluenceBounds(), cache.Skin->InfluenceBoundsSubmeshCount(), item.SkinPalette);
                }
                catch (const std::invalid_argument& error)
                {
                    KEIRE_CORE_WARN("Current-pose bounds rejected for skin id={}: {}", item.Skin.ToString(),
                                    error.what());
                }
            }
            const auto commitCurrentPoseBounds = [&]
            {
                if (currentPoseBounds.size() != mesh.Submeshes.size())
                    return;
                item.CurrentPoseSubmeshBounds = std::move(currentPoseBounds);
                item.BoundsPoseGeneration = item.PoseGeneration;
                item.BoundsFrameIndex = packet.FrameIndex;
            };

            const auto useCompute = cache.Resources.Influences && SkinningPipeline;
            if (!useCompute)
            {
                std::vector<MeshVertex> deformed(cache.Mesh->Vertices().size());
                std::vector<MeshVertex> previousDeformed(cache.Mesh->Vertices().size());
                SkinMeshCpu(cache.Mesh->Vertices(), cache.Skin->Influences8(), item.SkinPalette, item.Skinning,
                            deformed);
                SkinMeshCpu(cache.Mesh->Vertices(), cache.Skin->Influences8(), item.PreviousSkinPalette, item.Skinning,
                            previousDeformed);
                const auto& sourceBounds = cache.Mesh->Bounds();
                const auto sourceMagnitude =
                    std::max({1.0F, std::abs(sourceBounds.Minimum.X), std::abs(sourceBounds.Minimum.Y),
                              std::abs(sourceBounds.Minimum.Z), std::abs(sourceBounds.Maximum.X),
                              std::abs(sourceBounds.Maximum.Y), std::abs(sourceBounds.Maximum.Z)});
                const auto maximumCoordinate = sourceMagnitude * 8.0F;
                const auto validDeformation = [maximumCoordinate](const std::span<const MeshVertex> vertices)
                {
                    return std::ranges::all_of(vertices,
                                               [maximumCoordinate](const MeshVertex& vertex)
                                               {
                                                   return Math::IsFinite(vertex.Position) &&
                                                          Math::IsFinite(vertex.Normal) &&
                                                          Math::IsFinite(vertex.Tangent) &&
                                                          std::abs(vertex.Position.X) <= maximumCoordinate &&
                                                          std::abs(vertex.Position.Y) <= maximumCoordinate &&
                                                          std::abs(vertex.Position.Z) <= maximumCoordinate;
                                               });
                };
                if (!validDeformation(deformed) || !validDeformation(previousDeformed))
                    continue;
                const auto makeBuiltinVertices = [](const std::span<const MeshVertex> vertices)
                {
                    std::vector<RenderVertex> result;
                    result.reserve(vertices.size());
                    for (const auto& vertex : vertices)
                    {
                        result.push_back({vertex.Position,
                                          {vertex.VertexColor.Red, vertex.VertexColor.Green, vertex.VertexColor.Blue},
                                          vertex.Normal});
                    }
                    return result;
                };
                const auto builtinVertices = makeBuiltinVertices(deformed);
                const auto previousBuiltinVertices = makeBuiltinVertices(previousDeformed);
                item.SkinnedAssetVertices = UploadMeshVertexBuffer(commands, deformed);
                FrameTransientBuffers.push_back(item.SkinnedAssetVertices);
                item.SkinnedBuiltinVertices = UploadVertexBuffer(commands, builtinVertices);
                FrameTransientBuffers.push_back(item.SkinnedBuiltinVertices);
                item.PreviousSkinnedAssetVertices = UploadMeshVertexBuffer(commands, previousDeformed);
                FrameTransientBuffers.push_back(item.PreviousSkinnedAssetVertices);
                item.PreviousSkinnedBuiltinVertices = UploadVertexBuffer(commands, previousBuiltinVertices);
                FrameTransientBuffers.push_back(item.PreviousSkinnedBuiltinVertices);
                commitCurrentPoseBounds();
                continue;
            }

            const auto paletteMatrixCount = static_cast<std::uint64_t>(cache.Resources.MaximumBoneIndex) + 1U;
            const auto paletteBytes64 = paletteMatrixCount * sizeof(GpuSkinMatrix);
            if (paletteBytes64 > std::numeric_limits<std::uint32_t>::max())
                throw std::invalid_argument("Skin palette exceeds SDL's 32-bit buffer limit.");
            const auto paletteBytes = static_cast<std::uint32_t>(paletteBytes64);

            const GpuSkinInstanceKey instanceKey{item.Scene ? item.Scene : packet.Scene, item.Entity, surface};
            auto [instanceIterator, instanceInserted] = cache.Resources.Instances.try_emplace(instanceKey);
            auto& instance = instanceIterator->second;
            if (instanceInserted)
                instance.Outputs.resize(Specification.MaximumFramesInFlight);
            instance.LastPreparedFrame = Statistics.Frame;
            const auto outputSlot = SkinningOutputSlot(ActiveGpuSubmissionSerial, Specification.MaximumFramesInFlight);
            auto& output = instance.Outputs[outputSlot];
            if (output.Empty())
            {
                output = createOutput(cache.Resources.VertexCount, paletteBytes);
                ++SkinningOutputBuilds;
            }
            item.SkinnedAssetVertices = output.AssetVertices;
            item.SkinnedBuiltinVertices = output.BuiltinVertices;

            bool reusedPreviousOutput = false;
            if (instance.HasPublishedOutput && instance.LastPublishedOutputSlot < instance.Outputs.size())
            {
                const auto& previousOutput = instance.Outputs[instance.LastPublishedOutputSlot];
                reusedPreviousOutput =
                    CanReusePreviousSkinOutput(true, instance.LastPublishedOutputSlot, outputSlot,
                                               previousOutput.PoseGeneration, item.PreviousPoseGeneration);
                if (reusedPreviousOutput)
                {
                    item.PreviousSkinnedAssetVertices = previousOutput.AssetVertices;
                    item.PreviousSkinnedBuiltinVertices = previousOutput.BuiltinVertices;
                }
            }
            if (!reusedPreviousOutput)
            {
                ensurePreviousOutput(output, cache.Resources.VertexCount, paletteBytes);
                item.PreviousSkinnedAssetVertices = output.PreviousAssetVertices;
                item.PreviousSkinnedBuiltinVertices = output.PreviousBuiltinVertices;
            }

            const auto uploadPalette = [&](const std::span<const Matrix4> source, GpuSkinPaletteResources& destination)
            {
                if (source.size() < paletteMatrixCount || destination.Bytes != paletteBytes || !destination.Buffer ||
                    !destination.Transfer)
                {
                    throw std::logic_error("Skinned mesh palette resources do not match the active skin.");
                }
                auto* mapped =
                    static_cast<GpuSkinMatrix*>(SDL_MapGPUTransferBuffer(Device, destination.Transfer, true));
                if (!mapped)
                    throw std::runtime_error("SDL_MapGPUTransferBuffer(skin palette) failed: " + LastSdlError());
                for (std::size_t index = 0; index < paletteMatrixCount; ++index)
                {
                    const auto& elements = source[index].Elements;
                    mapped[index] = {
                        {elements[0], elements[1], elements[2], elements[3]},
                        {elements[4], elements[5], elements[6], elements[7]},
                        {elements[8], elements[9], elements[10], elements[11]},
                        {elements[12], elements[13], elements[14], elements[15]},
                    };
                }
                SDL_UnmapGPUTransferBuffer(Device, destination.Transfer);

                const SDL_GPUTransferBufferLocation sourceLocation{destination.Transfer, 0};
                const SDL_GPUBufferRegion destinationRegion{destination.Buffer, 0, paletteBytes};
                auto* copy = SDL_BeginGPUCopyPass(commands);
                if (!copy)
                    throw std::runtime_error("SDL_BeginGPUCopyPass(skin palette) failed: " + LastSdlError());
                SDL_UploadToGPUBuffer(copy, &sourceLocation, &destinationRegion, true);
                SDL_EndGPUCopyPass(copy);
                return destination.Buffer;
            };
            auto* paletteBuffer = uploadPalette(item.SkinPalette, output.Palette);
            const SkinDispatch dispatch{cache.Resources.VertexCount, cache.Resources.MaximumInfluences};
            const auto recordSkinning =
                [&](SDL_GPUBuffer* assetOutput, SDL_GPUBuffer* builtinOutput, SDL_GPUBuffer* selectedPalette)
            {
                const std::array writeBindings{SDL_GPUStorageBufferReadWriteBinding{assetOutput, false},
                                               SDL_GPUStorageBufferReadWriteBinding{builtinOutput, false}};
                auto* pass = SDL_BeginGPUComputePass(commands, nullptr, 0, writeBindings.data(),
                                                     static_cast<std::uint32_t>(writeBindings.size()));
                if (!pass)
                    throw std::runtime_error("SDL_BeginGPUComputePass(skin cache) failed: " + LastSdlError());
                SDL_BindGPUComputePipeline(pass, SkinningPipeline);
                const std::array readBindings{mesh.AssetVertices, cache.Resources.Influences, selectedPalette};
                SDL_BindGPUComputeStorageBuffers(pass, 0, readBindings.data(),
                                                 static_cast<std::uint32_t>(readBindings.size()));
                SDL_PushGPUComputeUniformData(commands, 0, &dispatch, sizeof(dispatch));
                SDL_DispatchGPUCompute(pass, (dispatch.VertexCount + 63U) / 64U, 1, 1);
                SDL_EndGPUComputePass(pass);
            };
            recordSkinning(item.SkinnedAssetVertices, item.SkinnedBuiltinVertices, paletteBuffer);
            if (!reusedPreviousOutput)
            {
                auto* previousPaletteBuffer = uploadPalette(item.PreviousSkinPalette, output.PreviousPalette);
                recordSkinning(item.PreviousSkinnedAssetVertices, item.PreviousSkinnedBuiltinVertices,
                               previousPaletteBuffer);
            }
            PendingGpuSkinOutputPublications.push_back({&instance, outputSlot, item.PoseGeneration});
            commitCurrentPoseBounds();
        }
    }

} // namespace Keire::RenderBackend
