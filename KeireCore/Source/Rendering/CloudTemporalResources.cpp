#include "KeireInternal/Rendering/CloudTemporalResources.h"

#include <SDL3/SDL_gpu.h>

#include <algorithm>
#include <stdexcept>
#include <string>
#include <utility>

namespace Keire::RenderBackend
{
    namespace
    {
        void ReleasePair(SDL_GPUDevice* device, CloudTemporalTextures& textures) noexcept
        {
            if (device)
            {
                if (textures.RadianceOpacity)
                {
                    SDL_ReleaseGPUTexture(device, textures.RadianceOpacity);
                }
                if (textures.CloudAndSceneDepth)
                {
                    SDL_ReleaseGPUTexture(device, textures.CloudAndSceneDepth);
                }
            }
            textures = {};
        }

        void CreatePair(SDL_GPUDevice* device, CloudTemporalTextures& textures, const std::uint32_t width,
                        const std::uint32_t height)
        {
            SDL_GPUTextureCreateInfo description{};
            description.type = SDL_GPU_TEXTURETYPE_2D;
            description.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
            description.width = width;
            description.height = height;
            description.layer_count_or_depth = 1;
            description.num_levels = 1;
            description.sample_count = SDL_GPU_SAMPLECOUNT_1;
            description.format = SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
            textures.RadianceOpacity = SDL_CreateGPUTexture(device, &description);
            if (!textures.RadianceOpacity)
            {
                throw std::runtime_error(std::string("Cloud history radiance allocation failed: ") + SDL_GetError());
            }
            // Keep metre-valued cloud/scene distances in float32; float16 overflows beyond 65 km.
            description.format = SDL_GPU_TEXTUREFORMAT_R32G32_FLOAT;
            textures.CloudAndSceneDepth = SDL_CreateGPUTexture(device, &description);
            if (!textures.CloudAndSceneDepth)
            {
                throw std::runtime_error(std::string("Cloud history depth allocation failed: ") + SDL_GetError());
            }
        }
    } // namespace

    void EnsureCloudTemporalResources(SDL_GPUDevice* device, CloudTemporalResources& resources,
                                      const std::uint32_t renderWidth, const std::uint32_t renderHeight,
                                      const std::uint32_t frameSlotCount)
    {
        if (!device || renderWidth == 0 || renderHeight == 0 || frameSlotCount < 1 || frameSlotCount > 3)
        {
            throw std::invalid_argument("Cloud history requires a device, nonzero extent and 1..3 frame slots.");
        }
        const auto width = renderWidth / 2U + renderWidth % 2U;
        const auto height = renderHeight / 2U + renderHeight % 2U;
        const bool writersComplete =
            std::all_of(resources.Writers.begin(), resources.Writers.end(), [](const CloudTemporalWriter& writer)
                        { return writer.Current.Complete() && writer.History.Complete(); });
        if (resources.Width == width && resources.Height == height && resources.Writers.size() == frameSlotCount &&
            resources.Published.Complete() && writersComplete)
        {
            return;
        }

        CloudTemporalResources candidate;
        try
        {
            candidate.Width = width;
            candidate.Height = height;
            candidate.Writers.resize(frameSlotCount);
            CreatePair(device, candidate.Published, width, height);
            for (auto& writer : candidate.Writers)
            {
                CreatePair(device, writer.Current, width, height);
                CreatePair(device, writer.History, width, height);
            }
        }
        catch (...)
        {
            ReleaseCloudTemporalResources(device, candidate);
            throw;
        }
        ReleaseCloudTemporalResources(device, resources);
        resources = std::move(candidate);
    }

    void ReleaseCloudTemporalResources(SDL_GPUDevice* device, CloudTemporalResources& resources) noexcept
    {
        ReleasePair(device, resources.Published);
        for (auto& writer : resources.Writers)
        {
            ReleasePair(device, writer.Current);
            ReleasePair(device, writer.History);
        }
        resources = {};
    }

    void DiscardCloudTemporalRecording(CloudTemporalResources& resources, const std::uint32_t frameSlot) noexcept
    {
        if (frameSlot < resources.Writers.size())
        {
            resources.Writers[frameSlot].RecordedView.reset();
        }
    }

    void PublishCloudTemporalHistory(CloudTemporalResources& resources, const std::uint32_t frameSlot) noexcept
    {
        if (frameSlot >= resources.Writers.size())
        {
            resources.PublishedView.reset();
            return;
        }
        auto& writer = resources.Writers[frameSlot];
        if (!writer.RecordedView || !writer.History.Complete())
        {
            resources.PublishedView.reset();
            return;
        }
        std::swap(resources.Published, writer.History);
        resources.PublishedView = writer.RecordedView;
        writer.RecordedView.reset();
    }
} // namespace Keire::RenderBackend
