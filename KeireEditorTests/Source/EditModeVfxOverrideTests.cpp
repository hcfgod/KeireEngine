#include "KeireClient/Editor/EditModeVfxPreview.h"
#include "KeireClient/Editor/ThumbnailService.h"
#include "KeireClientInternal/Editor/AssetPreviewReadiness.h"

#include "Keire/Audio/AudioAssets.h"
#include "Keire/ECS/Components/VfxEmitterComponent.h"
#include "Keire/Scenes/Scene.h"
#include "Keire/Scenes/SceneAsset.h"
#include "KeireInternal/Assets/AssetInternal.h"

#include <doctest/doctest.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <vector>

namespace
{
    [[nodiscard]] constexpr Keire::AssetId EditVfxId(const std::uint64_t value) noexcept
    {
        return Keire::AssetId(0x454449545646584fULL, value);
    }
} // namespace

TEST_CASE("Audio and VFX thumbnails wait for catalog publication and retain failed-import diagnostics")
{
    for (const bool audio : {false, true})
    {
        SUBCASE(audio ? "Audio" : "VFX")
        {
            Keire::AssetSystemSpecification specification;
            specification.Mode = Keire::AssetMode::Development;
            specification.DevelopmentCatalog.clear();
            specification.Decoders = {audio ? Keire::CreateAudioClipAssetDecoder()
                                            : Keire::CreateVfxEffectAssetDecoder()};
            const auto assets = Keire::CreateRef<Keire::AssetSystem>(specification);
            Keire::AssetSourceRecord source;
            source.Id = Keire::AssetId::Generate();
            source.Type = audio ? Keire::AudioClipAsset::StaticType() : Keire::VfxEffectAsset::StaticType();
            KeireEditor::ThumbnailRequest request;
            for (int frame = 0; frame < 3; ++frame)
            {
                const auto ready = KeireEditor::PrepareGeneratedAssetThumbnail(assets, source, request,
                                                                               Keire::AssetImportState::NotImported);
                REQUIRE(ready.has_value());
                CHECK_FALSE(*ready);
                CHECK(assets->PumpCompletions() == 0);
                CHECK_FALSE(request.PreviewAsset);
                CHECK_FALSE(request.Missing);
                CHECK(assets->Statistics().FailedLoads == 0);
            }
            SUBCASE("Published catalog permits the preview without reopening the project")
            {
                struct TemporaryCatalog
                {
                    std::filesystem::path Root = std::filesystem::temp_directory_path() /
                                                 ("Keire-VfxThumbnail-" + Keire::AssetId::Generate().ToString());
                    ~TemporaryCatalog()
                    {
                        std::error_code ignored;
                        std::filesystem::remove_all(Root, ignored);
                    }
                } catalog;
                std::filesystem::create_directories(catalog.Root);
                const std::array entries{Keire::Detail::CatalogEntry{.Id = source.Id,
                                                                     .Type = source.Type,
                                                                     .PackPath = "preview.pack",
                                                                     .Offset = 16,
                                                                     .CompressedBytes = 1,
                                                                     .UncompressedBytes = 1}};
                Keire::Detail::WriteCatalog(catalog.Root / "catalog.json", entries);
                {
                    std::ofstream pack(catalog.Root / "preview.pack", std::ios::binary);
                    Keire::Detail::WritePackHeader(pack);
                    pack.put(0);
                    REQUIRE(pack.good());
                }
                assets->Mount({catalog.Root / "catalog.json"});
                Keire::Ref<Keire::Asset> preview;
                if (audio)
                    preview = Keire::AudioClipAsset::Silence();
                else
                    preview = Keire::VfxEffectAsset::Default();
                REQUIRE(assets->PublishDevelopmentAsset(source.Id, std::move(preview)));
                const auto ready = KeireEditor::PrepareGeneratedAssetThumbnail(assets, source, request,
                                                                               Keire::AssetImportState::Imported);
                REQUIRE(ready.has_value());
                CHECK(*ready);
                CHECK(request.PreviewAsset);
                CHECK_FALSE(request.Missing);
                CHECK(assets->Statistics().FailedLoads == 0);
                assets->Close();
            }
            SUBCASE("Failed import still reports the missing runtime asset")
            {
                (void)KeireEditor::PrepareGeneratedAssetThumbnail(assets, source, request,
                                                                  Keire::AssetImportState::Failed);
                CHECK(assets->PumpCompletions() == 1);
                (void)KeireEditor::PrepareGeneratedAssetThumbnail(assets, source, request,
                                                                  Keire::AssetImportState::Failed);
                CHECK(request.Missing);
                CHECK(assets->Statistics().FailedLoads == 1);
                assets->Close();
            }
        }
    }
}

TEST_CASE("Edit-mode VFX previews defer only known pending initial imports")
{
    using Keire::AssetImportState;
    using KeireEditor::Detail::ShouldDeferAssetPreviewLoad;
    const auto effectType = Keire::VfxEffectAsset::StaticType();
    const auto wrongType = Keire::SceneAsset::StaticType();
    CHECK(ShouldDeferAssetPreviewLoad({}, effectType, AssetImportState::NotImported, effectType));
    CHECK_FALSE(ShouldDeferAssetPreviewLoad(effectType, effectType, AssetImportState::NotImported, effectType));
    CHECK_FALSE(ShouldDeferAssetPreviewLoad(wrongType, effectType, AssetImportState::NotImported, effectType));
    CHECK_FALSE(ShouldDeferAssetPreviewLoad({}, {}, AssetImportState::NotImported, effectType));
    CHECK_FALSE(ShouldDeferAssetPreviewLoad({}, wrongType, AssetImportState::NotImported, effectType));
    for (const auto state : {AssetImportState::Imported, AssetImportState::CacheHit, AssetImportState::Failed})
    {
        CHECK_FALSE(ShouldDeferAssetPreviewLoad({}, effectType, state, effectType));
        CHECK_FALSE(ShouldDeferAssetPreviewLoad(effectType, effectType, state, effectType));
    }
}

TEST_CASE("Edit-mode VFX collection filters authored Blackboard overrides against the loaded effect")
{
    auto scene = Keire::CreateRef<Keire::Scene>(EditVfxId(1), Keire::SceneAsset::EmptyDefinition("Edit VFX overrides"));
    auto entity = scene->CreateEntity("Preview VFX");
    const auto emitter = entity.AddComponent<Keire::VfxEmitterComponent>();
    REQUIRE(emitter);
    emitter->SetEffect(EditVfxId(2));
    emitter->SetEditModePreview(true);
    emitter->SetParameterOverride({EditVfxId(20), 8.0F});
    emitter->SetParameterOverride({EditVfxId(21), Keire::Vector3{1.0F, 2.0F, 3.0F}});
    emitter->SetParameterOverride({EditVfxId(22), 3.0F});
    emitter->SetParameterOverride({EditVfxId(23), 4.0F});

    const auto collected = KeireEditor::CollectEditModeVfxEmitters(scene);
    REQUIRE(collected.size() == 1);
    REQUIRE(collected.front().ParameterOverrides.size() == 4);

    Keire::VfxEffectDefinition definition;
    definition.Blackboard = {
        {EditVfxId(20), "Rate", Keire::VfxValueType::Scalar, 2.0F, true},
        {EditVfxId(21), "Hidden Direction", Keire::VfxValueType::Vector3, Keire::Vector3{}, false},
        {EditVfxId(22), "Count", Keire::VfxValueType::Integer, std::int64_t{1}, true},
    };
    const auto compatible =
        KeireEditor::CompatibleEditModeVfxOverrides(definition, collected.front().ParameterOverrides);
    REQUIRE(compatible.size() == 1);
    CHECK(compatible.front() == (Keire::VfxParameterOverride{EditVfxId(20), 8.0F}));

    scene->Close();
}
