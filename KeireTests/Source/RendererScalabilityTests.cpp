#include "KeireInternal/Rendering/InstanceBatchInternal.h"
#include "KeireInternal/Rendering/MaterialBlendingInternal.h"
#include "KeireInternal/Rendering/RenderBackendInternal.h"

#include <doctest/doctest.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <vector>

TEST_CASE("ten-thousand compatible objects collapse below the scene draw-call budget")
{
    using namespace Keire::RenderBackend;
    InstanceBatchKey key;
    key.Mesh = Keire::AssetId::Parse("00000000-0000-4000-8000-000000000001");
    key.Material = Keire::AssetId::Parse("00000000-0000-4000-8000-000000000002");
    key.SupportsInstancing = true;
    const std::vector<InstanceBatchKey> objects(10'000, key);

    const auto started = std::chrono::steady_clock::now();
    const auto batches = BuildInstanceBatches(objects);
    const auto preparation = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - started);

    REQUIRE(batches.size() < 25);
    REQUIRE(batches.size() == 1);
    CHECK(batches.front().First == 0);
    CHECK(batches.front().Count == 10'000);
    MESSAGE("10,000-object batch preparation: " << preparation.count() << " ms");
}

TEST_CASE("blended and incompatible draws preserve deterministic submission order")
{
    using namespace Keire::RenderBackend;
    InstanceBatchKey opaque;
    opaque.SupportsInstancing = true;
    InstanceBatchKey blended = opaque;
    blended.AlphaMode = Keire::MaterialAlphaMode::Blend;
    const std::vector keys{opaque, opaque, blended, blended};

    const auto batches = BuildInstanceBatches(keys);
    REQUIRE(batches.size() == 3);
    CHECK(batches[0].Count == 2);
    CHECK(batches[1].Count == 1);
    CHECK(batches[2].Count == 1);
    CHECK(batches[1].First == 2);
    CHECK(batches[1].GpuFirstInstance() == 0);
}

TEST_CASE("production material blend modes have explicit deterministic policies")
{
    using enum Keire::MaterialAlphaMode;
    using enum Keire::RenderBackend::MaterialBlendFactor;

    CHECK(Keire::RenderBackend::MaterialBlending(Opaque) ==
          Keire::RenderBackend::MaterialBlendPolicy{One, Zero, One, Zero, false, true});
    CHECK(Keire::RenderBackend::MaterialBlending(Mask) ==
          Keire::RenderBackend::MaterialBlendPolicy{One, Zero, One, Zero, false, true});
    CHECK(Keire::RenderBackend::MaterialBlending(Blend) ==
          Keire::RenderBackend::MaterialBlendPolicy{SourceAlpha, OneMinusSourceAlpha, One, OneMinusSourceAlpha, true,
                                                    false});
    CHECK(Keire::RenderBackend::MaterialBlending(Additive) ==
          Keire::RenderBackend::MaterialBlendPolicy{SourceAlpha, One, One, One, true, false});
    CHECK(Keire::RenderBackend::MaterialBlending(Modulate) ==
          Keire::RenderBackend::MaterialBlendPolicy{DestinationColor, Zero, Zero, One, true, false});
    CHECK(Keire::RenderBackend::MaterialBlending(AlphaComposite) ==
          Keire::RenderBackend::MaterialBlendPolicy{One, OneMinusSourceAlpha, One, OneMinusSourceAlpha, true, false});
    CHECK(Keire::RenderBackend::MaterialBlending(AlphaHoldout) ==
          Keire::RenderBackend::MaterialBlendPolicy{Zero, OneMinusSourceAlpha, Zero, OneMinusSourceAlpha, true, false});
}

TEST_CASE("all transparent material modes bypass opaque instancing")
{
    using namespace Keire::RenderBackend;
    for (const auto mode :
         {Keire::MaterialAlphaMode::Blend, Keire::MaterialAlphaMode::Additive, Keire::MaterialAlphaMode::Modulate,
          Keire::MaterialAlphaMode::AlphaComposite, Keire::MaterialAlphaMode::AlphaHoldout})
    {
        InstanceBatchKey key;
        key.SupportsInstancing = true;
        key.AlphaMode = mode;
        const std::vector objects{key, key};
        const auto batches = BuildInstanceBatches(objects);
        REQUIRE(batches.size() == 2);
        CHECK(batches[0].Count == 1);
        CHECK(batches[1].Count == 1);
    }
}

TEST_CASE("CPU VFX coalesces only adjacent particles with identical render state")
{
    using namespace Keire::RenderBackend;
    auto* firstTexture = reinterpret_cast<SDL_GPUTexture*>(std::uintptr_t{1});
    auto* secondTexture = reinterpret_cast<SDL_GPUTexture*>(std::uintptr_t{2});
    auto* sampler = reinterpret_cast<SDL_GPUSampler*>(std::uintptr_t{3});
    constexpr std::array<float, 4> alphaBlend{1.0F, 0.5F, 2.0F, 1.0F};
    constexpr std::array<float, 4> additive{1.0F, 0.5F, 3.0F, 1.0F};
    std::vector<PreparedCpuVfxBatch> batches;

    AppendPreparedCpuVfxBatch(batches, 0, 6, {firstTexture, sampler}, alphaBlend);
    AppendPreparedCpuVfxBatch(batches, 6, 6, {firstTexture, sampler}, alphaBlend);
    REQUIRE(batches.size() == 1);
    CHECK(batches.front().FirstVertex == 0);
    CHECK(batches.front().VertexCount == 12);

    AppendPreparedCpuVfxBatch(batches, 12, 6, {firstTexture, sampler}, additive);
    AppendPreparedCpuVfxBatch(batches, 18, 6, {secondTexture, sampler}, alphaBlend);
    AppendPreparedCpuVfxBatch(batches, 24, 6, {firstTexture, sampler}, alphaBlend);
    REQUIRE(batches.size() == 4);
    CHECK(batches[1].FirstVertex == 12);
    CHECK(batches[2].FirstVertex == 18);
    CHECK(batches[3].FirstVertex == 24);
}

TEST_CASE("sampled scene depth is requested only by GPU depth collision operations")
{
    Keire::VfxGpuEmitter emitter;
    std::vector emitters{emitter};
    CHECK_FALSE(Keire::RenderBackend::RequiresGpuDepthCollision(emitters));

    auto execution = std::make_shared<Keire::VfxGpuExecutionPayload>();
    execution->ParticleOperations.push_back({.Kind = Keire::VfxGpuParticleOperationKind::Collision,
                                             .Setting = static_cast<std::uint32_t>(Keire::VfxCollisionMode::Cpu)});
    emitters.front().Execution = execution;
    CHECK_FALSE(Keire::RenderBackend::RequiresGpuDepthCollision(emitters));

    execution->ParticleOperations.back().Setting = static_cast<std::uint32_t>(Keire::VfxCollisionMode::GpuDepth);
    CHECK(Keire::RenderBackend::RequiresGpuDepthCollision(emitters));
}
