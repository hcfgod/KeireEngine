#include "KeireInternal/Rendering/InstanceBatchInternal.h"

#include <doctest/doctest.h>

#include <chrono>
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
