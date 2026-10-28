#include "KeireInternal/Rendering/CloudLightCacheResources.h"

#include "Keire/BuiltinUnlitShaders.h"
#include "KeireInternal/Rendering/CloudLightCacheGrid.h"

#include <stdexcept>
#include <string>

namespace Keire::RenderBackend
{
    namespace
    {
        void EnsureResources(SDL_GPUDevice* device, CloudLightCacheResources& resources)
        {
            if (resources.Transmission && resources.Builder)
            {
                return;
            }

            CloudLightCacheResources candidate;
            try
            {
                candidate.Validity = std::make_shared<CloudLightCacheValidity>();
                SDL_GPUComputePipelineCreateInfo pipeline{};
                pipeline.entrypoint = "CSMain";
                pipeline.num_samplers = 1;
                pipeline.num_readwrite_storage_buffers = 1;
                pipeline.num_uniform_buffers = 1;
                pipeline.threadcount_x = 8;
                pipeline.threadcount_y = 8;
                pipeline.threadcount_z = 1;
                const auto formats = SDL_GetGPUShaderFormats(device);
                if (formats & SDL_GPU_SHADERFORMAT_DXIL)
                {
                    pipeline.format = SDL_GPU_SHADERFORMAT_DXIL;
                    pipeline.code = Detail::BuiltinCloudLightCacheComputeDxil;
                    pipeline.code_size = Detail::BuiltinCloudLightCacheComputeDxilSize;
                }
                else if (formats & SDL_GPU_SHADERFORMAT_MSL)
                {
                    pipeline.format = SDL_GPU_SHADERFORMAT_MSL;
                    pipeline.code = Detail::BuiltinCloudLightCacheComputeMsl;
                    pipeline.code_size = Detail::BuiltinCloudLightCacheComputeMslSize;
                }
                else if (formats & SDL_GPU_SHADERFORMAT_SPIRV)
                {
                    pipeline.format = SDL_GPU_SHADERFORMAT_SPIRV;
                    pipeline.code = Detail::BuiltinCloudLightCacheComputeSpirV;
                    pipeline.code_size = Detail::BuiltinCloudLightCacheComputeSpirVSize;
                }
                else
                {
                    throw std::runtime_error("No supported cloud light cache shader format.");
                }

                candidate.Builder = SDL_CreateGPUComputePipeline(device, &pipeline);
                if (!candidate.Builder)
                {
                    throw std::runtime_error(std::string("Cloud light cache pipeline: ") + SDL_GetError());
                }
                SDL_GPUBufferCreateInfo buffer{};
                buffer.usage = SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE | SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ;
                buffer.size = 129U * 33U * 129U * 16U;
                candidate.Transmission = SDL_CreateGPUBuffer(device, &buffer);
                if (!candidate.Transmission)
                {
                    throw std::runtime_error(std::string("Cloud light cache allocation: ") + SDL_GetError());
                }
            }
            catch (...)
            {
                ReleaseCloudLightCache(device, candidate);
                throw;
            }

            ReleaseCloudLightCache(device, resources);
            resources = candidate;
        }
    } // namespace

    void ReleaseCloudLightCache(SDL_GPUDevice* device, CloudLightCacheResources& resources) noexcept
    {
        InvalidateCloudLightCache(resources);
        if (device)
        {
            if (resources.Transmission)
            {
                SDL_ReleaseGPUBuffer(device, resources.Transmission);
            }
            if (resources.Builder)
            {
                SDL_ReleaseGPUComputePipeline(device, resources.Builder);
            }
        }
        resources = {};
    }

    void SubmitCloudLightCache(CloudLightCacheResources& resources) noexcept
    {
        if (resources.Validity)
        {
            resources.Validity->Submit();
        }
    }

    void InvalidateCloudLightCache(CloudLightCacheResources& resources) noexcept
    {
        if (resources.Validity)
        {
            resources.Validity->Invalidate();
        }
    }

    CloudLightCacheLookupUniforms RecordCloudLightCache(SDL_GPUDevice* device, SDL_GPUCommandBuffer* commands,
                                                        CloudLightCacheResources& resources,
                                                        const CloudVolumeViewUniforms& view,
                                                        const GpuTextureResources& noise,
                                                        const std::optional<float> cloudBasePatternPhase)
    {
        if (!device || !commands || !noise.Texture || !noise.Sampler)
        {
            throw std::invalid_argument("Cloud light cache requires a device, command buffer and noise texture.");
        }
        EnsureResources(device, resources);
        // An uncommitted prior recording is never evidence of valid GPU contents.
        if (resources.Validity->RecordedKey)
        {
            InvalidateCloudLightCache(resources);
        }
        const auto grid =
            MakeCloudLightCacheGrid(view.Volume, view.WeatherControls.X > 0.5F ? view.WeatherControls.W : 0.0F);
        if (view.Volume.Quality[0] == 0U || view.Volume.Density[0] <= 0.0F || view.Volume.Density[1] <= 0.0F || !grid)
        {
            return {};
        }

        const CloudLightCacheLookupUniforms lookup{{grid->Origin[0], grid->Origin[1], grid->Origin[2], 0.0F},
                                                   {grid->CellSize[0], grid->CellSize[1], grid->CellSize[2], 0.0F},
                                                   {grid->Dimensions[0], grid->Dimensions[1], grid->Dimensions[2], 0U}};
        auto buildView = view;
        auto buildGrid = *grid;
        AlignCloudLightCacheBuild(buildView.Volume, buildGrid, cloudBasePatternPhase);
        const auto key = MakeCloudLightCacheKey(buildView, buildGrid.Origin, buildGrid.CellSize, buildGrid.Dimensions);
        if (resources.Validity->SubmittedKey == key)
        {
            return lookup;
        }
        InvalidateCloudLightCache(resources);
        const Vector4 buildOrigin{buildGrid.Origin[0], buildGrid.Origin[1], buildGrid.Origin[2], 0.0F};
        const CloudLightCacheUniforms uniforms{buildView, buildOrigin, lookup.CellSize, lookup.GridDimensions};
        SDL_PushGPUComputeUniformData(commands, 0, &uniforms, sizeof(uniforms));
        const SDL_GPUStorageBufferReadWriteBinding storage{resources.Transmission, false, 0, 0, 0};
        auto* pass = SDL_BeginGPUComputePass(commands, nullptr, 0, &storage, 1);
        if (!pass)
        {
            throw std::runtime_error(std::string("Cloud light cache compute pass: ") + SDL_GetError());
        }
        SDL_BindGPUComputePipeline(pass, resources.Builder);
        const SDL_GPUTextureSamplerBinding binding{noise.Texture, noise.Sampler};
        SDL_BindGPUComputeSamplers(pass, 0, &binding, 1);
        SDL_DispatchGPUCompute(pass, (grid->Dimensions[0] + 7U) / 8U, (grid->Dimensions[1] + 7U) / 8U,
                               grid->Dimensions[2]);
        SDL_EndGPUComputePass(pass);
        resources.Validity->RecordedKey = key;
        return lookup;
    }
} // namespace Keire::RenderBackend
