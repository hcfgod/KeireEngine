#pragma once

#include "Keire/Assets/AssetSystem.h"
#include "Keire/Assets/RenderingAssets.h"
#include "Keire/Jobs/JobSystem.h"
#include "KeireInternal/Rendering/GpuAssetPipelineBatchInternal.h"
#include "KeireInternal/Rendering/MaterialPassRoutingInternal.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace Keire::RenderBackend
{
    struct GpuShaderEntry final
    {
        struct Pipeline final
        {
            SDL_GPUSampleCount Samples = SDL_GPU_SAMPLECOUNT_1;
            MaterialAlphaMode AlphaMode = MaterialAlphaMode::Opaque;
            bool DoubleSided = false;
            RuntimeMaterialPassRole PassRole = RuntimeMaterialPassRole::Primary;
            SDL_GPUGraphicsPipeline* Handle = nullptr;
        };

        AssetHandle<ShaderAsset> Handle;
        Ref<const ShaderAsset> LastGood;
        std::vector<Pipeline> Pipelines;
        std::vector<Pipeline> AttemptedPipelines;
        std::uint64_t LoadedRevision = 0;
        std::uint64_t LastAttemptedRevision = 0;
    };

    struct AssetPipelinePreparation final
    {
        JobHandle Job;
        std::shared_ptr<GpuAssetPipelineBatch> Batch;
        Ref<const ShaderAsset> Shader;
        AssetId Id;
        std::uint64_t Revision = 0;
        std::uint32_t Generation = 0;
        std::vector<GpuShaderEntry::Pipeline> Pipelines;
    };

} // namespace Keire::RenderBackend
