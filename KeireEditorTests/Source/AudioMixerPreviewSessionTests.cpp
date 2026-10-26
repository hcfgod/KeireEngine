#include "KeireClient/Editor/AudioMixerPreviewSession.h"

#include <doctest/doctest.h>

#include <functional>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace
{
    constexpr Keire::AssetId Mixer(10, 1);
    constexpr Keire::AssetId OtherMixer(10, 2);

    [[nodiscard]] Keire::AudioMixerDefinition Definition(const float gain)
    {
        auto definition = Keire::AudioMixerAsset::DefaultDefinition();
        definition.Buses.front().Gain = gain;
        return definition;
    }

    struct PreviewFixture final
    {
        Keire::Ref<Keire::AudioMixerAsset> Source = Keire::CreateRef<Keire::AudioMixerAsset>(Definition(1.0F));
        float Published = 1.0F;
        float Submitted = 1.0F;
        bool SourceMissing = false;
        bool ResolveFails = false;
        bool PublishRejected = false;
        bool PublishThrowsAfterCommit = false;
        int SubmitFailures = 0;
        int Publications = 0;
        int Submissions = 0;
        std::vector<Keire::AssetId> PublishedAssets;
        std::function<void()> AfterPublish;

        [[nodiscard]] KeireEditor::AudioMixerPreviewSession CreateSession()
        {
            return KeireEditor::AudioMixerPreviewSession(
                {.ReadSource =
                     [this](Keire::AssetId)
                 {
                     if (SourceMissing)
                     {
                         throw std::runtime_error("missing source");
                     }
                     return Source;
                 },
                 .ResolveImpulseResponses =
                     [this](const Keire::AudioMixerDefinition&)
                 {
                     if (ResolveFails)
                     {
                         throw std::runtime_error("IR unavailable");
                     }
                     return Keire::AudioMixerImpulseResponses{};
                 },
                 .Publish =
                     [this](const Keire::AssetId asset, const Keire::Ref<Keire::AudioMixerAsset>& mixer)
                 {
                     ++Publications;
                     if (PublishRejected)
                     {
                         return false;
                     }
                     Published = mixer->Definition().Buses.front().Gain;
                     PublishedAssets.push_back(asset);
                     if (const auto callback = std::exchange(AfterPublish, {}))
                     {
                         callback();
                     }
                     if (std::exchange(PublishThrowsAfterCommit, false))
                     {
                         throw std::runtime_error("publication callback failed");
                     }
                     return true;
                 },
                 .Submit =
                     [this](Keire::AssetId, const Keire::AudioMixerDefinition& definition,
                            const Keire::AudioMixerImpulseResponses&)
                 {
                     ++Submissions;
                     if (SubmitFailures > 0)
                     {
                         --SubmitFailures;
                         throw std::runtime_error("device submit failed");
                     }
                     Submitted = definition.Buses.front().Gain;
                 }});
        }
    };
} // namespace

TEST_CASE("Audio mixer preview preflights resources and source before shared mutation")
{
    PreviewFixture fixture;
    auto session = fixture.CreateSession();
    fixture.ResolveFails = true;
    CHECK_THROWS_WITH(session.Preview(Mixer, Definition(0.25F)), "IR unavailable");
    CHECK(fixture.Publications == 0);
    CHECK(fixture.Submissions == 0);
    CHECK_FALSE(session.Active());

    fixture.ResolveFails = false;
    fixture.SourceMissing = true;
    CHECK_THROWS_WITH(session.Preview(Mixer, Definition(0.25F)), "missing source");
    CHECK(fixture.Publications == 0);
    CHECK_FALSE(session.Active());
    CHECK_THROWS_AS(session.Preview({}, Definition(0.25F)), std::invalid_argument);
}

TEST_CASE("Audio mixer preview restores its last good value after device rejection")
{
    PreviewFixture fixture;
    auto session = fixture.CreateSession();
    session.Preview(Mixer, Definition(0.5F));
    fixture.SubmitFailures = 1;
    CHECK_THROWS_WITH(session.Preview(Mixer, Definition(0.25F)), "device submit failed");
    CHECK(fixture.Published == 0.5F);
    CHECK(fixture.Submitted == 0.5F);
    CHECK(session.Active());

    fixture.ResolveFails = true;
    const int publications = fixture.Publications;
    CHECK_THROWS_WITH(session.Preview(Mixer, Definition(0.75F)), "IR unavailable");
    CHECK(fixture.Publications == publications);
    CHECK(fixture.Published == 0.5F);
    session.Stop();
    CHECK(fixture.Published == 1.0F);
    CHECK(fixture.Submitted == 1.0F);
    CHECK_FALSE(session.Active());
}

TEST_CASE("Audio mixer preview rolls back publication callbacks that throw after commit")
{
    PreviewFixture fixture;
    auto session = fixture.CreateSession();
    fixture.PublishThrowsAfterCommit = true;
    CHECK_THROWS_WITH(session.Preview(Mixer, Definition(0.25F)), "publication callback failed");
    CHECK(fixture.Published == 1.0F);
    CHECK(fixture.Submitted == 1.0F);
    CHECK(session.Active());
    session.Stop();
    CHECK_FALSE(session.Active());
}

TEST_CASE("Audio mixer preview retains ownership after rollback and stop fail, allowing a retry")
{
    PreviewFixture fixture;
    auto session = fixture.CreateSession();
    session.Preview(Mixer, Definition(0.5F));
    fixture.SubmitFailures = 2;
    CHECK_THROWS_WITH(session.Preview(Mixer, Definition(0.25F)), "device submit failed");
    CHECK(session.Active());
    fixture.PublishRejected = true;
    CHECK_THROWS_AS(session.Stop(), std::runtime_error);
    CHECK(session.Active());
    fixture.PublishRejected = false;
    fixture.SourceMissing = true;
    session.Stop();
    CHECK(fixture.Published == 1.0F);
    CHECK(fixture.Submitted == 1.0F);
    CHECK_FALSE(session.Active());
    const int publications = fixture.Publications;
    session.Stop();
    CHECK(fixture.Publications == publications);
}

TEST_CASE("Audio mixer preview restores the authored source, not a reopened unsaved draft")
{
    PreviewFixture fixture;
    auto session = fixture.CreateSession();
    session.Preview(Mixer, Definition(0.2F));
    session.Stop();
    CHECK(fixture.Published == 1.0F);
    session.Preview(Mixer, Definition(0.2F));
    fixture.SourceMissing = true;
    session.Stop();
    CHECK(fixture.Published == 1.0F);
    CHECK(fixture.Submitted == 1.0F);
}

TEST_CASE("Audio mixer preview prefers fresh valid source but retains a saved fallback")
{
    PreviewFixture fixture;
    auto session = fixture.CreateSession();
    session.Preview(Mixer, Definition(0.2F));
    fixture.Source = Keire::CreateRef<Keire::AudioMixerAsset>(Definition(0.75F));
    session.Stop();
    CHECK(fixture.Submitted == 0.75F);

    session.Preview(Mixer, Definition(0.2F));
    const auto saved = session.PrepareSavedBaseline(Mixer, Definition(0.6F));
    session.CommitSavedBaseline(Mixer, saved);
    fixture.SourceMissing = true;
    fixture.ResolveFails = true;
    session.Stop();
    CHECK(fixture.Published == 0.6F);
    CHECK(fixture.Submitted == 0.6F);
    CHECK_FALSE(session.Active());
}

TEST_CASE("Audio mixer failed persistence does not advance the retained baseline")
{
    PreviewFixture fixture;
    auto session = fixture.CreateSession();
    CHECK_FALSE(session.PrepareSavedBaseline(Mixer, Definition(0.6F)));
    session.Preview(Mixer, Definition(0.2F));
    CHECK(session.PrepareSavedBaseline(Mixer, Definition(0.6F)));
    // The file write fails, so CommitSavedBaseline is deliberately not called.
    CHECK_FALSE(session.PrepareSavedBaseline(OtherMixer, Definition(0.6F)));
    session.CommitSavedBaseline(OtherMixer, {});
    fixture.SourceMissing = true;
    session.Stop();
    CHECK(fixture.Submitted == 1.0F);
}

TEST_CASE("Audio mixer switching restores the old asset before publishing the next")
{
    PreviewFixture fixture;
    auto session = fixture.CreateSession();
    session.Preview(Mixer, Definition(0.2F));
    session.Preview(OtherMixer, Definition(0.3F));
    REQUIRE(fixture.PublishedAssets.size() == 3);
    CHECK(fixture.PublishedAssets[0] == Mixer);
    CHECK(fixture.PublishedAssets[1] == Mixer);
    CHECK(fixture.PublishedAssets[2] == OtherMixer);
    session.Stop();
    CHECK_FALSE(session.Active());
}

TEST_CASE("Audio mixer preview rejects reentrant restoration before mutating session ownership")
{
    PreviewFixture fixture;
    auto session = fixture.CreateSession();
    fixture.AfterPublish = [&session] { session.Stop(); };
    CHECK_THROWS_AS(session.Preview(Mixer, Definition(0.2F)), std::logic_error);
    CHECK(session.Active());
    CHECK(fixture.Published == 1.0F);
    CHECK(fixture.Submitted == 1.0F);
    session.Stop();
    CHECK_FALSE(session.Active());
}

TEST_CASE("Audio mixer preview retains decoded impulse responses for missing-source restoration")
{
    constexpr Keire::AssetId impulse(20, 1);
    auto clip = std::make_shared<Keire::AudioClipData>();
    std::weak_ptr<const Keire::AudioClipData> lifetime = clip;
    bool sourceMissing = false;
    bool resolveFails = false;
    bool submittedResponse = false;
    auto definition = Definition(1.0F);
    definition.Buses.front().Effects.push_back({.Id = Keire::AssetId(20, 2),
                                                .Name = "Convolution",
                                                .Type = Keire::AudioGraphNodeType::ConvolutionReverb,
                                                .ImpulseResponse = impulse});
    auto source = Keire::CreateRef<Keire::AudioMixerAsset>(definition);
    KeireEditor::AudioMixerPreviewSession session(
        {.ReadSource =
             [&](Keire::AssetId)
         {
             if (sourceMissing)
             {
                 throw std::runtime_error("missing source");
             }
             return source;
         },
         .ResolveImpulseResponses =
             [&](const Keire::AudioMixerDefinition&)
         {
             if (resolveFails)
             {
                 throw std::runtime_error("IR no longer available");
             }
             return Keire::AudioMixerImpulseResponses{{impulse, clip}};
         },
         .Publish = [](Keire::AssetId, const Keire::Ref<Keire::AudioMixerAsset>&) { return true; },
         .Submit =
             [&](Keire::AssetId, const Keire::AudioMixerDefinition&, const Keire::AudioMixerImpulseResponses& responses)
         { submittedResponse = responses.at(impulse) == lifetime.lock(); }});
    session.Preview(Mixer, definition);
    clip.reset();
    CHECK_FALSE(lifetime.expired());
    sourceMissing = true;
    resolveFails = true;
    session.Stop();
    CHECK(submittedResponse);
    CHECK(lifetime.expired());
}
