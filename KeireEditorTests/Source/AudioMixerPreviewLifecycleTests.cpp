#include "KeireClient/Editor/AssetDocumentHost.h"
#include "KeireClient/Editor/AudioMixerPreviewSession.h"

#include <doctest/doctest.h>

#include <cstddef>
#include <span>
#include <stdexcept>
#include <vector>

namespace
{
    constexpr Keire::AssetId Mixer(30, 1);
    constexpr Keire::AssetId Snapshot(30, 2);
    using MixerHost = KeireEditor::AssetDocumentHost<Keire::AudioMixerDefinition>;

    [[nodiscard]] Keire::AudioMixerDefinition Definition(const float gain)
    {
        auto definition = Keire::AudioMixerAsset::DefaultDefinition();
        definition.Buses.front().Gain = gain;
        definition.Snapshots.push_back({.Id = Snapshot,
                                        .Name = "Quiet",
                                        .Parameters = {{.Type = Keire::AudioMixerSnapshotParameterType::BusGain,
                                                        .Target = definition.MasterBus,
                                                        .Value = 0.25F}}});
        return definition;
    }

    struct DocumentFixture final
    {
        std::vector<std::byte> Source = Keire::AudioMixerAsset::Encode(Definition(1.0F));
        float Published = 1.0F;
        float Submitted = 1.0F;
        bool SourceMissing = false;
        bool PersistFails = false;
        KeireEditor::AudioMixerPreviewSession Session{
            {.ReadSource =
                 [this](Keire::AssetId)
             {
                 if (SourceMissing)
                 {
                     throw std::runtime_error("source unavailable");
                 }
                 return Keire::AudioMixerAsset::Decode(Source);
             },
             .ResolveImpulseResponses = [](const Keire::AudioMixerDefinition&)
             { return Keire::AudioMixerImpulseResponses{}; },
             .Publish =
                 [this](Keire::AssetId, const Keire::Ref<Keire::AudioMixerAsset>& mixer)
             {
                 Published = mixer->Definition().Buses.front().Gain;
                 return true;
             },
             .Submit = [this](Keire::AssetId, const Keire::AudioMixerDefinition& definition,
                              const Keire::AudioMixerImpulseResponses&)
             { Submitted = definition.Buses.front().Gain; }}};

        [[nodiscard]] MixerHost::Specification Specification()
        {
            return {.Validate = [](const Keire::AudioMixerDefinition& definition)
                    { Keire::ValidateAudioMixer(definition); },
                    .Encode = [](const Keire::AudioMixerDefinition& definition)
                    { return Keire::AudioMixerAsset::Encode(definition); },
                    .Preview = [this](const Keire::AssetId asset, const Keire::AudioMixerDefinition& definition)
                    { Session.Preview(asset, definition); },
                    .CancelPreview = [this](Keire::AssetId) { Session.Stop(); },
                    .Persist =
                        [this](const Keire::AssetId asset, const std::span<const std::byte> bytes)
                    {
                        const auto baseline =
                            Session.PrepareSavedBaseline(asset, Keire::AudioMixerAsset::Decode(bytes)->Definition());
                        if (PersistFails)
                        {
                            throw std::runtime_error("atomic write failed");
                        }
                        Source.assign(bytes.begin(), bytes.end());
                        Session.CommitSavedBaseline(asset, baseline);
                    }};
        }
    };
} // namespace

TEST_CASE("Audio mixer document host audition save undo reload and close use retained preview state")
{
    DocumentFixture fixture;
    auto undoService = Keire::CreateRef<Keire::UndoService>();
    auto undo = undoService->CreateContext({.Name = "Mixer lifecycle"});
    MixerHost document(fixture.Specification());
    document.Open(Mixer, Keire::AudioMixerAsset::Decode(fixture.Source)->Definition(), 1, undo);
    const auto original = fixture.Source;
    document.PreviewTransient(Keire::BlendAudioMixerSnapshot(document.Draft(), Snapshot, 0.5F));
    CHECK(fixture.Submitted == 0.625F);
    CHECK(fixture.Source == original);
    CHECK_FALSE(document.Dirty());
    CHECK_FALSE(undo->CanUndo());
    REQUIRE(document.Edit("Authored gain", Definition(0.75F)));
    document.PreviewTransient(Keire::BlendAudioMixerSnapshot(document.Draft(), Snapshot, 1.0F));
    CHECK(fixture.Submitted == 0.25F);
    REQUIRE(document.Undo());
    CHECK(fixture.Submitted == 1.0F);
    REQUIRE(document.Redo());
    CHECK(fixture.Submitted == 0.75F);
    document.PreviewTransient(Keire::BlendAudioMixerSnapshot(document.Draft(), Snapshot, 1.0F));
    document.Save();
    CHECK(Keire::AudioMixerAsset::Decode(fixture.Source)->Definition().Buses.front().Gain == 0.75F);
    CHECK_FALSE(document.Dirty());

    fixture.SourceMissing = true;
    document.Close();
    CHECK(fixture.Published == 0.75F);
    CHECK(fixture.Submitted == 0.75F);
    CHECK_FALSE(fixture.Session.Active());

    fixture.SourceMissing = false;
    document.Open(Mixer, Keire::AudioMixerAsset::Decode(fixture.Source)->Definition(), 2, undo);
    CHECK(fixture.Submitted == 0.75F);
    fixture.Source = Keire::AudioMixerAsset::Encode(Definition(0.6F));
    CHECK(document.Reload(Keire::AudioMixerAsset::Decode(fixture.Source)->Definition(), 3) ==
          KeireEditor::AssetDocumentReloadResult::Applied);
    CHECK(fixture.Submitted == 0.6F);
    document.Close();
    CHECK(fixture.Submitted == 0.6F);
    CHECK_FALSE(fixture.Session.Active());
    undoService->Close();
}

TEST_CASE("Audio mixer document host failed save and repeated hide restore the last saved mix")
{
    DocumentFixture fixture;
    MixerHost document(fixture.Specification());
    document.Open(Mixer, Keire::AudioMixerAsset::Decode(fixture.Source)->Definition(), 1);
    REQUIRE(document.Edit("Unsaved gain", Definition(0.5F)));
    fixture.PersistFails = true;
    CHECK_THROWS_WITH(document.Save(), "atomic write failed");
    CHECK(document.Dirty());
    CHECK(Keire::AudioMixerAsset::Decode(fixture.Source)->Definition().Buses.front().Gain == 1.0F);
    fixture.Session.Stop();
    CHECK(fixture.Submitted == 1.0F);
    document.RestorePreview();
    CHECK(fixture.Submitted == 0.5F);
    fixture.SourceMissing = true;
    document.Close();
    CHECK(fixture.Submitted == 1.0F);
    CHECK_FALSE(fixture.Session.Active());
}
