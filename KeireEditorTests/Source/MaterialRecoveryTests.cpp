#include "KeireClient/Editor/MaterialDocument.h"
#include "KeireClient/Editor/MaterialGraphCreationPicker.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

TEST_CASE("material shader interfaces preserve graph metadata and reject incompatible targets")
{
    Keire::ShaderGraphDefinition graph;
    Keire::ShaderGraphNode parameter;
    parameter.Id = Keire::AssetId::Generate();
    parameter.Kind = Keire::ShaderGraphNodeKind::Parameter;
    parameter.Symbol = "Glow";
    parameter.Name = "Glow Color";
    parameter.ValueType = Keire::ShaderGraphValueType::Color;
    parameter.Value = Keire::Color{2.0F, 0.5F, 0.25F, 1.0F};
    parameter.ParameterMetadata.Category = "Surface";
    parameter.ParameterMetadata.Description = "Emitted light color";
    parameter.ParameterMetadata.HighDynamicRange = true;
    graph.Nodes.push_back(parameter);
    const auto reflected = KeireEditor::MaterialShaderInterface(graph);
    REQUIRE(reflected);
    REQUIRE(reflected->Properties.size() == 1);
    const auto& property = reflected->Properties.front();
    CHECK(property.Id == parameter.Id);
    CHECK(property.Name == "Glow");
    CHECK(property.DisplayName == "Glow Color");
    CHECK(property.Category == "Surface");
    CHECK(property.Description == "Emitted light color");
    CHECK(property.HighDynamicRange);
    CHECK(property.DefaultValue.X == 2.0F);
    for (const auto [target, domain] : std::array{
             std::pair{Keire::ShaderGraphTarget::Material, Keire::ShaderInterfaceDomain::Surface},
             std::pair{Keire::ShaderGraphTarget::Vfx, Keire::ShaderInterfaceDomain::Vfx},
             std::pair{Keire::ShaderGraphTarget::Fullscreen, Keire::ShaderInterfaceDomain::Fullscreen},
             std::pair{Keire::ShaderGraphTarget::CustomGraphics, Keire::ShaderInterfaceDomain::CustomGraphicsPass}})
    {
        graph.Target.Target = target;
        REQUIRE(KeireEditor::MaterialShaderInterface(graph));
        CHECK(KeireEditor::MaterialShaderInterface(graph)->Domain == domain);
    }
    graph.Target.Target = Keire::ShaderGraphTarget::Compute;
    CHECK_FALSE(KeireEditor::MaterialShaderInterface(graph));
    graph.Target.Target = Keire::ShaderGraphTarget::Ui;
    REQUIRE(KeireEditor::MaterialShaderInterface(graph));
    CHECK(KeireEditor::MaterialShaderInterface(graph)->Domain == Keire::ShaderInterfaceDomain::Ui);
}

TEST_CASE("material creation keeps the selected shader and inherits its exposed defaults")
{
    Keire::ShaderInterfaceDefinition interface;
    interface.Properties = {{"Roughness", Keire::ShaderPropertyType::Scalar, {0.37F, 0.0F, 0.0F, 0.0F}}};
    for (const auto type : std::array{Keire::ShaderAsset::StaticType(), Keire::ShaderGraphAsset::StaticType()})
    {
        Keire::AssetSourceRecord selected;
        selected.Id = Keire::AssetId::Generate();
        selected.Type = type;
        const auto material = KeireEditor::CreateMaterialForShader(selected, interface);
        CHECK(material.Shader.Asset == selected.Id);
        CHECK(material.Shader.Kind == (type == Keire::ShaderAsset::StaticType()
                                           ? Keire::MaterialShaderSourceKind::ShaderAsset
                                           : Keire::MaterialShaderSourceKind::ShaderGraph));
        CHECK(material.Properties.empty());
        const auto bytes = Keire::MaterialAsset::EncodeAuthoringSource(material);
        CHECK(KeireEditor::MaterialDocument::IsPropertySource(bytes));
        CHECK(Keire::MaterialAsset::DecodeAuthoringSource(bytes) == material);
        Keire::ShaderAssetDefinition shader;
        shader.Source = "Assets/Selected.hlsl";
        shader.Properties = interface.Properties;
        KeireEditor::MaterialDocument document;
        document.Open(bytes,
                      KeireEditor::MaterialDocument::ShaderReferenceResolver{
                          [&](const auto&) -> std::optional<KeireEditor::MaterialDocument::ResolvedShader>
                          { return {{selected.Id, shader}}; }});
        CHECK(document.Definition().Shader == selected.Id);
        CHECK(std::get<float>(document.Property("Roughness")) == doctest::Approx(0.37F));
    }
    Keire::AssetSourceRecord invalid;
    CHECK_THROWS_AS((void)KeireEditor::CreateMaterialForShader(invalid, interface), std::invalid_argument);
    invalid.Id = Keire::AssetId::Generate();
    invalid.Type = Keire::Texture2DAsset::StaticType();
    CHECK_THROWS_AS((void)KeireEditor::CreateMaterialForShader(invalid, interface), std::invalid_argument);
    invalid.Type = Keire::ShaderAsset::StaticType();
    interface.Domain = Keire::ShaderInterfaceDomain::Fullscreen;
    const auto effect = KeireEditor::CreateMaterialForShader(invalid, interface);
    CHECK(effect.Shader.Asset == invalid.Id);
    CHECK(Keire::MaterialAsset::DecodeAuthoringSource(Keire::MaterialAsset::EncodeAuthoringSource(effect)) == effect);
    for (const auto domain : {Keire::ShaderInterfaceDomain::Ui, Keire::ShaderInterfaceDomain::Vfx,
                              Keire::ShaderInterfaceDomain::CustomGraphicsPass})
    {
        interface.Domain = domain;
        const auto material = KeireEditor::CreateMaterialForShader(invalid, interface);
        CHECK(material.Shader.Asset == invalid.Id);
        CHECK(material.Properties.empty());
        CHECK(material.Surface.AlphaMode == (domain == Keire::ShaderInterfaceDomain::CustomGraphicsPass
                                                 ? Keire::MaterialAlphaMode::Opaque
                                                 : Keire::MaterialAlphaMode::Blend));
        CHECK(Keire::MaterialAsset::DecodeAuthoringSource(Keire::MaterialAsset::EncodeAuthoringSource(material)) ==
              material);
    }
}

TEST_CASE("material Inspector preserves missing shader values and permits transactional recovery")
{
    const auto missing = Keire::AssetId::Generate();
    const auto replacement = Keire::AssetId::Generate();
    Keire::MaterialAuthoringDefinition source;
    source.Shader.Kind = Keire::MaterialShaderSourceKind::ShaderGraph;
    source.Shader.Asset = missing;
    source.Properties.emplace("Roughness", 0.25F);
    const auto bytes = Keire::MaterialAsset::EncodeAuthoringSource(source);
    Keire::ShaderAssetDefinition shader;
    shader.Source = "Assets/Shaders/Recovery.hlsl";
    shader.Properties = {{"Roughness", Keire::ShaderPropertyType::Scalar, {0.5F, 0.0F, 0.0F, 0.0F}}};
    bool available = false;
    const KeireEditor::MaterialDocument::ShaderReferenceResolver resolver =
        [&](const Keire::MaterialShaderReference& reference)
        -> std::optional<KeireEditor::MaterialDocument::ResolvedShader>
    {
        if (reference.Asset == replacement || available)
            return KeireEditor::MaterialDocument::ResolvedShader{replacement, shader};
        return std::nullopt;
    };
    KeireEditor::MaterialDocument document;
    document.OpenAsset(Keire::AssetId::Generate(), "Assets/Recovery.keiremateriallegacy", bytes, resolver);
    CHECK_FALSE(document.HasResolvedShader());
    CHECK_FALSE(document.Dirty());
    CHECK(document.Shader() == missing);
    CHECK(Keire::MaterialAsset::DecodeAuthoringSource(document.SaveSource()) == source);
    CHECK_THROWS_AS((void)document.SetProperty("Roughness", 0.75F), std::logic_error);
    CHECK_THROWS_AS((void)document.ResetProperties(), std::logic_error);

    SUBCASE("pending shader import resolves without discarding authored overrides")
    {
        available = true;
        document.Open(document.DraftSource(), resolver);
        CHECK(document.HasResolvedShader());
        CHECK(std::get<float>(document.Property("Roughness")) == doctest::Approx(0.25F));
        CHECK_FALSE(document.Dirty());
    }
    SUBCASE("failed replacement preserves source and successful replacement preserves compatible values")
    {
        auto reference = source.Shader;
        reference.Asset = Keire::AssetId::Generate();
        CHECK_THROWS_AS((void)document.SetShaderReference(reference, resolver), std::invalid_argument);
        CHECK(Keire::MaterialAsset::DecodeAuthoringSource(document.SaveSource()) == source);
        reference.Kind = Keire::MaterialShaderSourceKind::ShaderAsset;
        reference.Asset = replacement;
        CHECK(document.SetShaderReference(reference, resolver));
        CHECK(document.HasResolvedShader());
        CHECK(document.Definition().Shader == replacement);
        CHECK(std::get<float>(document.Property("Roughness")) == doctest::Approx(0.25F));
        document.CaptureDraft();
        CHECK(document.Dirty());
    }
}

TEST_CASE("material property reset restores shader defaults and retains surface settings")
{
    Keire::ShaderAssetDefinition shader;
    shader.Source = "Assets/Shaders/Defaults.hlsl";
    const auto texture = Keire::AssetId::Generate();
    shader.Properties = {{"Roughness", Keire::ShaderPropertyType::Scalar, {0.5F, 0.0F, 0.0F, 0.0F}},
                         {"BaseMap", Keire::ShaderPropertyType::Texture2D, {}, texture}};
    Keire::MaterialAuthoringDefinition source;
    source.Shader.Asset = Keire::AssetId::Generate();
    source.Surface.DoubleSided = true;
    source.Properties.emplace("Roughness", 0.25F);
    source.Properties.emplace("BaseMap", Keire::AssetId::Generate());
    const KeireEditor::MaterialDocument::ShaderResolver resolver =
        [&](Keire::AssetId) -> std::optional<Keire::ShaderAssetDefinition> { return shader; };
    KeireEditor::MaterialDocument document;
    document.Open(Keire::MaterialAsset::EncodeAuthoringSource(source), resolver);
    CHECK(document.ResetProperty("Roughness"));
    CHECK_FALSE(document.ResetProperty("Roughness"));
    CHECK(std::get<float>(document.Property("Roughness")) == doctest::Approx(0.5F));
    CHECK(document.LastChangedProperty() == "Roughness");
    const auto beforeInvalidReset = document.SaveSource();
    CHECK_THROWS_AS((void)document.ResetProperty("NotDeclared"), std::invalid_argument);
    CHECK(document.SaveSource() == beforeInvalidReset);
    CHECK(document.ResetProperties());
    CHECK_FALSE(document.ResetProperties());
    CHECK(document.Texture("BaseMap") == texture);
    CHECK(document.Definition().Properties.empty());
    CHECK(document.Surface().DoubleSided);
    KeireEditor::MaterialDocument reopened;
    reopened.Open(document.SaveSource(), resolver);
    CHECK(reopened.Texture("BaseMap") == texture);
    CHECK(reopened.Surface().DoubleSided);
}

TEST_CASE("property-only material imports preserve generated identity without shader compilation")
{
    Keire::MaterialAuthoringDefinition source;
    source.Shader.Asset = Keire::AssetId::Generate();
    source.Properties.emplace("Roughness", 0.25F);
    const auto texture = Keire::AssetId::Generate();
    source.Properties.emplace("BaseMap", texture);
    source.Properties.emplace("Offset", Keire::Vector2{0.25F, 0.5F});
    source.Properties.emplace("Direction", Keire::Vector3{1.0F, 0.0F, 0.0F});
    source.Properties.emplace("Packed", Keire::Vector4{1.0F, 2.0F, 3.0F, 4.0F});
    source.Properties.emplace("Tint", Keire::Color{0.2F, 0.3F, 0.4F, 1.0F});
    source.Surface.DoubleSided = true;
    source.EmissiveGIIntensity = 2.0F;
    const auto sourceId = Keire::AssetId::Generate();
    const auto runtimeId = Keire::AssetId::Generate();
    Keire::AssetImportContext context;
    context.Asset = sourceId;
    context.ResolveSubAssetId = [&](const std::string_view key)
    {
        CHECK(key == "material/default");
        return runtimeId;
    };
    // No project file reader or compiler context: code-shader material value imports must be binding-only.
    const auto importer = Keire::CreateMaterialGraphAssetImporter();
    for (const auto roughness : {0.25F, 0.75F})
    {
        source.Properties.at("Roughness") = roughness;
        const auto bytes = Keire::MaterialAsset::EncodeAuthoringSource(source);
        const auto imported = importer.ContextualImport(context, bytes);
        REQUIRE(imported.SubAssets.size() == 1);
        CHECK(imported.SubAssets.front().Id == runtimeId);
        CHECK(imported.SubAssets.front().Type == Keire::MaterialAsset::StaticType());
        CHECK(imported.SourceDependencies.empty());
        const auto runtime = Keire::MaterialAsset::Decode(imported.SubAssets.front().Bytes)->Definition();
        CHECK(runtime.Shader == source.Shader.Asset);
        CHECK(runtime.Properties == source.Properties);
        CHECK(runtime.Surface == source.Surface);
        CHECK(runtime.EmissiveGIIntensity == 2.0F);
        CHECK(std::ranges::find(imported.AssetDependencies, texture) != imported.AssetDependencies.end());
        const auto compatibility = Keire::MaterialGraphAsset::Decode(imported.Bytes);
        const auto baked =
            Keire::BakeMaterialGraph(compatibility->Definition(), [](const auto& shader) { return shader.Asset; });
        CHECK(baked.Properties == source.Properties);
    }
    SUBCASE("cleared shaders remain property-only and preserve saved values")
    {
        source.Shader.Asset = {};
        const auto imported = importer.ContextualImport(context, Keire::MaterialAsset::EncodeAuthoringSource(source));
        REQUIRE(imported.SubAssets.size() == 1);
        const auto runtime = Keire::MaterialAsset::Decode(imported.SubAssets.front().Bytes)->Definition();
        CHECK_FALSE(runtime.Shader);
        CHECK(runtime.Properties == source.Properties);
    }
    SUBCASE("invalid schema is rejected without publishing a generated identity")
    {
        bool resolved = false;
        context.ResolveSubAssetId = [&](std::string_view)
        {
            resolved = true;
            return runtimeId;
        };
        const std::string invalid = R"({"kind":"material","schemaVersion":999})";
        const auto bytes = std::as_bytes(std::span(invalid.data(), invalid.size()));
        CHECK_THROWS(importer.ContextualImport(context, bytes));
        CHECK_FALSE(resolved);
        CHECK_FALSE(KeireEditor::MaterialDocument::IsPropertySource(bytes));
    }
}

TEST_CASE("property-only graph shader bindings resolve an existing shader without compiling it")
{
    const auto graphId = Keire::AssetId::Generate();
    const auto shaderId = Keire::AssetId::Generate();
    const auto materialId = Keire::AssetId::Generate();
    const auto runtimeId = Keire::AssetId::Generate();
    auto graph = Keire::CreateShaderGraphTemplate(Keire::ShaderGraphTemplate::Lit);
    Keire::MaterialAuthoringDefinition material;
    material.Shader.Kind = Keire::MaterialShaderSourceKind::ShaderGraph;
    material.Shader.Asset = graphId;
    Keire::AssetImportContext context;
    context.Asset = materialId;
    context.ProjectRoot = "Project";
    context.SourceRoot = "Project/Assets";
    context.ResolveSubAssetId = [&](std::string_view) { return runtimeId; };
    context.ResolveSubAssetIdFor = [&](const Keire::AssetId owner, const std::string_view key)
    {
        CHECK(owner == graphId);
        CHECK(key == Keire::MakeShaderGraphVariantSubAssetKey("default", std::span<const std::string>{}));
        return shaderId;
    };
    context.ResolveAssetSource = [&](const Keire::AssetId asset) -> std::optional<Keire::AssetImportSource>
    {
        CHECK(asset == graphId);
        Keire::AssetImportSource record;
        record.Id = graphId;
        record.Type = Keire::ShaderGraphAsset::StaticType();
        record.RelativePath = "Lit.keireshadergraph";
        return record;
    };
    context.ReadProjectFile = [&](const std::filesystem::path& path)
    {
        CHECK(path == std::filesystem::path("Assets/Lit.keireshadergraph"));
        return Keire::ShaderGraphAsset::EncodeSource(graph);
    };
    const auto imported = Keire::CreateMaterialGraphAssetImporter().ContextualImport(
        context, Keire::MaterialAsset::EncodeAuthoringSource(material));
    REQUIRE(imported.SubAssets.size() == 1);
    CHECK(imported.SubAssets.front().Id == runtimeId);
    CHECK(Keire::MaterialAsset::Decode(imported.SubAssets.front().Bytes)->Definition().Shader == shaderId);
    CHECK(imported.SourceDependencies.empty());
    graph = Keire::CreateShaderGraphTemplate(Keire::ShaderGraphTemplate::Ui);
    CHECK_THROWS_AS(Keire::CreateMaterialGraphAssetImporter().ContextualImport(
                        context, Keire::MaterialAsset::EncodeAuthoringSource(material)),
                    std::invalid_argument);
}

TEST_CASE("nested instances of property-only materials inherit values and select the nearest keyword override")
{
    const auto graphId = Keire::AssetId::Generate();
    const auto rootId = Keire::AssetId::Generate();
    const auto parentId = Keire::AssetId::Generate();
    const auto childId = Keire::AssetId::Generate();
    const auto shaderId = Keire::AssetId::Generate();
    const auto runtimeId = Keire::AssetId::Generate();
    auto graph = Keire::CreateShaderGraphTemplate(Keire::ShaderGraphTemplate::Lit);
    graph.Keywords.push_back({"DETAIL", {}, "false", true});
    auto roughness =
        Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::Parameter, Keire::ShaderGraphValueType::Scalar);
    roughness.Name = "Roughness";
    roughness.Symbol = "Roughness";
    roughness.Value = 0.5F;
    graph.Nodes.push_back(std::move(roughness));
    Keire::MaterialAuthoringDefinition root;
    root.Shader.Kind = Keire::MaterialShaderSourceKind::ShaderGraph;
    root.Shader.Asset = graphId;
    root.Properties.emplace("Roughness", 0.25F);
    Keire::MaterialInstanceDefinition parent;
    parent.Parent = rootId;
    parent.Properties.emplace("Roughness", 0.5F);
    parent.KeywordOverrides.emplace("DETAIL", "true");
    Keire::MaterialInstanceDefinition child;
    child.Parent = parentId;
    bool enabled = true;
    SUBCASE("inherits parent keyword") {}
    SUBCASE("nearest instance wins")
    {
        child.Properties.emplace("Roughness", 0.75F);
        child.KeywordOverrides.emplace("DETAIL", "false");
        enabled = false;
    }
    Keire::AssetImportContext context;
    context.Asset = childId;
    context.ProjectRoot = "Project";
    context.SourceRoot = "Project/Assets";
    context.ResolveSubAssetId = [&](std::string_view) { return runtimeId; };
    context.ResolveSubAssetIdFor = [&](const Keire::AssetId owner, const std::string_view key)
    {
        CHECK(owner == graphId);
        const std::vector<std::string> keywords =
            enabled ? std::vector<std::string>{"DETAIL"} : std::vector<std::string>{};
        CHECK(key == Keire::MakeShaderGraphVariantSubAssetKey("default", keywords));
        return shaderId;
    };
    context.ResolveAssetSource = [&](const Keire::AssetId asset) -> std::optional<Keire::AssetImportSource>
    {
        if (asset == rootId)
            return {{rootId, Keire::MaterialGraphAsset::StaticType(), "Root.keirematerial"}};
        if (asset == parentId)
            return {{parentId, Keire::MaterialInstanceAsset::StaticType(), "Parent.keirematerialinstance"}};
        if (asset == graphId)
            return {{graphId, Keire::ShaderGraphAsset::StaticType(), "Shader.keireshadergraph"}};
        return std::nullopt;
    };
    context.ReadProjectFile = [&](const std::filesystem::path& path)
    {
        if (path == std::filesystem::path("Assets/Root.keirematerial"))
            return Keire::MaterialAsset::EncodeAuthoringSource(root);
        if (path == std::filesystem::path("Assets/Parent.keirematerialinstance"))
            return Keire::MaterialInstanceAsset::EncodeSource(parent);
        CHECK(path == std::filesystem::path("Assets/Shader.keireshadergraph"));
        return Keire::ShaderGraphAsset::EncodeSource(graph);
    };
    const auto imported = Keire::CreateMaterialInstanceAssetImporter().ContextualImport(
        context, Keire::MaterialInstanceAsset::EncodeSource(child));
    REQUIRE(imported.SubAssets.size() == 1);
    CHECK(imported.SubAssets.front().Id == runtimeId);
    const auto runtime = Keire::MaterialAsset::Decode(imported.SubAssets.front().Bytes)->Definition();
    CHECK(runtime.Shader == shaderId);
    CHECK(std::get<float>(runtime.Properties.at("Roughness")) == (enabled ? 0.5F : 0.75F));
    const auto resolver = context.ResolveAssetSource;
    context.ResolveAssetSource = [&](const Keire::AssetId asset) -> std::optional<Keire::AssetImportSource>
    { return asset == graphId ? std::nullopt : resolver(asset); };
    CHECK_THROWS(Keire::CreateMaterialInstanceAssetImporter().ContextualImport(
        context, Keire::MaterialInstanceAsset::EncodeSource(child)));
}
