#include "KeireClient/Editor/ManagedReferenceGraphInspector.h"
#include "KeireClient/Editor/PropertyDrawerRegistry.h"

#include "Keire/Scripting/ManagedDataAsset.h"
#include "Keire/Scripting/ScriptSystem.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <limits>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>

namespace
{
    class GraphPropertyEditor final : public KeireEditor::IPropertyEditor
    {
      public:
        bool EditBoolean(std::string_view, bool&) override { return false; }
        bool EditInteger(std::string_view, std::int64_t&, double, std::optional<double>, std::optional<double>) override
        {
            return false;
        }
        bool EditChoice(std::string_view, std::int64_t&, std::span<const std::string_view>) override { return false; }
        bool EditScalar(std::string_view, double&, double, std::optional<double>, std::optional<double>) override
        {
            return false;
        }
        bool EditText(std::string_view, std::string&) override { return false; }
        bool EditVector2(std::string_view, Keire::Vector2&, double) override { return false; }
        bool EditVector3(std::string_view, Keire::Vector3&, double) override { return false; }
        bool EditVector4(std::string_view, Keire::Vector4&, double) override { return false; }
        bool EditQuaternion(std::string_view, Keire::Quaternion&, double) override { return false; }
        bool EditColor(std::string_view, Keire::Color&) override { return false; }
        bool EditAsset(std::string_view, Keire::AssetId&, std::optional<Keire::AssetTypeId>, std::string_view) override
        {
            return false;
        }
        bool EditEntity(std::string_view, Keire::EntityId&) override { return false; }
        bool EditEvent(std::string_view, Keire::ComponentEventValue&, std::size_t) override { return false; }
        bool EditManagedReferenceGraph(std::string_view, std::string& value,
                                       const Keire::ManagedReferenceGraphDescriptor& descriptor) override
        {
            TypeChoiceCount = descriptor.Root.ReferenceTypeChoices.size();
            if (CreateType)
            {
                auto graph = Keire::DecodeManagedReferenceGraph(value);
                (void)KeireEditor::ManagedReferenceGraphActions::CreateObject(graph.Root, graph, descriptor.Root,
                                                                              descriptor, *CreateType);
                KeireEditor::ManagedReferenceGraphActions::Finalize(graph, descriptor);
                value = Keire::EncodeManagedReferenceGraph(graph);
                return true;
            }
            if (NavigationTarget)
            {
                if (!Controller)
                    throw std::logic_error("The native Behaviour drawer did not bind its graph controller.");
                NavigationResult =
                    Controller->Focus(FocusedObject, Keire::DecodeManagedReferenceGraph(value), *NavigationTarget);
                if (NavigationOnly)
                    return false;
            }
            value = Replacement;
            return true;
        }
        bool EditManagedValue(std::string_view, std::string& value,
                              const Keire::ManagedAssetPropertyDescriptor&) override
        {
            value = Replacement;
            return true;
        }
        void SetManagedReferenceGraphEditController(
            const KeireEditor::ManagedReferenceGraphEditController* controller) noexcept override
        {
            Controller = controller;
            WasBound = WasBound || controller != nullptr;
        }

        std::string Replacement;
        const KeireEditor::ManagedReferenceGraphEditController* Controller = nullptr;
        std::optional<Keire::ManagedTypeId> CreateType;
        std::optional<std::uint32_t> NavigationTarget;
        std::uint32_t FocusedObject = 0;
        std::size_t TypeChoiceCount = 0;
        bool NavigationResult = false;
        bool NavigationOnly = false;
        bool WasBound = false;
    };

    class GraphComponent final : public Keire::Component
    {
      public:
        GraphComponent() : Component(StaticType()) {}

        [[nodiscard]] static constexpr Keire::ComponentTypeId StaticType() noexcept
        {
            return Keire::ComponentTypeId(Keire::AssetId(0xed17000000004000ULL, 0x8000000000000020ULL));
        }

        std::string Graph;
    };

    [[nodiscard]] std::shared_ptr<const Keire::ManagedReferenceGraphDescriptor> GraphDescriptor()
    {
        const auto nodeType = Keire::ManagedTypeId(Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000020"));
        const auto leafType = Keire::ManagedTypeId(Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000025"));
        Keire::ManagedAssetPropertyDescriptor key;
        key.StableFieldId = Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000021");
        key.Name = "Key";
        key.DisplayName = "Key";
        key.Kind = Keire::ManagedAssetPropertyKind::Text;
        key.ManagedTypeName = "System.String";

        Keire::ManagedAssetPropertyDescriptor value;
        value.StableFieldId = Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000022");
        value.Name = "Value";
        value.DisplayName = "Value";
        value.Kind = Keire::ManagedAssetPropertyKind::SerializableObject;
        value.ManagedTypeName = "Tests.GraphNode";
        value.ReferenceGraph = true;
        value.ReferenceTypeChoices = {nodeType, leafType};

        Keire::ManagedAssetPropertyDescriptor links;
        links.StableFieldId = Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000023");
        links.Name = "Links";
        links.DisplayName = "Links";
        links.Kind = Keire::ManagedAssetPropertyKind::Dictionary;
        links.ManagedTypeName = "System.Collections.Generic.Dictionary`2[System.String,Tests.GraphNode]";
        links.ReferenceGraph = true;
        links.Children = {key, value};

        auto result = std::make_shared<Keire::ManagedReferenceGraphDescriptor>();
        result->Root.StableFieldId = Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000024");
        result->Root.Name = "Graph";
        result->Root.DisplayName = "Graph";
        result->Root.Kind = Keire::ManagedAssetPropertyKind::SerializableObject;
        result->Root.ManagedTypeName = "Tests.GraphNode";
        result->Root.ReferenceGraph = true;
        result->Root.ReferenceTypeChoices = {nodeType, leafType};
        result->Types.push_back({.StableTypeId = nodeType,
                                 .FullName = "Tests.GraphNode",
                                 .DisplayName = "Graph Node",
                                 .Properties = {links}});
        result->Types.push_back({.StableTypeId = leafType, .FullName = "Tests.GraphLeaf", .DisplayName = "Graph Leaf"});
        return result;
    }

    [[nodiscard]] Keire::ManagedReferenceGraph GraphValue(const Keire::ManagedReferenceGraphDescriptor& descriptor,
                                                          const std::string_view key)
    {
        const auto& links = descriptor.Types.front().Properties.front();
        Keire::ManagedReferenceGraph result;
        const auto rootId = KeireEditor::ManagedReferenceGraphActions::CreateObject(
            result.Root, result, descriptor.Root, descriptor, descriptor.Types.front().StableTypeId);
        auto root = std::ranges::find(result.Objects, rootId, &Keire::ManagedReferenceGraphNode::Id);
        auto linksField =
            std::ranges::find(root->Fields, links.StableFieldId, &Keire::ManagedReferenceGraphField::StableFieldId);
        const auto dictionaryId =
            KeireEditor::ManagedReferenceGraphActions::CreateCollection(linksField->Value, result, links);
        auto dictionary = std::ranges::find(result.Objects, dictionaryId, &Keire::ManagedReferenceGraphNode::Id);
        for (const auto entryKey : {std::string(key), "shared-" + std::string(key)})
        {
            KeireEditor::ManagedReferenceGraphActions::AddDictionaryEntry(*dictionary, links);
            auto& entry = dictionary->Entries.back();
            entry.Key.Scalar = Keire::EncodeManagedAssetValue({.StableFieldId = links.Children.front().StableFieldId,
                                                               .Kind = Keire::ManagedAssetPropertyKind::Text,
                                                               .Value = entryKey},
                                                              links.Children.front());
            (void)KeireEditor::ManagedReferenceGraphActions::LinkValue(entry.Value, result, links.Children[1], rootId);
        }
        KeireEditor::ManagedReferenceGraphActions::Finalize(result, descriptor);
        return result;
    }

    [[nodiscard]] Keire::ComponentRegistration
    GraphRegistration(std::shared_ptr<const Keire::ManagedReferenceGraphDescriptor> descriptor)
    {
        Keire::ComponentProperty property;
        property.Key = "graph";
        property.DisplayName = "Graph";
        property.Kind = Keire::ComponentPropertyKind::ManagedReferenceGraph;
        property.ReferenceGraph = descriptor;

        Keire::ComponentRegistration result;
        result.Type = GraphComponent::StaticType();
        result.Name = "Graph Behaviour";
        result.Properties = {std::move(property)};
        result.Factory = [] { return Keire::Ref<Keire::Component>(Keire::CreateRef<GraphComponent>()); };
        result.Serialize = [](const Keire::Component& component)
        { return Keire::ComponentPropertyBag{{"graph", dynamic_cast<const GraphComponent&>(component).Graph}}; };
        result.Deserialize = [descriptor = std::move(descriptor)](Keire::Component& component,
                                                                  const Keire::ComponentPropertyBag& values,
                                                                  const std::uint32_t version)
        {
            if (version != 1)
                throw std::invalid_argument("Unsupported graph behaviour version.");
            const auto& encoded = std::get<std::string>(values.at("graph"));
            const auto graph = Keire::DecodeManagedReferenceGraph(encoded);
            Keire::ValidateManagedReferenceGraph(graph, descriptor->Root, descriptor->Types);
            dynamic_cast<GraphComponent&>(component).Graph = encoded;
        };
        return result;
    }

    [[nodiscard]] std::shared_ptr<const Keire::ManagedReferenceGraphDescriptor> DictionaryDescriptor()
    {
        Keire::ManagedAssetPropertyDescriptor nestedKey;
        nestedKey.StableFieldId = Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000031");
        nestedKey.Name = "Key";
        nestedKey.DisplayName = "Key";
        nestedKey.Kind = Keire::ManagedAssetPropertyKind::Text;
        nestedKey.ManagedTypeName = "System.String";

        Keire::ManagedAssetPropertyDescriptor nestedValue;
        nestedValue.StableFieldId = Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000032");
        nestedValue.Name = "Value";
        nestedValue.DisplayName = "Value";
        nestedValue.Kind = Keire::ManagedAssetPropertyKind::Integer;
        nestedValue.ManagedTypeName = "System.Int32";

        Keire::ManagedAssetPropertyDescriptor element;
        element.StableFieldId = Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000033");
        element.Name = "Element";
        element.DisplayName = "Element";
        element.Kind = Keire::ManagedAssetPropertyKind::Dictionary;
        element.ManagedTypeName = "System.Collections.Generic.Dictionary`2[System.String,System.Int32]";
        element.Children = {nestedKey, nestedValue};

        Keire::ManagedAssetPropertyDescriptor list;
        list.StableFieldId = Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000034");
        list.Name = "Value";
        list.DisplayName = "Value";
        list.Kind = Keire::ManagedAssetPropertyKind::List;
        list.ManagedTypeName = "System.Collections.Generic.List`1[System.Collections.Generic.Dictionary`2]";
        list.Children = {element};

        Keire::ManagedAssetPropertyDescriptor key = nestedKey;
        key.StableFieldId = Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000035");

        auto result = std::make_shared<Keire::ManagedReferenceGraphDescriptor>();
        result->Root.StableFieldId = Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000036");
        result->Root.Name = "Inventory";
        result->Root.DisplayName = "Inventory";
        result->Root.Kind = Keire::ManagedAssetPropertyKind::Dictionary;
        result->Root.ManagedTypeName =
            "System.Collections.Generic.Dictionary`2[System.String,System.Collections.Generic.List`1]";
        result->Root.Children = {key, list};
        return result;
    }

    [[nodiscard]] Keire::ComponentRegistration
    ManagedValueRegistration(std::shared_ptr<const Keire::ManagedReferenceGraphDescriptor> descriptor)
    {
        Keire::ComponentProperty property;
        property.Key = "inventory";
        property.DisplayName = "Inventory";
        property.Kind = Keire::ComponentPropertyKind::ManagedReferenceGraph;
        property.DeclaredManagedType = descriptor->Root.ManagedTypeName;
        property.ReferenceGraph = descriptor;

        Keire::ComponentRegistration result;
        result.Type = GraphComponent::StaticType();
        result.Name = "Dictionary Behaviour";
        result.Properties = {std::move(property)};
        result.Factory = [] { return Keire::Ref<Keire::Component>(Keire::CreateRef<GraphComponent>()); };
        result.Serialize = [](const Keire::Component& component)
        { return Keire::ComponentPropertyBag{{"inventory", dynamic_cast<const GraphComponent&>(component).Graph}}; };
        result.Deserialize = [descriptor = std::move(descriptor)](Keire::Component& component,
                                                                  const Keire::ComponentPropertyBag& values,
                                                                  const std::uint32_t version)
        {
            if (version != 1)
                throw std::invalid_argument("Unsupported dictionary behaviour version.");
            const auto& encoded = std::get<std::string>(values.at("inventory"));
            const auto value = Keire::DecodeManagedAssetValue(encoded, descriptor->Root);
            dynamic_cast<GraphComponent&>(component).Graph = Keire::EncodeManagedAssetValue(value, descriptor->Root);
        };
        return result;
    }

    [[nodiscard]] std::shared_ptr<const Keire::ManagedReferenceGraphDescriptor>
    WideGraphDescriptor(const std::size_t choiceCount)
    {
        auto result = std::make_shared<Keire::ManagedReferenceGraphDescriptor>();
        result->Root.StableFieldId = Keire::AssetId(0xed17000000004000ULL, 0x8000000000000040ULL);
        result->Root.Name = "Graph";
        result->Root.DisplayName = "Graph";
        result->Root.Kind = Keire::ManagedAssetPropertyKind::SerializableObject;
        result->Root.ManagedTypeName = "Tests.BoundaryGraphBase";
        result->Root.ReferenceGraph = true;
        result->Root.ReferenceTypeChoices.reserve(choiceCount);
        result->Types.reserve(choiceCount);
        for (std::size_t index = 0; index < choiceCount; ++index)
        {
            const auto type = Keire::ManagedTypeId(
                Keire::AssetId(0xed17000000004000ULL, 0x8000000000000100ULL + index));
            result->Root.ReferenceTypeChoices.push_back(type);
            result->Types.push_back({.StableTypeId = type,
                                     .FullName = "Tests.BoundaryType" + std::to_string(index),
                                     .DisplayName = "Boundary Type " + std::to_string(index)});
        }
        return result;
    }

    [[nodiscard]] Keire::ManagedAssetTypeDescriptor
    WideCatalogDescriptor(const Keire::ManagedReferenceGraphDescriptor& graph)
    {
        Keire::ManagedAssetTypeDescriptor result;
        result.StableTypeId =
            Keire::ManagedTypeId(Keire::AssetId(0xed17000000004000ULL, 0x8000000000000041ULL));
        result.FullName = "Tests.BoundaryGraphAsset";
        result.DisplayName = "Boundary Graph Asset";
        result.Properties = {graph.Root};
        result.ReferenceTypes = graph.Types;
        return result;
    }
} // namespace

TEST_CASE("managed reference graph IDs fill deterministic gaps even with the maximum ID present")
{
    const auto descriptor = GraphDescriptor();
    Keire::ManagedReferenceGraph graph;
    graph.Objects.push_back({.Id = 1, .Kind = Keire::ManagedReferenceGraphNodeKind::Object});
    graph.Objects.push_back(
        {.Id = std::numeric_limits<std::uint32_t>::max(), .Kind = Keire::ManagedReferenceGraphNodeKind::Object});

    const auto firstId = KeireEditor::ManagedReferenceGraphActions::CreateObject(
        graph.Root, graph, descriptor->Root, *descriptor, descriptor->Types.front().StableTypeId);
    CHECK(firstId == 2);

    graph.Objects.clear();
    graph.Objects.reserve(65'536);
    for (std::uint32_t id = 2; id <= 65'535; ++id)
        graph.Objects.push_back({.Id = id, .Kind = Keire::ManagedReferenceGraphNodeKind::Object});
    graph.Objects.push_back(
        {.Id = std::numeric_limits<std::uint32_t>::max(), .Kind = Keire::ManagedReferenceGraphNodeKind::Object});
    graph.Root = {};
    const auto gapId = KeireEditor::ManagedReferenceGraphActions::CreateObject(
        graph.Root, graph, descriptor->Root, *descriptor, descriptor->Types.front().StableTypeId);
    CHECK(gapId == 1);
    CHECK(graph.Objects.size() == 65'536);
}

TEST_CASE("native Behaviour graph drawer validates and commits atomically")
{
    const auto descriptor = GraphDescriptor();
    auto registration = GraphRegistration(descriptor);
    auto component = registration.Factory();
    const auto original = GraphValue(*descriptor, "self");
    registration.Deserialize(*component, {{"graph", Keire::EncodeManagedReferenceGraph(original)}},
                             registration.SchemaVersion);
    unsigned int commits = 0;
    const auto beforeNavigation = registration.Serialize(*component);
    const auto dictionary = std::ranges::find(original.Objects, Keire::ManagedReferenceGraphNodeKind::Dictionary,
                                              &Keire::ManagedReferenceGraphNode::Kind);
    REQUIRE(dictionary != original.Objects.end());
    REQUIRE(dictionary->Entries.size() == 2);
    CHECK(dictionary->Entries[0].Value.Reference == original.Root.Reference);
    CHECK(dictionary->Entries[1].Value.Reference == original.Root.Reference);
    const auto cycleTarget = dictionary->Entries.front().Value.Reference;

    auto noOp = original;
    const auto beforeNoOp = noOp;
    CHECK_FALSE(KeireEditor::ManagedReferenceGraphActions::LinkValue(noOp.Root, noOp, descriptor->Root,
                                                                     original.Root.Reference));
    CHECK(noOp == beforeNoOp);
    CHECK_THROWS_WITH_AS(
        static_cast<void>(KeireEditor::ManagedReferenceGraphActions::LinkValue(noOp.Root, noOp, descriptor->Root, 999)),
        "Managed reference graph link target #999 does not exist.", std::invalid_argument);
    CHECK(noOp == beforeNoOp);

    KeireEditor::PropertyDrawerRegistry drawers;
    GraphPropertyEditor editor;
    editor.NavigationTarget = cycleTarget;
    editor.NavigationOnly = true;
    CHECK_FALSE(drawers.EditComponent(editor, registration, *component, registration.Properties.front(),
                                      [&commits] { ++commits; }));
    CHECK(editor.WasBound);
    CHECK(editor.Controller == nullptr);
    CHECK(editor.NavigationResult);
    CHECK(editor.FocusedObject == cycleTarget);
    CHECK(registration.Serialize(*component) == beforeNavigation);
    CHECK(commits == 0);

    editor.NavigationTarget.reset();
    editor.NavigationOnly = false;
    const auto replacement = GraphValue(*descriptor, "replacement");
    editor.Replacement = Keire::EncodeManagedReferenceGraph(replacement);
    CHECK(drawers.EditComponent(editor, registration, *component, registration.Properties.front(),
                                [&commits] { ++commits; }));
    CHECK(editor.TypeChoiceCount == 2);
    CHECK(dynamic_cast<const GraphComponent&>(*component).Graph == editor.Replacement);
    CHECK(commits == 1);

    editor.CreateType = descriptor->Types[1].StableTypeId;
    CHECK(drawers.EditComponent(editor, registration, *component, registration.Properties.front(),
                                [&commits] { ++commits; }));
    const auto switched = Keire::DecodeManagedReferenceGraph(dynamic_cast<const GraphComponent&>(*component).Graph);
    REQUIRE(switched.Objects.size() == 1);
    CHECK(switched.Root.Reference == switched.Objects.front().Id);
    CHECK(switched.Objects.front().RuntimeType == descriptor->Types[1].StableTypeId);
    CHECK(commits == 2);

    editor.CreateType = Keire::ManagedTypeId(Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000099"));
    const auto beforeFailedConstruction = registration.Serialize(*component);
    CHECK_THROWS_WITH_AS(
        static_cast<void>(drawers.EditComponent(editor, registration, *component, registration.Properties.front(),
                                                [&commits] { ++commits; })),
        "Managed reference type 'ed170000-0000-4000-8000-000000000099' is not registered for field 'Graph'.",
        std::invalid_argument);
    CHECK(registration.Serialize(*component) == beforeFailedConstruction);
    CHECK(commits == 2);
    editor.CreateType.reset();

    registration.Deserialize(*component, {{"graph", Keire::EncodeManagedReferenceGraph(original)}},
                             registration.SchemaVersion);
    auto duplicate = original;
    auto duplicateDictionary = std::ranges::find(duplicate.Objects, Keire::ManagedReferenceGraphNodeKind::Dictionary,
                                                 &Keire::ManagedReferenceGraphNode::Kind);
    REQUIRE(duplicateDictionary != duplicate.Objects.end());
    duplicateDictionary->Entries.push_back(duplicateDictionary->Entries.front());
    editor.Replacement = Keire::EncodeManagedReferenceGraph(duplicate);
    CHECK_THROWS_WITH_AS(
        static_cast<void>(drawers.EditComponent(editor, registration, *component, registration.Properties.front(),
                                                [&commits] { ++commits; })),
        "KEIRE-MANAGED-SERIALIZATION-0003: Managed reference dictionary field 'Graph.Links' declared as "
        "'System.Collections.Generic.Dictionary`2[System.String,Tests.GraphNode]' contains duplicate key \"self\".",
        std::invalid_argument);
    CHECK(dynamic_cast<const GraphComponent&>(*component).Graph == Keire::EncodeManagedReferenceGraph(original));
    CHECK(commits == 2);

    const auto beforeOffThread = registration.Serialize(*component);
    std::string offThreadDiagnostic;
    std::thread rejected(
        [&]
        {
            try
            {
                (void)drawers.EditComponent(editor, registration, *component, registration.Properties.front(),
                                            [&commits] { ++commits; });
            }
            catch (const std::exception& error)
            {
                offThreadDiagnostic = error.what();
            }
        });
    rejected.join();
    CHECK(offThreadDiagnostic == "Managed reference graph Inspector edits require the owner thread.");
    CHECK(registration.Serialize(*component) == beforeOffThread);
    CHECK(commits == 2);
}

TEST_CASE("native Behaviour dictionary drawer supports nested collections and rejects duplicate keys atomically")
{
    const auto descriptor = DictionaryDescriptor();
    auto registration = ManagedValueRegistration(descriptor);
    auto component = registration.Factory();
    const std::string original = R"([{"key":"inventory","value":[[{"key":"count","value":1}]]}])";
    registration.Deserialize(*component, {{"inventory", original}}, registration.SchemaVersion);

    KeireEditor::PropertyDrawerRegistry drawers;
    GraphPropertyEditor editor;
    editor.Replacement = R"([{"key":"inventory","value":[[{"key":"count","value":2}]]}])";
    unsigned int commits = 0;
    CHECK(drawers.EditComponent(editor, registration, *component, registration.Properties.front(),
                                [&commits] { ++commits; }));
    CHECK(dynamic_cast<const GraphComponent&>(*component).Graph == editor.Replacement);
    CHECK(commits == 1);

    editor.Replacement = R"([{"key":"duplicate","value":[]},{"key":"duplicate","value":[]}])";
    CHECK_THROWS_WITH_AS(
        static_cast<void>(drawers.EditComponent(editor, registration, *component, registration.Properties.front(),
                                                [&commits] { ++commits; })),
        "KEIRE-MANAGED-SERIALIZATION-0003: Managed dictionary field 'Inventory' declared as "
        "'System.Collections.Generic.Dictionary`2[System.String,System.Collections.Generic.List`1]' contains "
        "duplicate key \"duplicate\".",
        std::invalid_argument);
    CHECK(dynamic_cast<const GraphComponent&>(*component).Graph ==
          R"([{"key":"inventory","value":[[{"key":"count","value":2}]]}])");
    CHECK(commits == 1);
}

TEST_CASE("managed serialization diagnostics retain structured Inspector context")
{
    const Keire::ManagedSerializationDiagnostic serialization{.Code = "KEIRE-MANAGED-SERIALIZATION-0003",
                                                              .Phase = "validate",
                                                              .Owner = "Tests.GraphBehaviour",
                                                              .RootField = "Graph",
                                                              .FieldPath = "Tests.GraphBehaviour.Graph.Links[1]",
                                                              .DeclaredType = "Tests.GraphNode",
                                                              .RuntimeType = "Tests.GraphLeaf",
                                                              .SerializedTypeId =
                                                                  "ed170000-0000-4000-8000-000000000025",
                                                              .ObjectId = 17};
    const auto formatted = KeireEditor::FormatManagedSerializationDiagnostic("Graph validation failed.", serialization);
    CHECK(formatted == "Graph validation failed.\n"
                       "Code: KEIRE-MANAGED-SERIALIZATION-0003\n"
                       "Phase: validate\n"
                       "Owner: Tests.GraphBehaviour\n"
                       "Root field: Graph\n"
                       "Field path: Tests.GraphBehaviour.Graph.Links[1]\n"
                       "Declared type: Tests.GraphNode\n"
                       "Runtime type: Tests.GraphLeaf\n"
                       "Serialized type ID: ed170000-0000-4000-8000-000000000025\n"
                       "Object ID: 17");

    const Keire::ManagedAssetTypeDiagnostic metadata{.TypeName = "Tests.GraphAsset",
                                                     .Message = "Metadata validation failed.",
                                                     .Code = serialization.Code,
                                                     .Phase = "metadata",
                                                     .Owner = "Tests.GraphAsset",
                                                     .RootField = "Root",
                                                     .FieldPath = "Tests.GraphAsset.Root",
                                                     .DeclaredType = "Tests.GraphNode",
                                                     .RuntimeType = "Tests.GraphLeaf",
                                                     .SerializedTypeId = serialization.SerializedTypeId,
                                                     .ObjectId = 23};
    const auto metadataFormatted = KeireEditor::FormatManagedAssetTypeDiagnostic(metadata);
    CHECK(metadataFormatted.find("Phase: metadata") != std::string::npos);
    CHECK(metadataFormatted.find("Owner: Tests.GraphAsset") != std::string::npos);
    CHECK(metadataFormatted.find("Root field: Root") != std::string::npos);
    CHECK(metadataFormatted.find("Object ID: 23") != std::string::npos);
}

TEST_CASE("managed reference slots accept 256 Inspector choices and reject the 257th atomically")
{
    const auto accepted = WideGraphDescriptor(256);
    const auto acceptedCatalog = WideCatalogDescriptor(*accepted);
    CHECK_NOTHROW(Keire::ValidateManagedAssetTypeDescriptor(acceptedCatalog));

    auto registration = GraphRegistration(accepted);
    auto component = registration.Factory();
    Keire::ManagedReferenceGraph graph;
    (void)KeireEditor::ManagedReferenceGraphActions::CreateObject(
        graph.Root, graph, accepted->Root, *accepted, accepted->Types.front().StableTypeId);
    KeireEditor::ManagedReferenceGraphActions::Finalize(graph, *accepted);
    const auto encoded = Keire::EncodeManagedReferenceGraph(graph);
    registration.Deserialize(*component, {{"graph", encoded}}, registration.SchemaVersion);
    const auto validState = registration.Serialize(*component);

    KeireEditor::PropertyDrawerRegistry drawers;
    GraphPropertyEditor editor;
    editor.Replacement = encoded;
    unsigned int commits = 0;
    CHECK(drawers.EditComponent(editor, registration, *component, registration.Properties.front(),
                                [&commits] { ++commits; }));
    CHECK(editor.TypeChoiceCount == 256);
    CHECK(registration.Serialize(*component) == validState);
    CHECK(commits == 1);

    const auto rejected = WideGraphDescriptor(257);
    const auto rejectedCatalog = WideCatalogDescriptor(*rejected);
    CHECK_THROWS_WITH_AS(Keire::ValidateManagedAssetTypeDescriptor(rejectedCatalog),
                         "Managed reference slots cannot expose more than 256 concrete types.", std::invalid_argument);
    CHECK(registration.Serialize(*component) == validState);
    CHECK(commits == 1);
}
