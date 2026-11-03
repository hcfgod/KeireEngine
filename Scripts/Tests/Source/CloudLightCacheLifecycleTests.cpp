// Standalone test executable: these SDL doubles must never be linked into the engine or normal test runner.
#include "KeireInternal/Rendering/CloudLightCacheResources.h"

#include <doctest/doctest.h>

#include <cstring>
#include <stdexcept>

TEST_CASE("Cloud cache uniforms preserve resolved planetary inputs within the Vulkan slot limit")
{
    using namespace Keire::RenderBackend;
    CloudVolumeViewUniforms view{};
    view.Volume.Density[0] = 0.7F;
    view.PlanetaryCloud.Controls[0] = 1U;
    view.PlanetaryCloud.RadiusGeometry.X = 6371000.0F;
    const CloudLightCacheUniforms uniforms{view, {1, 2, 3, 0}, {4, 5, 6, 0}, {129, 33, 129, 42}};
    CHECK(sizeof(uniforms) <= 4096U);
    CHECK(std::memcmp(uniforms.View.data(), &view, uniforms.View.size()) == 0);
    CHECK(uniforms.GridDimensions[3] == 42U);

    view.PlanetWeather.Controls[0] = 1U;
    const CloudLightCacheUniforms otherPlanets{view, {1, 2, 3, 0}, {4, 5, 6, 0}, {129, 33, 129, 42}};
    CHECK(uniforms.View == otherPlanets.View);
}

struct SDL_GPUDevice
{
};
struct SDL_GPUCommandBuffer
{
};
struct SDL_GPUComputePipeline
{
};
struct SDL_GPUBuffer
{
};
struct SDL_GPUComputePass
{
};
struct SDL_GPUTexture
{
};
struct SDL_GPUSampler
{
};

namespace
{
    struct State
    {
        bool FailPipeline = false;
        bool FailBuffer = false;
        bool FailPass = false;
        int FailLevel = -1;
        int Pipelines = 0;
        int Buffers = 0;
        int Dispatches = 0;
        std::uint32_t CurrentLevel = 0;
        std::array<int, Keire::RenderBackend::CloudLightCacheLevelCount> LevelDispatches{};
    } state;

    SDL_GPUComputePass computePass;

    struct Fixture
    {
        SDL_GPUDevice Device;
        SDL_GPUCommandBuffer Commands;
        SDL_GPUTexture Texture;
        SDL_GPUSampler Sampler;
        Keire::RenderBackend::CloudLightCacheResources Resources;
        Keire::RenderBackend::CloudVolumeViewUniforms View;
        Keire::RenderBackend::GpuTextureResources Noise;

        Fixture()
        {
            state = {};
            View.Volume.Geometry = {1500.0F, 1200.0F, 3000.0F, 100000.0F};
            View.Volume.Quality[0] = 1;
            View.Volume.Density = {0.6F, 0.004F, 0.2F, 0.65F};
            View.SunRadiance = {1, 1, 1, 0};
            Noise.Texture = &Texture;
            Noise.Sampler = &Sampler;
        }

        ~Fixture()
        {
            Keire::RenderBackend::ReleaseCloudLightCache(&Device, Resources);
            CHECK(state.Pipelines == 0);
            CHECK(state.Buffers == 0);
        }

        auto Record()
        {
            return Keire::RenderBackend::RecordCloudLightCache(&Device, &Commands, Resources, View, Noise).Levels[0];
        }
    };
} // namespace

extern "C"
{
    SDL_GPUShaderFormat SDLCALL SDL_GetGPUShaderFormats(SDL_GPUDevice*) { return SDL_GPU_SHADERFORMAT_DXIL; }

    const char* SDLCALL SDL_GetError() { return "injected cache failure"; }

    SDL_GPUComputePipeline* SDLCALL SDL_CreateGPUComputePipeline(SDL_GPUDevice*,
                                                                 const SDL_GPUComputePipelineCreateInfo* info)
    {
        CHECK(info->num_samplers == 1);
        CHECK(info->num_readwrite_storage_buffers == 1);
        if (state.FailPipeline)
        {
            return nullptr;
        }
        ++state.Pipelines;
        return new SDL_GPUComputePipeline;
    }

    SDL_GPUBuffer* SDLCALL SDL_CreateGPUBuffer(SDL_GPUDevice*, const SDL_GPUBufferCreateInfo* info)
    {
        CHECK(info->size == 8786448U * Keire::RenderBackend::CloudLightCacheLevelCount);
        if (state.FailBuffer)
        {
            return nullptr;
        }
        ++state.Buffers;
        return new SDL_GPUBuffer;
    }

    void SDLCALL SDL_ReleaseGPUComputePipeline(SDL_GPUDevice*, SDL_GPUComputePipeline* value)
    {
        --state.Pipelines;
        delete value;
    }

    void SDLCALL SDL_ReleaseGPUBuffer(SDL_GPUDevice*, SDL_GPUBuffer* value)
    {
        --state.Buffers;
        delete value;
    }

    void SDLCALL SDL_PushGPUComputeUniformData(SDL_GPUCommandBuffer*, Uint32 slot, const void* data, Uint32 size)
    {
        CHECK(slot == 0U);
        CHECK(size == sizeof(Keire::RenderBackend::CloudLightCacheUniforms));
        const auto& uniforms = *static_cast<const Keire::RenderBackend::CloudLightCacheUniforms*>(data);
        state.CurrentLevel = uniforms.GridDimensions[3] / Keire::RenderBackend::CloudLightCacheLevelCapacity;
        REQUIRE(state.CurrentLevel < Keire::RenderBackend::CloudLightCacheLevelCount);
        CHECK(uniforms.GridDimensions[3] % Keire::RenderBackend::CloudLightCacheLevelCapacity == 0);
    }

    SDL_GPUComputePass* SDLCALL SDL_BeginGPUComputePass(SDL_GPUCommandBuffer*,
                                                        const SDL_GPUStorageTextureReadWriteBinding*, Uint32,
                                                        const SDL_GPUStorageBufferReadWriteBinding* buffers,
                                                        Uint32 count)
    {
        CHECK(count == 1U);
        CHECK(buffers[0].buffer != nullptr);
        return state.FailPass || state.FailLevel == static_cast<int>(state.CurrentLevel) ? nullptr : &computePass;
    }

    void SDLCALL SDL_BindGPUComputePipeline(SDL_GPUComputePass*, SDL_GPUComputePipeline*) {}

    void SDLCALL SDL_BindGPUComputeSamplers(SDL_GPUComputePass*, Uint32, const SDL_GPUTextureSamplerBinding*, Uint32) {}

    void SDLCALL SDL_DispatchGPUCompute(SDL_GPUComputePass*, Uint32 x, Uint32 y, Uint32 z)
    {
        CHECK(x == 17U);
        CHECK(y == 5U);
        CHECK(z == 129U);
        ++state.LevelDispatches[state.CurrentLevel];
        if (state.CurrentLevel == 0)
        {
            ++state.Dispatches;
        }
    }

    void SDLCALL SDL_EndGPUComputePass(SDL_GPUComputePass*) {}
}

TEST_CASE("Cache allocation failure publishes no partial resources and permits retry")
{
    Fixture fixture;
    SUBCASE("Pipeline creation fails") { state.FailPipeline = true; }
    SUBCASE("Buffer creation fails after pipeline creation") { state.FailBuffer = true; }
    CHECK_THROWS_AS(fixture.Record(), std::runtime_error);
    CHECK(fixture.Resources.Empty());
    CHECK(state.Pipelines == 0);
    CHECK(state.Buffers == 0);
    state.FailPipeline = false;
    state.FailBuffer = false;
    CHECK(fixture.Record().GridDimensions[0] == 129U);
}

TEST_CASE("Cache levels publish independently and distant grids survive near-cell movement")
{
    using namespace Keire::RenderBackend;
    Fixture fixture;
    const auto lookup =
        RecordCloudLightCache(&fixture.Device, &fixture.Commands, fixture.Resources, fixture.View, fixture.Noise);
    for (std::size_t level = 0; level < CloudLightCacheLevelCount; ++level)
    {
        CHECK(state.LevelDispatches[level] == 1);
        CHECK(lookup.Levels[level].GridDimensions[3] == level * CloudLightCacheLevelCapacity);
        REQUIRE(fixture.Resources.Validity[level]->RecordedKey);
        CHECK_FALSE(fixture.Resources.Validity[level]->SubmittedKey);
    }
    SubmitCloudLightCache(fixture.Resources);
    fixture.View.Volume.PatternOrigin[0] = 0.03125F;
    fixture.Record();
    CHECK(state.LevelDispatches[0] == 2);
    CHECK(state.LevelDispatches[1] == 1);
    CHECK(state.LevelDispatches[2] == 1);
    SubmitCloudLightCache(fixture.Resources);
    fixture.View.Volume.PatternOrigin[0] = 0.125F;
    fixture.Record();
    CHECK(state.LevelDispatches[0] == 3);
    CHECK(state.LevelDispatches[1] == 2);
    CHECK(state.LevelDispatches[2] == 1);
    InvalidateCloudLightCache(fixture.Resources);
    SubmitCloudLightCache(fixture.Resources);
    for (const auto& validity : fixture.Resources.Validity)
    {
        CHECK_FALSE(validity->RecordedKey);
        CHECK_FALSE(validity->SubmittedKey);
    }
}

TEST_CASE("An interrupted multi-level cache recording retries every abandoned write")
{
    using namespace Keire::RenderBackend;
    Fixture fixture;
    fixture.Record();
    SubmitCloudLightCache(fixture.Resources);
    fixture.View.Volume.Density[0] += 0.1F;
    state.FailLevel = 1;
    CHECK_THROWS_AS(fixture.Record(), std::runtime_error);
    REQUIRE(fixture.Resources.Validity[0]->RecordedKey);
    CHECK_FALSE(fixture.Resources.Validity[0]->SubmittedKey);
    CHECK_FALSE(fixture.Resources.Validity[1]->SubmittedKey);
    state.FailLevel = -1;
    fixture.Record();
    CHECK(state.LevelDispatches[0] == 3);
    CHECK(state.LevelDispatches[1] == 2);
    CHECK(state.LevelDispatches[2] == 2);
    SubmitCloudLightCache(fixture.Resources);
    fixture.Record();
    CHECK(state.LevelDispatches[0] == 3);
    CHECK(state.LevelDispatches[1] == 2);
    CHECK(state.LevelDispatches[2] == 2);
}

TEST_CASE("Thin planetary shells do not use undersampled Cartesian distant levels")
{
    using namespace Keire::RenderBackend;
    Fixture fixture;
    fixture.View.PlanetaryCloud.Controls[0] = 1;
    const auto lookup =
        RecordCloudLightCache(&fixture.Device, &fixture.Commands, fixture.Resources, fixture.View, fixture.Noise);
    CHECK(lookup.Levels[0].GridDimensions[0] != 0U);
    for (std::size_t level = 1; level < CloudLightCacheLevelCount; ++level)
    {
        CHECK(lookup.Levels[level].GridDimensions[0] == 0U);
        CHECK(state.LevelDispatches[level] == 0);
        CHECK_FALSE(fixture.Resources.Validity[level]->RecordedKey);
    }
}

TEST_CASE("Cache dispatch rebuilds every use and resource release is idempotent")
{
    Fixture fixture;
    fixture.Record();
    fixture.Record();
    CHECK(state.Pipelines == 1);
    CHECK(state.Buffers == 1);
    CHECK(state.Dispatches == 2);
    Keire::RenderBackend::ReleaseCloudLightCache(&fixture.Device, fixture.Resources);
    Keire::RenderBackend::ReleaseCloudLightCache(&fixture.Device, fixture.Resources);
    CHECK(fixture.Resources.Empty());
    fixture.Record();
    CHECK(state.Dispatches == 3);
}

TEST_CASE("Cache recording failure retains releasable resources and retries the dispatch")
{
    Fixture fixture;
    state.FailPass = true;
    CHECK_THROWS_AS(fixture.Record(), std::runtime_error);
    CHECK(state.Dispatches == 0);
    state.FailPass = false;
    fixture.Record();
    CHECK(state.Dispatches == 1);
}

TEST_CASE("Disabled clouds and invalid recording inputs do not dispatch")
{
    Fixture fixture;
    fixture.Noise.Texture = nullptr;
    CHECK_THROWS_AS(fixture.Record(), std::invalid_argument);
    CHECK(fixture.Resources.Empty());
    fixture.Noise.Texture = &fixture.Texture;
    fixture.View.Volume.Quality[0] = 0;
    CHECK(fixture.Record().GridDimensions[0] == 0U);
    CHECK(state.Dispatches == 0);
}

TEST_CASE("Cloud cache reuse requires submission and exact light-field inputs")
{
    Fixture fixture;
    fixture.Record();
    CHECK_FALSE(fixture.Resources.Validity[0]->SubmittedKey);
    Keire::RenderBackend::SubmitCloudLightCache(fixture.Resources);
    REQUIRE(fixture.Resources.Validity[0]->SubmittedKey);
    fixture.Record();
    CHECK(state.Dispatches == 1);
    fixture.View.AmbientRadiance.X = 3.0F;
    fixture.View.Volume.Albedo[0] = 0.5F;
    fixture.Record();
    CHECK(state.Dispatches == 1);

    fixture.View.Volume.PatternOrigin[0] += 0.125F;
    fixture.Record();
    CHECK(state.Dispatches == 2);
    CHECK_FALSE(fixture.Resources.Validity[0]->SubmittedKey);
    Keire::RenderBackend::SubmitCloudLightCache(fixture.Resources);
    fixture.View.SunDirection.X += 0.1F;
    fixture.Record();
    CHECK(state.Dispatches == 3);
}

TEST_CASE("Horizontal movement reuses submitted cloud cells and updates lookup coordinates")
{
    Fixture fixture;
    const auto initial = fixture.Record();
    Keire::RenderBackend::SubmitCloudLightCache(fixture.Resources);
    fixture.View.Volume.PatternOrigin[0] = 0.015625F;
    fixture.View.Volume.PatternOrigin[2] = 0.0078125F;
    const auto moved = fixture.Record();
    CHECK(state.Dispatches == 1);
    CHECK(moved.GridOrigin.X == initial.GridOrigin.X - 46.875F);
    CHECK(moved.GridOrigin.Z == initial.GridOrigin.Z - 23.4375F);

    fixture.View.Volume.PatternOrigin[0] = 0.03125F;
    fixture.Record();
    CHECK(state.Dispatches == 2);
    Keire::RenderBackend::SubmitCloudLightCache(fixture.Resources);
    fixture.View.Volume.Geometry[0] -= 1.0F;
    fixture.Record();
    CHECK(state.Dispatches == 3);
}

TEST_CASE("Vertical observer movement preserves the cloud layer light field")
{
    Fixture fixture;
    const auto original = fixture.Record();
    Keire::RenderBackend::SubmitCloudLightCache(fixture.Resources);
    fixture.View.Volume.Geometry[0] -= 1.0F;
    fixture.View.Volume.PatternOrigin[1] += 1.0F / fixture.View.Volume.Geometry[2];
    const auto moved = fixture.Record();
    CHECK(state.Dispatches == 1);
    CHECK(moved.GridOrigin.Y == original.GridOrigin.Y - 1.0F);
    fixture.View.Volume.PatternOrigin[1] += 0.01F;
    fixture.Record();
    CHECK(state.Dispatches == 2);
}

TEST_CASE("Abandoned cloud cache writes cannot resurrect an earlier submitted key")
{
    Fixture fixture;
    fixture.Record();
    Keire::RenderBackend::SubmitCloudLightCache(fixture.Resources);
    const auto original = fixture.View;
    fixture.View.Volume.Density[0] += 0.1F;
    fixture.Record();
    Keire::RenderBackend::InvalidateCloudLightCache(fixture.Resources);
    Keire::RenderBackend::SubmitCloudLightCache(fixture.Resources);
    CHECK_FALSE(fixture.Resources.Validity[0]->SubmittedKey);
    fixture.View = original;
    fixture.Record();
    CHECK(state.Dispatches == 3);
    Keire::RenderBackend::SubmitCloudLightCache(fixture.Resources);
    fixture.View.Volume.Density[0] += 0.1F;
    state.FailPass = true;
    CHECK_THROWS_AS(fixture.Record(), std::runtime_error);
    CHECK_FALSE(fixture.Resources.Validity[0]->SubmittedKey);
    state.FailPass = false;
    fixture.View = original;
    fixture.Record();
    CHECK(state.Dispatches == 4);
}

TEST_CASE("Frame cloud publications survive retirement and abandon every recorded surface")
{
    Fixture fixture;
    Keire::RenderBackend::CloudLightCachePublications publications;
    fixture.Record();
    publications.Track(fixture.Resources.Validity[0]);
    const auto retired = fixture.Resources.Validity[0];
    Keire::RenderBackend::ReleaseCloudLightCache(&fixture.Device, fixture.Resources);
    fixture.Record();
    publications.Track(fixture.Resources.Validity[0]);
    publications.Submit();
    CHECK_FALSE(retired->SubmittedKey);
    REQUIRE(fixture.Resources.Validity[0]->SubmittedKey);
    fixture.Record();
    CHECK(state.Dispatches == 2);

    fixture.View.Volume.Density[0] += 0.1F;
    fixture.Record();
    publications.Track(fixture.Resources.Validity[0]);
    auto nested = std::make_shared<Keire::RenderBackend::CloudLightCacheValidity>();
    nested->RecordedKey = fixture.Resources.Validity[0]->RecordedKey;
    publications.Track(nested);
    publications.Invalidate();
    publications.Submit();
    CHECK_FALSE(nested->SubmittedKey);
    CHECK_FALSE(nested->RecordedKey);
    CHECK_FALSE(fixture.Resources.Validity[0]->SubmittedKey);
    fixture.Record();
    CHECK(state.Dispatches == 4);
}

TEST_CASE("Destroying an unfinished cloud publication transaction invalidates its writes")
{
    Fixture fixture;
    fixture.Record();
    {
        Keire::RenderBackend::CloudLightCachePublications publications;
        publications.Track(fixture.Resources.Validity[0]);
    }
    CHECK_FALSE(fixture.Resources.Validity[0]->RecordedKey);
    CHECK_FALSE(fixture.Resources.Validity[0]->SubmittedKey);
    fixture.Record();
    CHECK(state.Dispatches == 2);
}

TEST_CASE("Optically empty clouds skip cache dispatch and resume safely when density returns")
{
    Fixture fixture;
    (void)fixture.Record();
    Keire::RenderBackend::SubmitCloudLightCache(fixture.Resources);
    const int initialDispatches = state.Dispatches;
    fixture.View.Volume.Density[0] = 0.0F;
    CHECK(fixture.Record().GridDimensions[0] == 0U);
    CHECK(state.Dispatches == initialDispatches);
    fixture.View.Volume.Density[0] = 0.6F;
    fixture.View.Volume.Density[1] = 0.0F;
    CHECK(fixture.Record().GridDimensions[0] == 0U);
    CHECK(state.Dispatches == initialDispatches);
    fixture.View.Volume.Density[1] = 0.004F;
    CHECK(fixture.Record().GridDimensions[0] == 129U);
    CHECK(state.Dispatches == initialDispatches);
    fixture.View.Volume.Density[0] = 0.7F;
    (void)fixture.Record();
    CHECK(state.Dispatches == initialDispatches + 1);
}
