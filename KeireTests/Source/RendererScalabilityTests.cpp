#include "KeireInternal/Rendering/GpuOcclusionPolicyInternal.h"
#include "KeireInternal/Rendering/InstanceBatchInternal.h"
#include "KeireInternal/Rendering/MaterialBlendingInternal.h"
#include "KeireInternal/Rendering/RenderBackendInternal.h"

#include <doctest/doctest.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
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

TEST_CASE("GPU occlusion keeps conservative full-resolution depth within a per-surface texture budget")
{
    namespace Policy = Keire::RenderBackend::GpuOcclusionPolicy;

    CHECK(Policy::ResolveConservativeResourceExtent(1920U, 1080U) == (Policy::ResourceExtent{1920U, 1080U}));
    CHECK(Policy::ResolveConservativeResourceExtent(3840U, 2160U) == (Policy::ResourceExtent{3840U, 2160U}));
    CHECK(Policy::ResolveConservativeResourceExtent(2160U, 3840U) == (Policy::ResourceExtent{2160U, 3840U}));
    CHECK(Policy::ResolveConservativeResourceExtent(16'384U, 16'384U) == (Policy::ResourceExtent{16'384U, 16'384U}));
    CHECK(Policy::ResolveConservativeResourceExtent(0U, 1080U) == Policy::ResourceExtent{});

    const auto fullResolutionBytes =
        Policy::EstimateTextureMemoryBytes(Policy::ResolveConservativeResourceExtent(3840U, 2160U),
                                           Keire::RenderBackend::MaximumGpuOcclusionPyramidLevels, 3U);
    REQUIRE(fullResolutionBytes.has_value());
    CHECK(*fullResolutionBytes == 132'711'624ULL);
    CHECK(*fullResolutionBytes > 126ULL * 1024ULL * 1024ULL);
    CHECK(*fullResolutionBytes < 127ULL * 1024ULL * 1024ULL);
    CHECK(Policy::TextureMemoryWithinBudget(*fullResolutionBytes));

    const auto eightFrameSlots =
        Policy::EstimateTextureMemoryBytes({3840U, 2160U}, Keire::RenderBackend::MaximumGpuOcclusionPyramidLevels, 8U);
    REQUIRE(eightFrameSlots.has_value());
    CHECK_FALSE(Policy::TextureMemoryWithinBudget(*eightFrameSlots));

    const auto maximumSurface = Policy::EstimateTextureMemoryBytes(
        {16'384U, 16'384U}, Keire::RenderBackend::MaximumGpuOcclusionPyramidLevels, 1U);
    REQUIRE(maximumSurface.has_value());
    CHECK_FALSE(Policy::TextureMemoryWithinBudget(*maximumSurface));

    const auto exactBudget = Policy::EstimateTextureMemoryBytes({8192U, 8192U}, 0U, 1U);
    REQUIRE(exactBudget.has_value());
    CHECK(*exactBudget == Policy::MaximumTextureBytesPerSurface);
    CHECK(Policy::TextureMemoryWithinBudget(*exactBudget));
    CHECK_FALSE(Policy::TextureMemoryWithinBudget(*exactBudget + 1U));

    CHECK_FALSE(Policy::EstimateTextureMemoryBytes({0U, 1080U}, 14U, 3U));
    CHECK_FALSE(Policy::EstimateTextureMemoryBytes({1920U, 1080U}, 14U, 0U));
    CHECK_FALSE(Policy::EstimateTextureMemoryBytes(
        {std::numeric_limits<std::uint32_t>::max(), std::numeric_limits<std::uint32_t>::max()}, 14U, 8U));
}

TEST_CASE("GPU occlusion allocation retry uses bounded backoff and sparse warnings")
{
    namespace Policy = Keire::RenderBackend::GpuOcclusionPolicy;

    const Policy::ResourceExtent extent{1920U, 1080U};
    constexpr std::size_t firstSlot = 0U;
    constexpr std::size_t secondSlot = 1U;

    SUBCASE("failure delays grow exponentially and cap")
    {
        Policy::AllocationRetryState state;
        Policy::PrepareAllocationRetryExtent(state, extent);
        REQUIRE(Policy::BeginAllocationAttempt(state, firstSlot));
        constexpr std::array expectedDelays{
            1U, 2U, 4U, 8U, 16U, 32U, 64U, Policy::AllocationRetryMaximumFrames, Policy::AllocationRetryMaximumFrames};
        for (std::size_t failure = 0; failure < expectedDelays.size(); ++failure)
        {
            const bool warning = Policy::RegisterAllocationFailure(state, firstSlot);
            CHECK(state.Slots[firstSlot].FramesRemaining == expectedDelays[failure]);
            CHECK(warning == (failure == 0U || failure == 7U));
        }
    }

    SUBCASE("retry frames elapse before the next attempt")
    {
        Policy::AllocationRetryState state;
        Policy::PrepareAllocationRetryExtent(state, extent);
        REQUIRE(Policy::BeginAllocationAttempt(state, firstSlot));
        REQUIRE(Policy::RegisterAllocationFailure(state, firstSlot));
        CHECK(Policy::AllocationRetryPending(state, firstSlot));
        CHECK(Policy::AnyAllocationRetryPending(state));
        CHECK_FALSE(Policy::BeginAllocationAttempt(state, firstSlot));
        CHECK_FALSE(Policy::AllocationRetryPending(state, firstSlot));
        CHECK(Policy::BeginAllocationAttempt(state, firstSlot));
    }

    SUBCASE("success resets only its frame slot and a resize resets all failed slots")
    {
        Policy::AllocationRetryState state;
        Policy::PrepareAllocationRetryExtent(state, extent);
        REQUIRE(Policy::BeginAllocationAttempt(state, firstSlot));
        REQUIRE(Policy::BeginAllocationAttempt(state, secondSlot));
        REQUIRE(Policy::RegisterAllocationFailure(state, firstSlot));
        REQUIRE(Policy::RegisterAllocationFailure(state, secondSlot));
        Policy::RegisterAllocationSuccess(state, secondSlot);
        CHECK(state.Slots[firstSlot].FailureCount == 1U);
        CHECK(state.Slots[firstSlot].FramesRemaining == 1U);
        CHECK(state.Slots[secondSlot].FailureCount == 0U);
        CHECK(state.Slots[secondSlot].FramesRemaining == 0U);
        CHECK(Policy::AnyAllocationRetryPending(state));

        const Policy::ResourceExtent resizedSurface{3840U, 2160U};
        Policy::PrepareAllocationRetryExtent(state, resizedSurface);
        CHECK(state.Extent == resizedSurface);
        CHECK(state.Slots[firstSlot].FailureCount == 0U);
        CHECK(state.Slots[firstSlot].FramesRemaining == 0U);
        CHECK_FALSE(state.Slots[firstSlot].MaximumDelayWarningPublished);
        CHECK(state.Slots[secondSlot].FailureCount == 0U);
        CHECK_FALSE(Policy::AnyAllocationRetryPending(state));
        CHECK(Policy::BeginAllocationAttempt(state, firstSlot));
    }

    SUBCASE("surface resource reset retries the same extent immediately")
    {
        Policy::AllocationRetryState state;
        Policy::PrepareAllocationRetryExtent(state, extent);
        REQUIRE(Policy::RegisterAllocationFailure(state, firstSlot));
        REQUIRE(Policy::RegisterAllocationFailure(state, secondSlot));
        REQUIRE(Policy::AnyAllocationRetryPending(state));

        Policy::ResetAllocationRetry(state);
        Policy::PrepareAllocationRetryExtent(state, extent);
        CHECK_FALSE(Policy::AnyAllocationRetryPending(state));
        CHECK(Policy::BeginAllocationAttempt(state, firstSlot));
        CHECK(Policy::BeginAllocationAttempt(state, secondSlot));
    }
}

TEST_CASE("GPU visibility bounds retain conservative candidates when normal classification is unavailable")
{
    namespace Policy = Keire::RenderBackend::GpuOcclusionPolicy;

    CHECK(Policy::RequiresConservativeVisibilityDebugUpload(true, 4U, false));
    CHECK_FALSE(Policy::RequiresConservativeVisibilityDebugUpload(false, 4U, false));
    CHECK_FALSE(Policy::RequiresConservativeVisibilityDebugUpload(true, 0U, false));
    CHECK_FALSE(Policy::RequiresConservativeVisibilityDebugUpload(true, 4U, true));
}

TEST_CASE("optional GPU occlusion visualization failures remain isolated from core setup")
{
    namespace Policy = Keire::RenderBackend::GpuOcclusionPolicy;

    bool hzbAllocated = false;
    std::uint32_t hzbCleanupCount = 0U;
    const bool hzbCreated = Policy::TryCreateOptionalVisualization(
        [&]
        {
            hzbAllocated = true;
            throw std::runtime_error("injected optional pipeline failure");
        },
        [&](const std::exception&)
        {
            hzbAllocated = false;
            ++hzbCleanupCount;
        });
    bool boundsAllocated = false;
    std::uint32_t boundsCleanupCount = 0U;
    const bool boundsCreated = Policy::TryCreateOptionalVisualization(
        [&] { boundsAllocated = true; }, [&](const std::exception&) { ++boundsCleanupCount; });

    CHECK_FALSE(hzbCreated);
    CHECK_FALSE(hzbAllocated);
    CHECK(hzbCleanupCount == 1U);
    CHECK(boundsCreated);
    CHECK(boundsAllocated);
    CHECK(boundsCleanupCount == 0U);
}
