#include "KeireInternal/Rendering/CloudLightCacheResources.h"

#include "Keire/BuiltinUnlitShaders.h"
#include "KeireInternal/Rendering/CloudLightCacheDemand.h"
#include "KeireInternal/Rendering/CloudLightCacheGrid.h"

#include <algorithm>
#include <cmath>
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
                for (auto& validity : candidate.Validity)
                {
                    validity = std::make_shared<CloudLightCacheValidity>();
                }
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
                buffer.size =
                    CloudLightCacheLevelCapacity * static_cast<std::uint32_t>(CloudLightCacheLevelCount) * 16U;
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
        for (const auto& validity : resources.Validity)
        {
            if (validity)
            {
                validity->Submit();
            }
        }
    }

    void InvalidateCloudLightCache(CloudLightCacheResources& resources) noexcept
    {
        for (const auto& validity : resources.Validity)
        {
            if (validity)
            {
                validity->Invalidate();
            }
        }
    }

    static CloudLightCacheLevelUniforms
    RecordCloudLightCacheLevel(SDL_GPUCommandBuffer* commands, CloudLightCacheResources& resources,
                               const CloudVolumeViewUniforms& view, const GpuTextureResources& noise,
                               const std::optional<float> cloudBasePatternPhase,
                               const Detail::ResolvedPlanetFrame* planetFrame, const std::uint32_t level)
    {
        auto& validity = *resources.Validity[level];
        const float scale = static_cast<float>(1U << (level * 2U));
        const std::uint32_t bufferOffset = level * CloudLightCacheLevelCapacity;
        // Coarse Cartesian cells can miss a thin spherical shell. Preserve the existing
        // planetary cache and direct fallback until distant levels retain radial resolution.
        if (level != 0U && view.PlanetaryCloud.Controls[0] != 0U)
        {
            return {};
        }
        if (!CloudLightCacheHasDemand(view))
        {
            return {};
        }
        auto grid =
            MakeCloudLightCacheGrid(view.Volume, view.WeatherControls.X > 0.5F ? view.WeatherControls.W : 0.0F, scale);
        if (!grid)
        {
            return {};
        }

        CloudLightCacheLevelUniforms lookup{
            {grid->Origin[0], grid->Origin[1], grid->Origin[2], 0.0F},
            {grid->CellSize[0], grid->CellSize[1], grid->CellSize[2], 0.0F},
            {grid->Dimensions[0], grid->Dimensions[1], grid->Dimensions[2], bufferOffset}};
        auto buildView = view;
        auto buildGrid = *grid;
        if (view.PlanetaryCloud.Controls[0] != 0U && planetFrame)
        {
            // Keep the existing allocation budget: 81^3 cells are fewer than the flat grid's 129*33*129.
            // A planet-fixed snapped neighborhood is reusable across camera rebases and never flattens the shell.
            auto buildFrame = *planetFrame;
            buildFrame.PlanetToObserver = {};
            const auto& observer = planetFrame->ObserverFromCenterPlanetMetres;
            const double observerRadius = std::hypot(observer[0], observer[1], observer[2]);
            const double shellRadius =
                std::max(planetFrame->RadiusMetres,
                         planetFrame->RadiusMetres + view.Volume.Geometry[0] + view.Volume.Geometry[1] * 0.5);
            const double cellSize =
                std::min(shellRadius / 8.0, scale * std::max(static_cast<double>(view.Volume.Geometry[2]) / 16.0,
                                                             static_cast<double>(view.Volume.Geometry[1]) / 40.0));
            buildGrid.Dimensions = {81U, 81U, 81U};
            for (std::size_t axis = 0; axis < 3; ++axis)
            {
                const double anchor = std::floor(observer[axis] / observerRadius * shellRadius / cellSize) * cellSize;
                buildFrame.ObserverFromCenterPlanetMetres[axis] = anchor;
                const double displacement = anchor - observer[axis];
                buildGrid.CellSize[axis] = static_cast<float>(cellSize);
                buildGrid.Origin[axis] = static_cast<float>(-40.0 * cellSize);
                grid->Origin[axis] = static_cast<float>(displacement - 40.0 * cellSize);
                double phase = std::fmod(static_cast<double>(view.Volume.PatternOrigin[axis]) +
                                             displacement / view.Volume.Geometry[2],
                                         256.0);
                if (phase < 0.0)
                {
                    phase += 256.0;
                }
                buildView.Volume.PatternOrigin[axis] = static_cast<float>(phase);
            }
            grid->CellSize = buildGrid.CellSize;
            grid->Dimensions = buildGrid.Dimensions;
            buildView.PlanetaryCloud = MakePlanetaryCloudUniforms(buildFrame, view.Volume.Geometry[0]);
            // Reframing changes geometry, not the authored density profile used by the view.
            buildView.PlanetaryCloud.RadiusGeometry.Z = view.PlanetaryCloud.RadiusGeometry.Z;
            const auto sunlight = planetFrame->PlanetToObserver.Inverse().Apply(
                {view.SunDirection.X, view.SunDirection.Y, view.SunDirection.Z});
            buildView.SunDirection = {static_cast<float>(sunlight[0]), static_cast<float>(sunlight[1]),
                                      static_cast<float>(sunlight[2]), view.SunDirection.W};
            lookup = {{grid->Origin[0], grid->Origin[1], grid->Origin[2], 1.0F},
                      {grid->CellSize[0], grid->CellSize[1], grid->CellSize[2], 0.0F},
                      {81U, 81U, 81U, bufferOffset}};
        }
        else
        {
            AlignCloudLightCacheBuild(buildView.Volume, buildGrid, cloudBasePatternPhase);
        }
        auto key = MakeCloudLightCacheKey(buildView, buildGrid.Origin, buildGrid.CellSize, buildGrid.Dimensions);
        if (planetFrame)
        {
            key.Planet = planetFrame->Planet;
            key.DiscontinuityRevision = planetFrame->DiscontinuityRevision;
        }
        if (validity.SubmittedKey == key)
        {
            return lookup;
        }
        validity.Invalidate();
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
        validity.RecordedKey = key;
        return lookup;
    }

    CloudLightCacheLookupUniforms RecordCloudLightCache(SDL_GPUDevice* device, SDL_GPUCommandBuffer* commands,
                                                        CloudLightCacheResources& resources,
                                                        const CloudVolumeViewUniforms& view,
                                                        const GpuTextureResources& noise,
                                                        const std::optional<float> cloudBasePatternPhase,
                                                        const Detail::ResolvedPlanetFrame* planetFrame)
    {
        if (!device || !commands || !noise.Texture || !noise.Sampler)
        {
            throw std::invalid_argument("Cloud light cache requires a device, command buffer and noise texture.");
        }
        EnsureResources(device, resources);
        CloudLightCacheLookupUniforms lookup;
        for (std::uint32_t level = 0; level < CloudLightCacheLevelCount; ++level)
        {
            // Each level publishes independently; an unsubmitted write cannot be reused.
            if (resources.Validity[level]->RecordedKey)
            {
                resources.Validity[level]->Invalidate();
            }
            lookup.Levels[level] =
                RecordCloudLightCacheLevel(commands, resources, view, noise, cloudBasePatternPhase, planetFrame, level);
        }
        return lookup;
    }
} // namespace Keire::RenderBackend
