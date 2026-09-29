#include "KeireInternal/Rendering/RenderBackendInternal.h"

#include <array>
#include <cstring>
#include <limits>
#include <memory>
#include <stdexcept>

namespace Keire::RenderBackend
{
    void RenderSharedState::UploadCpuSkinning(SDL_GPUCommandBuffer* commands, GpuSkinOutputResources& output,
                                              const std::span<const MeshVertex> current,
                                              const std::span<const MeshVertex> previous)
    {
        const auto totalBytes64 = current.size() * std::uint64_t{2} * (sizeof(GpuMeshVertex) + sizeof(GpuRenderVertex));
        if (current.empty() || previous.size() != current.size() ||
            totalBytes64 > std::numeric_limits<std::uint32_t>::max())
            throw std::invalid_argument("CPU skin output requires matching nonempty poses within SDL's buffer limit.");
        const auto assetBytes = static_cast<std::uint32_t>(current.size() * sizeof(GpuMeshVertex));
        const auto builtinBytes = static_cast<std::uint32_t>(current.size() * sizeof(GpuRenderVertex));
        const auto totalBytes = static_cast<std::uint32_t>(totalBytes64);
        const std::array sizes{assetBytes, builtinBytes, assetBytes, builtinBytes};
        if (output.Empty())
        {
            const auto releaseBuffer = [this](SDL_GPUBuffer* buffer) { SDL_ReleaseGPUBuffer(Device, buffer); };
            const auto releaseTransfer = [this](SDL_GPUTransferBuffer* transfer)
            { SDL_ReleaseGPUTransferBuffer(Device, transfer); };
            using Buffer = std::unique_ptr<SDL_GPUBuffer, decltype(releaseBuffer)>;
            std::array buffers{Buffer(nullptr, releaseBuffer), Buffer(nullptr, releaseBuffer),
                               Buffer(nullptr, releaseBuffer), Buffer(nullptr, releaseBuffer)};
            for (std::size_t index = 0; index < buffers.size(); ++index)
            {
                SDL_GPUBufferCreateInfo info{};
                info.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
                if (index % 2 == 0)
                    info.usage |= SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ;
                info.size = sizes[index];
                buffers[index].reset(SDL_CreateGPUBuffer(Device, &info));
                if (!buffers[index])
                    throw std::runtime_error("SDL_CreateGPUBuffer(CPU skin) failed: " + LastSdlError());
            }
            SDL_GPUTransferBufferCreateInfo info{};
            info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
            info.size = totalBytes;
            std::unique_ptr<SDL_GPUTransferBuffer, decltype(releaseTransfer)> transfer(
                SDL_CreateGPUTransferBuffer(Device, &info), releaseTransfer);
            if (!transfer)
                throw std::runtime_error("SDL_CreateGPUTransferBuffer(CPU skin) failed: " + LastSdlError());
            // Publish only a complete slot; allocation failures leave the cache empty and retryable.
            output.AssetVertices = buffers[0].release();
            output.BuiltinVertices = buffers[1].release();
            output.PreviousAssetVertices = buffers[2].release();
            output.PreviousBuiltinVertices = buffers[3].release();
            output.CpuTransfer = transfer.release();
            output.CpuTransferBytes = totalBytes;
            ++SkinningOutputBuilds;
        }
        if (!output.CpuTransfer || output.CpuTransferBytes != totalBytes || !output.AssetVertices ||
            !output.BuiltinVertices || !output.PreviousAssetVertices || !output.PreviousBuiltinVertices)
            throw std::logic_error("CPU skin output resources do not match the active mesh.");

        // Cycle both staging and destination storage so pending GPU work keeps its original contents.
        auto* mapped = static_cast<std::byte*>(SDL_MapGPUTransferBuffer(Device, output.CpuTransfer, true));
        if (!mapped)
            throw std::runtime_error("SDL_MapGPUTransferBuffer(CPU skin) failed: " + LastSdlError());
        const auto writePose = [&](const std::span<const MeshVertex> vertices, const std::size_t offset)
        {
            for (std::size_t index = 0; index < vertices.size(); ++index)
            {
                const auto& vertex = vertices[index];
                const GpuMeshVertex asset{{vertex.Position.X, vertex.Position.Y, vertex.Position.Z, 1.0F},
                                          {vertex.Normal.X, vertex.Normal.Y, vertex.Normal.Z, 0.0F},
                                          {vertex.UV0.X, vertex.UV0.Y, 0.0F, 0.0F},
                                          {vertex.VertexColor.Red, vertex.VertexColor.Green, vertex.VertexColor.Blue,
                                           vertex.VertexColor.Alpha},
                                          vertex.Tangent,
                                          {vertex.UV1.X, vertex.UV1.Y, 0.0F, 0.0F}};
                const GpuRenderVertex builtin{
                    {vertex.Position.X, vertex.Position.Y, vertex.Position.Z, 1.0F},
                    {vertex.VertexColor.Red, vertex.VertexColor.Green, vertex.VertexColor.Blue, 1.0F},
                    {vertex.Normal.X, vertex.Normal.Y, vertex.Normal.Z, 0.0F}};
                std::memcpy(mapped + offset + index * sizeof(asset), &asset, sizeof(asset));
                std::memcpy(mapped + offset + assetBytes + index * sizeof(builtin), &builtin, sizeof(builtin));
            }
        };
        writePose(current, 0);
        writePose(previous, static_cast<std::size_t>(assetBytes) + builtinBytes);
        SDL_UnmapGPUTransferBuffer(Device, output.CpuTransfer);

        auto* copy = SDL_BeginGPUCopyPass(commands);
        if (!copy)
            throw std::runtime_error("SDL_BeginGPUCopyPass(CPU skin) failed: " + LastSdlError());
        const std::array buffers{output.AssetVertices, output.BuiltinVertices, output.PreviousAssetVertices,
                                 output.PreviousBuiltinVertices};
        std::uint32_t offset = 0;
        for (std::size_t index = 0; index < buffers.size(); ++index)
        {
            const SDL_GPUTransferBufferLocation source{output.CpuTransfer, offset};
            const SDL_GPUBufferRegion destination{buffers[index], 0, sizes[index]};
            SDL_UploadToGPUBuffer(copy, &source, &destination, true);
            offset += sizes[index];
        }
        SDL_EndGPUCopyPass(copy);
    }
} // namespace Keire::RenderBackend
