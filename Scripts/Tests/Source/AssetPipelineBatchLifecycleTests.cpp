// Standalone only: SDL doubles must not enter the regular engine test runner.
#include "KeireInternal/Rendering/GpuAssetPipelineBatchInternal.h"

#include <doctest/doctest.h>
#include <stdexcept>

struct SDL_GPUDevice
{
    int ShaderReleases = 0;
    int PipelineReleases = 0;
};

struct SDL_GPUShader
{
};

struct SDL_GPUGraphicsPipeline
{
};

extern "C" void SDL_ReleaseGPUShader(SDL_GPUDevice* device, SDL_GPUShader*) { ++device->ShaderReleases; }

extern "C" void SDL_ReleaseGPUGraphicsPipeline(SDL_GPUDevice* device, SDL_GPUGraphicsPipeline*)
{
    ++device->PipelineReleases;
}

TEST_CASE("Partial material compilation releases each acquired handle once")
{
    SDL_GPUDevice device;
    SDL_GPUShader vertex;
    SDL_GPUShader secondVertex;
    SDL_GPUShader fragment;
    SDL_GPUGraphicsPipeline pipeline;
    {
        Keire::RenderBackend::GpuAssetPipelineBatch batch(&device, 2);
        batch.At(0) = {&vertex, &fragment, &pipeline};
        batch.At(1).Vertex = &secondVertex;
        batch.Reset();
        CHECK(device.ShaderReleases == 3);
        CHECK(device.PipelineReleases == 1);
        batch.Reset();
    }
    CHECK(device.ShaderReleases == 3);
    CHECK(device.PipelineReleases == 1);
}

TEST_CASE("Published pipeline survives batch cleanup while stages are released")
{
    SDL_GPUDevice device;
    SDL_GPUShader vertex;
    SDL_GPUShader fragment;
    SDL_GPUGraphicsPipeline pipeline;
    {
        Keire::RenderBackend::GpuAssetPipelineBatch batch(&device, 1);
        batch.At(0) = {&vertex, &fragment, &pipeline};
        CHECK(batch.ReleasePipeline(0) == &pipeline);
        CHECK(batch.ReleasePipeline(0) == nullptr);
    }
    CHECK(device.ShaderReleases == 2);
    CHECK(device.PipelineReleases == 0);
}

TEST_CASE("Lost device batch abandons every handle without calling the device")
{
    SDL_GPUDevice device;
    SDL_GPUShader vertex;
    SDL_GPUShader fragment;
    SDL_GPUGraphicsPipeline pipeline;
    {
        Keire::RenderBackend::GpuAssetPipelineBatch batch(&device, 1);
        batch.At(0) = {&vertex, &fragment, &pipeline};
        batch.Abandon();
        batch.Reset();
    }
    CHECK(device.ShaderReleases == 0);
    CHECK(device.PipelineReleases == 0);
}

TEST_CASE("Cancellation between passes preserves partial ownership and prevents publication")
{
    SDL_GPUDevice device;
    SDL_GPUShader vertex;
    SDL_GPUGraphicsPipeline pipeline;
    std::size_t calls = 0;
    {
        Keire::RenderBackend::GpuAssetPipelineBatch batch(&device, 3);
        batch.Prepare(
            [&](std::size_t, auto& slot)
            {
                ++calls;
                slot.Vertex = &vertex;
                slot.Pipeline = &pipeline;
            },
            [&] { return calls == 1; });
        CHECK(calls == 1);
        CHECK_FALSE(batch.IsComplete());
        CHECK(device.ShaderReleases == 0);
    }
    CHECK(device.ShaderReleases == 1);
    CHECK(device.PipelineReleases == 1);
}

TEST_CASE("Later pass failure retains earlier and partial handles for classified cleanup")
{
    SDL_GPUDevice device;
    SDL_GPUShader vertices[2];
    SDL_GPUGraphicsPipeline pipeline;
    {
        Keire::RenderBackend::GpuAssetPipelineBatch batch(&device, 2);
        const auto prepare = [&]
        {
            batch.Prepare(
                [&](std::size_t index, auto& slot)
                {
                    slot.Vertex = &vertices[index];
                    if (index == 1)
                    {
                        throw std::runtime_error("injected pipeline failure");
                    }
                    slot.Pipeline = &pipeline;
                },
                [] { return false; });
        };
        CHECK_THROWS_WITH_AS(prepare(), "injected pipeline failure", std::runtime_error);
        CHECK_FALSE(batch.IsComplete());
        CHECK(device.ShaderReleases == 0);
        CHECK(device.PipelineReleases == 0);
    }
    CHECK(device.ShaderReleases == 2);
    CHECK(device.PipelineReleases == 1);
}

TEST_CASE("Only a fully prepared nonempty batch is publishable")
{
    SDL_GPUDevice device;
    SDL_GPUGraphicsPipeline pipelines[2];
    Keire::RenderBackend::GpuAssetPipelineBatch empty(&device, 0);
    CHECK_FALSE(empty.IsComplete());
    Keire::RenderBackend::GpuAssetPipelineBatch batch(&device, 2);
    CHECK_FALSE(batch.IsComplete());
    batch.Prepare([&](std::size_t index, auto& slot) { slot.Pipeline = &pipelines[index]; }, [] { return false; });
    CHECK(batch.IsComplete());
    CHECK(batch.ReleasePipeline(0) == &pipelines[0]);
    CHECK_FALSE(batch.IsComplete());
    batch.Abandon();
}
