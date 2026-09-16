#include "KeireClient/Editor/MaterialVariantEditing.h"
#include "KeireClient/Editor/SceneDocument.h"

#include "Keire/Rendering/MaterialGraph.h"
#include "Keire/Scenes/Scene.h"
#include "Keire/Scenes/SceneAsset.h"

#include <doctest/doctest.h>

#include <map>
#include <stdexcept>

TEST_CASE("material variant clipboard preserves identity history and undoes as one edit")
{
    Keire::MaterialInstanceDefinition instance;
    instance.Parent = Keire::AssetId::Generate();
    instance.Surface = Keire::MaterialSurfaceState{};
    instance.Surface->DoubleSided = true;
    instance.KeywordOverrides["Detail"] = "true";
    Keire::ShaderAssetDefinition shader;
    shader.Source = "VariantClipboard.hlsl";
    Keire::ShaderPropertyDefinition property;
    property.Id = Keire::AssetId::Generate();
    property.Name = "RenamedRoughness";
    property.Minimum = 0.0F;
    property.Maximum = 1.0F;
    shader.Properties.push_back(property);
    property.Id = Keire::AssetId::Generate();
    property.Name = "LegacyOpacity";
    shader.Properties.push_back(property);
    instance.PropertyOverrides.push_back({shader.Properties[0].Id, "PreviousName", Keire::Vector2{2.0F, 3.0F}});
    const auto before = Keire::MaterialInstanceAsset::EncodeSource(instance);
    const std::vector<Keire::MaterialPropertyOverride> clipboard{
        {shader.Properties[0].Id, "OldRoughness", 0.8F},
        {Keire::AssetId::Generate(), "RenamedRoughness", 0.1F},
        {shader.Properties[0].Id, "OldRoughness", 2.0F},
        {shader.Properties[0].Id, "OldRoughness", Keire::Vector2{0.2F, 0.3F}},
        {{}, "LegacyOpacity", 0.4F}};
    CHECK(KeireEditor::PasteMaterialVariantProperties(instance, shader, clipboard) == 2);
    const auto resolved = KeireEditor::ResolveMaterialVariantOverrides(instance, shader);
    CHECK(std::get<float>(resolved.Properties.at("RenamedRoughness")) == 0.8F);
    CHECK(std::get<float>(resolved.Properties.at("LegacyOpacity")) == 0.4F);
    CHECK(resolved.InactiveOverrideIndices == std::vector<std::size_t>{0});
    CHECK(instance.Surface->DoubleSided);
    CHECK(instance.KeywordOverrides.at("Detail") == "true");
    auto source = Keire::MaterialInstanceAsset::EncodeSource(instance);
    const auto after = source;
    CHECK(KeireEditor::PasteMaterialVariantProperties(instance, shader, clipboard) == 0);
    CHECK(Keire::MaterialInstanceAsset::EncodeSource(instance) == after);
    const std::vector<Keire::MaterialPropertyOverride> incompatible{{shader.Properties[0].Id, "OldRoughness", 2.0F}};
    CHECK(KeireEditor::PasteMaterialVariantProperties(instance, shader, incompatible) == 0);
    CHECK(Keire::MaterialInstanceAsset::EncodeSource(instance) == after);
    auto service = Keire::CreateRef<Keire::UndoService>();
    auto undo = service->CreateContext({.Name = "Variant paste"});
    undo->RecordApplied(KeireEditor::CreateMaterialVariantEdit(Keire::AssetId::Generate(), before, after, 0,
                                                               [&](std::span<const std::byte> bytes)
                                                               { source.assign(bytes.begin(), bytes.end()); }, {}));
    REQUIRE(undo->Undo());
    CHECK(source == before);
    REQUIRE(undo->Redo());
    CHECK(source == after);
    service->Close();
}

TEST_CASE("material variant cross shader paste requires explicit name matching and keeps target identities")
{
    Keire::MaterialInstanceDefinition instance;
    instance.Parent = Keire::AssetId::Generate();
    Keire::ShaderAssetDefinition shader;
    shader.Source = "OtherShader.hlsl";
    Keire::ShaderPropertyDefinition property;
    property.Id = Keire::AssetId::Generate();
    property.Name = "Amount";
    property.Maximum = 1.0F;
    shader.Properties.push_back(property);
    instance.PropertyOverrides.push_back({property.Id, "OldName", Keire::Vector2{2.0F, 3.0F}});
    const std::vector<Keire::MaterialPropertyOverride> clipboard{{Keire::AssetId::Generate(), "Amount", 0.6F},
                                                                 {Keire::AssetId::Generate(), "Missing", 0.8F},
                                                                 {Keire::AssetId::Generate(), "Amount", 2.0F}};
    CHECK(KeireEditor::PasteMaterialVariantProperties(instance, shader, clipboard) == 0);
    CHECK(KeireEditor::PasteMaterialVariantPropertiesByName(instance, shader, clipboard) == 1);
    REQUIRE(instance.PropertyOverrides.size() == 2);
    CHECK(instance.PropertyOverrides.back().Property == property.Id);
    CHECK(instance.PropertyOverrides.back().Property != clipboard.front().Property);
    CHECK(std::get<float>(instance.PropertyOverrides.back().Value) == 0.6F);
    CHECK(std::holds_alternative<Keire::Vector2>(instance.PropertyOverrides.front().Value));
    CHECK(KeireEditor::PasteMaterialVariantPropertiesByName(instance, shader, clipboard) == 0);
}

TEST_CASE("material variant individual reset follows identity and preserves incompatible history")
{
    Keire::MaterialInstanceDefinition instance;
    instance.Parent = Keire::AssetId::Generate();
    Keire::ShaderPropertyDefinition property;
    property.Name = "RenamedRoughness";
    property.Id = Keire::AssetId::Generate();
    property.Minimum = 0.0F;
    property.Maximum = 1.0F;
    const auto other = Keire::AssetId::Generate();
    instance.PropertyOverrides = {{property.Id, "OldName", 0.2F},
                                  {property.Id, "OldName", Keire::Vector2{0.3F, 0.4F}},
                                  {property.Id, "OldName", 2.0F},
                                  {other, "Other", 0.8F},
                                  {property.Id, "OldName", 0.6F}};
    instance.Properties[property.Name] = 0.1F;
    CHECK(KeireEditor::HasMaterialVariantProperty(instance, property));
    CHECK(KeireEditor::SetMaterialVariantProperty(instance, property, std::nullopt));
    CHECK_FALSE(KeireEditor::HasMaterialVariantProperty(instance, property));
    CHECK(instance.Properties.empty());
    REQUIRE(instance.PropertyOverrides.size() == 3);
    CHECK(std::holds_alternative<Keire::Vector2>(instance.PropertyOverrides[0].Value));
    CHECK(std::get<float>(instance.PropertyOverrides[1].Value) == 2.0F);
    CHECK(instance.PropertyOverrides[2].Property == other);
    CHECK_FALSE(KeireEditor::SetMaterialVariantProperty(instance, property, std::nullopt));
    CHECK(KeireEditor::SetMaterialVariantProperty(instance, property, 0.5F));
    CHECK(KeireEditor::HasMaterialVariantProperty(instance, property));
    CHECK(instance.PropertyOverrides.back().Name == property.Name);
    const auto before = Keire::MaterialInstanceAsset::EncodeSource(instance);
    CHECK_THROWS_AS((void)KeireEditor::SetMaterialVariantProperty(instance, property, 4.0F), std::invalid_argument);
    CHECK_THROWS_AS((void)KeireEditor::SetMaterialVariantProperty(instance, property, Keire::AssetId::Generate()),
                    std::invalid_argument);
    CHECK(Keire::MaterialInstanceAsset::EncodeSource(instance) == before);
}

TEST_CASE("material variant individual reset supports legacy packed vectors and texture overrides")
{
    Keire::MaterialInstanceDefinition instance;
    instance.Parent = Keire::AssetId::Generate();
    Keire::ShaderPropertyDefinition property;
    property.Name = "Direction";
    property.Type = Keire::ShaderPropertyType::Vector3;
    property.Minimum = 0.0F;
    property.Maximum = 1.0F;
    instance.Properties[property.Name] = Keire::Vector4{1.0F, 0.0F, 0.0F, 99.0F};
    CHECK(KeireEditor::HasMaterialVariantProperty(instance, property));
    CHECK(KeireEditor::SetMaterialVariantProperty(instance, property, std::nullopt));
    CHECK(instance.Properties.empty());
    property.Name = "Texture";
    property.Type = Keire::ShaderPropertyType::Texture2D;
    property.Minimum.reset();
    property.Maximum.reset();
    property.Id = Keire::AssetId::Generate();
    CHECK(KeireEditor::SetMaterialVariantProperty(instance, property, Keire::AssetId{}));
    CHECK(KeireEditor::HasMaterialVariantProperty(instance, property));
    CHECK(KeireEditor::SetMaterialVariantProperty(instance, property, std::nullopt));
    CHECK_FALSE(KeireEditor::HasMaterialVariantProperty(instance, property));
}

TEST_CASE("material variant color and packed vector compatibility matches runtime resolution")
{
    Keire::MaterialInstanceDefinition instance;
    instance.Parent = Keire::AssetId::Generate();
    Keire::ShaderAssetDefinition shader;
    shader.Source = "PackedColors.hlsl";
    Keire::ShaderPropertyDefinition property;
    property.Id = Keire::AssetId::Generate();
    property.Name = "Tint";
    property.Type = Keire::ShaderPropertyType::Color;
    property.Minimum = 0.0F;
    property.Maximum = 1.0F;
    shader.Properties.push_back(property);
    instance.PropertyOverrides = {{property.Id, "OldTint", Keire::Vector4{0.2F, 0.3F, 0.4F, 1.0F}},
                                  {property.Id, "OldTint", Keire::Vector4{2.0F, 0.3F, 0.4F, 1.0F}}};
    CHECK(KeireEditor::HasMaterialVariantProperty(instance, property));
    CHECK(KeireEditor::SetMaterialVariantProperty(instance, property, std::nullopt));
    CHECK_FALSE(KeireEditor::HasMaterialVariantProperty(instance, property));
    CHECK(KeireEditor::ResolveMaterialVariantOverrides(instance, shader).Properties.empty());
    REQUIRE(instance.PropertyOverrides.size() == 1);
    CHECK(std::get<Keire::Vector4>(instance.PropertyOverrides.front().Value).X == 2.0F);
    const std::vector<Keire::MaterialPropertyOverride> clipboard{
        {property.Id, "OldTint", Keire::Vector4{0.5F, 0.6F, 0.7F, 1.0F}}};
    CHECK(KeireEditor::PasteMaterialVariantProperties(instance, shader, clipboard) == 1);
    CHECK(std::get<Keire::Color>(KeireEditor::ResolveMaterialVariantOverrides(instance, shader).Properties.at("Tint")) ==
          Keire::Color{0.5F, 0.6F, 0.7F, 1.0F});
    property.Type = Keire::ShaderPropertyType::Vector4;
    shader.Properties.front() = property;
    instance.PropertyOverrides = {{property.Id, "Tint", Keire::Color{0.2F, 0.3F, 0.4F, 1.0F}}};
    CHECK(KeireEditor::HasMaterialVariantProperty(instance, property));
    CHECK(KeireEditor::SetMaterialVariantProperty(instance, property, std::nullopt));
    CHECK(KeireEditor::ResolveMaterialVariantOverrides(instance, shader).Properties.empty());
    CHECK(KeireEditor::SetMaterialVariantProperty(instance, property, Keire::Color{0.4F, 0.3F, 0.2F, 1.0F}));
    CHECK(std::get<Keire::Vector4>(KeireEditor::ResolveMaterialVariantOverrides(instance, shader).Properties.at("Tint")) ==
          Keire::Vector4{0.4F, 0.3F, 0.2F, 1.0F});
}

TEST_CASE("material variant inactive cleanup preserves effective overrides and can be undone")
{
    Keire::MaterialInstanceDefinition instance;
    instance.Parent = Keire::AssetId::Generate();
    instance.Surface = Keire::MaterialSurfaceState{};
    instance.Surface->DoubleSided = true;
    Keire::ShaderAssetDefinition shader;
    shader.Source = "Assets/VariantCleanup.hlsl";
    Keire::ShaderPropertyDefinition property;
    property.Name = "Renamed";
    property.Id = Keire::AssetId::Generate();
    shader.Properties.push_back(property);
    instance.Properties["Removed"] = 0.3F;
    instance.Properties[property.Name] = 0.2F;
    instance.PropertyOverrides = {{property.Id, "Old", Keire::Vector2{1.0F, 2.0F}},
                                  {Keire::AssetId::Generate(), "Gone", 0.9F},
                                  {property.Id, "Old", 0.8F}};
    const auto resolved = KeireEditor::ResolveMaterialVariantOverrides(instance, shader);
    CHECK(resolved.InactiveProperties.size() == 4);
    const auto before = Keire::MaterialInstanceAsset::EncodeSource(instance);
    CHECK(KeireEditor::RemoveInactiveMaterialVariantProperties(instance, shader));
    CHECK(KeireEditor::ResolveMaterialVariantOverrides(instance, shader).InactiveProperties.empty());
    CHECK(KeireEditor::ResolveMaterialVariantOverrides(instance, shader).Properties == resolved.Properties);
    CHECK(instance.Surface->DoubleSided);
    CHECK_FALSE(KeireEditor::RemoveInactiveMaterialVariantProperties(instance, shader));
    auto source = Keire::MaterialInstanceAsset::EncodeSource(instance);
    auto service = Keire::CreateRef<Keire::UndoService>();
    auto undo = service->CreateContext({.Name = "Variant cleanup"});
    undo->RecordApplied(KeireEditor::CreateMaterialVariantEdit(Keire::AssetId::Generate(), before, source, 0,
                                                               [&](std::span<const std::byte> bytes)
                                                               { source.assign(bytes.begin(), bytes.end()); }, {}));
    REQUIRE(undo->Undo());
    CHECK(source == before);
    REQUIRE(undo->Redo());
    CHECK(source == Keire::MaterialInstanceAsset::EncodeSource(instance));
    service->Close();
}

TEST_CASE("material variant asset history survives stopping an isolated play session")
{
    auto service = Keire::CreateRef<Keire::UndoService>();
    auto assetHistory = service->CreateContext({.Name = "Assets"});
    auto sceneHistory = service->CreateContext({.Name = "Scene"});
    auto playHistory = service->CreateContext({.Name = "Play"});
    KeireEditor::SceneDocument document;
    auto scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(),
                                                Keire::SceneAsset::EmptyDefinition("Variant Play persistence"));
    document.Open(scene, {}, {}, sceneHistory);
    document.BeginPlay(playHistory);
    std::vector<std::byte> source{std::byte{1}};
    assetHistory->Execute(KeireEditor::CreateMaterialVariantEdit(Keire::AssetId::Generate(), source, {std::byte{2}}, 0,
                                                                 [&](std::span<const std::byte> bytes)
                                                                 { source.assign(bytes.begin(), bytes.end()); }, {}));
    document.EndPlay();
    CHECK_FALSE(playHistory->IsOpen());
    CHECK(assetHistory->IsOpen());
    CHECK(source == std::vector{std::byte{2}});
    CHECK_FALSE(sceneHistory->CanUndo());
    REQUIRE(assetHistory->Undo());
    CHECK(source == std::vector{std::byte{1}});
    REQUIRE(assetHistory->Redo());
    CHECK(source == std::vector{std::byte{2}});
    document.Close();
    service->Close();
}

TEST_CASE("material variant parent selection rejects cycles missing ancestors and unsupported roots")
{
    const auto variant = Keire::AssetId::Generate();
    const auto parent = Keire::AssetId::Generate();
    const auto root = Keire::AssetId::Generate();
    std::map<Keire::AssetId, KeireEditor::MaterialVariantAncestor> ancestors;
    ancestors[parent] = {Keire::MaterialInstanceAsset::StaticType(), root};
    ancestors[root] = {Keire::MaterialAsset::StaticType(), {}};
    const auto resolve = [&](Keire::AssetId id) -> std::optional<KeireEditor::MaterialVariantAncestor>
    {
        const auto found = ancestors.find(id);
        return found == ancestors.end() ? std::nullopt : std::optional(found->second);
    };
    CHECK_NOTHROW(KeireEditor::ValidateMaterialVariantParent(variant, parent, resolve));
    CHECK_NOTHROW(KeireEditor::ValidateMaterialVariantParent(variant, root, resolve));
    CHECK_THROWS_AS(KeireEditor::ValidateMaterialVariantParent(variant, variant, resolve), std::invalid_argument);
    ancestors[root] = {Keire::MaterialInstanceAsset::StaticType(), variant};
    CHECK_THROWS_AS(KeireEditor::ValidateMaterialVariantParent(variant, parent, resolve), std::invalid_argument);
    ancestors[root].Parent = parent;
    CHECK_THROWS_AS(KeireEditor::ValidateMaterialVariantParent(variant, parent, resolve), std::invalid_argument);
    ancestors.erase(root);
    CHECK_THROWS_AS(KeireEditor::ValidateMaterialVariantParent(variant, parent, resolve), std::invalid_argument);
    ancestors[root] = {Keire::ShaderGraphAsset::StaticType(), {}};
    CHECK_THROWS_AS(KeireEditor::ValidateMaterialVariantParent(variant, parent, resolve), std::invalid_argument);
    ancestors[root] = {Keire::MaterialGraphAsset::StaticType(), {}};
    CHECK_NOTHROW(KeireEditor::ValidateMaterialVariantParent(variant, parent, resolve));
    CHECK_THROWS_AS(KeireEditor::ValidateMaterialVariantParent(variant, {}, resolve), std::invalid_argument);
}

TEST_CASE("material variant parent depth matches the importer limit")
{
    std::vector<Keire::AssetId> chain(18);
    for (auto& id : chain)
        id = Keire::AssetId::Generate();
    std::map<Keire::AssetId, KeireEditor::MaterialVariantAncestor> ancestors;
    for (std::size_t i = 1; i + 1 < chain.size(); ++i)
        ancestors[chain[i]] = {Keire::MaterialInstanceAsset::StaticType(), chain[i + 1]};
    ancestors[chain.back()] = {Keire::MaterialAsset::StaticType(), {}};
    const auto resolve = [&](Keire::AssetId id) { return std::optional(ancestors.at(id)); };
    CHECK_THROWS_AS(KeireEditor::ValidateMaterialVariantParent(chain[0], chain[1], resolve), std::invalid_argument);
    CHECK_NOTHROW(KeireEditor::ValidateMaterialVariantParent(chain[1], chain[2], resolve));
}

TEST_CASE("material variant undo groups a continuous edit and preserves separate actions")
{
    const auto asset = Keire::AssetId::Generate();
    auto service = Keire::CreateRef<Keire::UndoService>();
    auto undo = service->CreateContext({.Name = "Variants"});
    std::vector<std::byte> source{std::byte{0}};
    bool available = true;
    bool fail = false;
    const auto apply = [&](std::span<const std::byte> bytes)
    {
        if (fail)
            throw std::runtime_error("Source write failed.");
        source.assign(bytes.begin(), bytes.end());
    };
    const auto edit = [&](std::byte value, std::uint64_t serial)
    {
        undo->Execute(
            KeireEditor::CreateMaterialVariantEdit(asset, source, {value}, serial, apply, [&] { return available; }));
    };
    edit(std::byte{1}, 0);
    edit(std::byte{2}, 0);
    CHECK(undo->UndoCount() == 1);
    edit(std::byte{3}, 1);
    CHECK(undo->UndoCount() == 2);
    REQUIRE(undo->Undo());
    CHECK(source == std::vector{std::byte{2}});
    REQUIRE(undo->Undo());
    CHECK(source == std::vector{std::byte{0}});
    REQUIRE(undo->Redo());
    CHECK(source == std::vector{std::byte{2}});
    fail = true;
    CHECK_THROWS_AS((void)undo->Undo(), std::runtime_error);
    CHECK(source == std::vector{std::byte{2}});
    CHECK(undo->UndoCount() == 1);
    fail = false;
    available = false;
    CHECK_FALSE(undo->CanUndo());
    available = true;
    REQUIRE(undo->Undo());
    CHECK(source == std::vector{std::byte{0}});
    service->Close();
    CHECK_FALSE(undo->IsOpen());
}

TEST_CASE("material variant undo never merges different sources or discontinuous revisions")
{
    const auto asset = Keire::AssetId::Generate();
    const auto apply = [](std::span<const std::byte>) {};
    auto first = KeireEditor::CreateMaterialVariantEdit(asset, {std::byte{0}}, {std::byte{1}}, 1, apply, {});
    auto different = KeireEditor::CreateMaterialVariantEdit(Keire::AssetId::Generate(), {std::byte{1}}, {std::byte{2}},
                                                            1, apply, {});
    CHECK_FALSE(first->TryMerge(*different));
    auto discontinuous = KeireEditor::CreateMaterialVariantEdit(asset, {std::byte{3}}, {std::byte{4}}, 1, apply, {});
    CHECK_FALSE(first->TryMerge(*discontinuous));
    auto boundary = KeireEditor::CreateMaterialVariantEdit(asset, {std::byte{1}}, {std::byte{2}}, 2, apply, {});
    CHECK_FALSE(first->TryMerge(*boundary));
    auto unavailable = KeireEditor::CreateMaterialVariantEdit(asset, {}, {}, 1, apply, []() -> bool
                                                              { throw std::runtime_error("Database closed."); });
    CHECK_FALSE(unavailable->Available());
}
