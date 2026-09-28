#include "Keire/Core.h"
#include "KeireTests/TestSupport.h"

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

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

TEST_CASE("Scene schema v2 migrates to canonical v6 without prefab metadata")
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
    CHECK(text.find("\"schemaVersion\": 6") != std::string::npos);
}

TEST_CASE("Prefab entity metadata overrides round trip and compose")
{
    const auto prefabId = Keire::AssetId::Parse("20000000-0000-4000-8000-000000000011");
    const auto root = Keire::AssetId::Parse("20000000-0000-4000-8000-000000000012");
    Keire::PrefabDefinition definition;
    definition.Template = Keire::SceneAsset::EmptyDefinition("Layered Prefab");
    definition.Template.Objects.push_back(Object(root, "Root"));
    definition.Template.PrefabOverrides.push_back(
        {.Kind = Keire::PrefabOverrideKind::SetObjectLayer, .Object = root, .Layer = 12});
    definition.Template.PrefabOverrides.push_back(
        {.Kind = Keire::PrefabOverrideKind::SetObjectTags, .Object = root, .Tags = {"Enemy", "Targetable"}});

    const auto decoded = Keire::PrefabAsset::Decode(Keire::PrefabAsset::Encode(definition));
    REQUIRE(decoded->Definition().Template.PrefabOverrides.size() == 2);
    CHECK(decoded->Definition().Template.PrefabOverrides.front().Layer == 12);
    CHECK(decoded->Definition().Template.PrefabOverrides.back().Tags ==
          std::vector<std::string>{"Enemy", "Targetable"});
    const auto composed =
        Keire::ComposePrefab(prefabId, [&](const Keire::AssetId id)
                             { return id == prefabId ? decoded : Keire::Ref<const Keire::PrefabAsset>{}; });
    REQUIRE(composed.Objects.size() == 1);
    CHECK(composed.Objects.front().Layer == 12);
    CHECK(composed.Objects.front().Tags == std::vector<std::string>{"Enemy", "Targetable"});
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

TEST_CASE("first person package prefab and input survive native decoding")
{
    const auto root = std::filesystem::path("Samples/KeireSandbox/Assets/FirstPersonController");
    const auto prefab = Keire::PrefabAsset::Decode(Bytes(KeireTests::ReadFile(root / "FirstPersonPlayer.keireprefab")));
    const auto& objects = prefab->Definition().Template.Objects;
    REQUIRE(objects.size() == 2);
    CHECK(objects[1].Parent == objects[0].Id);
    CHECK(objects[0].Components.size() == 2);
    CHECK(objects[1].Components.size() == 4);
    const auto controller = nlohmann::json::parse(objects[1].Components.back().Data);
    CHECK(controller.at("_walkSpeed").get<double>() == doctest::Approx(5.0));
    CHECK(controller.at("_sprintMultiplier").get<double>() == doctest::Approx(1.6));
    CHECK(controller.at("_jumpHeight").get<double>() == doctest::Approx(1.2));
    CHECK(controller.at("_gravity").get<double>() == doctest::Approx(24.0));
    CHECK(controller.at("_mouseSensitivity").get<double>() == doctest::Approx(0.12));
    CHECK(controller.at("_gamepadLookSpeed").get<double>() == doctest::Approx(150.0));
    CHECK_FALSE(controller.at("_invertY").get<bool>());
    const auto input =
        Keire::InputActionAsset::Decode(Bytes(KeireTests::ReadFile(root / "FirstPersonInput.keireinput")));
    REQUIRE(input);
    const auto assembly =
        Keire::ManagedAssemblyAsset::Decode(Bytes(KeireTests::ReadFile(root / "FirstPerson.keireasm")));
    CHECK(assembly->Definition().Name == "KeireFirstPerson");
}
