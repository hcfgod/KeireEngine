#include "Keire/Core.h"

#include <doctest/doctest.h>

#include <cstring>
#include <string>
#include <unordered_map>

namespace
{
    [[nodiscard]] std::vector<std::byte> Bytes(const std::string& text)
    {
        std::vector<std::byte> result(text.size());
        std::memcpy(result.data(), text.data(), text.size());
        return result;
    }

    [[nodiscard]] Keire::SceneObjectDefinition Object(const Keire::AssetId id, std::string name,
                                                      const Keire::AssetId parent = {})
    {
        return {id, parent, std::move(name), true, {}};
    }
} // namespace

TEST_CASE("Scene schema v2 migrates to canonical v5 without prefab metadata")
{
    const auto id = Keire::AssetId::Parse("10000000-0000-4000-8000-000000000001");
    const auto source = std::string("{\"schemaVersion\":2,\"name\":\"Legacy\",\"entities\":[{") + "\"id\":\"" +
                        id.ToString() + "\",\"parent\":null,\"name\":\"Root\",\"active\":true,\"components\":[]}] }";
    const auto asset = Keire::SceneAsset::Decode(Bytes(source));
    CHECK(asset->Definition().SchemaVersion == Keire::CurrentSceneSchemaVersion);
    CHECK(asset->Definition().PrefabInstances.empty());
    CHECK(asset->Definition().PrefabOverrides.empty());

    const auto encoded = Keire::SceneAsset::Encode(asset->Definition());
    const std::string text(reinterpret_cast<const char*>(encoded.data()), encoded.size());
    CHECK(text.find("\"schemaVersion\": 5") != std::string::npos);
}

TEST_CASE("Prefab entity layer overrides round trip and compose")
{
    const auto prefabId = Keire::AssetId::Parse("20000000-0000-4000-8000-000000000011");
    const auto root = Keire::AssetId::Parse("20000000-0000-4000-8000-000000000012");
    Keire::PrefabDefinition definition;
    definition.Template = Keire::SceneAsset::EmptyDefinition("Layered Prefab");
    definition.Template.Objects.push_back(Object(root, "Root"));
    definition.Template.PrefabOverrides.push_back(
        {.Kind = Keire::PrefabOverrideKind::SetObjectLayer, .Object = root, .Layer = 12});

    const auto decoded = Keire::PrefabAsset::Decode(Keire::PrefabAsset::Encode(definition));
    REQUIRE(decoded->Definition().Template.PrefabOverrides.size() == 1);
    CHECK(decoded->Definition().Template.PrefabOverrides.front().Layer == 12);
    const auto composed =
        Keire::ComposePrefab(prefabId, [&](const Keire::AssetId id)
                             { return id == prefabId ? decoded : Keire::Ref<const Keire::PrefabAsset>{}; });
    REQUIRE(composed.Objects.size() == 1);
    CHECK(composed.Objects.front().Layer == 12);
}

TEST_CASE("Prefab source round trips variant and instance override metadata")
{
    const auto base = Keire::AssetId::Parse("20000000-0000-4000-8000-000000000001");
    const auto root = Keire::AssetId::Parse("20000000-0000-4000-8000-000000000002");
    const auto source = Keire::AssetId::Parse("20000000-0000-4000-8000-000000000003");

    Keire::PrefabDefinition definition;
    definition.BasePrefab = base;
    definition.Template = Keire::SceneAsset::EmptyDefinition("Variant");
    definition.Template.Objects.push_back(Object(root, "Nested Root"));
    Keire::PrefabInstanceDefinition instance;
    instance.Prefab = base;
    instance.Root = root;
    instance.Objects.push_back({source, root});
    Keire::PrefabOverrideDefinition overrideValue;
    overrideValue.Kind = Keire::PrefabOverrideKind::SetComponentProperty;
    overrideValue.Object = source;
    overrideValue.Component = Keire::MeshRendererComponent::StaticType();
    overrideValue.Property = "visible";
    overrideValue.Value = true;
    instance.Overrides.push_back(overrideValue);
    definition.Template.PrefabInstances.push_back(instance);

    const auto encoded = Keire::PrefabAsset::Encode(definition);
    const auto decoded = Keire::PrefabAsset::Decode(encoded);
    CHECK(decoded->Definition().BasePrefab == base);
    REQUIRE(decoded->Definition().Template.PrefabInstances.size() == 1);
    REQUIRE(decoded->Definition().Template.PrefabInstances.front().Overrides.size() == 1);
    const auto& roundTrip = decoded->Definition().Template.PrefabInstances.front().Overrides.front();
    CHECK(roundTrip.Property == "visible");
    CHECK(std::get<bool>(roundTrip.Value));
}

TEST_CASE("Prefab composition resolves variants, nesting, mappings, and cycles deterministically")
{
    const auto baseId = Keire::AssetId::Parse("30000000-0000-4000-8000-000000000001");
    const auto variantId = Keire::AssetId::Parse("30000000-0000-4000-8000-000000000002");
    const auto outerId = Keire::AssetId::Parse("30000000-0000-4000-8000-000000000003");
    const auto sourceRoot = Keire::AssetId::Parse("30000000-0000-4000-8000-000000000010");
    const auto instanceRoot = Keire::AssetId::Parse("30000000-0000-4000-8000-000000000020");

    Keire::PrefabDefinition base;
    base.Template = Keire::SceneAsset::EmptyDefinition("Base");
    base.Template.Objects.push_back(Object(sourceRoot, "Base Root"));

    Keire::PrefabDefinition variant;
    variant.BasePrefab = baseId;
    variant.Template = Keire::SceneAsset::EmptyDefinition("Variant");
    Keire::PrefabOverrideDefinition rename;
    rename.Kind = Keire::PrefabOverrideKind::RenameObject;
    rename.Object = sourceRoot;
    rename.Name = "Variant Root";
    variant.Template.PrefabOverrides.push_back(rename);

    Keire::PrefabDefinition outer;
    outer.Template = Keire::SceneAsset::EmptyDefinition("Outer");
    outer.Template.Objects.push_back(Object(instanceRoot, "Instance"));
    outer.Template.PrefabInstances.push_back({variantId, instanceRoot, {{sourceRoot, instanceRoot}}, {}});

    std::unordered_map<Keire::AssetId, Keire::Ref<const Keire::PrefabAsset>> assets;
    assets.emplace(baseId, Keire::CreateRef<Keire::PrefabAsset>(base));
    assets.emplace(variantId, Keire::CreateRef<Keire::PrefabAsset>(variant));
    assets.emplace(outerId, Keire::CreateRef<Keire::PrefabAsset>(outer));
    const auto resolver = [&](const Keire::AssetId id)
    {
        const auto found = assets.find(id);
        return found == assets.end() ? Keire::Ref<const Keire::PrefabAsset>{} : found->second;
    };

    const auto composed = Keire::ComposePrefab(outerId, resolver);
    REQUIRE(composed.Objects.size() == 1);
    CHECK(composed.Objects.front().Id == instanceRoot);
    CHECK(composed.Objects.front().Name == "Variant Root");
    CHECK(composed.PrefabInstances.empty());
    CHECK(composed.PrefabOverrides.empty());

    auto cycleA = variant;
    auto cycleB = variant;
    const auto cycleAId = Keire::AssetId::Parse("30000000-0000-4000-8000-000000000004");
    const auto cycleBId = Keire::AssetId::Parse("30000000-0000-4000-8000-000000000005");
    cycleA.BasePrefab = cycleBId;
    cycleB.BasePrefab = cycleAId;
    assets.emplace(cycleAId, Keire::CreateRef<Keire::PrefabAsset>(cycleA));
    assets.emplace(cycleBId, Keire::CreateRef<Keire::PrefabAsset>(cycleB));
    CHECK_THROWS_AS((void)Keire::ComposePrefab(cycleAId, resolver), std::invalid_argument);
}
