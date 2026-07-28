#include "Keire/Core.h"

#include <doctest/doctest.h>

#include <array>

TEST_CASE("Standalone animation clip importer preserves bytes and declares its skeleton")
{
    const Keire::AssetId skeleton(0x1122334455667788ULL, 0x8877665544332211ULL);
    Keire::AnimationTrack track;
    track.Bone = 0;
    track.Keys = {{0.0F, {}}, {1.0F, {{1.0F, 2.0F, 3.0F}, {}, {1.0F, 1.0F, 1.0F}}}};
    const auto bytes = Keire::AnimationClipAsset::Encode(skeleton, 1.0F, std::span(&track, 1), {}, false);
    const auto importer = Keire::CreateAnimationClipAssetImporter();

    REQUIRE(importer.ContextualImport);
    const auto imported = importer.ContextualImport({}, bytes);

    CHECK(imported.Bytes == bytes);
    CHECK(imported.AssetDependencies == std::vector<Keire::AssetId>{skeleton});
    const auto decoded = Keire::AnimationClipAsset::Decode(imported.Bytes);
    CHECK(decoded->Skeleton() == skeleton);
    CHECK(decoded->Duration() == doctest::Approx(1.0F));
}

TEST_CASE("Standalone animation clip importer rejects malformed source")
{
    const auto importer = Keire::CreateAnimationClipAssetImporter();
    const std::array malformed{std::byte{0x1}, std::byte{0x2}};
    CHECK_THROWS(importer.ContextualImport({}, malformed));
}
