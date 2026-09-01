#include "KeireClient/Editor/AssetPicker.h"

#include <doctest/doctest.h>

#include <array>
#include <optional>
#include <string>

TEST_CASE("Asset picker filters environment textures without exposing raw asset IDs")
{
    Keire::AssetSourceRecord hdr;
    hdr.Id = Keire::AssetId::Generate();
    hdr.Type = Keire::Texture2DAsset::StaticType();
    hdr.RelativePath = "Sky/Studio.hdr";
    CHECK(KeireEditor::AssetPicker::AcceptsEnvironmentTexture(hdr));

    auto color = hdr;
    color.Id = Keire::AssetId::Generate();
    color.RelativePath = "Textures/Albedo.png";
    CHECK_FALSE(KeireEditor::AssetPicker::AcceptsEnvironmentTexture(color));

    color.ImportSettings["semantic"] = std::string("environment");
    CHECK(KeireEditor::AssetPicker::AcceptsEnvironmentTexture(color));

    KeireEditor::AssetPickerOptions options;
    options.Label = "Skybox";
    options.ExpectedType = Keire::Texture2DAsset::StaticType();
    options.Filter = &KeireEditor::AssetPicker::AcceptsEnvironmentTexture;
    CHECK(KeireEditor::AssetPicker::Accepts(hdr, options));
    CHECK(KeireEditor::AssetPicker::Accepts(color, options));

    auto mesh = hdr;
    mesh.Type = Keire::MeshAsset::StaticType();
    CHECK_FALSE(KeireEditor::AssetPicker::Accepts(mesh, options));
}

TEST_CASE("Asset picker resolves material authoring sources but hides Shader Graph preview materials")
{
    Keire::AssetSourceRecord shaderGraph;
    shaderGraph.Id = Keire::AssetId::Generate();
    shaderGraph.Type = Keire::ShaderGraphAsset::StaticType();
    shaderGraph.RelativePath = "Shaders/Layered.keireshadergraph";
    const auto compiledShader = Keire::AssetId::Generate();
    const auto previewMaterial = Keire::AssetId::Generate();
    shaderGraph.SubAssets = {compiledShader, previewMaterial};
    Keire::AssetSourceRecord graph;
    graph.Id = Keire::AssetId::Generate();
    graph.Type = Keire::MaterialGraphAsset::StaticType();
    graph.RelativePath = "Materials/Layered.keirematerial";
    const auto runtimeMaterial = Keire::AssetId::Generate();
    graph.SubAssets = {runtimeMaterial};
    Keire::AssetSourceRecord instance;
    instance.Id = Keire::AssetId::Generate();
    instance.Type = Keire::MaterialInstanceAsset::StaticType();
    instance.RelativePath = "Materials/LayeredInstance.keirematerialinstance";
    const auto instanceMaterial = Keire::AssetId::Generate();
    instance.SubAssets = {instanceMaterial};
    const std::array records{shaderGraph, graph, instance};

    KeireEditor::AssetPickerOptions materialOptions;
    materialOptions.Label = "Material";
    materialOptions.ExpectedType = Keire::MaterialAsset::StaticType();
    materialOptions.ResolveType = [previewMaterial, runtimeMaterial,
                                   instanceMaterial](const Keire::AssetId asset) -> std::optional<Keire::AssetTypeId>
    {
        return asset == previewMaterial || asset == runtimeMaterial || asset == instanceMaterial
                   ? std::optional{Keire::MaterialAsset::StaticType()}
                   : std::nullopt;
    };
    CHECK_FALSE(KeireEditor::AssetPicker::ResolveCompatibleAsset(records, shaderGraph.Id, materialOptions));
    CHECK_FALSE(KeireEditor::AssetPicker::ResolveCompatibleAsset(records, previewMaterial, materialOptions));
    CHECK(KeireEditor::AssetPicker::ResolveCompatibleAsset(records, graph.Id, materialOptions) == runtimeMaterial);
    CHECK(KeireEditor::AssetPicker::ResolveCompatibleAsset(records, runtimeMaterial, materialOptions) ==
          runtimeMaterial);
    CHECK(KeireEditor::AssetPicker::ResolveCompatibleAsset(records, instance.Id, materialOptions) == instanceMaterial);
    CHECK(KeireEditor::AssetPicker::ResolveCompatibleAsset(records, instanceMaterial, materialOptions) ==
          instanceMaterial);
    materialOptions.ResolveType = {};
    CHECK_FALSE(KeireEditor::AssetPicker::ResolveCompatibleAsset(records, graph.Id, materialOptions));

    KeireEditor::AssetPickerOptions meshOptions;
    meshOptions.Label = "Mesh";
    meshOptions.ExpectedType = Keire::MeshAsset::StaticType();
    CHECK(KeireEditor::AssetPicker::ResolveCompatibleAsset({}, Keire::MeshAsset::CubeId(), meshOptions) ==
          Keire::MeshAsset::CubeId());
    CHECK(KeireEditor::AssetPicker::ResolveCompatibleAsset({}, Keire::MeshAsset::TorusId(), meshOptions) ==
          Keire::MeshAsset::TorusId());
    CHECK_FALSE(KeireEditor::AssetPicker::ResolveCompatibleAsset({}, Keire::AssetId::Generate(), meshOptions));
}

TEST_CASE("Asset picker accepts only compatible managed data types for ScriptableObject fields")
{
    const auto baseType = Keire::ManagedTypeId(Keire::AssetId::Generate());
    const auto derivedType = Keire::ManagedTypeId(Keire::AssetId::Generate());
    const auto unrelatedType = Keire::ManagedTypeId(Keire::AssetId::Generate());
    std::array<Keire::ManagedAssetTypeDescriptor, 3> types;
    types[0].StableTypeId = baseType;
    types[0].FullName = "Tests.BaseData";
    types[1].StableTypeId = derivedType;
    types[1].FullName = "Tests.DerivedData";
    types[1].BaseTypeId = baseType;
    types[2].StableTypeId = unrelatedType;
    types[2].FullName = "Tests.UnrelatedData";

    std::array<Keire::AssetSourceRecord, 3> records;
    for (auto& record : records)
    {
        record.Id = Keire::AssetId::Generate();
        record.Type = Keire::ManagedDataAsset::StaticType();
    }
    records[0].RelativePath = "Data/Base.keiredata";
    records[1].RelativePath = "Data/Derived.keiredata";
    records[2].RelativePath = "Data/Unrelated.keiredata";

    KeireEditor::AssetPickerOptions options;
    options.Label = "Data";
    options.ExpectedType = Keire::ManagedDataAsset::StaticType();
    options.ExpectedManagedType = baseType;
    options.ManagedTypes = types;
    options.ResolveManagedType = [&, base = records[0].Id, derived = records[1].Id, unrelated = records[2].Id](
                                     const Keire::AssetId asset) -> std::optional<Keire::ManagedTypeId>
    {
        if (asset == base)
            return baseType;
        if (asset == derived)
            return derivedType;
        if (asset == unrelated)
            return unrelatedType;
        return std::nullopt;
    };

    CHECK(KeireEditor::AssetPicker::Accepts(records[0], options));
    CHECK(KeireEditor::AssetPicker::Accepts(records[1], options));
    CHECK_FALSE(KeireEditor::AssetPicker::Accepts(records[2], options));
    CHECK(KeireEditor::AssetPicker::ResolveCompatibleAsset(records, records[0].Id, options) == records[0].Id);
    CHECK(KeireEditor::AssetPicker::ResolveCompatibleAsset(records, records[1].Id, options) == records[1].Id);
    CHECK_FALSE(KeireEditor::AssetPicker::ResolveCompatibleAsset(records, records[2].Id, options));

    options.ResolveManagedType = {};
    CHECK_FALSE(KeireEditor::AssetPicker::Accepts(records[0], options));
}
