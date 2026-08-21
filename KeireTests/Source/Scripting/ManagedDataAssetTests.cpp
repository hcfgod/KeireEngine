#include "Keire/Scripting/ManagedDataAsset.h"

#include "Keire/Assets/AssetSystem.h"

#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>
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

TEST_CASE("managed data assets reject malformed duplicate and non-canonical source state")
{
    CHECK_THROWS_AS((void)Keire::ManagedDataAsset::Decode(Bytes("{}")), std::invalid_argument);
    CHECK_THROWS_AS(
        (void)Keire::ManagedDataAsset::Decode(Bytes(
            R"({"schemaVersion":2,"managedTypeId":"10000000-0000-4000-8000-000000000001","managedTypeName":"Example","fields":[],"dependencies":[]})")),
        std::invalid_argument);

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
