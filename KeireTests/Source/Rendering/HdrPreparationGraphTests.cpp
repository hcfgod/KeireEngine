#include "Keire/Rendering/RenderSystem.h"
#include "KeireInternal/Rendering/FrameGraphInternal.h"

#include <doctest/doctest.h>

#include <algorithm>

TEST_CASE("bloom graph has ordered half-resolution HDR resources without read-write aliasing")
{
    using namespace Keire::RenderBackend;

    for (const auto path : {Keire::RenderPath::ForwardPlus, Keire::RenderPath::DeferredHybrid})
    {
        const auto disabled = BuildStaticSceneFrameGraph(path);
        CHECK_FALSE(disabled.BloomExtraction);
        CHECK_FALSE(disabled.BloomResult);
        const auto scene = BuildStaticSceneFrameGraph(path, true);
        CHECK(scene.Compiled.Order.size() == disabled.Compiled.Order.size() + 3);
        for (const auto resource : {scene.BloomExtracted, scene.BloomHorizontal, scene.BloomResult})
        {
            const auto& description = scene.Graph.Resources()[resource.Value];
            CHECK_FALSE(description.Imported);
            CHECK(description.Texture.Format == FrameGraphTextureFormat::Rgba16Float);
            CHECK(description.Texture.WidthScaleNumerator == 1U);
            CHECK(description.Texture.WidthScaleDenominator == 2U);
            CHECK(description.Texture.HeightScaleNumerator == 1U);
            CHECK(description.Texture.HeightScaleDenominator == 2U);
        }
        const auto allocation = [&](const FrameGraphResource resource)
        { return scene.Compiled.PhysicalResources[resource.Value]; };
        CHECK(allocation(scene.BloomExtracted) != allocation(scene.BloomHorizontal));
        CHECK(allocation(scene.BloomHorizontal) != allocation(scene.BloomResult));
        CHECK(allocation(scene.HdrScene) != allocation(scene.BloomResult));

        const auto position = [&](const FrameGraphPass pass)
        { return std::ranges::distance(scene.Compiled.Order.begin(), std::ranges::find(scene.Compiled.Order, pass)); };
        CHECK(position(scene.HdrPreparation) < position(scene.BloomExtraction));
        CHECK(position(scene.BloomExtraction) < position(scene.BloomHorizontalBlur));
        CHECK(position(scene.BloomHorizontalBlur) < position(scene.BloomVerticalBlur));
        CHECK(position(scene.BloomVerticalBlur) < position(scene.ToneMap));
        const auto& inputs = scene.Graph.Passes()[scene.ToneMap.Value].Reads;
        CHECK(std::ranges::find(inputs, scene.BloomResult) != inputs.end());
        CHECK(scene.Compiled.Lifetimes[scene.BloomResult.Value].LastPass == position(scene.ToneMap));
    }
}

TEST_CASE("HDR preparation is an explicit dependency before tone mapping in both render paths")
{
    using namespace Keire::RenderBackend;

    for (const auto path : {Keire::RenderPath::ForwardPlus, Keire::RenderPath::DeferredHybrid})
    {
        const auto scene = BuildStaticSceneFrameGraph(path);
        const auto& preparation = scene.Graph.Passes()[scene.HdrPreparation.Value];
        const auto& toneMap = scene.Graph.Passes()[scene.ToneMap.Value];
        REQUIRE(preparation.Writes.size() == 1);
        CHECK(std::ranges::find(preparation.Reads, scene.HdrScene) != preparation.Reads.end());
        CHECK(std::ranges::find(toneMap.Reads, preparation.Writes.front()) != toneMap.Reads.end());

        const auto position = [&](const FrameGraphPass pass)
        {
            const auto found = std::ranges::find(scene.Compiled.Order, pass);
            REQUIRE(found != scene.Compiled.Order.end());
            return std::ranges::distance(scene.Compiled.Order.begin(), found);
        };
        const auto lastScenePass =
            path == Keire::RenderPath::DeferredHybrid ? scene.IrradynComposite : scene.Transparency;
        CHECK(position(lastScenePass) < position(scene.HdrPreparation));
        CHECK(position(scene.HdrPreparation) < position(scene.ToneMap));
        CHECK(position(scene.ToneMap) < position(scene.Overlays));
        // The completion token is ordering-only; it must not allocate an otherwise unused GPU buffer.
        CHECK(scene.Graph.Resources()[preparation.Writes.front().Value].Imported);
    }
}
