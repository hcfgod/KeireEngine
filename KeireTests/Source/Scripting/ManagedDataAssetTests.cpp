#include "Keire/Scripting/ManagedDataAsset.h"

#include "Keire/Assets/AssetSystem.h"

#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace
{
    [[nodiscard]] std::vector<std::byte> Bytes(const std::string_view text)
    {
        std::vector<std::byte> result(text.size());
        for (std::size_t index = 0; index < text.size(); ++index)
            result[index] = static_cast<std::byte>(text[index]);
        return result;
    }

    [[nodiscard]] std::string RepeatUtf8(const std::string_view value, const std::size_t count)
    {
        std::string result;
        result.reserve(value.size() * count);
        for (std::size_t index = 0; index < count; ++index)
            result.append(value);
        return result;
    }

    [[nodiscard]] std::string Text(const std::span<const std::byte> bytes)
    {
        return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
    }

    [[nodiscard]] Keire::ManagedAssetPropertyDescriptor NestedArrayProperty(const std::size_t levels)
    {
        Keire::ManagedAssetPropertyDescriptor property{.StableFieldId = Keire::AssetId(0x2200000000004000ULL, 1),
                                                       .Name = "Value",
                                                       .DisplayName = "Value",
                                                       .ManagedTypeName = "System.Int32",
                                                       .Kind = Keire::ManagedAssetPropertyKind::Integer};
        for (std::size_t level = 0; level < levels; ++level)
        {
            property = {.StableFieldId = Keire::AssetId(0x2200000000004000ULL, level + 2),
                        .Name = "Values",
                        .DisplayName = "Values",
                        .ManagedTypeName = "System.Array",
                        .Kind = Keire::ManagedAssetPropertyKind::Array,
                        .Children = {std::move(property)}};
        }
        return property;
    }

    [[nodiscard]] std::string NestedArrayValue(const std::size_t levels)
    {
        std::string result = "7";
        for (std::size_t level = 0; level < levels; ++level)
            result = '[' + result + ']';
        return result;
    }

    [[nodiscard]] Keire::ManagedDataDefinition Definition()
    {
        Keire::ManagedDataDefinition result;
        result.ManagedType = Keire::ManagedTypeId::Parse("10000000-0000-4000-8000-000000000001");
        result.ManagedTypeName = "Example.InventoryDefinition";
        result.Fields = {{.StableFieldId = Keire::AssetId::Parse("20000000-0000-4000-8000-000000000002"),
                          .Name = "Capacity",
                          .ManagedTypeName = "System.Int32",
                          .Value = "32"},
                         {.StableFieldId = Keire::AssetId::Parse("20000000-0000-4000-8000-000000000001"),
                          .Name = "Presentation",
                          .ManagedTypeName = "Example.Presentation",
                          .FormerNames = {"Visual", "Appearance"},
                          .Value = R"({"z":1,"a":[true,false]})"}};
        result.Dependencies = {{.Asset = Keire::AssetId::Parse("30000000-0000-4000-8000-000000000002"),
                                .AssetType = Keire::AssetTypeId::Parse("40000000-0000-4000-8000-000000000002")},
                               {.Asset = Keire::AssetId::Parse("30000000-0000-4000-8000-000000000001"),
                                .AssetType = Keire::ManagedDataAsset::StaticType(),
                                .ManagedType = Keire::ManagedTypeId::Parse("50000000-0000-4000-8000-000000000001")}};
        return result;
    }
} // namespace

TEST_CASE("managed data assets round trip in canonical stable-ID order")
{
    const auto encoded = Keire::ManagedDataAsset::Encode(Definition());
    const auto decoded = Keire::ManagedDataAsset::Decode(encoded);
    const auto& definition = decoded->Definition();

    REQUIRE(definition.Fields.size() == 2);
    CHECK(definition.Fields[0].Name == "Presentation");
    CHECK(definition.Fields[0].FormerNames == std::vector<std::string>{"Appearance", "Visual"});
    CHECK(definition.Fields[0].Value == R"({"a":[true,false],"z":1})");
    CHECK(definition.Fields[1].Name == "Capacity");
    REQUIRE(definition.Dependencies.size() == 2);
    CHECK(definition.Dependencies[0].Asset == Keire::AssetId::Parse("30000000-0000-4000-8000-000000000001"));
    CHECK(definition.Dependencies[0].ManagedType ==
          Keire::ManagedTypeId::Parse("50000000-0000-4000-8000-000000000001"));
    CHECK(Keire::ManagedDataAsset::Encode(definition) == encoded);

    const auto importer = Keire::CreateManagedDataAssetImporter();
    CHECK(importer.Name == "Keire.ManagedData");
    CHECK(importer.Type == Keire::ManagedDataAsset::StaticType());
    CHECK(importer.Extensions == std::vector<std::string>{".keiredata"});
    REQUIRE(importer.Import);
    REQUIRE(importer.ContextualImport);
    CHECK(importer.Import(encoded) == encoded);
    const auto output = importer.ContextualImport({}, encoded);
    CHECK(output.Bytes == encoded);
    CHECK(output.AssetDependencies ==
          std::vector<Keire::AssetId>{Keire::AssetId::Parse("30000000-0000-4000-8000-000000000001"),
                                      Keire::AssetId::Parse("30000000-0000-4000-8000-000000000002")});

    const auto decoder = Keire::CreateManagedDataAssetDecoder();
    CHECK(decoder.Type == Keire::ManagedDataAsset::StaticType());
    REQUIRE(decoder.Fallback);
    REQUIRE(decoder.Decode);
    CHECK(decoder.Decode(encoded)->Type() == Keire::ManagedDataAsset::StaticType());
}

TEST_CASE("managed data dictionaries preserve nested values in canonical key order")
{
    Keire::ManagedAssetPropertyDescriptor element{.StableFieldId =
                                                      Keire::AssetId::Parse("22000000-0000-4000-8000-000000000004"),
                                                  .Name = "Element",
                                                  .DisplayName = "Element",
                                                  .ManagedTypeName = "System.Int32",
                                                  .Kind = Keire::ManagedAssetPropertyKind::Integer};
    Keire::ManagedAssetPropertyDescriptor value{.StableFieldId =
                                                    Keire::AssetId::Parse("22000000-0000-4000-8000-000000000003"),
                                                .Name = "Value",
                                                .DisplayName = "Value",
                                                .ManagedTypeName = "System.Collections.Generic.List`1[System.Int32]",
                                                .Kind = Keire::ManagedAssetPropertyKind::List,
                                                .Children = {element}};
    Keire::ManagedAssetPropertyDescriptor key{.StableFieldId =
                                                  Keire::AssetId::Parse("22000000-0000-4000-8000-000000000002"),
                                              .Name = "Key",
                                              .DisplayName = "Key",
                                              .ManagedTypeName = "System.String",
                                              .Kind = Keire::ManagedAssetPropertyKind::Text};
    Keire::ManagedAssetPropertyDescriptor dictionary{
        .StableFieldId = Keire::AssetId::Parse("22000000-0000-4000-8000-000000000001"),
        .Name = "Values",
        .DisplayName = "Values",
        .ManagedTypeName = "System.Collections.Generic.Dictionary`2[System.String,System.Collections.Generic.List`1]",
        .Kind = Keire::ManagedAssetPropertyKind::Dictionary,
        .Children = {key, value}};

    Keire::ManagedAssetTypeDescriptor descriptor;
    descriptor.StableTypeId = Keire::ManagedTypeId::Parse("12000000-0000-4000-8000-000000000001");
    descriptor.FullName = "Example.DictionaryAsset";
    descriptor.DisplayName = "Dictionary Asset";
    descriptor.DefaultFileName = "DictionaryAsset";
    descriptor.Properties = {dictionary};
    CHECK_NOTHROW(Keire::ValidateManagedAssetTypeDescriptor(descriptor));

    auto decoded =
        Keire::DecodeManagedAssetValue(R"([{"key":"zeta","value":[9]},{"key":"alpha","value":[1,2]}])", dictionary);
    REQUIRE(decoded.Children.size() == 2);
    CHECK(Keire::EncodeManagedAssetValue(decoded, dictionary) ==
          R"([{"key":"alpha","value":[1,2]},{"key":"zeta","value":[9]}])");

    decoded.Children.push_back(decoded.Children.front());
    CHECK_THROWS_AS((void)Keire::EncodeManagedAssetValue(decoded, dictionary), std::invalid_argument);
}

TEST_CASE("managed custom values preserve bounded canonical codec records")
{
    const auto codec = Keire::ManagedTypeId::Parse("62000000-0000-4000-8000-000000000001");
    Keire::ManagedAssetPropertyDescriptor property{.StableFieldId =
                                                       Keire::AssetId::Parse("22000000-0000-4000-8000-000000000021"),
                                                   .Name = "Duration",
                                                   .DisplayName = "Duration",
                                                   .ManagedTypeName = "Example.Duration",
                                                   .Kind = Keire::ManagedAssetPropertyKind::CustomValue,
                                                   .CustomValueTypeId = codec,
                                                   .CustomValueVersion = 3};

    const auto decoded = Keire::DecodeManagedAssetValue(
        R"({"version":2,"payload":{"seconds":1.5,"labels":["a","b"]},"$custom":"62000000-0000-4000-8000-000000000001"})",
        property);
    CHECK(
        Keire::EncodeManagedAssetValue(decoded, property) ==
        R"({"$custom":"62000000-0000-4000-8000-000000000001","payload":{"labels":["a","b"],"seconds":1.5},"version":2})");

    CHECK_THROWS_AS((void)Keire::DecodeManagedAssetValue(
                        R"({"$custom":"62000000-0000-4000-8000-000000000002","version":2,"payload":null})", property),
                    std::invalid_argument);
    CHECK_THROWS_AS((void)Keire::DecodeManagedAssetValue(
                        R"({"$custom":"62000000-0000-4000-8000-000000000001","version":4,"payload":null})", property),
                    std::invalid_argument);
}

TEST_CASE("managed data string limits count UTF-8 bytes")
{
    Keire::ManagedAssetPropertyDescriptor text{.StableFieldId =
                                                   Keire::AssetId::Parse("22000000-0000-4000-8000-000000000011"),
                                               .Name = "Message",
                                               .DisplayName = "Message",
                                               .ManagedTypeName = "System.String",
                                               .Kind = Keire::ManagedAssetPropertyKind::Text};

    Keire::ManagedAssetValueNode value{.StableFieldId = text.StableFieldId,
                                       .Kind = Keire::ManagedAssetPropertyKind::Text,
                                       .Value = RepeatUtf8("\xC3\xA9", 524'288)};
    const auto exactTwoByte = Keire::EncodeManagedAssetValue(value, text);
    const auto decodedTwoByte = Keire::DecodeManagedAssetValue(exactTwoByte, text);
    REQUIRE(std::holds_alternative<std::string>(decodedTwoByte.Value));
    CHECK(std::get<std::string>(decodedTwoByte.Value).size() == 1'048'576);

    value.Value = RepeatUtf8("\xF0\x9F\x98\x80", 262'144);
    const auto exactSurrogatePair = Keire::EncodeManagedAssetValue(value, text);
    const auto decodedSurrogatePair = Keire::DecodeManagedAssetValue(exactSurrogatePair, text);
    REQUIRE(std::holds_alternative<std::string>(decodedSurrogatePair.Value));
    CHECK(std::get<std::string>(decodedSurrogatePair.Value).size() == 1'048'576);

    std::get<std::string>(value.Value).append("\xF0\x9F\x98\x80");
    CHECK_THROWS_WITH_AS(
        (void)Keire::EncodeManagedAssetValue(value, text),
        "KEIRE-MANAGED-SERIALIZATION-0003: Managed string field 'Message' declared as 'System.String' exceeds the "
        "1,048,576 UTF-8 byte limit.",
        std::invalid_argument);
}

TEST_CASE("managed reference graph values preserve cycles links and reject duplicate dictionary keys")
{
    const auto runtimeType = Keire::ManagedTypeId::Parse("12000000-0000-4000-8000-000000000101");
    auto text = Keire::ManagedAssetPropertyDescriptor{.StableFieldId =
                                                          Keire::AssetId::Parse("22000000-0000-4000-8000-000000000101"),
                                                      .Name = "Name",
                                                      .DisplayName = "Name",
                                                      .ManagedTypeName = "System.String",
                                                      .Kind = Keire::ManagedAssetPropertyKind::Text};
    auto next = Keire::ManagedAssetPropertyDescriptor{.StableFieldId =
                                                          Keire::AssetId::Parse("22000000-0000-4000-8000-000000000102"),
                                                      .Name = "Next",
                                                      .DisplayName = "Next",
                                                      .ManagedTypeName = "Tests.GraphNode",
                                                      .Kind = Keire::ManagedAssetPropertyKind::SerializableObject,
                                                      .ReferenceGraph = true,
                                                      .ReferenceTypeChoices = {runtimeType}};
    auto key = Keire::ManagedAssetPropertyDescriptor{.StableFieldId =
                                                         Keire::AssetId::Parse("22000000-0000-4000-8000-000000000104"),
                                                     .Name = "Key",
                                                     .DisplayName = "Key",
                                                     .ManagedTypeName = "System.String",
                                                     .Kind = Keire::ManagedAssetPropertyKind::Text};
    auto value = next;
    value.StableFieldId = Keire::AssetId::Parse("22000000-0000-4000-8000-000000000105");
    value.Name = "Value";
    value.DisplayName = "Value";
    auto dictionary = Keire::ManagedAssetPropertyDescriptor{
        .StableFieldId = Keire::AssetId::Parse("22000000-0000-4000-8000-000000000103"),
        .Name = "Links",
        .DisplayName = "Links",
        .ManagedTypeName = "System.Collections.Generic.Dictionary`2[System.String,Tests.GraphNode]",
        .Kind = Keire::ManagedAssetPropertyKind::Dictionary,
        .Children = {key, value},
        .ReferenceGraph = true};
    Keire::ManagedAssetReferenceTypeDescriptor nodeType{.StableTypeId = runtimeType,
                                                        .FullName = "Tests.GraphNode",
                                                        .DisplayName = "Graph Node",
                                                        .Properties = {text, next, dictionary}};
    auto root = next;
    root.StableFieldId = Keire::AssetId::Parse("22000000-0000-4000-8000-000000000100");
    root.Name = "Root";
    root.DisplayName = "Root";

    Keire::ManagedReferenceGraph graph;
    graph.Root = {.Reference = 1};
    graph.Objects = {{.Id = 1,
                      .Kind = Keire::ManagedReferenceGraphNodeKind::Object,
                      .RuntimeType = runtimeType,
                      .Fields = {{text.StableFieldId, text.Name, {.Scalar = R"("root")"}},
                                 {next.StableFieldId, next.Name, {.Reference = 1}},
                                 {dictionary.StableFieldId, dictionary.Name, {.Reference = 2}}}},
                     {.Id = 2,
                      .Kind = Keire::ManagedReferenceGraphNodeKind::Dictionary,
                      .Entries = {{{.Scalar = R"("self")"}, {.Reference = 1}}}}};
    CHECK_NOTHROW(Keire::ValidateManagedReferenceGraph(graph, root, std::span(&nodeType, 1)));
    const auto encoded = Keire::EncodeManagedReferenceGraph(graph);
    CHECK(Keire::DecodeManagedReferenceGraph(encoded) == graph);

    graph.Objects[1].Entries.push_back(graph.Objects[1].Entries.front());
    // Serialized object fields validate in deterministic field order. The self-linked Next field is visited before
    // Links, so the first actionable route to this shared dictionary is the cycle-safe Root.Next.Links path.
    CHECK_THROWS_WITH_AS(Keire::ValidateManagedReferenceGraph(graph, root, std::span(&nodeType, 1)),
                         "KEIRE-MANAGED-SERIALIZATION-0003: Managed reference dictionary field 'Root.Next.Links' "
                         "declared as 'System.Collections.Generic.Dictionary`2[System.String,Tests.GraphNode]' "
                         "contains duplicate key \"self\".",
                         std::invalid_argument);
}

TEST_CASE("managed reference graph document roots preserve cross-field sharing and prune transactionally")
{
    Keire::ManagedReferenceGraph shared{.Version = 2,
                                        .Roots = {{"id:first", {.Reference = 1}}, {"id:second", {.Reference = 1}}},
                                        .Objects = {{.Id = 1,
                                                     .Kind = Keire::ManagedReferenceGraphNodeKind::Object,
                                                     .Fields = {{{}, "Self", {.Reference = 1}}}}}};
    CHECK_NOTHROW(Keire::ValidateManagedReferenceGraphDocument(shared));
    const auto first = Keire::ExtractManagedReferenceGraphRoot(shared, "id:first");
    const auto second = Keire::ExtractManagedReferenceGraphRoot(shared, "id:second");
    CHECK(first.Root.Reference == second.Root.Reference);
    CHECK(first.Objects == second.Objects);
    CHECK(Keire::DecodeManagedReferenceGraph(Keire::EncodeManagedReferenceGraph(shared)) == shared);

    Keire::RemoveManagedReferenceGraphRoot(shared, "id:first");
    REQUIRE(shared.Roots.size() == 1);
    CHECK(shared.Objects.size() == 1);
    Keire::RemoveManagedReferenceGraphRoot(shared, "id:second");
    CHECK(shared.Roots.empty());
    CHECK(shared.Objects.empty());

    Keire::ManagedReferenceGraph destination{
        .Version = 2,
        .Roots = {{"retained", {.Reference = 1}}},
        .Objects = {{.Id = 1, .Kind = Keire::ManagedReferenceGraphNodeKind::Object}}};
    Keire::ManagedReferenceGraph replacement{
        .Root = {.Reference = 1},
        .Objects = {{.Id = 1, .Kind = Keire::ManagedReferenceGraphNodeKind::Object},
                    {.Id = 2, .Kind = Keire::ManagedReferenceGraphNodeKind::Object}}};
    replacement.Objects[0].Fields.push_back({{}, "Next", {.Reference = 2}});
    Keire::UpdateManagedReferenceGraphRoot(destination, "replacement", replacement);
    REQUIRE(destination.Roots.size() == 2);
    REQUIRE(destination.Objects.size() == 3);
    CHECK(destination.Roots[0].Key == "replacement");
    CHECK(destination.Roots[0].Value.Reference != destination.Roots[1].Value.Reference);
}

TEST_CASE("managed reference graph failures expose structured native diagnostics")
{
    try
    {
        (void)Keire::DecodeManagedReferenceGraph(
            R"({"Version":1,"Root":{"Reference":1,"Scalar":null},"Objects":[{"Id":1,"Kind":"object","StableTypeId":"not-a-guid","Fields":[],"Items":[],"Entries":[]}]})");
        FAIL("Malformed stable type IDs must be rejected.");
    }
    catch (const Keire::ManagedSerializationError& error)
    {
        CHECK(error.Details().Code == "KEIRE-MANAGED-SERIALIZATION-0003");
        CHECK(error.Details().Phase == "validate");
        CHECK(error.Details().RootField == "Root");
        CHECK(error.Details().FieldPath == "Objects[1].StableTypeId");
        CHECK(error.Details().SerializedTypeId == "not-a-guid");
        CHECK(error.Details().ObjectId == 1);
    }

    Keire::ManagedAssetPropertyDescriptor root{
        .StableFieldId = Keire::AssetId(0x2200000000004000ULL, 100),
        .Name = "Graph",
        .DisplayName = "Graph",
        .ManagedTypeName = "Tests.GraphNode",
        .Kind = Keire::ManagedAssetPropertyKind::SerializableObject,
        .ReferenceGraph = true,
        .ReferenceTypeChoices = {Keire::ManagedTypeId::Parse("12000000-0000-4000-8000-000000000101")}};
    Keire::ManagedReferenceGraph missing{.Root = {.Reference = 999}};
    try
    {
        Keire::ValidateManagedReferenceGraph(missing, root, {});
        FAIL("Dangling graph links must be rejected.");
    }
    catch (const Keire::ManagedSerializationError& error)
    {
        CHECK(error.Details().Phase == "validate");
        CHECK(error.Details().Owner == "Tests.GraphNode");
        CHECK(error.Details().RootField == "Graph");
        CHECK(error.Details().FieldPath == "Graph");
        CHECK(error.Details().DeclaredType == "Tests.GraphNode");
        CHECK(error.Details().ObjectId == 999);
    }
}

TEST_CASE("managed value depth accepts 32 nested collections and rejects 33")
{
    const auto exactProperty = NestedArrayProperty(32);
    const auto exactValue = NestedArrayValue(32);
    const auto decoded = Keire::DecodeManagedAssetValue(exactValue, exactProperty);
    CHECK(Keire::EncodeManagedAssetValue(decoded, exactProperty) == exactValue);

    const auto oversizedProperty = NestedArrayProperty(33);
    CHECK_THROWS_WITH_AS((void)Keire::DecodeManagedAssetValue(NestedArrayValue(33), oversizedProperty),
                         doctest::Contains("exceeds 32 nested levels"), std::invalid_argument);
}

TEST_CASE("managed data schema three stores one object table and migrates per-field schema one graphs")
{
    const auto graphField = Keire::AssetId::Parse("20000000-0000-4000-8000-000000000010");
    const auto sharedField = Keire::AssetId::Parse("20000000-0000-4000-8000-000000000011");
    Keire::ManagedReferenceGraph shared{.Version = 2,
                                        .Roots = {{"id:" + graphField.ToString(), {.Reference = 1}},
                                                  {"id:" + sharedField.ToString(), {.Reference = 1}}},
                                        .Objects = {{.Id = 1,
                                                     .Kind = Keire::ManagedReferenceGraphNodeKind::Object,
                                                     .Fields = {{{}, "Self", {.Reference = 1}}}}}};
    Keire::ManagedDataDefinition definition;
    definition.ManagedType = Keire::ManagedTypeId::Parse("10000000-0000-4000-8000-000000000001");
    definition.ManagedTypeName = "Example.SharedGraph";
    definition.ReferenceGraph = Keire::EncodeManagedReferenceGraph(shared);
    definition.Fields = {{.StableFieldId = graphField,
                          .Name = "Graph",
                          .ManagedTypeName = "Tests.GraphNode",
                          .ReferenceGraph = true,
                          .ReferenceGraphRoot = shared.Roots[0].Key},
                         {.StableFieldId = sharedField,
                          .Name = "Shared",
                          .ManagedTypeName = "Tests.GraphNode",
                          .ReferenceGraph = true,
                          .ReferenceGraphRoot = shared.Roots[1].Key}};
    const auto encoded = Keire::ManagedDataAsset::Encode(definition);
    const auto text = Text(encoded);
    CHECK(text.find(R"("schemaVersion": 4)") != std::string::npos);
    CHECK(text.find(R"("referenceGraph")") != std::string::npos);
    const auto roundTrip = Keire::ManagedDataAsset::Decode(encoded)->Definition();
    const auto roundTripGraph = Keire::DecodeManagedReferenceGraph(roundTrip.ReferenceGraph);
    REQUIRE(roundTripGraph.Roots.size() == 2);
    CHECK(roundTripGraph.Roots[0].Value.Reference == roundTripGraph.Roots[1].Value.Reference);

    const auto standalone = Keire::EncodeManagedReferenceGraph(
        Keire::ExtractManagedReferenceGraphRoot(roundTripGraph, roundTripGraph.Roots.front().Key));
    const std::string legacy =
        R"({"schemaVersion":1,"managedTypeId":"10000000-0000-4000-8000-000000000001","managedTypeName":"Example.SharedGraph","fields":[{"stableId":")" +
        graphField.ToString() +
        R"(","name":"Graph","managedTypeName":"Tests.GraphNode","formerNames":[],"referenceGraph":true,"value":)" +
        standalone + R"(}],"dependencies":[]})";
    const auto migrated = Keire::ManagedDataAsset::Decode(Bytes(legacy))->Definition();
    CHECK(migrated.SchemaVersion == 4);
    CHECK_FALSE(migrated.ReferenceGraph.empty());
    REQUIRE(migrated.Fields.size() == 1);
    CHECK_FALSE(migrated.Fields.front().ReferenceGraphRoot.empty());
}

TEST_CASE("managed data assets reject malformed duplicate and non-canonical source state")
{
    CHECK_THROWS_AS((void)Keire::ManagedDataAsset::Decode(Bytes("{}")), std::invalid_argument);
    CHECK(
        Keire::ManagedDataAsset::Decode(
            Bytes(
                R"({"schemaVersion":2,"managedTypeId":"10000000-0000-4000-8000-000000000001","managedTypeName":"Example","fields":[],"dependencies":[]})"))
            ->Definition()
            .SchemaVersion == 4);

    CHECK_THROWS_AS(
        (void)Keire::ManagedDataAsset::Decode(Bytes(
            R"({"schemaVersion":1,"managedTypeId":"10000000-0000-4000-8000-000000000001","managedTypeName":"Example","fields":[{"stableId":"20000000-0000-4000-8000-000000000002","name":"B","managedTypeName":"System.Int32","formerNames":[],"value":2},{"stableId":"20000000-0000-4000-8000-000000000001","name":"A","managedTypeName":"System.Int32","formerNames":[],"value":1}],"dependencies":[]})")),
        std::invalid_argument);
    CHECK_THROWS_AS(
        (void)Keire::ManagedDataAsset::Decode(Bytes(
            R"({"schemaVersion":1,"managedTypeId":"10000000-0000-4000-8000-000000000001","managedTypeName":"Example","fields":[{"stableId":"20000000-0000-4000-8000-000000000001","name":"A","managedTypeName":"System.Int32","formerNames":[],"value":1},{"stableId":"20000000-0000-4000-8000-000000000001","name":"B","managedTypeName":"System.Int32","formerNames":[],"value":2}],"dependencies":[]})")),
        std::invalid_argument);
    CHECK_THROWS_AS(
        (void)Keire::ManagedDataAsset::Decode(Bytes(
            R"({"schemaVersion":1,"managedTypeId":"10000000-0000-4000-8000-000000000001","managedTypeName":"Example","fields":[],"dependencies":[{"assetId":"30000000-0000-4000-8000-000000000002","assetTypeId":"40000000-0000-4000-8000-000000000001"},{"assetId":"30000000-0000-4000-8000-000000000001","assetTypeId":"40000000-0000-4000-8000-000000000001"}]})")),
        std::invalid_argument);
    CHECK_THROWS_AS(
        (void)Keire::ManagedDataAsset::Decode(Bytes(
            R"({"schemaVersion":1,"managedTypeId":"10000000-0000-4000-8000-000000000001","managedTypeName":"Example","fields":[],"dependencies":[{"assetId":"30000000-0000-4000-8000-000000000001","assetTypeId":"40000000-0000-4000-8000-000000000001"},{"assetId":"30000000-0000-4000-8000-000000000001","assetTypeId":"40000000-0000-4000-8000-000000000001"}]})")),
        std::invalid_argument);
    CHECK_THROWS_AS(
        (void)Keire::ManagedDataAsset::Decode(Bytes(
            R"({"schemaVersion":1,"managedTypeId":"10000000-0000-4000-8000-000000000001","managedTypeName":"Example","fields":[],"dependencies":[{"assetId":"30000000-0000-4000-8000-000000000001"}]})")),
        std::invalid_argument);

    auto invalidValue = Definition();
    invalidValue.Fields.front().Value = "{";
    CHECK_THROWS_AS((void)Keire::ManagedDataAsset::Encode(invalidValue), std::invalid_argument);
}

TEST_CASE("managed data reload keeps identity and last-good state transactionally")
{
    auto asset = Keire::CreateRef<Keire::ManagedDataAsset>(Definition());
    auto* const identity = asset.Get();
    const auto original = asset->Definition();
    const auto originalBytes = asset->ResidentBytes();
    const auto originalRevision = asset->Revision();

    const auto rejected = asset->TryReload(Bytes("{"));
    CHECK_FALSE(rejected.Applied);
    CHECK_FALSE(rejected.Diagnostic.empty());
    CHECK(rejected.Revision == originalRevision);
    CHECK(asset.Get() == identity);
    CHECK(asset->Definition() == original);
    CHECK(asset->ResidentBytes() == originalBytes);

    auto incompatible = Definition();
    incompatible.ManagedType = Keire::ManagedTypeId::Parse("10000000-0000-4000-8000-000000000002");
    const auto incompatibleResult = asset->TryReload(Keire::ManagedDataAsset::Encode(incompatible));
    CHECK_FALSE(incompatibleResult.Applied);
    CHECK(incompatibleResult.Revision == originalRevision);
    CHECK(asset->Definition() == original);

    auto replacement = Definition();
    replacement.ManagedTypeName = "Example.RenamedInventoryDefinition";
    replacement.Fields.front().Value = "64";
    const auto applied = asset->TryReload(Keire::ManagedDataAsset::Encode(replacement));
    CHECK(applied.Applied);
    CHECK(applied.Diagnostic.empty());
    CHECK(applied.Revision == originalRevision + 1);
    CHECK(asset.Get() == identity);
    CHECK(asset->Definition().ManagedTypeName == "Example.RenamedInventoryDefinition");
    CHECK(asset->Definition().Fields.back().Value == "64");
}

TEST_CASE("asset-system managed data publication preserves cached object identity")
{
    Keire::AssetSystemSpecification specification;
    specification.Mode = Keire::AssetMode::Development;
    specification.Decoders.push_back(Keire::CreateManagedDataAssetDecoder());
    auto assets = Keire::CreateRef<Keire::AssetSystem>(std::move(specification));
    const auto id = Keire::AssetId::Generate();

    REQUIRE(assets->PublishDevelopmentAsset(id, Keire::CreateRef<Keire::ManagedDataAsset>(Definition())));
    const auto handle = assets->Load<Keire::ManagedDataAsset>(id);
    const auto cached = handle.TryGetLoaded();
    REQUIRE(cached);
    const auto* const identity = cached.Get();
    const auto handleRevision = handle.Revision();

    auto replacement = Definition();
    replacement.Fields.front().Value = "96";
    REQUIRE(assets->PublishDevelopmentAsset(id, Keire::CreateRef<Keire::ManagedDataAsset>(std::move(replacement))));
    REQUIRE(handle.TryGetLoaded());
    CHECK(handle.TryGetLoaded().Get() == identity);
    CHECK(cached->Definition().Fields.back().Value == "96");
    CHECK(handle.Revision() == handleRevision + 1);

    auto incompatible = Definition();
    incompatible.ManagedType = Keire::ManagedTypeId::Parse("10000000-0000-4000-8000-000000000099");
    const auto revisionBeforeFailure = handle.Revision();
    CHECK_THROWS_AS(
        (void)assets->PublishDevelopmentAsset(id, Keire::CreateRef<Keire::ManagedDataAsset>(std::move(incompatible))),
        std::runtime_error);
    CHECK(handle.TryGetLoaded().Get() == identity);
    CHECK(cached->Definition().Fields.back().Value == "96");
    CHECK(handle.Revision() == revisionBeforeFailure);
    assets->Close();
}

TEST_CASE("managed asset type metadata validates property trees without editor implementation types")
{
    Keire::ManagedAssetTypeDescriptor descriptor;
    descriptor.StableTypeId = Keire::ManagedTypeId::Parse("60000000-0000-4000-8000-000000000001");
    descriptor.FullName = "Example.InventoryDefinition";
    descriptor.DisplayName = "Inventory Definition";
    descriptor.MenuPath = "Gameplay/Inventory";
    descriptor.DefaultFileName = "Inventory";
    descriptor.Properties = {{.StableFieldId = Keire::AssetId::Parse("61000000-0000-4000-8000-000000000001"),
                              .Name = "Capacity",
                              .DisplayName = "Capacity",
                              .ManagedTypeName = "System.Int32",
                              .Kind = Keire::ManagedAssetPropertyKind::Integer,
                              .Minimum = 1.0,
                              .Maximum = 256.0,
                              .Header = "Inventory Limits",
                              .Tooltip = "Maximum number of entries.",
                              .Step = 2.0,
                              .Slider = true},
                             {.StableFieldId = Keire::AssetId::Parse("61000000-0000-4000-8000-000000000002"),
                              .Name = "Icon",
                              .DisplayName = "Icon",
                              .ManagedTypeName = "Keire.AssetReference<Keire.Texture2D>",
                              .Kind = Keire::ManagedAssetPropertyKind::AssetReference,
                              .ExpectedAssetType = Keire::AssetTypeId::Parse("62000000-0000-4000-8000-000000000001")}};
    CHECK_NOTHROW(Keire::ValidateManagedAssetTypeDescriptor(descriptor));

    auto duplicate = descriptor;
    duplicate.Properties[1].StableFieldId = duplicate.Properties[0].StableFieldId;
    CHECK_THROWS_AS(Keire::ValidateManagedAssetTypeDescriptor(duplicate), std::invalid_argument);

    auto invalidMenu = descriptor;
    invalidMenu.MenuPath = "/Gameplay";
    CHECK_THROWS_AS(Keire::ValidateManagedAssetTypeDescriptor(invalidMenu), std::invalid_argument);

    auto invalidArray = descriptor;
    invalidArray.Properties[0].Kind = Keire::ManagedAssetPropertyKind::Array;
    invalidArray.Properties[0].Minimum.reset();
    invalidArray.Properties[0].Maximum.reset();
    CHECK_THROWS_AS(Keire::ValidateManagedAssetTypeDescriptor(invalidArray), std::invalid_argument);

    auto oneSidedBound = descriptor;
    oneSidedBound.Properties[0].Maximum.reset();
    oneSidedBound.Properties[0].Slider = false;
    CHECK_NOTHROW(Keire::ValidateManagedAssetTypeDescriptor(oneSidedBound));

    auto invalidSlider = descriptor;
    invalidSlider.Properties[0].Maximum.reset();
    CHECK_THROWS_AS(Keire::ValidateManagedAssetTypeDescriptor(invalidSlider), std::invalid_argument);

    auto invalidStep = descriptor;
    invalidStep.Properties[0].Step = 0.0;
    CHECK_THROWS_AS(Keire::ValidateManagedAssetTypeDescriptor(invalidStep), std::invalid_argument);

    auto invalidMultiline = descriptor;
    invalidMultiline.Properties[0].TextLines = 4;
    CHECK_THROWS_AS(Keire::ValidateManagedAssetTypeDescriptor(invalidMultiline), std::invalid_argument);
}

TEST_CASE("managed reference type metadata enforces 1024 fields per concrete type")
{
    Keire::ManagedAssetTypeDescriptor descriptor;
    descriptor.StableTypeId = Keire::ManagedTypeId::Parse("60000000-0000-4000-8000-000000000010");
    descriptor.FullName = "Example.GraphOwner";
    descriptor.DisplayName = "Graph Owner";
    Keire::ManagedAssetReferenceTypeDescriptor referenceType;
    referenceType.StableTypeId = Keire::ManagedTypeId::Parse("60000000-0000-4000-8000-000000000011");
    referenceType.FullName = "Example.GraphNode";
    referenceType.DisplayName = "Graph Node";
    referenceType.Properties.reserve(1'025);
    for (std::size_t index = 0; index < 1'024; ++index)
    {
        referenceType.Properties.push_back({.StableFieldId = Keire::AssetId(0x6100000000004000ULL, index + 1),
                                            .Name = "Field" + std::to_string(index),
                                            .DisplayName = "Field " + std::to_string(index),
                                            .ManagedTypeName = "System.Int32",
                                            .Kind = Keire::ManagedAssetPropertyKind::Integer});
    }
    descriptor.ReferenceTypes = {referenceType};
    CHECK_NOTHROW(Keire::ValidateManagedAssetTypeDescriptor(descriptor));

    descriptor.ReferenceTypes.front().Properties.push_back(
        {.StableFieldId = Keire::AssetId(0x6100000000004000ULL, 1'025),
         .Name = "Field1024",
         .DisplayName = "Field 1024",
         .ManagedTypeName = "System.Int32",
         .Kind = Keire::ManagedAssetPropertyKind::Integer});
    CHECK_THROWS_WITH_AS(Keire::ValidateManagedAssetTypeDescriptor(descriptor),
                         doctest::Contains("depth or property-count limit"), std::invalid_argument);
}

TEST_CASE("managed type catalogs round trip deterministically and reject incompatible metadata")
{
    Keire::ManagedAssetTypeDescriptor second;
    second.StableTypeId = Keire::ManagedTypeId::Parse("70000000-0000-4000-8000-000000000002");
    second.FullName = "Example.Zebra";
    second.DisplayName = "Zebra";

    Keire::ManagedAssetTypeDescriptor first;
    first.StableTypeId = Keire::ManagedTypeId::Parse("70000000-0000-4000-8000-000000000001");
    first.FullName = "Example.Alpha";
    first.DisplayName = "Alpha";
    first.Properties = {{.StableFieldId = Keire::AssetId::Parse("71000000-0000-4000-8000-000000000001"),
                         .Name = "Enabled",
                         .DisplayName = "Enabled",
                         .ManagedTypeName = "System.Boolean",
                         .Kind = Keire::ManagedAssetPropertyKind::Boolean}};

    const std::array source{second, first};
    const auto encoded = Keire::EncodeManagedAssetTypeCatalog(source);
    const auto decoded = Keire::DecodeManagedAssetTypeCatalog(encoded);
    REQUIRE(decoded.size() == 2);
    CHECK(decoded[0] == first);
    CHECK(decoded[1] == second);
    CHECK(Keire::EncodeManagedAssetTypeCatalog(decoded) == encoded);
    CHECK_THROWS_AS((void)Keire::DecodeManagedAssetTypeCatalog("{}"), std::invalid_argument);

    auto duplicate = second;
    duplicate.FullName = first.FullName;
    const std::array invalid{first, duplicate};
    CHECK_THROWS_AS((void)Keire::EncodeManagedAssetTypeCatalog(invalid), std::invalid_argument);
}

TEST_CASE("strict managed data validation uses stable fields and typed dependency constraints")
{
    const auto loadoutType = Keire::ManagedTypeId::Parse("72000000-0000-4000-8000-000000000001");
    const auto settingsBaseType = Keire::ManagedTypeId::Parse("72000000-0000-4000-8000-000000000002");
    const auto settingsType = Keire::ManagedTypeId::Parse("72000000-0000-4000-8000-000000000003");
    const auto countField = Keire::AssetId::Parse("73000000-0000-4000-8000-000000000001");
    const auto settingsField = Keire::AssetId::Parse("73000000-0000-4000-8000-000000000002");
    const auto loadoutAsset = Keire::AssetId::Parse("74000000-0000-4000-8000-000000000001");
    const auto settingsAsset = Keire::AssetId::Parse("74000000-0000-4000-8000-000000000002");

    Keire::ManagedAssetTypeDescriptor loadout;
    loadout.StableTypeId = loadoutType;
    loadout.FullName = "Example.Loadout";
    loadout.DisplayName = "Loadout";
    loadout.Properties = {{.StableFieldId = countField,
                           .Name = "Count",
                           .DisplayName = "Count",
                           .ManagedTypeName = "System.Int32",
                           .Kind = Keire::ManagedAssetPropertyKind::Integer},
                          {.StableFieldId = settingsField,
                           .Name = "Settings",
                           .DisplayName = "Settings",
                           .ManagedTypeName = "Keire.AssetReference`1[Example.SettingsBase]",
                           .Kind = Keire::ManagedAssetPropertyKind::AssetReference,
                           .ExpectedAssetType = Keire::ManagedDataAsset::StaticType(),
                           .ExpectedManagedType = settingsBaseType,
                           .IncludeDerivedAssetTypes = true}};
    Keire::ManagedAssetTypeDescriptor settings;
    settings.StableTypeId = settingsType;
    settings.FullName = "Example.Settings";
    settings.DisplayName = "Settings";
    settings.BaseTypeId = settingsBaseType;
    const std::array descriptors{loadout, settings};

    Keire::ManagedDataDefinition definition;
    definition.ManagedType = loadoutType;
    definition.ManagedTypeName = loadout.FullName;
    definition.Fields = {
        {.StableFieldId = countField, .Name = "Count", .ManagedTypeName = "System.Int32", .Value = "4"},
        {.StableFieldId = settingsField,
         .Name = "Settings",
         .ManagedTypeName = "Keire.AssetReference`1[Example.SettingsBase]",
         .Value = "null"}};
    // Use exact unsigned halves so the field value resolves to settingsAsset.
    definition.Fields[1].Value = R"({"Id":{"High":)" + std::to_string(settingsAsset.High()) + R"(,"Low":)" +
                                 std::to_string(settingsAsset.Low()) + "}}";
    definition.Dependencies = {{settingsAsset, Keire::ManagedDataAsset::StaticType(), settingsBaseType}};
    definition = Keire::ManagedDataAsset::Canonicalize(std::move(definition));
    const std::array assets{
        Keire::ManagedDataCookAsset{loadoutAsset, Keire::ManagedDataAsset::StaticType(), loadoutType},
        Keire::ManagedDataCookAsset{settingsAsset, Keire::ManagedDataAsset::StaticType(), settingsType}};
    CHECK_NOTHROW(Keire::ValidateManagedDataForCook(loadoutAsset, definition, descriptors, assets));

    auto missingType = definition;
    missingType.ManagedType = Keire::ManagedTypeId::Parse("72000000-0000-4000-8000-00000000ffff");
    CHECK_THROWS_WITH_AS(Keire::ValidateManagedDataForCook(loadoutAsset, missingType, descriptors, assets),
                         doctest::Contains("cannot resolve managed data type"), std::runtime_error);

    auto incompatibleField = definition;
    incompatibleField.Fields[0].ManagedTypeName = "System.String";
    CHECK_THROWS_WITH_AS(Keire::ValidateManagedDataForCook(loadoutAsset, incompatibleField, descriptors, assets),
                         doctest::Contains("type is incompatible"), std::runtime_error);

    auto malformedValue = definition;
    malformedValue.Fields[0].Value = R"("four")";
    CHECK_THROWS_WITH_AS(Keire::ValidateManagedDataForCook(loadoutAsset, malformedValue, descriptors, assets),
                         doctest::Contains("signed integer"), std::runtime_error);

    auto staleDependencies = definition;
    staleDependencies.Dependencies.clear();
    CHECK_THROWS_WITH_AS(Keire::ValidateManagedDataForCook(loadoutAsset, staleDependencies, descriptors, assets),
                         doctest::Contains("stale or incomplete"), std::runtime_error);

    auto exactOnly = loadout;
    exactOnly.Properties[1].IncludeDerivedAssetTypes = false;
    const std::array exactDescriptors{exactOnly, settings};
    CHECK_THROWS_WITH_AS(Keire::ValidateManagedDataForCook(loadoutAsset, definition, exactDescriptors, assets),
                         doctest::Contains("violates the typed reference constraint"), std::runtime_error);
}
