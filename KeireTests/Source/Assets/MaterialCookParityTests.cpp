#include "Keire/Assets/AssetPipeline.h"
#include "Keire/Assets/AssetSystem.h"
#include "Keire/Assets/RenderingAssets.h"
#include "Keire/Rendering/MaterialGraph.h"
#include "KeireTests/TestSupport.h"

#include <doctest/doctest.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

namespace
{
    class CookedMaterialFixture final
    {
      public:
        CookedMaterialFixture() : Root(KeireTests::MakeTestDirectory("MaterialCookParity"))
        {
            std::filesystem::create_directories(Root / "Assets");
        }

        ~CookedMaterialFixture()
        {
            std::error_code ignored;
            std::filesystem::remove_all(Root, ignored);
        }

        std::filesystem::path Root;
    };

    template <typename Predicate> void WaitFor(Keire::AssetSystem& assets, Predicate predicate)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!predicate() && std::chrono::steady_clock::now() < deadline)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            (void)assets.PumpCompletions();
        }
        REQUIRE(predicate());
    }

    [[nodiscard]] std::vector<std::byte> Bytes(const std::string_view text)
    {
        const auto bytes = std::as_bytes(std::span(text.data(), text.size()));
        return {bytes.begin(), bytes.end()};
    }
} // namespace

TEST_CASE("property-only materials cook and load in the runtime without shader reimport")
{
    CookedMaterialFixture fixture;
    const auto propertyId = Keire::AssetId::Parse("8c300000-0000-4000-8000-000000000001");
    const auto shaderImports = std::make_shared<std::atomic_size_t>();
    Keire::AssetImporterRegistration shaderImporter;
    shaderImporter.Name = "Test.CookedMaterialShader";
    shaderImporter.Version = 1;
    shaderImporter.Type = Keire::ShaderAsset::StaticType();
    shaderImporter.Extensions = {".keirefixtureshader"};
    shaderImporter.Import = [propertyId, shaderImports](const std::span<const std::byte>)
    {
        ++*shaderImports;
        Keire::ShaderAssetDefinition shader;
        shader.Source = "Assets/Shaders/CookedMaterialFixture.hlsl";
        shader.Properties.push_back({"Roughness", Keire::ShaderPropertyType::Scalar, {0.5F, 0.0F, 0.0F, 0.0F}});
        shader.Properties.back().Id = propertyId;
        constexpr std::array formats{Keire::ShaderBinaryFormat::Dxil, Keire::ShaderBinaryFormat::SpirV,
                                     Keire::ShaderBinaryFormat::Msl};
        for (std::size_t index = 0; index < formats.size(); ++index)
        {
            shader.Variants.push_back({formats[index],
                                       {std::byte{static_cast<unsigned char>(index + 1)}},
                                       {std::byte{static_cast<unsigned char>(index + 11)}}});
        }
        return Keire::ShaderAsset::Encode(shader);
    };

    auto database = Keire::CreateRef<Keire::AssetDatabase>(Keire::AssetDatabaseSpecification{
        .ProjectRoot = fixture.Root, .Importers = {shaderImporter, Keire::CreateMaterialGraphAssetImporter()}});
    const auto shaderManifest =
        std::string("{\"schemaVersion\":1,\"source\":\"Assets/Shaders/CookedMaterialFixture.hlsl\",") +
        "\"stages\":{\"vertex\":\"VSMain\",\"fragment\":\"PSMain\"},\"properties\":[{\"name\":\"Roughness\",\"id\":\"" +
        propertyId.ToString() + "\",\"type\":\"Float\",\"default\":[0.5,0,0,0]}]}";
    const auto shader =
        database->CreateAsset("Shaders/CookedMaterial.keirefixtureshader", shaderImporter, Bytes(shaderManifest));

    Keire::MaterialAuthoringDefinition source;
    source.SchemaVersion = 5;
    source.Shader.Kind = Keire::MaterialShaderSourceKind::ShaderAsset;
    source.Shader.Asset = shader;
    source.PropertyOverrides.push_back({propertyId, "Roughness", 0.25F});
    const auto material =
        database->CreateAsset("Materials/CookedMaterial.keirematerial", Keire::CreateMaterialGraphAssetImporter(),
                              Keire::MaterialAsset::EncodeAuthoringSource(source));
    REQUIRE(shaderImports->load() == 1);
    (void)database->ImportAll();
    shaderImports->store(0);

    const std::array changedAssets{material};
    for (const float value : {0.1F, 0.9F, 0.25F, 0.8F})
    {
        source.PropertyOverrides.front().Value = value;
        database->ReplaceAssetSource(material, Keire::MaterialAsset::EncodeAuthoringSource(source));
        (void)database->ImportAssets(changedAssets, Keire::AssetImportPolicy::FailFast);
        CHECK(shaderImports->load() == 0);
    }
    float expectedRoughness = 0.8F;
    SUBCASE("edited override survives cooking") {}
    SUBCASE("reset inherits the shader default without reimport")
    {
        source.PropertyOverrides.clear();
        expectedRoughness = 0.5F;
    }
    SUBCASE("an inactive identity cannot replace an active property with the same name")
    {
        source.PropertyOverrides.push_back(
            {Keire::AssetId::Parse("8c300000-0000-4000-8000-000000000002"), "Roughness", 0.1F});
    }
    database->ReplaceAssetSource(material, Keire::MaterialAsset::EncodeAuthoringSource(source));
    (void)database->ImportAssets(changedAssets, Keire::AssetImportPolicy::FailFast);
    CHECK(shaderImports->load() == 0);
    const auto sourceRecord = database->Find(material);
    REQUIRE(sourceRecord);
    CHECK(sourceRecord->Type == Keire::MaterialGraphAsset::StaticType());
    CHECK(sourceRecord->Dependencies == std::vector<Keire::AssetId>{shader});
    REQUIRE(sourceRecord->SubAssets.size() == 1);
    const auto runtimeMaterial = sourceRecord->SubAssets.front();

    Keire::AssetBuildProfile profile;
    profile.Strict = true;
    profile.Roots = {runtimeMaterial};
    const auto cooked = Keire::AssetCooker::Cook(*database, profile, fixture.Root / "Cooked");
    CHECK_NOTHROW(Keire::AssetCooker::Validate(cooked.CatalogPath));
    CHECK(cooked.AssetCount == 2);
    CHECK(shaderImports->load() == 0);

    Keire::AssetSystemSpecification runtimeSpecification;
    runtimeSpecification.Mode = Keire::AssetMode::Cooked;
    runtimeSpecification.WorkerCount = 1;
    runtimeSpecification.Mounts.push_back({cooked.CatalogPath});
    runtimeSpecification.Decoders.push_back(Keire::CreateMaterialAssetDecoder());
    runtimeSpecification.Decoders.push_back(Keire::CreateShaderAssetDecoder());
    auto runtime = Keire::CreateRef<Keire::AssetSystem>(std::move(runtimeSpecification));
    CHECK(runtime->TryGetType(runtimeMaterial) == Keire::MaterialAsset::StaticType());
    CHECK(runtime->TryGetType(shader) == Keire::ShaderAsset::StaticType());
    const auto loadedShader = runtime->Load<Keire::ShaderAsset>(shader);
    WaitFor(*runtime, [&loadedShader] { return loadedShader.State() == Keire::AssetState::Ready; });
    REQUIRE(loadedShader.Get());
    CHECK_FALSE(loadedShader.UsingFallback());
    const auto& shaderDefinition = loadedShader.Require()->Definition();
    REQUIRE(shaderDefinition.Properties.size() == 1);
    CHECK(shaderDefinition.Properties.front().Id == propertyId);
    CHECK(shaderDefinition.Properties.front().Name == "Roughness");
    CHECK(shaderDefinition.Properties.front().DefaultValue.X == doctest::Approx(0.5F));
    CHECK(shaderDefinition.Variants.size() == 3);
    CHECK(loadedShader.Require()->Variant(Keire::ShaderBinaryFormat::Dxil) != nullptr);
    CHECK(loadedShader.Require()->Variant(Keire::ShaderBinaryFormat::SpirV) != nullptr);
    CHECK(loadedShader.Require()->Variant(Keire::ShaderBinaryFormat::Msl) != nullptr);
    const auto loaded = runtime->Load<Keire::MaterialAsset>(runtimeMaterial);
    WaitFor(*runtime, [&loaded] { return loaded.State() == Keire::AssetState::Ready; });
    REQUIRE(loaded.Get());
    CHECK_FALSE(loaded.UsingFallback());
    CHECK(loaded.Require()->Definition().Shader == shader);
    const bool hasActiveOverride =
        std::ranges::any_of(source.PropertyOverrides, [propertyId](const Keire::MaterialPropertyOverride& property)
                            { return property.Property == propertyId; });
    CHECK(loaded.Require()->Definition().Properties.contains("Roughness") == hasActiveOverride);
    const auto resolvedRoughness = hasActiveOverride
                                       ? std::get<float>(loaded.Require()->Definition().Properties.at("Roughness"))
                                       : shaderDefinition.Properties.front().DefaultValue.X;
    CHECK(resolvedRoughness == doctest::Approx(expectedRoughness));
    runtime->Close();
}
