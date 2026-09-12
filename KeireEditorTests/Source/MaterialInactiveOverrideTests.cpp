#include "KeireClient/Editor/MaterialDocument.h"

#include <doctest/doctest.h>

#include <limits>
#include <optional>
#include <stdexcept>
#include <string>

TEST_CASE("material inactive overrides survive shader changes and reopen without runtime bindings")
{
    const auto first = Keire::AssetId::Generate();
    const auto second = Keire::AssetId::Generate();
    Keire::ShaderAssetDefinition shader;
    shader.Source = "Assets/Properties.hlsl";
    shader.Properties = {{"Value", Keire::ShaderPropertyType::Scalar, {0.5F, 0.0F, 0.0F, 0.0F}}};
    Keire::MaterialAuthoringDefinition source;
    source.Shader.Asset = first;
    source.Properties.emplace("Value", 0.25F);
    KeireEditor::MaterialDocument document;
    const KeireEditor::MaterialDocument::ShaderResolver resolver =
        [&](Keire::AssetId) -> std::optional<Keire::ShaderAssetDefinition> { return shader; };
    document.Open(Keire::MaterialAsset::EncodeAuthoringSource(source), resolver);

    SUBCASE("removed declaration") { shader.Properties.clear(); }
    SUBCASE("changed type") { shader.Properties.front().Type = Keire::ShaderPropertyType::Vector2; }
    SUBCASE("narrowed range") { shader.Properties.front().Minimum = 0.4F; }

    CHECK(document.SetShader(second, resolver));
    CHECK(document.Definition().Properties.empty());
    REQUIRE(document.InactiveProperties().size() == 1);
    CHECK(std::get<float>(document.InactiveProperties().find("Value")->second) == doctest::Approx(0.25F));
    const auto saved = document.SaveSource();
    KeireEditor::MaterialDocument reopened;
    reopened.Open(saved, resolver);
    CHECK(reopened.SaveSource() == saved);
    CHECK_FALSE(reopened.ResetProperties());
    CHECK(reopened.SaveSource() == saved);
    const auto runtime = KeireEditor::MaterialDocument::ResolveRuntimeRevision(
        saved, [&](const Keire::MaterialShaderReference&) { return second; });
    REQUIRE(runtime);
    CHECK(runtime->Properties.empty());

    shader.Properties = {{"Value", Keire::ShaderPropertyType::Scalar, {0.5F, 0.0F, 0.0F, 0.0F}}};
    CHECK(reopened.SetShader(first, resolver));
    CHECK(reopened.InactiveProperties().empty());
    CHECK(std::get<float>(reopened.Property("Value")) == doctest::Approx(0.25F));
}

TEST_CASE("material reopening reconciles removed properties and cleanup is undoable through source snapshots")
{
    Keire::ShaderAssetDefinition shader;
    shader.Source = "Assets/Properties.hlsl";
    Keire::MaterialAuthoringDefinition source;
    source.Shader.Asset = Keire::AssetId::Generate();
    source.Properties.emplace("Removed", Keire::AssetId::Generate());
    const KeireEditor::MaterialDocument::ShaderResolver resolver =
        [&](Keire::AssetId) -> std::optional<Keire::ShaderAssetDefinition> { return shader; };
    KeireEditor::MaterialDocument document;
    document.Open(Keire::MaterialAsset::EncodeAuthoringSource(source), resolver);
    CHECK(document.Definition().Properties.empty());
    REQUIRE(document.InactiveProperties().contains("Removed"));
    const auto undo = document.SaveSource();
    CHECK(document.RemoveInactiveProperties());
    CHECK(document.LastChangedProperty() == "$inactiveProperties");
    CHECK_FALSE(document.RemoveInactiveProperties());
    const auto redo = document.SaveSource();
    document.Open(undo, resolver);
    CHECK(document.InactiveProperties().contains("Removed"));
    document.Open(redo, resolver);
    CHECK(document.InactiveProperties().empty());
}

TEST_CASE("material shader recovery does not overwrite an already archived value")
{
    Keire::ShaderAssetDefinition shader;
    shader.Source = "Assets/Properties.hlsl";
    shader.Properties = {{"Value", Keire::ShaderPropertyType::Scalar, {0.5F, 0.0F, 0.0F, 0.0F}}};
    Keire::MaterialAuthoringDefinition source;
    source.Shader.Asset = Keire::AssetId::Generate();
    source.Properties.emplace("Value", 0.25F);
    source.InactiveProperties.emplace("Value", Keire::Vector2{1.0F, 2.0F});
    const KeireEditor::MaterialDocument::ShaderResolver resolver =
        [&](Keire::AssetId) -> std::optional<Keire::ShaderAssetDefinition> { return shader; };
    KeireEditor::MaterialDocument document;
    document.Open(Keire::MaterialAsset::EncodeAuthoringSource(source), resolver);
    const auto before = document.SaveSource();
    shader.Properties.clear();
    CHECK(document.SetShader(Keire::AssetId::Generate(), resolver));
    REQUIRE(document.InactiveProperties().count("Value") == 2);
    CHECK(document.Definition().Properties.empty());
    document.Open(document.SaveSource(), resolver);
    CHECK(document.InactiveProperties().count("Value") == 2);
    shader.Properties = {{"Value", Keire::ShaderPropertyType::Scalar, {0.5F, 0.0F, 0.0F, 0.0F}}};
    CHECK(document.SetShader(source.Shader.Asset, resolver));
    CHECK(document.SaveSource() == before);
    SUBCASE("individual reset preserves incompatible history") { CHECK(document.ResetProperty("Value")); }
    SUBCASE("reset all preserves incompatible history") { CHECK(document.ResetProperties()); }
    document.Open(document.SaveSource(), resolver);
    REQUIRE(document.InactiveProperties().count("Value") == 1);
    CHECK(std::holds_alternative<Keire::Vector2>(document.InactiveProperties().find("Value")->second));
    CHECK(std::get<float>(document.Property("Value")) == doctest::Approx(0.5F));
    CHECK(document.RemoveInactiveProperties());
    CHECK(document.InactiveProperties().empty());
}

TEST_CASE("material inactive overrides are excluded from imported runtime and dependencies")
{
    Keire::MaterialAuthoringDefinition source;
    source.Shader.Asset = Keire::AssetId::Generate();
    source.InactiveProperties.emplace("RemovedTexture", Keire::AssetId::Generate());
    const auto bytes = Keire::MaterialAsset::EncodeAuthoringSource(source);
    CHECK(Keire::MaterialAsset::DecodeAuthoringSource(bytes) == source);
    const auto imported = Keire::CreateMaterialAssetImporter().ContextualImport({}, bytes);
    CHECK(Keire::MaterialAsset::Decode(imported.Bytes)->Definition().Properties.empty());
    REQUIRE(imported.AssetDependencies.size() == 1);
    CHECK(imported.AssetDependencies.front() == source.Shader.Asset);
}

TEST_CASE("material inactive source validates values and preserves missing-shader recovery data")
{
    Keire::MaterialAuthoringDefinition source;
    source.Shader.Asset = Keire::AssetId::Generate();
    source.InactiveProperties.emplace("Value", 0.25F);
    KeireEditor::MaterialDocument document;
    const KeireEditor::MaterialDocument::ShaderResolver missing =
        [](Keire::AssetId) -> std::optional<Keire::ShaderAssetDefinition> { return std::nullopt; };
    const auto saved = Keire::MaterialAsset::EncodeAuthoringSource(source);
    document.Open(saved, missing);
    CHECK(document.SaveSource() == saved);
    CHECK_FALSE(document.HasResolvedShader());
    CHECK(document.RemoveInactiveProperties());
    CHECK_FALSE(document.HasResolvedShader());
    CHECK_FALSE(KeireEditor::MaterialDocument::ResolveRuntimeRevision(
        document.SaveSource(), [](const Keire::MaterialShaderReference&) { return Keire::AssetId{}; }));

    source.InactiveProperties.emplace("NonFinite", std::numeric_limits<float>::infinity());
    CHECK_THROWS_AS((void)Keire::MaterialAsset::EncodeAuthoringSource(source), std::invalid_argument);
}

TEST_CASE("material archive limits reject shader switching without losing the active document")
{
    Keire::MaterialAuthoringDefinition source;
    source.Shader.Asset = Keire::AssetId::Generate();
    source.Properties.emplace("Value", 0.25F);
    for (int index = 0; index < 80; ++index)
        source.InactiveProperties.emplace("Removed" + std::to_string(index), 0.5F);
    Keire::ShaderAssetDefinition shader;
    shader.Source = "Assets/Properties.hlsl";
    shader.Properties = {{"Value", Keire::ShaderPropertyType::Scalar}};
    const KeireEditor::MaterialDocument::ShaderResolver resolver =
        [&](Keire::AssetId) -> std::optional<Keire::ShaderAssetDefinition> { return shader; };
    KeireEditor::MaterialDocument document;
    document.Open(Keire::MaterialAsset::EncodeAuthoringSource(source), resolver);
    const auto before = document.SaveSource();
    shader.Properties.clear();
    CHECK_THROWS_AS((void)document.SetShader(Keire::AssetId::Generate(), resolver), std::invalid_argument);
    CHECK(document.SaveSource() == before);
    CHECK(document.Definition().Properties == source.Properties);
}

TEST_CASE("material property edits normalize compatible packed values for Inspector controls")
{
    Keire::ShaderAssetDefinition shader;
    shader.Source = "Assets/Properties.hlsl";
    shader.Properties = {{"Packed", Keire::ShaderPropertyType::Vector4},
                         {"Tint", Keire::ShaderPropertyType::Color},
                         {"Offset", Keire::ShaderPropertyType::Vector2}};
    Keire::MaterialAuthoringDefinition source;
    source.Shader.Asset = Keire::AssetId::Generate();
    const KeireEditor::MaterialDocument::ShaderResolver resolver =
        [&](Keire::AssetId) -> std::optional<Keire::ShaderAssetDefinition> { return shader; };
    KeireEditor::MaterialDocument document;
    document.Open(Keire::MaterialAsset::EncodeAuthoringSource(source), resolver);
    CHECK(document.SetProperty("Packed", Keire::Color{0.1F, 0.2F, 0.3F, 1.0F}));
    CHECK(std::get<Keire::Vector4>(document.Property("Packed")) == Keire::Vector4{0.1F, 0.2F, 0.3F, 1.0F});
    CHECK_FALSE(document.SetProperty("Packed", Keire::Color{0.1F, 0.2F, 0.3F, 1.0F}));
    CHECK(document.SetProperty("Tint", Keire::Vector4{0.1F, 0.2F, 0.3F, 1.0F}));
    CHECK(std::get<Keire::Color>(document.Property("Tint")) == Keire::Color{0.1F, 0.2F, 0.3F, 1.0F});
    CHECK(document.SetProperty("Offset", Keire::Vector4{0.1F, 0.2F, 0.0F, 0.0F}));
    CHECK(std::get<Keire::Vector2>(document.Property("Offset")) == Keire::Vector2{0.1F, 0.2F});
}
