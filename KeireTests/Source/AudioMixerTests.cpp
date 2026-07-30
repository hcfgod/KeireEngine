#include "Keire/Core.h"

#include <doctest/doctest.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace
{
    [[nodiscard]] constexpr Keire::AssetId Id(const std::uint64_t value) noexcept
    {
        return Keire::AssetId(0x415544494f4d4958ULL, value);
    }

    [[nodiscard]] Keire::AudioMixerDefinition MixerDefinition()
    {
        constexpr auto master = Id(1);
        constexpr auto music = Id(2);
        constexpr auto effectsBus = Id(3);
        constexpr auto send = Id(4);
        constexpr auto snapshot = Id(5);
        constexpr auto ducking = Id(6);
        constexpr auto impulseResponse = Id(7);

        const std::array effectTypes{
            Keire::AudioGraphNodeType::Gain,
            Keire::AudioGraphNodeType::LowPass,
            Keire::AudioGraphNodeType::HighPass,
            Keire::AudioGraphNodeType::Equalizer,
            Keire::AudioGraphNodeType::Compressor,
            Keire::AudioGraphNodeType::Limiter,
            Keire::AudioGraphNodeType::Gate,
            Keire::AudioGraphNodeType::Delay,
            Keire::AudioGraphNodeType::Chorus,
            Keire::AudioGraphNodeType::Distortion,
            Keire::AudioGraphNodeType::AlgorithmicReverb,
            Keire::AudioGraphNodeType::ConvolutionReverb,
            Keire::AudioGraphNodeType::Meter,
            Keire::AudioGraphNodeType::Capture,
        };
        std::vector<Keire::AudioMixerEffectDefinition> effects;
        effects.reserve(effectTypes.size());
        for (std::size_t index = 0; index < effectTypes.size(); ++index)
        {
            const auto type = effectTypes[index];
            effects.push_back({.Id = Id(100 + index),
                               .Name = "Effect " + std::to_string(index),
                               .Type = type,
                               .Parameters = {0.5F},
                               .ImpulseResponse = type == Keire::AudioGraphNodeType::ConvolutionReverb
                                                      ? impulseResponse
                                                      : Keire::AssetId{}});
        }

        Keire::AudioMixerDefinition definition;
        definition.MasterBus = master;
        definition.Buses = {
            {.Id = master, .Name = "Master", .Gain = 1.0F},
            {.Id = music,
             .Name = "Music",
             .Parent = master,
             .Gain = 0.8F,
             .Effects = std::move(effects),
             .Sends = {{.Id = send,
                        .DestinationBus = effectsBus,
                        .Stage = Keire::AudioMixerSendStage::PreFader,
                        .Gain = 0.25F}}},
            {.Id = effectsBus, .Name = "Effects", .Parent = master, .Mute = true, .Solo = false, .Gain = 0.9F},
        };
        definition.Snapshots = {
            {.Id = snapshot,
             .Name = "Gameplay",
             .Parameters =
                 {
                     {.Type = Keire::AudioMixerSnapshotParameterType::BusGain, .Target = music, .Value = 0.6F},
                     {.Type = Keire::AudioMixerSnapshotParameterType::BusMute, .Target = effectsBus, .Value = 0.0F},
                     {.Type = Keire::AudioMixerSnapshotParameterType::BusSolo, .Target = music, .Value = 1.0F},
                     {.Type = Keire::AudioMixerSnapshotParameterType::SendGain, .Target = send, .Value = 0.4F},
                     {.Type = Keire::AudioMixerSnapshotParameterType::EffectBypass, .Target = Id(100), .Value = 0.0F},
                     {.Type = Keire::AudioMixerSnapshotParameterType::EffectParameter,
                      .Target = Id(100),
                      .Parameter = 0,
                      .Value = 0.75F},
                 }},
        };
        definition.Ducking = {
            {.Id = ducking,
             .Name = "Music under effects",
             .SidechainBus = effectsBus,
             .TargetBus = music,
             .ThresholdDb = -18.0F,
             .Ratio = 6.0F,
             .AttackSeconds = 0.02F,
             .HoldSeconds = 0.05F,
             .ReleaseSeconds = 0.25F,
             .MaximumAttenuationDb = 9.0F},
        };
        return definition;
    }
} // namespace

TEST_CASE("Audio mixer schema round trips deterministically and stable IDs survive renames")
{
    auto definition = MixerDefinition();
    CHECK_NOTHROW(Keire::ValidateAudioMixer(definition));

    const auto encoded = Keire::AudioMixerAsset::Encode(definition);
    const auto decoded = Keire::AudioMixerAsset::Decode(encoded);
    REQUIRE(decoded);
    CHECK(decoded->Definition().SchemaVersion == 1);
    CHECK(decoded->Definition().MasterBus == Id(1));
    REQUIRE(decoded->Definition().Buses.size() == 3);
    CHECK(decoded->Definition().Buses[1].Effects.size() == 14);
    CHECK(decoded->Definition().Buses[1].Effects[0].Type == Keire::AudioGraphNodeType::Gain);
    CHECK(decoded->Definition().Buses[1].Effects[11].Type == Keire::AudioGraphNodeType::ConvolutionReverb);
    CHECK(decoded->Definition().Buses[1].Effects.back().Type == Keire::AudioGraphNodeType::Capture);
    CHECK(decoded->Definition().Buses[1].Sends.front().Stage == Keire::AudioMixerSendStage::PreFader);
    CHECK(decoded->Definition().Snapshots.front().Parameters.size() == 6);
    CHECK(decoded->Definition().Ducking.front().MaximumAttenuationDb == doctest::Approx(9.0F));
    CHECK(Keire::AudioMixerAsset::Encode(decoded->Definition()) == encoded);

    definition.Buses[1].Name = "Score";
    CHECK_NOTHROW(Keire::ValidateAudioMixer(definition));
    CHECK(definition.Buses[1].Id == Id(2));
    CHECK(definition.Buses[1].Sends.front().DestinationBus == Id(3));
    CHECK(definition.Snapshots.front().Parameters.front().Target == Id(2));
    CHECK(definition.Ducking.front().TargetBus == Id(2));
    const auto renamed = Keire::AudioMixerAsset::Decode(Keire::AudioMixerAsset::Encode(definition));
    CHECK(renamed->Definition().Buses[1].Name == "Score");
    CHECK(renamed->Definition().Buses[1].Id == Id(2));
}

TEST_CASE("Audio mixer validation rejects malformed hierarchy and routing cycles")
{
    auto definition = MixerDefinition();

    SUBCASE("Hierarchy cycle")
    {
        definition.Buses[1].Parent = definition.Buses[1].Id;
        CHECK_THROWS_WITH_AS(Keire::ValidateAudioMixer(definition), "Audio mixer routing contains a cycle.",
                             std::invalid_argument);
    }

    SUBCASE("Send and parent cycle")
    {
        definition.Buses[0].Sends.push_back({.Id = Id(50), .DestinationBus = definition.Buses[1].Id, .Gain = 1.0F});
        CHECK_THROWS_WITH_AS(Keire::ValidateAudioMixer(definition), "Audio mixer routing contains a cycle.",
                             std::invalid_argument);
    }

    SUBCASE("Missing send destination")
    {
        definition.Buses[1].Sends.front().DestinationBus = Id(999);
        CHECK_THROWS_WITH_AS(Keire::ValidateAudioMixer(definition), "Audio mixer send destination is unavailable.",
                             std::invalid_argument);
    }

    SUBCASE("Duplicate stable ID")
    {
        definition.Snapshots.front().Id = definition.Buses.front().Id;
        CHECK_THROWS_WITH_AS(Keire::ValidateAudioMixer(definition),
                             "Audio mixer contains an empty or duplicate stable ID.", std::invalid_argument);
    }

    SUBCASE("Input and output nodes cannot appear in an effect rack")
    {
        definition.Buses[1].Effects.front().Type = Keire::AudioGraphNodeType::Input;
        CHECK_THROWS_WITH_AS(Keire::ValidateAudioMixer(definition),
                             "Audio mixer contains an invalid or duplicate effect.", std::invalid_argument);
        definition.Buses[1].Effects.front().Type = Keire::AudioGraphNodeType::Output;
        CHECK_THROWS_WITH_AS(Keire::ValidateAudioMixer(definition),
                             "Audio mixer contains an invalid or duplicate effect.", std::invalid_argument);
    }
}

TEST_CASE("Audio mixer importer canonicalizes source and extracts sorted AudioClip dependencies")
{
    auto definition = MixerDefinition();
    constexpr auto secondImpulseResponse = Id(8);
    definition.Buses[2].Effects = {
        {.Id = Id(200),
         .Name = "Hall",
         .Type = Keire::AudioGraphNodeType::ConvolutionReverb,
         .ImpulseResponse = secondImpulseResponse},
        {.Id = Id(201),
         .Name = "Shared room",
         .Type = Keire::AudioGraphNodeType::ConvolutionReverb,
         .ImpulseResponse = Id(7)},
    };

    const auto dependencies = Keire::AudioMixerDependencies(definition);
    CHECK(dependencies == std::vector<Keire::AssetId>{Id(7), secondImpulseResponse});

    const auto importer = Keire::CreateAudioMixerAssetImporter();
    CHECK(importer.Name == "Keire.AudioMixer");
    CHECK(importer.Type == Keire::AudioMixerAsset::StaticType());
    CHECK(importer.Extensions == std::vector<std::string>{".keiremixer"});
    REQUIRE(importer.Import);
    REQUIRE(importer.ContextualImport);
    const auto source = Keire::AudioMixerAsset::Encode(definition);
    CHECK(importer.Import(source) == source);
    const auto output = importer.ContextualImport({}, source);
    CHECK(output.Bytes == source);
    CHECK(output.AssetDependencies == dependencies);

    const auto decoder = Keire::CreateAudioMixerAssetDecoder();
    CHECK(decoder.Type == Keire::AudioMixerAsset::StaticType());
    CHECK(decoder.Fallback->Type() == Keire::AudioMixerAsset::StaticType());
    CHECK(Keire::DynamicRefCast<Keire::AudioMixerAsset>(decoder.Decode(source))->Definition().Buses.size() == 3);
}

TEST_CASE("Audio meter snapshots are owner-thread affine, ordered, and bounded")
{
    Keire::AudioSystemSpecification specification;
    specification.Mode = Keire::AudioMode::Headless;
    specification.MaximumMeterReadings = 2;
    auto audio = Keire::CreateRef<Keire::AudioSystem>(specification);

    Keire::AudioMeterSnapshot snapshot;
    snapshot.Revision = 1;
    snapshot.DroppedReadings = 3;
    snapshot.Readings = {
        {.Bus = Id(2), .Peak = 1.1F, .Rms = 0.5F, .Clipping = true},
        {.Bus = Id(1), .Peak = 0.8F, .Rms = 0.25F},
    };
    CHECK_NOTHROW(audio->SubmitMeterSnapshot(snapshot));
    const auto latest = audio->LatestMeterSnapshot();
    CHECK(latest.Revision == 1);
    CHECK(latest.DroppedReadings == 3);
    REQUIRE(latest.Readings.size() == 2);
    CHECK(latest.Readings[0].Bus == Id(1));
    CHECK(latest.Readings[1].Bus == Id(2));
    CHECK(latest.Readings[1].Clipping);

    CHECK_THROWS_WITH_AS(audio->SubmitMeterSnapshot(snapshot), "Audio meter snapshot revision is stale.",
                         std::invalid_argument);

    auto overflow = snapshot;
    overflow.Revision = 2;
    overflow.Readings.push_back({.Bus = Id(3), .Peak = 0.2F, .Rms = 0.1F});
    CHECK_THROWS_WITH_AS(audio->SubmitMeterSnapshot(overflow), "Audio meter snapshot header is invalid.",
                         std::invalid_argument);

    auto invalid = snapshot;
    invalid.Revision = 2;
    invalid.Readings[1].Bus = invalid.Readings[0].Bus;
    CHECK_THROWS_WITH_AS(audio->SubmitMeterSnapshot(invalid),
                         "Audio meter snapshot contains an invalid or duplicate reading.", std::invalid_argument);

    std::atomic<bool> rejected = false;
    std::thread worker(
        [audio, &rejected]
        {
            try
            {
                (void)audio->LatestMeterSnapshot();
            }
            catch (const std::logic_error&)
            {
                rejected = true;
            }
        });
    worker.join();
    CHECK(rejected.load());

    audio->Close();
}

TEST_CASE("Audio voices pause, seek, resume, and report deterministic playback state")
{
    Keire::AudioSystemSpecification specification;
    specification.Mode = Keire::AudioMode::Headless;
    auto audio = Keire::CreateRef<Keire::AudioSystem>(specification);

    auto clip = std::make_shared<Keire::AudioClipData>();
    clip->SampleRate = 48'000;
    clip->Channels = 1;
    clip->Frames = 8;
    clip->Samples.assign(8, 0.5F);
    Keire::AudioPlaybackRequest request;
    request.Clip = clip;
    request.Spatial = false;
    const auto voice = audio->Play(std::move(request));
    REQUIRE(voice);

    (void)audio->RenderVoicesOffline(2);
    REQUIRE(audio->Voice(voice));
    CHECK(audio->Voice(voice)->Frame == 2);
    CHECK(audio->Voice(voice)->DurationFrames == 8);
    CHECK(audio->Voice(voice)->Playing);
    CHECK_FALSE(audio->Voice(voice)->Paused);

    CHECK(audio->Pause(voice));
    (void)audio->RenderVoicesOffline(3);
    REQUIRE(audio->Voice(voice));
    CHECK(audio->Voice(voice)->Frame == 2);
    CHECK_FALSE(audio->Voice(voice)->Playing);
    CHECK(audio->Voice(voice)->Paused);
    CHECK(audio->Statistics().AudibleVoices == 0);

    CHECK(audio->Seek(voice, 6));
    CHECK(audio->Voice(voice)->Frame == 6);
    CHECK_THROWS_AS((void)audio->Seek(voice, 9), std::out_of_range);
    CHECK(audio->Pause(voice, false));
    (void)audio->RenderVoicesOffline(1);
    CHECK(audio->Voice(voice)->Frame == 7);
    CHECK(audio->Stop(voice));
    CHECK_FALSE(audio->Voice(voice));
    audio->Close();
}
