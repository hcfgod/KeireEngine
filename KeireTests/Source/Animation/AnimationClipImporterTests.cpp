#include "Keire/Core.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <map>
#include <string>

TEST_CASE("Model importer preserves single-frame actions as constant pose clips")
{
    const std::string source = R"({
        "asset":{"version":"2.0"},
        "buffers":[{"uri":"data:application/octet-stream;base64,AAAAAAAAgD8AAABAAABAQA==","byteLength":16}],
        "bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":4},{"buffer":0,"byteOffset":4,"byteLength":12}],
        "accessors":[{"bufferView":0,"componentType":5126,"count":1,"type":"SCALAR","min":[0],"max":[0]},
                     {"bufferView":1,"componentType":5126,"count":1,"type":"VEC3"}],
        "nodes":[{"name":"Hips"}],
        "animations":[{"name":"Authored Pose","samplers":[{"input":0,"output":1}],
                       "channels":[{"sampler":0,"target":{"node":0,"path":"translation"}}]}],
        "scenes":[{"nodes":[0]}],"scene":0
    })";
    Keire::AssetImportContext context;
    context.Asset = Keire::AssetId::Generate();
    context.ProjectRoot = std::filesystem::current_path();
    context.SourceRoot = context.ProjectRoot;
    context.SourcePath = context.SourceRoot / "Characters/pose.gltf";
    context.RelativePath = "Characters/pose.gltf";
    std::map<std::string, Keire::AssetId> identities;
    context.ResolveSubAssetId = [&identities](const std::string_view key)
    { return identities.try_emplace(std::string(key), Keire::AssetId::Generate()).first->second; };
    const auto importer = Keire::CreateMeshAssetImporter();
    const auto output = importer.ContextualImport(context, std::as_bytes(std::span(source)));
    const auto repeated = importer.ContextualImport(context, std::as_bytes(std::span(source)));
    CHECK(output.Bytes == repeated.Bytes);
    const auto take = std::ranges::find(output.SubAssets, Keire::AnimationClipAsset::StaticType(),
                                        &Keire::AssetGeneratedSubAsset::Type);
    REQUIRE(take != output.SubAssets.end());
    const auto clip = Keire::AnimationClipAsset::Decode(take->Bytes);
    CHECK(clip->Duration() == doctest::Approx(1.0F / 30.0F));
    REQUIRE(clip->Tracks().size() == 1);
    REQUIRE(clip->Tracks().front().Keys.size() == 1);
    CHECK(clip->Tracks().front().Keys.front().Time == 0.0F);
    // glTF's right-handed coordinates are converted to the engine's left-handed space.
    CHECK(clip->Tracks().front().Keys.front().Value.Translation == Keire::Vector3{1.0F, 2.0F, -3.0F});
    const auto repeatedTake = std::ranges::find(repeated.SubAssets, take->Id, &Keire::AssetGeneratedSubAsset::Id);
    REQUIRE(repeatedTake != repeated.SubAssets.end());
    CHECK(repeatedTake->Bytes == take->Bytes);
    CHECK(std::ranges::any_of(
        output.Diagnostics, [](const auto& diagnostic)
        { return diagnostic.Message.find("single-frame animation 'Authored Pose'") != std::string::npos; }));
}

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
