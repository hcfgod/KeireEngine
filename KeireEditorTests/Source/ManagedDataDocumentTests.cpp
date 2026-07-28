#include "KeireClient/Editor/ManagedDataDocument.h"

#include "Keire/Audio/AudioAssets.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <cstddef>
#include <span>
#include <string>
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
