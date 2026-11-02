#include "KeireClient/Editor/AssetPicker.h"
#include "KeireClient/Editor/PostProcessDocument.h"
#include "KeireClientInternal/Editor/PostProcessAuthoring.h"
#include "KeireClientInternal/Editor/PostProcessCreation.h"

#include <doctest/doctest.h>

#include <array>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>

TEST_CASE("post process creation validates portable names before queuing an asset operation")
{
    for (const auto name : {"Post Processing Profile", "Evening Grade", "Grade.02", ".Look", "COM0", "LPT10"})
    {
        CAPTURE(name);
        CHECK(KeireEditor::Detail::PostProcessProfileNameError(name).empty());
    }
    for (const auto name :
         {"",          ".",          "..",        " ",      " Evening", "Evening ",  "Evening.", "../Grade",
          "Grade/Day", "Grade\\Day", "C:Grade",   "Grade*", "Grade?",   "Grade|",    "<Grade>",  "Grade\"",
          "Grade\t",   "Grade\n",    "CON",       "con",    "Con.look", "CON .look", "PRN",      "AUX",
          "NUL",       "COM1",       "com9.look", "LPT1",   "lpt9"})
    {
        CAPTURE(name);
        CHECK_FALSE(KeireEditor::Detail::PostProcessProfileNameError(name).empty());
    }
    CHECK_FALSE(KeireEditor::Detail::PostProcessProfileNameError(std::string("A\0B", 3)).empty());
    CHECK(KeireEditor::Detail::PostProcessProfileNameError("Grading \xC3\xA9").empty());
    CHECK(KeireEditor::Detail::PostProcessProfileNameError(std::string(230, 'A')).empty());
    CHECK_FALSE(KeireEditor::Detail::PostProcessProfileNameError(std::string(231, 'A')).empty());
}

TEST_CASE("enabling a post process effect activates its parameters as one undoable edit")
{
    using namespace Keire;
    using namespace KeireEditor;
    PostProcessProfileDefinition original;
    const auto intensity = static_cast<std::size_t>(PostProcessParameter::BloomIntensity);
    original.Parameters[intensity] = {false, 0.7};
    const auto service = CreateRef<UndoService>();
    const auto undo = service->CreateContext({.Name = "Effect activation"});
    PostProcessDocument document({.Persist = [](AssetId, std::span<const std::byte>) {}});
    document.Open(AssetId(7, 1), PostProcessProfileAsset::Encode(original), 1, undo);
    const auto enabled = KeireEditor::Detail::SetPostProcessEffectEnabled(original, PostProcessEffect::Bloom, true);
    const auto& group = KeireEditor::Detail::PostProcessGroupForEffect(PostProcessEffect::Bloom);
    CHECK(enabled.Effects[static_cast<std::size_t>(PostProcessEffect::Bloom)].Value);
    for (std::size_t index = 0; index < original.Parameters.size(); ++index)
    {
        CHECK(enabled.Parameters[index].Value == original.Parameters[index].Value);
        if (index >= static_cast<std::size_t>(group.First) && index <= static_cast<std::size_t>(group.Last))
        {
            CHECK(enabled.Parameters[index].Override);
        }
        else
        {
            CHECK(enabled.Parameters[index] == original.Parameters[index]);
        }
    }
    REQUIRE(document.Edit("Enable bloom", enabled));
    REQUIRE(document.Undo());
    CHECK(document.Definition() == original);
    REQUIRE(document.Redo());
    CHECK(document.Definition() == enabled);
}

TEST_CASE("editing an inherited effect parameter activates it without extra toggles")
{
    using namespace Keire;
    using namespace KeireEditor;
    PostProcessProfileDefinition original;
    const auto changed =
        KeireEditor::Detail::EditPostProcessParameter(original, PostProcessParameter::BloomIntensity, 0.8);
    const auto effect = static_cast<std::size_t>(PostProcessEffect::Bloom);
    CHECK(changed.Effects[effect].Override);
    CHECK(changed.Effects[effect].Value);
    CHECK(changed.Parameters[static_cast<std::size_t>(PostProcessParameter::BloomIntensity)].Value == 0.8);
    const auto disabled = KeireEditor::Detail::SetPostProcessEffectEnabled(changed, PostProcessEffect::Bloom, false);
    CHECK_FALSE(disabled.Effects[effect].Value);
    CHECK(disabled.Parameters == changed.Parameters);
    const auto inherited = KeireEditor::Detail::InheritPostProcessEffect(changed, PostProcessEffect::Bloom);
    CHECK_FALSE(inherited.Effects[effect].Override);
    CHECK_FALSE(inherited.Parameters[static_cast<std::size_t>(PostProcessParameter::BloomIntensity)].Override);
    CHECK(inherited.Parameters[static_cast<std::size_t>(PostProcessParameter::BloomIntensity)].Value == 0.8);
}

TEST_CASE("post process authoring rejects invalid edits without changing the source")
{
    using namespace Keire;
    using namespace KeireEditor;
    const PostProcessProfileDefinition original;
    const auto before = original;
    CHECK_THROWS_AS((void)KeireEditor::Detail::SetPostProcessEffectEnabled(original, PostProcessEffect::Count, true),
                    std::invalid_argument);
    CHECK_THROWS_AS((void)KeireEditor::Detail::EditPostProcessParameter(original, PostProcessParameter::Count, 1.0),
                    std::invalid_argument);
    CHECK_THROWS_AS((void)KeireEditor::Detail::EditPostProcessParameter(original, PostProcessParameter::BloomIntensity,
                                                                        std::numeric_limits<double>::quiet_NaN()),
                    std::invalid_argument);
    CHECK(original == before);
}

TEST_CASE("post process workspace exposes every parameter in exactly one effect group")
{
    using namespace Keire;
    using namespace KeireEditor;
    std::array<unsigned, static_cast<std::size_t>(PostProcessParameter::Count)> occurrences{};
    for (const auto& group : KeireEditor::Detail::PostProcessAuthoringGroups)
    {
        CHECK(KeireEditor::Detail::PostProcessGroupMatches(group.Name, group));
        CHECK(KeireEditor::Detail::PostProcessGroupMatches("", group));
        for (auto index = static_cast<std::size_t>(group.First); index <= static_cast<std::size_t>(group.Last); ++index)
        {
            REQUIRE(index < occurrences.size());
            ++occurrences[index];
        }
        if (group.Effect != PostProcessEffect::Count)
        {
            const PostProcessProfileDefinition original;
            const auto enabled = KeireEditor::Detail::SetPostProcessEffectEnabled(original, group.Effect, true);
            CHECK(KeireEditor::Detail::PostProcessGroupState(enabled, group) == "Enabled");
            const auto inherited = KeireEditor::Detail::InheritPostProcessEffect(enabled, group.Effect);
            CHECK(KeireEditor::Detail::PostProcessGroupState(inherited, group) == "Inherited");
        }
    }
    for (const auto count : occurrences)
    {
        CHECK(count == 1);
    }
}

TEST_CASE("basic lookup selection upgrades legacy profiles in one reversible edit")
{
    using namespace Keire;
    using namespace KeireEditor;
    PostProcessProfileDefinition original;
    original.SchemaVersion = 1;
    const auto service = CreateRef<UndoService>();
    const auto undo = service->CreateContext({.Name = "Lookup selection"});
    PostProcessDocument document({.Persist = [](AssetId, std::span<const std::byte>) {}});
    document.Open(AssetId(8, 1), PostProcessProfileAsset::Encode(original), 1, undo);
    const auto replacement =
        KeireEditor::Detail::ApplyPostProcessLookup(original, {false, AssetId(8, 2)}, {false, 0.75});
    CHECK(replacement.SchemaVersion == 3);
    CHECK(replacement.ColorLookup.Override);
    CHECK(replacement.ColorLookupWeight.Override);
    CHECK(replacement.ColorLookup.Value == AssetId(8, 2));
    CHECK(replacement.ColorLookupWeight.Value == 0.75);
    CHECK(replacement.Effects[static_cast<std::size_t>(PostProcessEffect::ColorGrading)].Value);
    REQUIRE(document.Edit("Choose lookup", replacement));
    REQUIRE(document.Undo());
    CHECK(document.Definition() == original);
    REQUIRE(document.Redo());
    CHECK(document.Definition() == replacement);
}

TEST_CASE("lookup changes preserve enabled grading inheritance and clearing does not activate grading")
{
    using namespace Keire;
    PostProcessProfileDefinition source;
    source.Effects[static_cast<std::size_t>(PostProcessEffect::ColorGrading)] = {true, true};
    source.Parameters[static_cast<std::size_t>(PostProcessParameter::Contrast)] = {true, 1.5};
    source.Parameters[static_cast<std::size_t>(PostProcessParameter::Saturation)] = {false, 0.4};
    const auto selected = KeireEditor::Detail::ApplyPostProcessLookup(source, {false, AssetId(8, 2)}, {false, 0.75});
    CHECK(selected.Parameters == source.Parameters);
    CHECK(selected.Effects == source.Effects);
    const auto adjusted = KeireEditor::Detail::ApplyPostProcessLookup(selected, selected.ColorLookup, {false, 0.25});
    CHECK(adjusted.Parameters == source.Parameters);
    CHECK(adjusted.ColorLookupWeight.Value == 0.25);
    const auto cleared = KeireEditor::Detail::ApplyPostProcessLookup(adjusted, {}, {false, 1.0});
    CHECK(cleared.Parameters == source.Parameters);
    CHECK(cleared.Effects == source.Effects);
    CHECK(cleared.ColorLookup.Override);
    CHECK_FALSE(cleared.ColorLookup.Value);

    for (const auto grading : {PostProcessOverride<bool>{false, false}, PostProcessOverride<bool>{true, false}})
    {
        source.Effects[static_cast<std::size_t>(PostProcessEffect::ColorGrading)] = grading;
        const auto disabledClear = KeireEditor::Detail::ApplyPostProcessLookup(source, {}, {false, 1.0});
        CHECK(disabledClear.Effects == source.Effects);
        CHECK(disabledClear.Parameters == source.Parameters);
        CHECK(disabledClear.ColorLookup.Override);
        CHECK_FALSE(disabledClear.ColorLookup.Value);
    }
}

TEST_CASE("post process lookup selection resolves texture subassets and rejects incompatible references")
{
    Keire::AssetSourceRecord texture;
    texture.Id = Keire::AssetId(42, 1);
    texture.Type = Keire::Texture2DAsset::StaticType();
    texture.RelativePath = "Textures/Identity.png";
    Keire::AssetSourceRecord source;
    source.Id = Keire::AssetId(42, 2);
    source.Type = Keire::MaterialGraphAsset::StaticType();
    source.RelativePath = "Materials/Generated.keirematerial";
    const Keire::AssetId generatedTexture(42, 3);
    const Keire::AssetId generatedMaterial(42, 4);
    source.SubAssets = {generatedTexture, generatedMaterial};
    const std::array records{texture, source};
    KeireEditor::AssetPickerOptions options;
    options.Label = "Lookup Texture";
    options.ExpectedType = Keire::Texture2DAsset::StaticType();
    options.ResolveType = [=](const Keire::AssetId id) -> std::optional<Keire::AssetTypeId>
    {
        if (id == generatedTexture)
        {
            return Keire::Texture2DAsset::StaticType();
        }
        if (id == generatedMaterial)
        {
            return Keire::MaterialAsset::StaticType();
        }
        return std::nullopt;
    };
    CHECK(KeireEditor::AssetPicker::ResolveCompatibleAsset(records, texture.Id, options) == texture.Id);
    const auto resolved = KeireEditor::AssetPicker::ResolveCompatibleAsset(records, generatedTexture, options);
    REQUIRE(resolved == generatedTexture);
    CHECK_FALSE(KeireEditor::AssetPicker::ResolveCompatibleAsset(records, source.Id, options));
    CHECK_FALSE(KeireEditor::AssetPicker::ResolveCompatibleAsset(records, generatedMaterial, options));
    CHECK_FALSE(KeireEditor::AssetPicker::ResolveCompatibleAsset(records, Keire::AssetId(42, 99), options));

    Keire::PostProcessProfileDefinition initial;
    const auto edited = KeireEditor::Detail::ApplyPostProcessLookup(initial, {true, *resolved}, {true, 0.5});
    CHECK(edited.ColorLookup.Value == generatedTexture);
    CHECK(edited.ColorLookupWeight.Value == 0.5);
    CHECK(Keire::PostProcessProfileAsset::Decode(Keire::PostProcessProfileAsset::Encode(edited))->Definition() ==
          edited);
    options.ResolveType = {};
    CHECK_FALSE(KeireEditor::AssetPicker::ResolveCompatibleAsset(records, generatedTexture, options));
    CHECK(KeireEditor::AssetPicker::ResolveCompatibleAsset(records, texture.Id, options) == texture.Id);
}
