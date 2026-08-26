#include "KeireClient/Editor/ManagedDataDocument.h"
#include "KeireClient/Editor/ManagedDataInspectorPanel.h"
#include "KeireClient/Editor/ManagedReferenceGraphInspector.h"

#include "Keire/Audio/AudioAssets.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <cstddef>
#include <ranges>
#include <span>
#include <string>
#include <thread>
#include <vector>

namespace
{
    [[nodiscard]] Keire::AssetId Id(const std::uint64_t value) { return Keire::AssetId(0x4d414e4147454444ULL, value); }

    [[nodiscard]] Keire::ManagedAssetPropertyDescriptor Property(const std::uint64_t id, std::string name,
                                                                 const Keire::ManagedAssetPropertyKind kind,
                                                                 std::string managedType)
    {
        return {.StableFieldId = Id(id),
                .Name = std::move(name),
                .DisplayName = "Property " + std::to_string(id),
                .ManagedTypeName = std::move(managedType),
                .Kind = kind};
    }

    [[nodiscard]] Keire::ManagedAssetTypeDescriptor Descriptor()
    {
        Keire::ManagedAssetTypeDescriptor result;
        result.StableTypeId = Keire::ManagedTypeId(Id(100));
        result.FullName = "Tests.ManagedData";
        result.DisplayName = "Managed Data";
        result.MenuPath = "Tests/Managed Data";
        result.DefaultFileName = "ManagedData";
        result.Properties.push_back(Property(1, "Enabled", Keire::ManagedAssetPropertyKind::Boolean, "System.Boolean"));
        result.Properties.push_back(Property(2, "Mode", Keire::ManagedAssetPropertyKind::Enum, "Tests.Mode"));

        auto nested = Property(3, "Settings", Keire::ManagedAssetPropertyKind::SerializableObject, "Tests.Settings");
        nested.Children.push_back(Property(31, "Speed", Keire::ManagedAssetPropertyKind::Scalar, "System.Single"));
        nested.Children.push_back(Property(32, "Offset", Keire::ManagedAssetPropertyKind::Vector3, "Keire.Vector3"));
        nested.Children.push_back(Property(33, "Tint", Keire::ManagedAssetPropertyKind::Color, "Keire.Color"));
        result.Properties.push_back(std::move(nested));

        auto references = Property(4, "References", Keire::ManagedAssetPropertyKind::List,
                                   "System.Collections.Generic.List`1[Keire.AssetReference`1[Tests.BaseData]]");
        auto element = Property(41, "Element", Keire::ManagedAssetPropertyKind::AssetReference,
                                "Keire.AssetReference`1[Tests.BaseData]");
        element.ExpectedAssetType = Keire::ManagedDataAsset::StaticType();
        element.ExpectedManagedType = Keire::ManagedTypeId(Id(200));
        references.Children.push_back(std::move(element));
        result.Properties.push_back(std::move(references));

        auto points = Property(5, "Points", Keire::ManagedAssetPropertyKind::Array, "Keire.Vector2[]");
        points.Children.push_back(Property(51, "Element", Keire::ManagedAssetPropertyKind::Vector2, "Keire.Vector2"));
        result.Properties.push_back(std::move(points));
        Keire::ValidateManagedAssetTypeDescriptor(result);
        return result;
    }

    [[nodiscard]] Keire::ManagedDataDefinition Definition()
    {
        Keire::ManagedDataDefinition result;
        result.ManagedType = Keire::ManagedTypeId(Id(100));
        result.ManagedTypeName = "Tests.ManagedData";
        return result;
    }

    [[nodiscard]] Keire::ManagedAssetTypeDescriptor GraphDescriptor()
    {
        auto result = Descriptor();
        const auto nodeType = Keire::ManagedTypeId(Id(300));
        const auto leafType = Keire::ManagedTypeId(Id(305));
        auto root = Property(10, "Root", Keire::ManagedAssetPropertyKind::SerializableObject, "Tests.GraphNode");
        root.ReferenceGraph = true;
        root.ReferenceTypeChoices = {nodeType, leafType};
        auto label = Property(301, "Label", Keire::ManagedAssetPropertyKind::Text, "System.String");
        auto key = Property(303, "Key", Keire::ManagedAssetPropertyKind::Text, "System.String");
        auto value = Property(304, "Value", Keire::ManagedAssetPropertyKind::SerializableObject, "Tests.GraphNode");
        value.ReferenceGraph = true;
        value.ReferenceTypeChoices = {nodeType, leafType};
        auto links = Property(302, "Links", Keire::ManagedAssetPropertyKind::Dictionary,
                              "System.Collections.Generic.Dictionary`2[System.String,Tests.GraphNode]");
        links.ReferenceGraph = true;
        links.Children = {key, value};
        result.Properties.push_back(root);
        result.ReferenceTypes.push_back({.StableTypeId = nodeType,
                                         .FullName = "Tests.GraphNode",
                                         .DisplayName = "Graph Node",
                                         .Properties = {label, links}});
        result.ReferenceTypes.push_back(
            {.StableTypeId = leafType, .FullName = "Tests.GraphLeaf", .DisplayName = "Graph Leaf"});
        Keire::ValidateManagedAssetTypeDescriptor(result);
        return result;
    }

    [[nodiscard]] Keire::ManagedReferenceGraph GraphValue(const Keire::ManagedAssetTypeDescriptor& descriptor)
    {
        const auto& root = descriptor.Properties.back();
        const auto& type = descriptor.ReferenceTypes.front();
        const auto& label = type.Properties[0];
        const auto& links = type.Properties[1];
        Keire::ManagedReferenceGraph result;
        result.Root = {.Reference = 1};
        result.Objects = {{.Id = 1,
                           .Kind = Keire::ManagedReferenceGraphNodeKind::Object,
                           .RuntimeType = type.StableTypeId,
                           .Fields = {{label.StableFieldId, label.Name, {.Scalar = R"("valid")"}},
                                      {links.StableFieldId, links.Name, {.Reference = 2}}}},
                          {.Id = 2,
                           .Kind = Keire::ManagedReferenceGraphNodeKind::Dictionary,
                           .Entries = {{{.Scalar = R"("self")"}, {.Reference = 1}}}}};
        Keire::ValidateManagedReferenceGraph(result, root, descriptor.ReferenceTypes);
        return result;
    }
} // namespace

TEST_CASE("Managed data document authors supported values with undo persistence and dependencies")
{
    auto undoService = Keire::CreateRef<Keire::UndoService>();
    auto undo = undoService->CreateContext({.Name = "Managed Data"});
    std::vector<std::byte> persisted;
    Keire::ManagedDataDefinition preview;
    KeireEditor::ManagedDataDocument document(
        {.Preview = [&](const Keire::AssetId, const Keire::ManagedDataDefinition& definition) { preview = definition; },
         .Persist = [&](const Keire::AssetId, const std::span<const std::byte> bytes)
         { persisted.assign(bytes.begin(), bytes.end()); }});
    const auto descriptor = Descriptor();
    document.Open(Id(500), Definition(), 1, descriptor, undo);

    auto enabled = document.Property(descriptor.Properties[0]);
    CHECK_FALSE(enabled.Serialized);
    CHECK(std::holds_alternative<std::monostate>(enabled.Value.Value));
    enabled.Value = KeireEditor::ManagedDataDocument::MaterializedDefaultValue(descriptor.Properties[0]);
    std::get<bool>(enabled.Value.Value) = true;
    CHECK(document.SetProperty(descriptor.Properties[0], enabled.Value, "Enable"));
    CHECK(document.Dirty());
    CHECK(std::get<bool>(document.Property(descriptor.Properties[0]).Value.Value));

    auto mode = KeireEditor::ManagedDataDocument::MaterializedDefaultValue(descriptor.Properties[1]);
    mode.Value = std::int64_t{2};
    CHECK(document.SetProperty(descriptor.Properties[1], mode, "Set mode"));

    auto settings = KeireEditor::ManagedDataDocument::MaterializedDefaultValue(descriptor.Properties[2]);
    settings.Children[0] =
        KeireEditor::ManagedDataDocument::MaterializedDefaultValue(descriptor.Properties[2].Children[0]);
    settings.Children[0].Value = 4.5;
    settings.Children[1] =
        KeireEditor::ManagedDataDocument::MaterializedDefaultValue(descriptor.Properties[2].Children[1]);
    settings.Children[1].Value = Keire::Vector3{1.0F, 2.0F, 3.0F};
    settings.Children[2] =
        KeireEditor::ManagedDataDocument::MaterializedDefaultValue(descriptor.Properties[2].Children[2]);
    settings.Children[2].Value = Keire::Color{0.1F, 0.2F, 0.3F, 0.4F};
    CHECK(document.SetProperty(descriptor.Properties[2], settings, "Edit settings"));

    const auto referencedAsset = Id(700);
    auto references = KeireEditor::ManagedDataDocument::MaterializedDefaultValue(descriptor.Properties[3]);
    auto reference =
        KeireEditor::ManagedDataDocument::MaterializedDefaultValue(descriptor.Properties[3].Children.front());
    reference.Value = referencedAsset;
    references.Children.push_back(reference);
    CHECK(document.SetProperty(descriptor.Properties[3], references, "Add reference"));
    REQUIRE(document.Draft().Dependencies.size() == 1);
    CHECK(document.Draft().Dependencies.front().Asset == referencedAsset);
    CHECK(document.Draft().Dependencies.front().ManagedType == Keire::ManagedTypeId(Id(200)));

    auto points = KeireEditor::ManagedDataDocument::MaterializedDefaultValue(descriptor.Properties[4]);
    auto point = KeireEditor::ManagedDataDocument::MaterializedDefaultValue(descriptor.Properties[4].Children.front());
    point.Value = Keire::Vector2{8.0F, 9.0F};
    points.Children.push_back(point);
    CHECK(document.SetProperty(descriptor.Properties[4], points, "Add point"));

    CHECK(document.Undo());
    CHECK(document.Property(descriptor.Properties[4]).Serialized == false);
    CHECK(document.Redo());
    CHECK(document.Property(descriptor.Properties[4]).Value.Children.size() == 1);
    CHECK(preview == document.Draft());

    document.Save();
    CHECK_FALSE(document.Dirty());
    const auto decoded = Keire::ManagedDataAsset::Decode(persisted);
    CHECK(decoded->Definition() == document.Draft());

    CHECK(document.ClearProperty(descriptor.Properties[3]));
    CHECK(document.Draft().Dependencies.empty());
    document.Discard();
    CHECK(document.Draft().Dependencies.size() == 1);
    undoService->Close();
}

TEST_CASE("Managed data document preserves raw diagnostics and rejects stale reloads")
{
    const auto descriptor = Descriptor();
    auto definition = Definition();
    definition.Fields.push_back({.StableFieldId = descriptor.Properties[0].StableFieldId,
                                 .Name = descriptor.Properties[0].Name,
                                 .ManagedTypeName = "System.String",
                                 .Value = "\"legacy\""});
    definition = Keire::ManagedDataAsset::Canonicalize(std::move(definition));

    KeireEditor::ManagedDataDocument document(
        {.Persist = [](const Keire::AssetId, const std::span<const std::byte>) {}});
    document.Open(Id(501), definition, 1, descriptor);
    const auto state = document.Property(descriptor.Properties[0]);
    CHECK(state.Serialized);
    CHECK_FALSE(state.Diagnostic.empty());
    CHECK(state.RawValue == "\"legacy\"");

    auto candidate = Definition();
    CHECK(document.Reload(candidate, 2) == KeireEditor::AssetDocumentReloadResult::Applied);
    auto enabled = KeireEditor::ManagedDataDocument::MaterializedDefaultValue(descriptor.Properties[0]);
    enabled.Value = true;
    CHECK(document.SetProperty(descriptor.Properties[0], enabled));
    CHECK(document.Reload(Definition(), 3) == KeireEditor::AssetDocumentReloadResult::LocalChanges);

    document.Close();
    document.Open(Id(502), Definition(), 1);
    CHECK(document.Descriptor() == nullptr);
    CHECK_THROWS_AS(document.SetProperty(descriptor.Properties[0], enabled), std::logic_error);
}

TEST_CASE("Managed data document round-trips characters and nullable supported values")
{
    auto descriptor = Descriptor();
    descriptor.Properties.push_back(Property(6, "Key", Keire::ManagedAssetPropertyKind::Integer, "System.Char"));
    descriptor.Properties.push_back(Property(7, "Label", Keire::ManagedAssetPropertyKind::Text, "System.String"));
    auto nullableObject =
        Property(8, "OptionalSettings", Keire::ManagedAssetPropertyKind::SerializableObject, "Tests.Settings");
    nullableObject.Children.push_back(Property(81, "Speed", Keire::ManagedAssetPropertyKind::Scalar, "System.Single"));
    descriptor.Properties.push_back(std::move(nullableObject));
    auto nullableList = Property(9, "OptionalPoints", Keire::ManagedAssetPropertyKind::List,
                                 "System.Collections.Generic.List`1[Keire.Vector2]");
    nullableList.Children.push_back(Property(91, "Element", Keire::ManagedAssetPropertyKind::Vector2, "Keire.Vector2"));
    descriptor.Properties.push_back(std::move(nullableList));
    Keire::ValidateManagedAssetTypeDescriptor(descriptor);

    auto definition = Definition();
    definition.Fields = {
        {.StableFieldId = descriptor.Properties[5].StableFieldId,
         .Name = descriptor.Properties[5].Name,
         .ManagedTypeName = descriptor.Properties[5].ManagedTypeName,
         .Value = "\"K\""},
        {.StableFieldId = descriptor.Properties[6].StableFieldId,
         .Name = descriptor.Properties[6].Name,
         .ManagedTypeName = descriptor.Properties[6].ManagedTypeName,
         .Value = "null"},
        {.StableFieldId = descriptor.Properties[7].StableFieldId,
         .Name = descriptor.Properties[7].Name,
         .ManagedTypeName = descriptor.Properties[7].ManagedTypeName,
         .Value = "null"},
        {.StableFieldId = descriptor.Properties[8].StableFieldId,
         .Name = descriptor.Properties[8].Name,
         .ManagedTypeName = descriptor.Properties[8].ManagedTypeName,
         .Value = "null"},
    };
    definition = Keire::ManagedDataAsset::Canonicalize(std::move(definition));

    KeireEditor::ManagedDataDocument document(
        {.Persist = [](const Keire::AssetId, const std::span<const std::byte>) {}});
    document.Open(Id(503), definition, 1, descriptor);

    auto character = document.Property(descriptor.Properties[5]);
    CHECK(character.Diagnostic.empty());
    CHECK(std::get<std::string>(character.Value.Value) == "K");
    character.Value.Value = std::string("Q");
    CHECK(document.SetProperty(descriptor.Properties[5], character.Value, "Edit character"));
    CHECK(document.Property(descriptor.Properties[5]).RawValue == "\"Q\"");

    for (std::size_t index = 6; index <= 8; ++index)
    {
        const auto nullable = document.Property(descriptor.Properties[index]);
        CHECK(nullable.Diagnostic.empty());
        CHECK(nullable.Serialized);
        CHECK(std::holds_alternative<std::monostate>(nullable.Value.Value));
        CHECK(document.SetProperty(descriptor.Properties[index], nullable.Value, "Preserve null") == false);
        CHECK(document.Property(descriptor.Properties[index]).RawValue == "null");
    }
}

TEST_CASE("Managed data reference filtering enforces native and managed inheritance constraints")
{
    const auto descriptor = Descriptor();
    const auto& property = descriptor.Properties[3].Children.front();
    Keire::AssetSourceRecord record;
    record.Id = Id(800);
    record.Type = Keire::ManagedDataAsset::StaticType();

    Keire::ManagedAssetTypeDescriptor base;
    base.StableTypeId = Keire::ManagedTypeId(Id(200));
    base.FullName = "Tests.BaseData";
    base.DisplayName = "Base Data";
    Keire::ManagedAssetTypeDescriptor derived;
    derived.StableTypeId = Keire::ManagedTypeId(Id(201));
    derived.BaseTypeId = base.StableTypeId;
    derived.FullName = "Tests.DerivedData";
    derived.DisplayName = "Derived Data";
    const std::vector types{base, derived};

    auto target = Definition();
    target.ManagedType = derived.StableTypeId;
    target.ManagedTypeName = derived.FullName;
    CHECK(KeireEditor::ManagedDataDocument::AcceptsAssetReference(record, property, target, types));

    auto exact = property;
    exact.IncludeDerivedAssetTypes = false;
    CHECK_FALSE(KeireEditor::ManagedDataDocument::AcceptsAssetReference(record, exact, target, types));
    target.ManagedType = base.StableTypeId;
    CHECK(KeireEditor::ManagedDataDocument::AcceptsAssetReference(record, exact, target, types));

    record.Type = Keire::AudioClipAsset::StaticType();
    CHECK_FALSE(KeireEditor::ManagedDataDocument::AcceptsAssetReference(record, property, target, types));
}

TEST_CASE("Managed data graph edits validate atomically and use one undo record")
{
    const auto descriptor = GraphDescriptor();
    const auto& property = descriptor.Properties.back();
    const Keire::ManagedReferenceGraphDescriptor graphDescriptor{.Root = property, .Types = descriptor.ReferenceTypes};
    auto undoService = Keire::CreateRef<Keire::UndoService>();
    auto undo = undoService->CreateContext({.Name = "Managed Graph"});
    KeireEditor::ManagedDataDocument document(
        {.Persist = [](const Keire::AssetId, const std::span<const std::byte>) {}});
    document.Open(Id(504), Definition(), 1, descriptor, undo);
    KeireEditor::ManagedReferenceGraphEditController graphEdits;

    Keire::ManagedReferenceGraph created;
    const auto createdObject = KeireEditor::ManagedReferenceGraphActions::CreateObject(
        created.Root, created, property, graphDescriptor, descriptor.ReferenceTypes.front().StableTypeId);
    KeireEditor::ManagedReferenceGraphActions::Finalize(created, graphDescriptor);
    CHECK(created.Root.Reference == createdObject);
    CHECK(KeireEditor::ManagedDataInspectorPanel::ApplyReferenceGraphEdit(graphEdits, document, property, created,
                                                                          "Create graph object"));
    CHECK(document.GraphProperty(property).Value == created);
    CHECK(undo->UndoCount() == 1);

    auto valid = GraphValue(descriptor);
    CHECK(KeireEditor::ManagedDataInspectorPanel::ApplyReferenceGraphEdit(graphEdits, document, property, valid,
                                                                          "Link cyclic graph"));
    REQUIRE(document.Draft().Fields.size() == 1);
    CHECK(document.Draft().Fields.front().ReferenceGraph);
    CHECK(document.GraphProperty(property).Value == valid);
    CHECK(undo->UndoCount() == 2);

    auto noOp = valid;
    const auto beforeNoOp = noOp;
    CHECK_FALSE(KeireEditor::ManagedReferenceGraphActions::LinkValue(noOp.Root, noOp, property, valid.Root.Reference));
    CHECK(noOp == beforeNoOp);
    CHECK_THROWS_WITH_AS(
        static_cast<void>(KeireEditor::ManagedReferenceGraphActions::LinkValue(noOp.Root, noOp, property, 999)),
        "Managed reference graph link target #999 does not exist.", std::invalid_argument);
    CHECK(noOp == beforeNoOp);

    std::uint32_t focusedObject = 0;
    const auto beforeNavigation = document.GraphProperty(property).Value;
    const auto cycleTarget = beforeNavigation.Objects[1].Entries.front().Value.Reference;
    CHECK(KeireEditor::ManagedDataInspectorPanel::FocusReferenceGraphObject(graphEdits, focusedObject, beforeNavigation,
                                                                            cycleTarget));
    CHECK(focusedObject == cycleTarget);
    CHECK(document.GraphProperty(property).Value == beforeNavigation);
    CHECK(undo->UndoCount() == 2);
    CHECK_FALSE(KeireEditor::ManagedDataInspectorPanel::FocusReferenceGraphObject(graphEdits, focusedObject,
                                                                                  beforeNavigation, 999));
    CHECK(focusedObject == cycleTarget);

    auto failedConstruction = valid;
    const auto beforeFailedConstruction = failedConstruction;
    CHECK_THROWS_WITH_AS(
        static_cast<void>(KeireEditor::ManagedReferenceGraphActions::CreateObject(
            failedConstruction.Root, failedConstruction, property, graphDescriptor, Keire::ManagedTypeId(Id(999)))),
        "Managed reference type '4d414e41-4745-4444-0000-0000000003e7' is not registered for field 'Root'.",
        std::invalid_argument);
    CHECK(failedConstruction == beforeFailedConstruction);
    CHECK(document.GraphProperty(property).Value == valid);

    auto duplicate = valid;
    duplicate.Objects[1].Entries.push_back(duplicate.Objects[1].Entries.front());
    const auto draftBeforeFailure = document.Draft();
    CHECK_THROWS_WITH_AS(static_cast<void>(KeireEditor::ManagedDataInspectorPanel::ApplyReferenceGraphEdit(
                             graphEdits, document, property, duplicate, "Invalid duplicate")),
                         "KEIRE-MANAGED-SERIALIZATION-0003: Managed reference dictionary field 'Root.Links' declared "
                         "as 'System.Collections.Generic.Dictionary`2[System.String,Tests.GraphNode]' contains "
                         "duplicate key \"self\".",
                         std::invalid_argument);
    CHECK(document.GraphProperty(property).Value == valid);
    CHECK(document.Draft() == draftBeforeFailure);
    CHECK(undo->UndoCount() == 2);

    auto shared = valid;
    auto dictionary = std::ranges::find(shared.Objects, Keire::ManagedReferenceGraphNodeKind::Dictionary,
                                        &Keire::ManagedReferenceGraphNode::Kind);
    REQUIRE(dictionary != shared.Objects.end());
    const auto& links = descriptor.ReferenceTypes.front().Properties[1];
    KeireEditor::ManagedReferenceGraphActions::AddDictionaryEntry(*dictionary, links);
    dictionary->Entries.back().Key.Scalar = R"("shared")";
    CHECK(KeireEditor::ManagedReferenceGraphActions::LinkValue(dictionary->Entries.back().Value, shared,
                                                               links.Children[1], shared.Root.Reference));
    KeireEditor::ManagedReferenceGraphActions::Finalize(shared, graphDescriptor);
    dictionary = std::ranges::find(shared.Objects, Keire::ManagedReferenceGraphNodeKind::Dictionary,
                                   &Keire::ManagedReferenceGraphNode::Kind);
    REQUIRE(dictionary != shared.Objects.end());
    REQUIRE(dictionary->Entries.size() == 2);
    CHECK(dictionary->Entries[0].Value.Reference == dictionary->Entries[1].Value.Reference);
    CHECK(KeireEditor::ManagedDataInspectorPanel::ApplyReferenceGraphEdit(graphEdits, document, property, shared,
                                                                          "Share graph object"));
    CHECK(document.GraphProperty(property).Value == shared);
    CHECK(undo->UndoCount() == 3);

    auto switched = shared;
    const auto leaf = KeireEditor::ManagedReferenceGraphActions::CreateObject(
        switched.Root, switched, property, graphDescriptor, descriptor.ReferenceTypes[1].StableTypeId);
    KeireEditor::ManagedReferenceGraphActions::Finalize(switched, graphDescriptor);
    REQUIRE(switched.Objects.size() == 1);
    CHECK(switched.Root.Reference == leaf);
    CHECK(switched.Objects.front().RuntimeType == descriptor.ReferenceTypes[1].StableTypeId);
    CHECK(KeireEditor::ManagedDataInspectorPanel::ApplyReferenceGraphEdit(graphEdits, document, property, switched,
                                                                          "Switch graph type"));
    CHECK(document.GraphProperty(property).Value == switched);
    CHECK(undo->UndoCount() == 4);

    const auto beforeOffThread = document.Draft();
    std::string offThreadDiagnostic;
    std::thread rejected(
        [&]
        {
            try
            {
                (void)KeireEditor::ManagedDataInspectorPanel::ApplyReferenceGraphEdit(graphEdits, document, property,
                                                                                      valid, "Off-thread graph edit");
            }
            catch (const std::exception& error)
            {
                offThreadDiagnostic = error.what();
            }
        });
    rejected.join();
    CHECK(offThreadDiagnostic == "Managed reference graph Inspector edits require the owner thread.");
    CHECK(document.Draft() == beforeOffThread);
    CHECK(undo->UndoCount() == 4);

    CHECK(document.Undo());
    CHECK(document.GraphProperty(property).Value == shared);
    CHECK(document.Redo());
    CHECK(document.GraphProperty(property).Value == switched);
    undoService->Close();
}

TEST_CASE("Managed data graph diagnostics preserve structured context for the persistent Inspector")
{
    auto descriptor = GraphDescriptor();
    const auto property = descriptor.Properties.back();
    const auto validGraph = GraphValue(descriptor);

    auto validDefinition = Definition();
    validDefinition.Fields.push_back({.StableFieldId = property.StableFieldId,
                                      .Name = property.Name,
                                      .ManagedTypeName = property.ManagedTypeName,
                                      .ReferenceGraph = true,
                                      .Value = Keire::EncodeManagedReferenceGraph(validGraph)});
    validDefinition = Keire::ManagedDataAsset::Canonicalize(std::move(validDefinition));

    auto rejectedGraph = validGraph;
    rejectedGraph.Objects.front().RuntimeType = descriptor.ReferenceTypes.back().StableTypeId;
    descriptor.ReferenceTypes.pop_back();

    auto undoService = Keire::CreateRef<Keire::UndoService>();
    auto undo = undoService->CreateContext({.Name = "Managed Graph Diagnostic"});
    KeireEditor::ManagedDataDocument document(
        {.Persist = [](const Keire::AssetId, const std::span<const std::byte>) {}});
    document.Open(Id(505), validDefinition, 1, descriptor, undo);
    const auto beforeDraft = document.Draft();
    const auto beforeBaseline = document.Baseline();

    auto rejectedDefinition = validDefinition;
    auto& rejectedField = rejectedDefinition.Fields.back();
    auto sharedGraph = Keire::DecodeManagedReferenceGraph(rejectedDefinition.ReferenceGraph);
    Keire::UpdateManagedReferenceGraphRoot(sharedGraph, rejectedField.ReferenceGraphRoot, rejectedGraph);
    rejectedDefinition.ReferenceGraph = Keire::EncodeManagedReferenceGraph(sharedGraph);
    rejectedDefinition = Keire::ManagedDataAsset::Canonicalize(std::move(rejectedDefinition));

    CHECK_THROWS_AS(static_cast<void>(document.Reload(rejectedDefinition, 2)), Keire::ManagedSerializationError);
    CHECK(document.Draft() == beforeDraft);
    CHECK(document.Baseline() == beforeBaseline);
    CHECK(document.Revision() == 1);
    CHECK(undo->UndoCount() == 0);

    const auto state = document.GraphProperty(property);
    CHECK(state.Value == validGraph);
    REQUIRE(state.StructuredDiagnostic);
    CHECK(state.Diagnostic.find("uses an unregistered stable type ID") != std::string::npos);
    CHECK(state.StructuredDiagnostic->Code == "KEIRE-MANAGED-SERIALIZATION-0003");
    CHECK(state.StructuredDiagnostic->Phase == "validate");
    CHECK(state.StructuredDiagnostic->Owner == property.ManagedTypeName);
    CHECK(state.StructuredDiagnostic->RootField == property.Name);
    CHECK(state.StructuredDiagnostic->FieldPath == property.Name);
    CHECK(state.StructuredDiagnostic->DeclaredType == property.ManagedTypeName);
    CHECK(state.StructuredDiagnostic->RuntimeType == rejectedGraph.Objects.front().RuntimeType.ToString());
    CHECK(state.StructuredDiagnostic->SerializedTypeId == rejectedGraph.Objects.front().RuntimeType.ToString());
    CHECK(state.StructuredDiagnostic->ObjectId == rejectedGraph.Objects.front().Id);

    CHECK(document.Reload(validDefinition, 2) == KeireEditor::AssetDocumentReloadResult::Unchanged);
    CHECK(document.Revision() == 2);
    const auto recovered = document.GraphProperty(property);
    CHECK(recovered.Value == validGraph);
    CHECK(recovered.Diagnostic.empty());
    CHECK_FALSE(recovered.StructuredDiagnostic);
    CHECK(undo->UndoCount() == 0);
    undoService->Close();
}
