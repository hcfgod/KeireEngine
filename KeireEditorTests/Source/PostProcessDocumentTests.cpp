#include "KeireClient/Editor/PostProcessDocument.h"
#include "KeireClientInternal/Editor/PostProcessAuthoring.h"
#include "KeireClientInternal/Editor/PostProcessDiagnostics.h"
#include "KeireClientInternal/Editor/PostProcessLooks.h"

#include <doctest/doctest.h>

#include <limits>
#include <stdexcept>
#include <vector>

namespace
{
    struct ContinuousProfileFixture
    {
        Keire::Ref<Keire::UndoService> Service = Keire::CreateRef<Keire::UndoService>();
        Keire::Ref<Keire::UndoContext> Undo = Service->CreateContext({.Name = "Continuous profile editing"});
        Keire::PostProcessProfileDefinition Initial;
        Keire::PostProcessProfileDefinition Preview;
        std::vector<std::byte> Saved;
        bool RejectPreview = false;
        bool RejectAllPreviews = false;
        bool RejectSave = false;
        unsigned CancelCount = 0;
        KeireEditor::PostProcessDocument Document{
            {.Preview =
                 [this](Keire::AssetId, const Keire::PostProcessProfileDefinition& value)
             {
                 if (RejectAllPreviews || (RejectPreview && value != Initial))
                 {
                     throw std::runtime_error("preview unavailable");
                 }
                 Preview = value;
             },
             .CancelPreview = [this](Keire::AssetId) { ++CancelCount; },
             .Persist =
                 [this](Keire::AssetId, const std::span<const std::byte> bytes)
             {
                 if (RejectSave)
                 {
                     throw std::runtime_error("source unavailable");
                 }
                 Saved.assign(bytes.begin(), bytes.end());
             }}};

        explicit ContinuousProfileFixture(const bool withHistory = true)
        {
            Document.Open(Keire::AssetId(12, 1), Keire::PostProcessProfileAsset::Encode(Initial), 1,
                          withHistory ? Undo : Keire::Ref<Keire::UndoContext>{});
        }

        [[nodiscard]] Keire::PostProcessProfileDefinition Bloom(const double intensity) const
        {
            return KeireEditor::Detail::EditPostProcessParameter(
                Document.Definition(), Keire::PostProcessParameter::BloomIntensity, intensity);
        }
    };
} // namespace

TEST_CASE("post process history accounts for custom effect snapshot payloads")
{
    ContinuousProfileFixture empty;
    ContinuousProfileFixture populated;
    auto initial = populated.Initial;
    initial.CustomEffects.Override = true;
    for (std::size_t index = 0; index < Keire::MaximumPostProcessCustomEffects; ++index)
    {
        initial.CustomEffects.Value.push_back({Keire::AssetId(20, index + 1)});
    }
    populated.Document.Open(Keire::AssetId(12, 1), Keire::PostProcessProfileAsset::Encode(initial), 1, populated.Undo);

    REQUIRE(empty.Document.Edit("Adjust Bloom", empty.Bloom(0.5)));
    REQUIRE(populated.Document.Edit("Adjust Bloom", populated.Bloom(0.5)));
    REQUIRE(empty.Undo->UndoCount() == 1);
    REQUIRE(populated.Undo->UndoCount() == 1);
    const auto snapshotPayload = 2 * Keire::MaximumPostProcessCustomEffects * sizeof(Keire::PostProcessCustomEffect);
    CHECK(populated.Undo->EstimatedBytes() == empty.Undo->EstimatedBytes() + snapshotPayload);

    const auto retainedBytes = populated.Undo->EstimatedBytes();
    REQUIRE(populated.Document.Undo());
    CHECK(populated.Document.Definition() == initial);
    CHECK(populated.Undo->EstimatedBytes() == retainedBytes);
    REQUIRE(populated.Document.Redo());
    CHECK(populated.Undo->EstimatedBytes() == retainedBytes);
}

TEST_CASE("continuous post process samples record one command including automatic activation")
{
    ContinuousProfileFixture fixture;
    auto& document = fixture.Document;
    document.BeginContinuousEdit("Adjust Bloom");
    for (unsigned sample = 1; sample <= 128; ++sample)
    {
        REQUIRE(document.UpdateContinuousEdit(fixture.Bloom(static_cast<double>(sample) / 128.0)));
        CHECK(fixture.Preview == document.Definition());
        CHECK(fixture.Undo->UndoCount() == 0);
        CHECK_FALSE(fixture.Undo->CanUndo());
        CHECK(document.Dirty());
        CHECK(document.PreservesLivePreview(document.Asset()));
    }
    const auto final = document.Definition();
    document.CommitContinuousEdit();
    CHECK_FALSE(document.ContinuousEditActive());
    REQUIRE(fixture.Undo->UndoCount() == 1);
    CHECK(fixture.Undo->UndoName() == "Adjust Bloom");
    REQUIRE(document.Undo());
    CHECK(document.Definition() == fixture.Initial);
    CHECK(fixture.Preview == fixture.Initial);
    CHECK_FALSE(document.Dirty());
    REQUIRE(document.Redo());
    CHECK(document.Definition() == final);
    CHECK(fixture.Preview == final);

    document.BeginContinuousEdit("Adjust Bloom");
    REQUIRE(document.UpdateContinuousEdit(fixture.Bloom(0.25)));
    document.CommitContinuousEdit();
    REQUIRE(fixture.Undo->UndoCount() == 2);
    REQUIRE(document.Undo());
    CHECK(document.Definition() == final);

    // Cancelling an independent gesture preserves the redo branch and all inherited state.
    document.BeginContinuousEdit("Adjust Bloom");
    REQUIRE(document.UpdateContinuousEdit(fixture.Bloom(0.75)));
    document.CancelContinuousEdit();
    CHECK(document.Definition() == final);
    CHECK(fixture.Preview == final);
    CHECK(fixture.Undo->RedoCount() == 1);
    REQUIRE(document.Redo());
    CHECK(
        document.Definition().Parameters[static_cast<std::size_t>(Keire::PostProcessParameter::BloomIntensity)].Value ==
        0.25);
}

TEST_CASE("continuous post process cancellation and round trips work without history or changes")
{
    for (const bool withHistory : {false, true})
    {
        CAPTURE(withHistory);
        ContinuousProfileFixture fixture(withHistory);
        auto& document = fixture.Document;
        document.BeginContinuousEdit("Adjust Bloom");
        CHECK_FALSE(document.UpdateContinuousEdit(fixture.Initial));
        document.CommitContinuousEdit();
        CHECK(fixture.Undo->UndoCount() == 0);
        CHECK_FALSE(document.Dirty());

        document.BeginContinuousEdit("Adjust Bloom");
        REQUIRE(document.UpdateContinuousEdit(fixture.Bloom(0.5)));
        REQUIRE(document.UpdateContinuousEdit(fixture.Initial));
        CHECK_FALSE(document.Dirty());
        document.CommitContinuousEdit();
        CHECK(fixture.Undo->UndoCount() == 0);

        document.BeginContinuousEdit("Adjust Bloom");
        REQUIRE(document.UpdateContinuousEdit(fixture.Bloom(0.75)));
        document.CancelContinuousEdit();
        document.CancelContinuousEdit();
        CHECK_FALSE(document.ContinuousEditActive());
        CHECK(document.Definition() == fixture.Initial);
        CHECK(fixture.Preview == fixture.Initial);
        CHECK_FALSE(document.Dirty());
        CHECK(fixture.Undo->UndoCount() == 0);
    }
}

TEST_CASE("invalid continuous post process samples cancel preview without losing earlier history")
{
    ContinuousProfileFixture fixture;
    auto& document = fixture.Document;
    REQUIRE(document.SetParameter(Keire::PostProcessParameter::ExposureStops, {true, 1.0}));
    const auto before = document.Definition();
    document.BeginContinuousEdit("Adjust Bloom");
    REQUIRE(document.UpdateContinuousEdit(fixture.Bloom(0.5)));
    auto invalid = document.Definition();
    invalid.Parameters[static_cast<std::size_t>(Keire::PostProcessParameter::BloomIntensity)].Value =
        std::numeric_limits<double>::quiet_NaN();
    CHECK_THROWS_AS((void)document.UpdateContinuousEdit(invalid), std::invalid_argument);
    CHECK_FALSE(document.ContinuousEditActive());
    CHECK(document.Definition() == before);
    CHECK(fixture.Preview == before);
    CHECK(fixture.Undo->UndoCount() == 1);
    REQUIRE(document.Undo());
    CHECK(document.Definition() == fixture.Initial);

    document.BeginContinuousEdit("Adjust Bloom");
    REQUIRE(document.UpdateContinuousEdit(fixture.Bloom(0.5)));
    fixture.RejectPreview = true;
    CHECK_THROWS_AS((void)document.UpdateContinuousEdit(fixture.Bloom(0.75)), std::runtime_error);
    CHECK_FALSE(document.ContinuousEditActive());
    CHECK(document.Definition() == fixture.Initial);
    CHECK(fixture.Preview == fixture.Initial);
    CHECK(fixture.Undo->RedoCount() == 1);
    CHECK_THROWS_AS((void)document.UpdateContinuousEdit(fixture.Initial), std::logic_error);
    CHECK_THROWS_AS(document.BeginContinuousEdit(""), std::invalid_argument);
}

TEST_CASE("continuous post process edits finish safely at document lifecycle boundaries")
{
    ContinuousProfileFixture fixture;
    auto& document = fixture.Document;
    document.BeginContinuousEdit("Adjust Bloom");
    REQUIRE(document.UpdateContinuousEdit(fixture.Bloom(0.5)));
    const auto final = document.Definition();

    SUBCASE("save persists the final sample and retains one undo command")
    {
        document.Save();
        CHECK_FALSE(document.ContinuousEditActive());
        CHECK_FALSE(document.Dirty());
        CHECK(Keire::PostProcessProfileAsset::Decode(fixture.Saved)->Definition() == final);
        CHECK(fixture.Undo->UndoCount() == 1);
        REQUIRE(document.Undo());
        CHECK(document.Definition() == fixture.Initial);
        CHECK(document.Dirty());
    }
    SUBCASE("failed save retains the final undoable draft")
    {
        fixture.RejectSave = true;
        CHECK_THROWS_AS(document.Save(), std::runtime_error);
        CHECK_FALSE(document.ContinuousEditActive());
        CHECK(document.Dirty());
        CHECK(document.Definition() == final);
        REQUIRE(document.Undo());
        CHECK(document.Definition() == fixture.Initial);
    }
    SUBCASE("reload preserves unsaved continuous changes and releases the transaction")
    {
        const auto source = Keire::PostProcessProfileAsset::Encode(fixture.Initial);
        CHECK(document.Reload(source, 2) == KeireEditor::AssetDocumentReloadResult::LocalChanges);
        CHECK_FALSE(document.ContinuousEditActive());
        CHECK(document.Definition() == final);
        REQUIRE(document.Undo());
        auto external = fixture.Initial;
        external.Parameters[static_cast<std::size_t>(Keire::PostProcessParameter::ExposureStops)] = {true, -1.0};
        CHECK(document.Reload(Keire::PostProcessProfileAsset::Encode(external), 3) ==
              KeireEditor::AssetDocumentReloadResult::Applied);
        CHECK(document.Definition() == external);
        CHECK(fixture.Preview == external);
        CHECK_FALSE(document.Dirty());
        CHECK(fixture.Undo->UndoCount() == 0);
    }
    SUBCASE("discard restores the baseline and clears history")
    {
        document.Discard();
        CHECK_FALSE(document.ContinuousEditActive());
        CHECK_FALSE(document.Dirty());
        CHECK(document.Definition() == fixture.Initial);
        CHECK(fixture.Preview == fixture.Initial);
        CHECK(fixture.Undo->UndoCount() == 0);
    }
    SUBCASE("invalid replacement keeps the current gesture and valid replacement releases it")
    {
        CHECK_THROWS_AS(document.Open(Keire::AssetId(12, 2), {}, 2, fixture.Undo), std::runtime_error);
        CHECK(document.ContinuousEditActive());
        CHECK(document.Definition() == final);
        CHECK_THROWS_AS(document.Open({}, Keire::PostProcessProfileAsset::Encode(fixture.Initial), 2),
                        std::invalid_argument);
        CHECK(document.ContinuousEditActive());
        document.Open(Keire::AssetId(12, 2), Keire::PostProcessProfileAsset::Encode(fixture.Initial), 2, fixture.Undo);
        CHECK_FALSE(document.ContinuousEditActive());
        CHECK(document.Asset() == Keire::AssetId(12, 2));
        CHECK(document.Definition() == fixture.Initial);
        CHECK(fixture.Preview == fixture.Initial);
        CHECK(fixture.Undo->UndoCount() == 0);
        CHECK_FALSE(document.Undo());
    }
    SUBCASE("undo finishes the gesture before undoing it")
    {
        REQUIRE(document.Undo());
        CHECK_FALSE(document.ContinuousEditActive());
        CHECK(document.Definition() == fixture.Initial);
        REQUIRE(document.Redo());
        CHECK(document.Definition() == final);
    }
    SUBCASE("a discrete edit finishes the gesture as a separate command")
    {
        REQUIRE(document.SetParameter(Keire::PostProcessParameter::ExposureStops, {true, 1.0}));
        CHECK_FALSE(document.ContinuousEditActive());
        CHECK(fixture.Undo->UndoCount() == 2);
        REQUIRE(document.Undo());
        CHECK(document.Definition() == final);
        REQUIRE(document.Undo());
        CHECK(document.Definition() == fixture.Initial);
    }
    SUBCASE("editing with a view of the transient definition remains valid after commit")
    {
        CHECK_FALSE(document.Edit("Keep current values", document.Definition()));
        CHECK_FALSE(document.ContinuousEditActive());
        CHECK(document.Definition() == final);
        CHECK(fixture.Undo->UndoCount() == 1);
        REQUIRE(document.Undo());
        CHECK(document.Definition() == fixture.Initial);
    }
    SUBCASE("close cancels preview without leaving a transaction in the reused history")
    {
        document.Close();
        document.Close();
        CHECK_FALSE(document.ContinuousEditActive());
        CHECK_FALSE(document.IsOpen());
        CHECK(fixture.CancelCount == 1);
        CHECK_NOTHROW(fixture.Undo->Clear());
        document.Open(Keire::AssetId(12, 2), Keire::PostProcessProfileAsset::Encode(fixture.Initial), 2, fixture.Undo);
        REQUIRE(document.SetParameter(Keire::PostProcessParameter::ExposureStops, {true, 1.0}));
        REQUIRE(document.Undo());
        CHECK(document.Definition() == fixture.Initial);
    }
    SUBCASE("closing the undo service during a gesture remains safe")
    {
        fixture.Service->Close();
        document.CancelContinuousEdit();
        CHECK(document.Definition() == fixture.Initial);
        CHECK(fixture.Preview == fixture.Initial);
        document.BeginContinuousEdit("Adjust Bloom");
        REQUIRE(document.UpdateContinuousEdit(fixture.Bloom(0.5)));
        document.CommitContinuousEdit();
        CHECK(document.Definition() == final);
    }
}

TEST_CASE("rejected post process reload inputs preserve an active gesture and its redo branch")
{
    ContinuousProfileFixture fixture;
    auto& document = fixture.Document;
    REQUIRE(document.SetParameter(Keire::PostProcessParameter::ExposureStops, {true, 1.0}));
    REQUIRE(document.Undo());
    document.BeginContinuousEdit("Adjust Bloom");
    REQUIRE(document.UpdateContinuousEdit(fixture.Bloom(0.5)));
    const auto draft = document.Definition();
    SUBCASE("invalid revision")
    {
        CHECK_THROWS_AS((void)document.Reload(Keire::PostProcessProfileAsset::Encode(draft), 0), std::invalid_argument);
    }
    SUBCASE("invalid source") { CHECK_THROWS_AS((void)document.Reload({}, 2), std::runtime_error); }
    CHECK(document.ContinuousEditActive());
    CHECK(document.Definition() == draft);
    CHECK(fixture.Preview == draft);
    CHECK(fixture.Undo->UndoCount() == 0);
    CHECK(fixture.Undo->RedoCount() == 1);
    document.CancelContinuousEdit();
    CHECK(document.Definition() == fixture.Initial);
    REQUIRE(document.Redo());
    CHECK(
        document.Definition().Parameters[static_cast<std::size_t>(Keire::PostProcessParameter::ExposureStops)].Value ==
        1.0);
}

TEST_CASE("continuous post process dirty state compares the current sample to the saved baseline")
{
    ContinuousProfileFixture fixture;
    auto& document = fixture.Document;
    REQUIRE(document.SetParameter(Keire::PostProcessParameter::ExposureStops, {true, 1.0}));
    const auto before = document.Definition();
    document.BeginContinuousEdit("Return to saved values");
    REQUIRE(document.UpdateContinuousEdit(fixture.Initial));
    CHECK_FALSE(document.Dirty());
    document.CancelContinuousEdit();
    CHECK(document.Dirty());
    CHECK(document.Definition() == before);
    CHECK(fixture.Preview == before);
    CHECK(fixture.Undo->UndoCount() == 1);
}

TEST_CASE("continuous post process restoration failures release history and permit later recovery")
{
    ContinuousProfileFixture fixture;
    auto& document = fixture.Document;
    document.BeginContinuousEdit("Adjust Bloom");
    REQUIRE(document.UpdateContinuousEdit(fixture.Bloom(0.5)));
    fixture.RejectAllPreviews = true;
    CHECK_THROWS_AS((void)document.UpdateContinuousEdit(fixture.Bloom(0.75)), std::runtime_error);
    CHECK_FALSE(document.ContinuousEditActive());
    CHECK(document.Definition() == fixture.Initial);
    CHECK_NOTHROW(fixture.Undo->Clear());
    fixture.RejectAllPreviews = false;
    document.Discard();
    CHECK(fixture.Preview == fixture.Initial);
    document.BeginContinuousEdit("Adjust Bloom");
    REQUIRE(document.UpdateContinuousEdit(fixture.Bloom(0.25)));
    document.CommitContinuousEdit();
    REQUIRE(document.Undo());
    CHECK(fixture.Preview == fixture.Initial);
}

TEST_CASE("destroying a post process document releases an abandoned continuous transaction")
{
    const auto service = Keire::CreateRef<Keire::UndoService>();
    const auto undo = service->CreateContext({.Name = "Abandoned profile edit"});
    unsigned cancellations = 0;
    {
        KeireEditor::PostProcessDocument document({.CancelPreview = [&](Keire::AssetId) { ++cancellations; },
                                                   .Persist = [](Keire::AssetId, std::span<const std::byte>) {}});
        document.Open(Keire::AssetId(13, 1), Keire::PostProcessProfileAsset::Encode({}), 1, undo);
        document.BeginContinuousEdit("Adjust exposure");
        auto replacement = document.Definition();
        replacement.Parameters[static_cast<std::size_t>(Keire::PostProcessParameter::ExposureStops)] = {true, 1.0};
        REQUIRE(document.UpdateContinuousEdit(replacement));
    }
    CHECK(cancellations == 1);
    CHECK_NOTHROW(undo->Clear());
    CHECK(undo->UndoCount() == 0);
}

TEST_CASE("continuous lookup weight editing upgrades legacy data in a single reversible command")
{
    ContinuousProfileFixture fixture;
    auto original = fixture.Initial;
    original.SchemaVersion = 1;
    fixture.Document.Open(Keire::AssetId(12, 1), Keire::PostProcessProfileAsset::Encode(original), 1, fixture.Undo);
    fixture.Document.BeginContinuousEdit("Adjust lookup weight");
    for (const double weight : {0.2, 0.4, 0.6, 0.8})
    {
        const auto replacement = KeireEditor::Detail::ApplyPostProcessLookup(
            fixture.Document.Definition(), {false, Keire::AssetId(12, 2)}, {false, weight});
        REQUIRE(fixture.Document.UpdateContinuousEdit(replacement));
    }
    fixture.Document.CommitContinuousEdit();
    CHECK(fixture.Undo->UndoCount() == 1);
    CHECK(fixture.Document.Definition().SchemaVersion == 3);
    CHECK(fixture.Document.Definition().ColorLookupWeight.Value == 0.8);
    REQUIRE(fixture.Document.Undo());
    CHECK(fixture.Document.Definition() == original);
    REQUIRE(fixture.Document.Redo());
    CHECK(fixture.Document.Definition().ColorLookupWeight.Value == 0.8);
}

TEST_CASE("post process lens diagnostics respect units and inherited values")
{
    Keire::PostProcessProfileDefinition profile;
    auto& focus = profile.Parameters[static_cast<std::size_t>(Keire::PostProcessParameter::FocusDistance)];
    auto& focal = profile.Parameters[static_cast<std::size_t>(Keire::PostProcessParameter::FocalLength)];
    focus = {true, 0.01};
    focal = {true, 50.0};
    CHECK_FALSE(KeireEditor::Detail::PostProcessLensDiagnostic(profile).empty());
    focus.Value = 0.05;
    CHECK_FALSE(KeireEditor::Detail::PostProcessLensDiagnostic(profile).empty());
    focus.Value = 0.051;
    CHECK(KeireEditor::Detail::PostProcessLensDiagnostic(profile).empty());
    focus.Value = 0.01;
    focal.Override = false;
    CHECK(KeireEditor::Detail::PostProcessLensDiagnostic(profile).empty());
    focal.Override = true;
    focus.Override = false;
    CHECK(KeireEditor::Detail::PostProcessLensDiagnostic(profile).empty());
}

TEST_CASE("post process color looks preserve unrelated effects and undo as one edit")
{
    using namespace Keire;
    using namespace KeireEditor;
    for (std::size_t look = 0; look < KeireEditor::Detail::PostProcessColorLooks.size(); ++look)
    {
        CAPTURE(KeireEditor::Detail::PostProcessColorLooks[look].Name);
        PostProcessProfileDefinition original;
        const auto descriptions = PostProcessParameterDescriptions();
        for (std::size_t index = 0; index < original.Parameters.size(); ++index)
        {
            original.Parameters[index] = {index % 2 == 0, descriptions[index].Maximum};
        }
        const auto service = CreateRef<UndoService>();
        const auto undo = service->CreateContext({.Name = "Color look"});
        PostProcessProfileDefinition preview;
        std::vector<std::byte> saved;
        PostProcessDocument document(
            {.Preview = [&](AssetId, const PostProcessProfileDefinition& value) { preview = value; },
             .Persist = [&](AssetId, std::span<const std::byte> bytes) { saved.assign(bytes.begin(), bytes.end()); }});
        document.Open(AssetId(5, 1), PostProcessProfileAsset::Encode(original), 1, undo);
        const auto replacement = KeireEditor::Detail::ApplyPostProcessColorLook(original, look);
        REQUIRE(document.Edit("Apply color look", replacement));
        CHECK(preview == replacement);
        const auto& expectedLook = KeireEditor::Detail::PostProcessColorLooks[look];
        CHECK(replacement.Parameters[static_cast<std::size_t>(PostProcessParameter::Temperature)].Value ==
              expectedLook.Temperature);
        CHECK(replacement.Parameters[static_cast<std::size_t>(PostProcessParameter::Contrast)].Value ==
              expectedLook.Contrast);
        CHECK(replacement.Parameters[static_cast<std::size_t>(PostProcessParameter::Saturation)].Value ==
              expectedLook.Saturation);
        CHECK(replacement.Effects[static_cast<std::size_t>(PostProcessEffect::ColorGrading)].Override);
        CHECK(replacement.Effects[static_cast<std::size_t>(PostProcessEffect::ColorGrading)].Value);
        for (std::size_t index = 0; index < original.Parameters.size(); ++index)
        {
            if (index < static_cast<std::size_t>(PostProcessParameter::Temperature) ||
                index > static_cast<std::size_t>(PostProcessParameter::BlueGain))
            {
                CHECK(replacement.Parameters[index] == original.Parameters[index]);
            }
            else
            {
                CHECK(replacement.Parameters[index].Override);
            }
        }
        for (std::size_t index = 0; index < original.Effects.size(); ++index)
        {
            if (index != static_cast<std::size_t>(PostProcessEffect::ColorGrading))
            {
                CHECK(replacement.Effects[index] == original.Effects[index]);
            }
        }
        CHECK(PostProcessProfileAsset::Decode(PostProcessProfileAsset::Encode(replacement))->Definition() ==
              replacement);
        REQUIRE(document.Undo());
        CHECK(document.Definition() == original);
        CHECK(preview == original);
        REQUIRE(document.Redo());
        CHECK(document.Definition() == replacement);
        document.Save();
        CHECK_FALSE(document.Dirty());
        CHECK(PostProcessProfileAsset::Decode(saved)->Definition() == replacement);
        CHECK_THROWS_AS((void)KeireEditor::Detail::ApplyPostProcessColorLook(
                            original, KeireEditor::Detail::PostProcessColorLooks.size()),
                        std::invalid_argument);
        auto invalid = original;
        invalid.Parameters[static_cast<std::size_t>(PostProcessParameter::ExposureStops)].Value =
            std::numeric_limits<double>::infinity();
        CHECK_THROWS_AS((void)KeireEditor::Detail::ApplyPostProcessColorLook(invalid, look), std::invalid_argument);
        CHECK(document.Definition() == replacement);
    }
}

TEST_CASE("implemented post process controls preserve preview and history across boundary edits and invalid input")
{
    using namespace Keire;
    using namespace KeireEditor;
    const auto descriptions = PostProcessParameterDescriptions();
    for (std::size_t index = 0; index < descriptions.size(); ++index)
    {
        CAPTURE(descriptions[index].Key);
        const auto parameter = static_cast<PostProcessParameter>(index);
        const auto undoService = CreateRef<UndoService>();
        const auto undo = undoService->CreateContext({.Name = "Profile boundary editing"});
        PostProcessProfileDefinition preview;
        std::vector<std::byte> saved;
        PostProcessDocument document(
            {.Preview = [&](AssetId, const PostProcessProfileDefinition& value) { preview = value; },
             .Persist = [&](AssetId, std::span<const std::byte> bytes) { saved.assign(bytes.begin(), bytes.end()); }});
        const PostProcessProfileDefinition initial;
        document.Open(AssetId(3, 1), PostProcessProfileAsset::Encode(initial), 1, undo);
        REQUIRE(document.SetParameter(parameter, {true, descriptions[index].Minimum}));
        REQUIRE(document.SetParameter(parameter, {true, descriptions[index].Maximum}));
        const auto maximumDraft = document.Definition();
        CHECK(preview == maximumDraft);
        CHECK_THROWS_AS((void)document.SetParameter(parameter, {true, std::numeric_limits<double>::quiet_NaN()}),
                        std::invalid_argument);
        CHECK(document.Definition() == maximumDraft);
        CHECK(preview == maximumDraft);
        REQUIRE(document.Undo());
        CHECK(document.Definition().Parameters[index].Value == descriptions[index].Minimum);
        REQUIRE(document.Redo());
        CHECK(preview == maximumDraft);
        document.Save();
        CHECK_FALSE(document.Dirty());
        CHECK(PostProcessProfileAsset::Decode(saved)->Definition() == maximumDraft);
        REQUIRE(document.ResetParameter(parameter));
        CHECK_FALSE(document.Definition().Parameters[index].Override);
        CHECK(document.Definition().Parameters[index].Value == descriptions[index].Default);
        document.Discard();
        CHECK(document.Definition() == maximumDraft);
        CHECK(preview == maximumDraft);
    }
}

TEST_CASE("post process document supports undo save discard and conflicting reloads")
{
    using namespace Keire;
    using namespace KeireEditor;
    const auto undoService = CreateRef<UndoService>();
    const auto undo = undoService->CreateContext({.Name = "Post Processing"});
    std::vector<std::byte> saved;
    PostProcessProfileDefinition preview;
    PostProcessDocument document(
        {.Preview = [&](AssetId, const PostProcessProfileDefinition& definition) { preview = definition; },
         .Persist = [&](AssetId, std::span<const std::byte> bytes) { saved.assign(bytes.begin(), bytes.end()); }});
    const PostProcessProfileDefinition initial;
    document.Open(AssetId(1, 1), PostProcessProfileAsset::Encode(initial), 1, undo);
    CHECK_FALSE(document.PreservesLivePreview(AssetId(1, 1)));
    REQUIRE(document.SetParameter(PostProcessParameter::ExposureStops, {true, 2.0}));
    CHECK(document.Dirty());
    CHECK(document.PreservesLivePreview(AssetId(1, 1)));
    CHECK_FALSE(document.PreservesLivePreview(AssetId(1, 2)));
    CHECK(preview.Parameters[0].Value == 2.0);
    REQUIRE(document.Undo());
    CHECK_FALSE(document.Dirty());
    CHECK(preview == initial);
    REQUIRE(document.Redo());
    CHECK(preview.Parameters[0].Value == 2.0);
    auto external = initial;
    external.Parameters[0] = {true, -1.0};
    CHECK(document.Reload(PostProcessProfileAsset::Encode(external), 2) == AssetDocumentReloadResult::LocalChanges);
    CHECK(document.Definition().Parameters[0].Value == 2.0);
    document.Save();
    CHECK_FALSE(document.Dirty());
    CHECK_FALSE(document.PreservesLivePreview(AssetId(1, 1)));
    CHECK(PostProcessProfileAsset::Decode(saved)->Definition() == document.Definition());
    REQUIRE(document.ResetParameter(PostProcessParameter::ExposureStops));
    CHECK(document.PreservesLivePreview(AssetId(1, 1)));
    document.Discard();
    CHECK_FALSE(document.PreservesLivePreview(AssetId(1, 1)));
    CHECK(document.Definition().Parameters[0].Value == 2.0);
    CHECK(preview == document.Definition());
}

TEST_CASE("color lookup edits upgrade legacy profiles transactionally with undo preview and save")
{
    using namespace Keire;
    using namespace KeireEditor;
    const auto undoService = CreateRef<UndoService>();
    const auto undo = undoService->CreateContext({.Name = "Post Processing"});
    std::vector<std::byte> saved;
    PostProcessProfileDefinition preview;
    PostProcessDocument document(
        {.Preview = [&](AssetId, const PostProcessProfileDefinition& definition) { preview = definition; },
         .Persist = [&](AssetId, std::span<const std::byte> bytes) { saved.assign(bytes.begin(), bytes.end()); }});
    PostProcessProfileDefinition initial;
    initial.SchemaVersion = 1;
    const AssetId profile(8, 1);
    const AssetId texture(8, 2);
    document.Open(profile, PostProcessProfileAsset::Encode(initial), 1, undo);
    CHECK_FALSE(document.SetColorLookup(initial.ColorLookup, initial.ColorLookupWeight));
    CHECK(document.Definition().SchemaVersion == 1);
    CHECK_FALSE(document.Dirty());
    REQUIRE(document.SetParameter(PostProcessParameter::ExposureStops, {true, 1.0}));
    CHECK(document.Definition().SchemaVersion == 1);
    REQUIRE(document.Undo());
    REQUIRE(document.SetColorLookup({true, texture}, {true, 0.75}));
    const auto edited = document.Definition();
    CHECK(edited.SchemaVersion == 3);
    CHECK(edited.ColorLookup.Value == texture);
    CHECK(edited.ColorLookupWeight.Value == 0.75);
    CHECK(preview == edited);
    REQUIRE(document.Undo());
    CHECK(document.Definition() == initial);
    CHECK_FALSE(document.Dirty());
    REQUIRE(document.Redo());
    CHECK(preview == edited);
    CHECK_THROWS_AS((void)document.SetColorLookup({true, texture}, {true, std::numeric_limits<double>::quiet_NaN()}),
                    std::invalid_argument);
    CHECK_THROWS_AS((void)document.SetColorLookup({true, profile}, {true, 1.0}), std::invalid_argument);
    CHECK(document.Definition() == edited);
    CHECK(preview == edited);
    document.Save();
    CHECK(PostProcessProfileAsset::Decode(saved)->Definition() == edited);
    REQUIRE(document.SetColorLookup({true, {}}, edited.ColorLookupWeight));
    CHECK(document.Definition().ColorLookup.Override);
    CHECK_FALSE(document.Definition().ColorLookup.Value);
    CHECK(document.Definition().ColorLookupWeight == edited.ColorLookupWeight);
    document.Discard();
    CHECK(document.Definition() == edited);
    CHECK(preview == edited);
}

TEST_CASE("failed profile saves preserve dirty edits and invalid opens preserve the current document")
{
    using namespace Keire;
    using namespace KeireEditor;
    PostProcessDocument document(
        {.Persist = [](AssetId, std::span<const std::byte>) { throw std::runtime_error("disk unavailable"); }});
    document.Open(AssetId(1, 1), PostProcessProfileAsset::Encode({}), 1);
    REQUIRE(document.SetEffect(PostProcessEffect::Bloom, {true, true}));
    const auto draft = document.Definition();
    CHECK_THROWS_AS(document.Save(), std::runtime_error);
    CHECK(document.Dirty());
    CHECK(document.Definition() == draft);
    CHECK_THROWS_AS(document.Open(AssetId(1, 2), {}, 1), std::runtime_error);
    CHECK(document.Asset() == AssetId(1, 1));
    CHECK(document.Definition() == draft);
    CHECK_THROWS_AS((void)document.SetParameter(PostProcessParameter::Count, {}), std::invalid_argument);
    CHECK(document.Definition() == draft);
}

TEST_CASE("custom material replacement preserves row settings order preview and history")
{
    ContinuousProfileFixture fixture;
    auto initial = fixture.Initial;
    initial.CustomEffects = {true,
                             {{Keire::AssetId(35, 1), Keire::PostProcessCustomStage::BeforeToneMap, true},
                              {Keire::AssetId(35, 2), Keire::PostProcessCustomStage::AfterUi, false},
                              {Keire::AssetId(35, 3), Keire::PostProcessCustomStage::AfterToneMap, true}}};
    fixture.Document.Open(Keire::AssetId(12, 1), Keire::PostProcessProfileAsset::Encode(initial), 1, fixture.Undo);
    auto replacement = initial.CustomEffects;
    replacement.Value[1].Material = Keire::AssetId(35, 4);
    REQUIRE(fixture.Document.SetCustomEffects(replacement));
    const auto edited = fixture.Document.Definition();
    CHECK(edited.CustomEffects.Value[0] == initial.CustomEffects.Value[0]);
    CHECK(edited.CustomEffects.Value[2] == initial.CustomEffects.Value[2]);
    CHECK(edited.CustomEffects.Value[1].Material == Keire::AssetId(35, 4));
    CHECK(edited.CustomEffects.Value[1].Stage == Keire::PostProcessCustomStage::AfterUi);
    CHECK_FALSE(edited.CustomEffects.Value[1].Enabled);
    CHECK(fixture.Preview == edited);
    CHECK(fixture.Undo->UndoCount() == 1);
    CHECK_FALSE(fixture.Document.SetCustomEffects(replacement));
    CHECK(fixture.Undo->UndoCount() == 1);
    auto invalid = replacement;
    invalid.Value[1].Material = fixture.Document.Asset();
    CHECK_THROWS_AS((void)fixture.Document.SetCustomEffects(invalid), std::invalid_argument);
    CHECK(fixture.Document.Definition() == edited);
    CHECK(fixture.Preview == edited);
    CHECK(fixture.Undo->UndoCount() == 1);
    REQUIRE(fixture.Document.Undo());
    CHECK(fixture.Document.Definition() == initial);
    CHECK(fixture.Preview == initial);
    REQUIRE(fixture.Document.Redo());
    CHECK(fixture.Document.Definition() == edited);
    fixture.Document.Save();
    CHECK(Keire::PostProcessProfileAsset::Decode(fixture.Saved)->Definition() == edited);
    CHECK_FALSE(fixture.Document.Dirty());
}

TEST_CASE("custom stack edits upgrade legacy profiles with transactional undo and persistence")
{
    using namespace Keire;
    using namespace KeireEditor;
    const auto undoService = CreateRef<UndoService>();
    const auto undo = undoService->CreateContext({.Name = "Post Processing"});
    std::vector<std::byte> saved;
    PostProcessProfileDefinition preview;
    PostProcessDocument document(
        {.Preview = [&](AssetId, const PostProcessProfileDefinition& definition) { preview = definition; },
         .Persist = [&](AssetId, std::span<const std::byte> bytes) { saved.assign(bytes.begin(), bytes.end()); }});
    PostProcessProfileDefinition initial;
    initial.SchemaVersion = 2;
    const AssetId profile(9, 1);
    document.Open(profile, PostProcessProfileAsset::Encode(initial), 1, undo);
    CHECK_FALSE(document.SetCustomEffects({}));
    CHECK(document.Definition().SchemaVersion == 2);
    const PostProcessOverride<std::vector<PostProcessCustomEffect>> stack{
        true,
        {{AssetId(9, 2), PostProcessCustomStage::AfterToneMap, true},
         {AssetId(9, 3), PostProcessCustomStage::AfterUi, false}}};
    REQUIRE(document.SetCustomEffects(stack));
    const auto edited = document.Definition();
    CHECK(edited.SchemaVersion == 3);
    CHECK(edited.CustomEffects == stack);
    CHECK(preview == edited);
    REQUIRE(document.Undo());
    CHECK(document.Definition() == initial);
    CHECK_FALSE(document.Dirty());
    REQUIRE(document.Redo());
    CHECK(document.Definition() == edited);
    CHECK_THROWS_AS((void)document.SetCustomEffects({true, {{profile}}}), std::invalid_argument);
    CHECK(document.Definition() == edited);
    document.Save();
    CHECK_FALSE(document.Dirty());
    CHECK(PostProcessProfileAsset::Decode(saved)->Definition() == edited);
    REQUIRE(document.SetCustomEffects({true, {}}));
    document.Discard();
    CHECK(document.Definition() == edited);
}
