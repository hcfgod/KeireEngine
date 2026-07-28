#include "KeireClient/Editor/AudioMixerDocument.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
    [[nodiscard]] constexpr Keire::AssetId Id(const std::uint64_t value) noexcept
    {
        return Keire::AssetId(0x4d49584552444f43ULL, value);
    }

    [[nodiscard]] Keire::AudioMixerDefinition Definition()
    {
        Keire::AudioMixerDefinition result;
        result.MasterBus = Id(1);
        result.Buses = {
            {.Id = Id(1), .Name = "Master"},
            {.Id = Id(2),
             .Name = "Music",
             .Parent = Id(1),
             .Effects = {{.Id = Id(10), .Name = "Gain", .Type = Keire::AudioGraphNodeType::Gain, .Parameters = {1.0F}}},
             .Sends = {{.Id = Id(20),
                        .DestinationBus = Id(3),
                        .Stage = Keire::AudioMixerSendStage::PostFader,
                        .Gain = 0.25F}}},
            {.Id = Id(3), .Name = "SFX", .Parent = Id(1)},
        };
        result.Snapshots = {
            {.Id = Id(30),
             .Name = "Gameplay",
             .Parameters =
                 {
                     {.Type = Keire::AudioMixerSnapshotParameterType::BusGain, .Target = Id(2), .Value = 0.75F},
                 }},
        };
        return result;
    }

    [[nodiscard]] KeireEditor::StableNodeId NodeFor(const KeireEditor::AudioMixerRoutingGraph& graph,
                                                    const Keire::AssetId bus)
    {
        const auto found = std::ranges::find_if(graph.Buses, [bus](const auto& entry) { return entry.second == bus; });
        if (found == graph.Buses.end())
            throw std::logic_error("Expected bus projection was unavailable.");
        return found->first;
    }
} // namespace

TEST_CASE("Audio Mixer document coordinates typed preview undo save discard and reload")
{
    auto undoService = Keire::CreateRef<Keire::UndoService>();
    auto undo = undoService->CreateContext({.Name = "Audio Mixer"});
    Keire::AudioMixerDefinition preview;
    std::vector<std::byte> persisted;
    std::size_t previewCount = 0;
    std::size_t stopCount = 0;
    std::size_t persistCount = 0;
    KeireEditor::AudioMixerDocument document(
        {.Preview =
             [&](const Keire::AssetId asset, const Keire::AudioMixerDefinition& definition)
         {
             CHECK(asset == Id(100));
             preview = definition;
             ++previewCount;
         },
         .StopPreview =
             [&](const Keire::AssetId asset)
         {
             CHECK(asset == Id(100));
             ++stopCount;
         },
         .Persist =
             [&](const Keire::AssetId asset, const std::span<const std::byte> bytes)
         {
             CHECK(asset == Id(100));
             persisted.assign(bytes.begin(), bytes.end());
             ++persistCount;
         }});

    const auto authored = Definition();
    document.Open(Id(100), Keire::AudioMixerAsset::Encode(authored), 1, undo);
    CHECK_FALSE(document.Dirty());
    CHECK(preview == authored);
    CHECK(previewCount == 1);
    CHECK(persistCount == 0);
    CHECK(document.SelectedBus() == Id(1));

    const auto initialGraph = document.RoutingGraph();
    CHECK(initialGraph.Nodes.size() == 3);
    CHECK(initialGraph.Connections.size() == 3);
    const auto musicNode = NodeFor(initialGraph, Id(2));

    CHECK(document.RenameBus(Id(2), "Score"));
    CHECK(document.Dirty());
    CHECK(document.Definition().Buses[1].Id == Id(2));
    CHECK(document.Definition().Buses[1].Name == "Score");
    CHECK(document.Definition().Buses[1].Sends.front().DestinationBus == Id(3));
    CHECK(document.Definition().Snapshots.front().Parameters.front().Target == Id(2));
    CHECK(NodeFor(document.RoutingGraph(), Id(2)) == musicNode);
    CHECK(preview.Buses[1].Name == "Score");
    CHECK(persistCount == 0);

    CHECK(document.Undo());
    CHECK(document.Definition().Buses[1].Name == "Music");
    CHECK(document.Redo());
    CHECK(document.Definition().Buses[1].Name == "Score");
    CHECK(document.AddBus({.Id = Id(4), .Name = "Voice", .Parent = Id(1)}));
    document.SelectBus(Id(4));
    CHECK(document.SelectedBus() == Id(4));
    CHECK(document.Undo());
    CHECK(document.SelectedBus() == Id(1));
    CHECK(document.Redo());
    CHECK(document.SelectedBus() == Id(1));

    CHECK(document.AddEffect(
        Id(2), {.Id = Id(11), .Name = "Limiter", .Type = Keire::AudioGraphNodeType::Limiter, .Parameters = {0.9F}}));
    CHECK(document.MoveEffect(Id(2), Id(11), 0));
    CHECK(document.Definition().Buses[1].Effects.front().Id == Id(11));
    CHECK(document.RenameEffect(Id(11), "Safety Limiter"));
    CHECK(document.AddSend(
        Id(3), {.Id = Id(21), .DestinationBus = Id(1), .Stage = Keire::AudioMixerSendStage::PreFader, .Gain = 0.1F}));
    CHECK(document.AddSnapshot({.Id = Id(31), .Name = "Paused"}));
    CHECK(document.RenameSnapshot(Id(31), "Pause Menu"));
    CHECK(document.AddDucking({.Id = Id(40), .Name = "SFX over music", .SidechainBus = Id(3), .TargetBus = Id(2)}));

    document.Save();
    CHECK_FALSE(document.Dirty());
    CHECK(persistCount == 1);
    const auto saved = Keire::AudioMixerAsset::Decode(persisted);
    CHECK(saved->Definition() == document.Definition());
    const auto previewCountAfterSave = previewCount;
    CHECK(document.Reload(document.Definition(), 2) == KeireEditor::AssetDocumentReloadResult::Unchanged);
    CHECK(document.Revision() == 2);
    CHECK(previewCount == previewCountAfterSave);

    auto external = document.Definition();
    external.Buses[2].Name = "Effects";
    CHECK(document.Reload(external, 3) == KeireEditor::AssetDocumentReloadResult::Applied);
    CHECK(document.Definition().Buses[2].Name == "Effects");
    CHECK_FALSE(document.Dirty());

    CHECK(document.RenameBus(Id(3), "Local Effects"));
    auto newerExternal = external;
    newerExternal.Buses[2].Name = "External Effects";
    CHECK(document.Reload(newerExternal, 4) == KeireEditor::AssetDocumentReloadResult::LocalChanges);
    document.Discard();
    CHECK(document.Definition().Buses[2].Name == "Effects");
    CHECK_FALSE(document.Dirty());

    document.Close();
    CHECK(stopCount == 1);
    CHECK_FALSE(document.IsOpen());
    undoService->Close();
}

TEST_CASE("Audio Mixer document rejects invalid stable identity and routing edits transactionally")
{
    std::size_t previews = 0;
    std::size_t stops = 0;
    KeireEditor::AudioMixerDocument document(
        {.Preview = [&](const Keire::AssetId, const Keire::AudioMixerDefinition&) { ++previews; },
         .StopPreview = [&](const Keire::AssetId) { ++stops; },
         .Persist = [](const Keire::AssetId, const std::span<const std::byte>) {}});
    const auto authored = Definition();
    document.Open(Id(101), authored, 1);
    const auto baselinePreviews = previews;

    CHECK_THROWS_AS((void)document.AddBus({.Id = Id(2), .Name = "Duplicate", .Parent = Id(1)}), std::invalid_argument);
    CHECK(document.Definition() == authored);
    CHECK(previews == baselinePreviews);

    CHECK_THROWS_AS((void)document.AddSend(
                        Id(1), {.Id = Id(22), .DestinationBus = Id(2), .Stage = Keire::AudioMixerSendStage::PostFader}),
                    std::invalid_argument);
    CHECK(document.Definition() == authored);
    CHECK(previews == baselinePreviews);

    const std::string malformed = "{\"schemaVersion\":99}";
    const auto malformedBytes = std::as_bytes(std::span(malformed));
    CHECK_THROWS_AS(document.Open(Id(102), malformedBytes, 1), std::invalid_argument);
    CHECK(document.Asset() == Id(101));
    CHECK(document.Definition() == authored);

    document.Close();
    CHECK(stops == 1);
}

TEST_CASE("Discarding a newly created Audio Mixer stops preview without persistence")
{
    std::size_t stops = 0;
    std::size_t persists = 0;
    KeireEditor::AudioMixerDocument document(
        {.StopPreview = [&](const Keire::AssetId) { ++stops; },
         .Persist = [&](const Keire::AssetId, const std::span<const std::byte>) { ++persists; }});
    document.Create(Id(102));
    CHECK(document.Dirty());
    document.Discard();
    CHECK_FALSE(document.IsOpen());
    CHECK(stops == 1);
    CHECK(persists == 0);
}

TEST_CASE("Audio Mixer stable-ID removals clean dependent authored state")
{
    KeireEditor::AudioMixerDocument document(
        {.Persist = [](const Keire::AssetId, const std::span<const std::byte>) {}});
    auto authored = Definition();
    authored.Snapshots.front().Parameters.push_back(
        {.Type = Keire::AudioMixerSnapshotParameterType::SendGain, .Target = Id(20), .Value = 0.5F});
    authored.Snapshots.front().Parameters.push_back(
        {.Type = Keire::AudioMixerSnapshotParameterType::EffectBypass, .Target = Id(10), .Value = 0.0F});
    authored.Ducking.push_back({.Id = Id(40), .Name = "Effects over music", .SidechainBus = Id(3), .TargetBus = Id(2)});
    document.Open(Id(103), authored, 1);

    CHECK(document.RemoveEffect(Id(10)));
    CHECK(document.Definition().Buses[1].Effects.empty());
    CHECK(std::ranges::none_of(document.Definition().Snapshots.front().Parameters,
                               [](const auto& parameter) { return parameter.Target == Id(10); }));

    CHECK(document.RemoveBus(Id(3)));
    CHECK(std::ranges::none_of(document.Definition().Buses, [](const auto& bus) { return bus.Id == Id(3); }));
    CHECK(document.Definition().Buses[1].Sends.empty());
    CHECK(std::ranges::none_of(document.Definition().Snapshots.front().Parameters,
                               [](const auto& parameter) { return parameter.Target == Id(20); }));
    CHECK(document.Definition().Ducking.empty());
    CHECK_THROWS_AS((void)document.RemoveBus(Id(1)), std::invalid_argument);
}
