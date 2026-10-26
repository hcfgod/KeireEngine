#include "KeireClient/Editor/ThumbnailService.h"

#include "Keire/Core.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <future>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

namespace
{
    class ThumbnailTestDirectory final
    {
      public:
        ThumbnailTestDirectory()
            : Root(std::filesystem::temp_directory_path() /
                   ("Keire-ThumbnailTests-" + Keire::AssetId::Generate().ToString()))
        {
            if (!std::filesystem::create_directory(Root))
            {
                throw std::runtime_error("Could not acquire an isolated thumbnail test directory.");
            }
        }

        ~ThumbnailTestDirectory()
        {
            std::error_code ignored;
            std::filesystem::remove_all(Root, ignored);
        }

        ThumbnailTestDirectory(const ThumbnailTestDirectory&) = delete;
        ThumbnailTestDirectory& operator=(const ThumbnailTestDirectory&) = delete;

        const std::filesystem::path Root;
    };
} // namespace

TEST_CASE("thumbnail invalidation supersedes an in-flight revision of the same asset")
{
    const ThumbnailTestDirectory directory;
    const auto& root = directory.Root;
    {
        KeireEditor::ThumbnailService thumbnails(root);
        std::atomic_uint32_t calls = 0;
        std::promise<void> firstEntered;
        auto firstEnteredFuture = firstEntered.get_future();
        std::promise<void> releaseFirst;
        const auto releaseFirstFuture = releaseFirst.get_future().share();
        thumbnails.RegisterProvider(
            ".refresh", 1,
            [&](const KeireEditor::ThumbnailRequest&, const std::uint32_t width, const std::uint32_t height)
            {
                const auto call = ++calls;
                if (call == 1)
                {
                    firstEntered.set_value();
                    releaseFirstFuture.wait();
                }
                return std::vector<std::byte>(static_cast<std::size_t>(width) * height * 4U,
                                              static_cast<std::byte>(call));
            });
        const auto asset = Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000078");
        REQUIRE(thumbnails.Request({.Asset = asset, .RelativePath = "Preview.refresh", .Digest = "same-digest"}));
        if (firstEnteredFuture.wait_for(std::chrono::seconds(2)) != std::future_status::ready)
        {
            releaseFirst.set_value();
            FAIL("The first thumbnail job did not start.");
        }
        thumbnails.Invalidate(asset);
        const bool replacementQueued =
            thumbnails.Request({.Asset = asset, .RelativePath = "Preview.refresh", .Digest = "same-digest"});
        releaseFirst.set_value();
        REQUIRE(replacementQueued);

        KeireEditor::ThumbnailResult completed;
        for (int attempt = 0; attempt < 200 && completed.Pixels.empty(); ++attempt)
        {
            auto results = thumbnails.DrainCompleted();
            if (!results.empty())
            {
                completed = std::move(results.front());
            }
            else
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        }
        REQUIRE(completed.Pixels.size() == 96U * 96U * 4U);
        CHECK(std::to_integer<unsigned>(completed.Pixels.front()) == 2U);
        CHECK(calls.load() == 2U);
    }
}

TEST_CASE("thumbnail invalidation bypasses a stale persistent cache entry")
{
    const ThumbnailTestDirectory directory;
    const auto& root = directory.Root;
    {
        KeireEditor::ThumbnailService thumbnails(root);
        std::atomic_uint32_t calls = 0;
        thumbnails.RegisterProvider(
            ".refreshcache", 1,
            [&](const KeireEditor::ThumbnailRequest&, const std::uint32_t width, const std::uint32_t height)
            {
                const auto call = ++calls;
                return std::vector<std::byte>(static_cast<std::size_t>(width) * height * 4U,
                                              static_cast<std::byte>(call));
            });
        const auto await = [&thumbnails]
        {
            for (int attempt = 0; attempt < 200; ++attempt)
            {
                auto completed = thumbnails.DrainCompleted();
                if (!completed.empty())
                {
                    return std::move(completed.front());
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            return KeireEditor::ThumbnailResult{};
        };
        const auto asset = Keire::AssetId::Generate();
        const KeireEditor::ThumbnailRequest request{
            .Asset = asset, .RelativePath = "Preview.refreshcache", .Digest = "stable-digest"};
        REQUIRE(thumbnails.Request(request));
        REQUIRE(await().Pixels.size() == 96U * 96U * 4U);
        CHECK(calls.load() == 1U);

        thumbnails.Invalidate(asset);
        REQUIRE(thumbnails.Request(request));
        const auto refreshed = await();
        REQUIRE(refreshed.Pixels.size() == 96U * 96U * 4U);
        CHECK(std::to_integer<unsigned>(refreshed.Pixels.front()) == 2U);
        CHECK(calls.load() == 2U);

        REQUIRE(thumbnails.Request(request));
        const auto cached = await();
        REQUIRE(cached.Pixels.size() == 96U * 96U * 4U);
        CHECK(std::to_integer<unsigned>(cached.Pixels.front()) == 2U);
        CHECK(calls.load() == 2U);
    }
}

TEST_CASE("content previews use immutable loaded assets without blocking shutdown")
{
    const ThumbnailTestDirectory directory;
    const auto& root = directory.Root;
    KeireEditor::ThumbnailService thumbnails(root);
    const auto await = [&thumbnails]
    {
        for (int attempt = 0; attempt < 200; ++attempt)
        {
            auto completed = thumbnails.DrainCompleted();
            if (!completed.empty())
            {
                return std::move(completed.front());
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return KeireEditor::ThumbnailResult{};
    };

    Keire::TextureImportSettings textureSettings;
    textureSettings.Mips = Keire::TextureMipPolicy::None;
    Keire::TextureMipLevel mip;
    mip.Width = 2;
    mip.Height = 1;
    mip.Pixels = {std::byte{255}, std::byte{0},   std::byte{0}, std::byte{255},
                  std::byte{0},   std::byte{255}, std::byte{0}, std::byte{0}};
    auto texture = Keire::CreateRef<Keire::Texture2DAsset>(textureSettings, std::vector{mip});
    const auto textureId = Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000070");
    REQUIRE(thumbnails.Request({.Asset = textureId,
                                .Type = Keire::Texture2DAsset::StaticType(),
                                .PreviewAsset = texture,
                                .RelativePath = "Preview.png",
                                .Digest = "texture-preview"}));
    const auto textureResult = await();
    REQUIRE(textureResult.Pixels.size() == 96U * 96U * 4U);
    const auto redOffset = (48U * 96U + 24U) * 4U;
    CHECK(std::to_integer<unsigned>(textureResult.Pixels[redOffset]) >
          std::to_integer<unsigned>(textureResult.Pixels[redOffset + 1]));
    const auto alphaOffset = (48U * 96U + 72U) * 4U;
    CHECK(std::to_integer<unsigned>(textureResult.Pixels[alphaOffset]) ==
          std::to_integer<unsigned>(textureResult.Pixels[alphaOffset + 1]));

    const auto recoveredTextureId = Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000073");
    REQUIRE(thumbnails.Request({.Asset = recoveredTextureId,
                                .Type = Keire::Texture2DAsset::StaticType(),
                                .RelativePath = "Recovered.png",
                                .Digest = "recovered-texture-preview",
                                .Missing = true}));
    REQUIRE(await().Pixels.size() == 96U * 96U * 4U);
    REQUIRE(thumbnails.Request({.Asset = recoveredTextureId,
                                .Type = Keire::Texture2DAsset::StaticType(),
                                .PreviewAsset = texture,
                                .RelativePath = "Recovered.png",
                                .Digest = "recovered-texture-preview"}));
    const auto recoveredTexture = await();
    REQUIRE(recoveredTexture.Pixels.size() == 96U * 96U * 4U);
    CHECK(std::to_integer<unsigned>(recoveredTexture.Pixels[redOffset]) >
          std::to_integer<unsigned>(recoveredTexture.Pixels[redOffset + 1]));

    Keire::MaterialAssetDefinition materialDefinition;
    materialDefinition.Properties.emplace("Tint", Keire::Color{1.0F, 0.05F, 0.05F, 1.0F});
    auto material = Keire::CreateRef<Keire::MaterialAsset>(materialDefinition);
    const auto materialId = Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000071");
    REQUIRE(thumbnails.Request({.Asset = materialId,
                                .Type = Keire::MaterialAsset::StaticType(),
                                .PreviewAsset = material,
                                .RelativePath = "Preview.keirematerial",
                                .Digest = "material-preview"}));
    const auto materialResult = await();
    REQUIRE(materialResult.Pixels.size() == 96U * 96U * 4U);
    const auto center = (48U * 96U + 48U) * 4U;
    CHECK(std::to_integer<unsigned>(materialResult.Pixels[center]) >
          std::to_integer<unsigned>(materialResult.Pixels[center + 1]));

    const auto materialGraphId = Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000075");
    REQUIRE(thumbnails.Request({.Asset = materialGraphId,
                                .Type = Keire::ShaderGraphAsset::StaticType(),
                                .PreviewAsset = material,
                                .RelativePath = "Preview.keireshadergraph",
                                .Digest = "material-graph-preview"}));
    const auto materialGraphResult = await();
    REQUIRE(materialGraphResult.Pixels.size() == 96U * 96U * 4U);
    CHECK(std::to_integer<unsigned>(materialGraphResult.Pixels[center]) >
          std::to_integer<unsigned>(materialGraphResult.Pixels[center + 1]));
    const auto badgeBorder = (79U * 96U + 71U) * 4U;
    CHECK(std::to_integer<unsigned>(materialGraphResult.Pixels[badgeBorder]) > 200U);
    CHECK(std::to_integer<unsigned>(materialGraphResult.Pixels[badgeBorder + 1]) > 150U);

    Keire::MaterialAssetDefinition generatedMaterialDefinition;
    generatedMaterialDefinition.Properties.emplace("MG_SurfaceTint", Keire::Color{0.05F, 0.8F, 0.15F, 1.0F});
    auto generatedMaterial = Keire::CreateRef<Keire::MaterialAsset>(std::move(generatedMaterialDefinition));
    Keire::ShaderAssetDefinition generatedShaderDefinition;
    Keire::ShaderPropertyDefinition surfaceTint;
    surfaceTint.Name = "MG_SurfaceTint";
    surfaceTint.Type = Keire::ShaderPropertyType::Color;
    surfaceTint.Category = "Surface";
    generatedShaderDefinition.Properties.push_back(std::move(surfaceTint));
    auto generatedShader = Keire::CreateRef<Keire::ShaderAsset>(std::move(generatedShaderDefinition));
    const auto generatedMaterialGraphId = Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000077");
    REQUIRE(thumbnails.Request({.Asset = generatedMaterialGraphId,
                                .Type = Keire::MaterialGraphAsset::StaticType(),
                                .PreviewAsset = generatedMaterial,
                                .PreviewShader = generatedShader,
                                .RelativePath = "Generated.keirematerial",
                                .Digest = "generated-material-graph-preview"}));
    const auto generatedMaterialGraphResult = await();
    REQUIRE(generatedMaterialGraphResult.Pixels.size() == 96U * 96U * 4U);
    CHECK(std::to_integer<unsigned>(generatedMaterialGraphResult.Pixels[center + 1]) >
          std::to_integer<unsigned>(generatedMaterialGraphResult.Pixels[center]));
    CHECK(std::to_integer<unsigned>(generatedMaterialGraphResult.Pixels[center + 1]) >
          std::to_integer<unsigned>(generatedMaterialGraphResult.Pixels[center + 2]));

    REQUIRE(thumbnails.Request({.Asset = Keire::AssetId::Generate(),
                                .PreviewAsset = material,
                                .PreviewShader = Keire::CreateRef<Keire::ShaderAsset>(
                                    Keire::ShaderAssetDefinition{.ProgramTarget = "Fullscreen"}),
                                .RelativePath = "Fullscreen.keirematerial",
                                .Digest = "fullscreen-material-placeholder"}));
    const auto fullscreenResult = await();
    REQUIRE(fullscreenResult.Pixels.size() == 96U * 96U * 4U);
    CHECK(std::to_integer<unsigned>(fullscreenResult.Pixels[center + 2]) == 57U);

    const auto vfxId = Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000076");
    REQUIRE(thumbnails.Request({.Asset = vfxId,
                                .Type = Keire::VfxEffectAsset::StaticType(),
                                .PreviewAsset = Keire::VfxEffectAsset::Default(),
                                .RelativePath = "Preview.keirevfx",
                                .Digest = "vfx-preview"}));
    const auto vfxResult = await();
    REQUIRE(vfxResult.Pixels.size() == 96U * 96U * 4U);
    CHECK(std::to_integer<unsigned>(vfxResult.Pixels[badgeBorder]) > 200U);

    const auto materialGraphFallback =
        KeireEditor::MakeAssetFallbackThumbnail(Keire::ShaderGraphAsset::StaticType(), 96, 96);
    const auto materialInstanceFallback =
        KeireEditor::MakeAssetFallbackThumbnail(Keire::MaterialInstanceAsset::StaticType(), 96, 96);
    const auto vfxFallback = KeireEditor::MakeAssetFallbackThumbnail(Keire::VfxEffectAsset::StaticType(), 96, 96);
    const auto mixerFallback = KeireEditor::MakeAssetFallbackThumbnail(Keire::AudioMixerAsset::StaticType(), 96, 96);
    const auto animationFallback =
        KeireEditor::MakeAssetFallbackThumbnail(Keire::AnimationSourceAsset::StaticType(), 96, 96);
    const auto genericFallback = KeireEditor::MakeAssetFallbackThumbnail({}, 96, 96);
    CHECK(materialGraphFallback != materialInstanceFallback);
    CHECK(materialGraphFallback != vfxFallback);
    CHECK(materialInstanceFallback != vfxFallback);
    CHECK(mixerFallback != materialGraphFallback);
    CHECK(mixerFallback != vfxFallback);
    CHECK(animationFallback != genericFallback);

    const auto requestArtwork = [&](const std::string_view id, const Keire::AssetTypeId type,
                                    const std::filesystem::path& path, const std::string_view digest)
    {
        const bool queued = thumbnails.Request(
            {.Asset = Keire::AssetId::Parse(id), .Type = type, .RelativePath = path, .Digest = std::string(digest)});
        CHECK(queued);
        return await();
    };
    const auto textArtwork = requestArtwork("ed170000-0000-4000-8000-000000000080", Keire::TextAsset::StaticType(),
                                            "Notes.txt", "text-artwork");
    const auto extensionlessArtwork =
        requestArtwork("ed170000-0000-4000-8000-000000000081", Keire::TextAsset::StaticType(), "LICENSE",
                       "extensionless-text-artwork");
    const auto scriptArtwork = requestArtwork("ed170000-0000-4000-8000-000000000082", Keire::TextAsset::StaticType(),
                                              "PlayerController.cs", "script-artwork");
    const auto assemblyArtwork =
        requestArtwork("ed170000-0000-4000-8000-000000000083", Keire::ManagedAssemblyAsset::StaticType(),
                       "Gameplay.keireasm", "assembly-artwork");
    const auto dataArtwork = requestArtwork("ed170000-0000-4000-8000-000000000084", Keire::TextAsset::StaticType(),
                                            "Settings.json", "data-artwork");
    CHECK(textArtwork.Pixels == extensionlessArtwork.Pixels);
    CHECK(textArtwork.Pixels != scriptArtwork.Pixels);
    CHECK(scriptArtwork.Pixels != assemblyArtwork.Pixels);
    CHECK(assemblyArtwork.Pixels != dataArtwork.Pixels);
    CHECK(dataArtwork.Pixels != textArtwork.Pixels);

    const auto animationId = Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000078");
    REQUIRE(thumbnails.Request({.Asset = animationId,
                                .Type = Keire::AnimationSourceAsset::StaticType(),
                                .RelativePath = "Walk.fbx",
                                .Digest = "animation-source-preview"}));
    const auto animationResult = await();
    CHECK(animationResult.Pixels == animationFallback);

    const auto meshId = Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000072");
    REQUIRE(thumbnails.Request({.Asset = meshId,
                                .Type = Keire::MeshAsset::StaticType(),
                                .PreviewAsset = Keire::MeshAsset::Cube(),
                                .RelativePath = "Preview.obj",
                                .Digest = "mesh-preview"}));
    const auto meshResult = await();
    REQUIRE(meshResult.Pixels.size() == 96U * 96U * 4U);
    CHECK(std::ranges::any_of(meshResult.Pixels,
                              [](const std::byte value) { return std::to_integer<unsigned>(value) > 210U; }));

    const auto prefabId = Keire::AssetId::Parse("00000000-0000-0000-0000-000000000074");
    KeireEditor::ThumbnailRequest prefabRequest{
        .Asset = prefabId,
        .Type = Keire::PrefabAsset::StaticType(),
        .RelativePath = "Preview.keireprefab",
        .Digest = "prefab-preview",
    };
    prefabRequest.PreviewMeshes.push_back(
        KeireEditor::ThumbnailMeshInstance{.Mesh = Keire::MeshAsset::Cube(), .Transform = Keire::Matrix4{}});

    REQUIRE(thumbnails.Request(std::move(prefabRequest)));
    const auto prefabResult = await();
    REQUIRE(prefabResult.Pixels.size() == 96U * 96U * 4U);
    CHECK(std::ranges::any_of(prefabResult.Pixels,
                              [](const std::byte value) { return std::to_integer<unsigned>(value) > 180U; }));

    thumbnails.CancelAll();
}
