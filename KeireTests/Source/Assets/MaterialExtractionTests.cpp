#include "doctest/doctest.h"

#include "Keire/Assets/AssetPipeline.h"
#include "Keire/Assets/AssetSystem.h"
#include "Keire/Assets/RenderingAssets.h"
#include "Keire/Rendering/MaterialGraph.h"
#include "KeireTests/TestSupport.h"

#include <array>
#include <filesystem>
#include <ranges>
#include <span>
#include <string>
#include <system_error>

namespace
{
    class TemporaryMaterialExtractionProject final
    {
      public:
        TemporaryMaterialExtractionProject() : Root(KeireTests::MakeTestDirectory("MaterialExtraction"))
        {
            std::filesystem::create_directories(Root / "Assets");
        }

        ~TemporaryMaterialExtractionProject()
        {
            std::error_code ignored;
            std::filesystem::remove_all(Root, ignored);
        }

        std::filesystem::path Root;
    };
} // namespace

TEST_CASE("Generated model materials can be extracted as editable source assets")
{
    TemporaryMaterialExtractionProject project;
    Keire::AssetImporterRegistration modelImporter;
    modelImporter.Name = "Test.ExtractableModel";
    modelImporter.Type = Keire::MeshAsset::StaticType();
    modelImporter.Extensions = {".model"};
    modelImporter.ContextualImport = [](const Keire::AssetImportContext& context, std::span<const std::byte>)
    {
        const std::array vertices{Keire::MeshVertex{{0.0F, 0.0F, 0.0F}}, Keire::MeshVertex{{1.0F, 0.0F, 0.0F}},
                                  Keire::MeshVertex{{0.0F, 1.0F, 0.0F}}};
        constexpr std::array<std::uint32_t, 3> indices{0, 1, 2};
        const Keire::MeshBounds bounds{{0.0F, 0.0F, 0.0F}, {1.0F, 1.0F, 0.0F}};
        const auto material = context.ResolveSubAssetId("material/Paint/0");
        const std::array submeshes{Keire::MeshSubmesh{0, 3, 0, bounds}};
        const std::array slots{Keire::MeshMaterialSlot{"Paint", material}};
        const std::array lods{Keire::MeshLod{0.0F, 0, 1, bounds}};
        Keire::MaterialAssetDefinition definition;
        definition.Shader = Keire::AssetId::Parse("bad00000-0000-4000-8000-000000000001");
        definition.Properties.emplace("Tint", Keire::Color{0.2F, 0.4F, 0.6F, 1.0F});
        Keire::AssetImportOutput output;
        output.Bytes = Keire::MeshAsset::Encode(vertices, indices, submeshes, slots, lods);
        output.SubAssets.push_back({material, Keire::MaterialAsset::StaticType(), "material/Paint/0", "Paint",
                                    Keire::MaterialAsset::Encode(definition)});
        output.AssetDependencies.push_back(material);
        return output;
    };
    auto database = Keire::CreateRef<Keire::AssetDatabase>(Keire::AssetDatabaseSpecification{
        .ProjectRoot = project.Root, .Importers = {modelImporter, Keire::CreateMaterialGraphAssetImporter()}});
    const std::string source = "model";
    const auto model = database->CreateAsset("Models/Vehicle.model", modelImporter,
                                             std::as_bytes(std::span(source.data(), source.size())));
    const auto extracted = database->ExtractMaterials(model, "Materials/Vehicle");
    REQUIRE(extracted.size() == 1);
    const auto record = database->Find(extracted.front());
    REQUIRE(record);
    CHECK(record->Type == Keire::MaterialGraphAsset::StaticType());
    CHECK(record->RelativePath == std::filesystem::path("Materials/Vehicle/Paint.keirematerial"));
    const auto materialSource = KeireTests::ReadFile(project.Root / "Assets" / record->RelativePath);
    const auto definition = Keire::MaterialGraphAsset::DecodeSource(std::as_bytes(std::span(materialSource)));
    const auto tint = std::ranges::find(definition.Properties, "Tint", &Keire::MaterialGraphPropertyBinding::Name);
    REQUIRE(tint != definition.Properties.end());
    REQUIRE(std::holds_alternative<Keire::Color>(tint->Value));
    CHECK(std::get<Keire::Color>(tint->Value).Blue == doctest::Approx(0.6F));
}
