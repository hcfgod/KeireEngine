#include "KeireClient/Editor/AudioMixerDocument.h"

#include <algorithm>
#include <bit>
#include <cstdint>
#include <iterator>
#include <ranges>
#include <set>
#include <stdexcept>
#include <utility>

namespace KeireEditor
{
    namespace
    {
        [[nodiscard]] StableNodeId StableCanvasId(const Keire::AssetId id, const std::uint64_t salt) noexcept
        {
            std::uint64_t value = id.High() ^ std::rotl(id.Low(), 29) ^ salt;
            value += 0x9e3779b97f4a7c15ULL;
            value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
            value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
            value ^= value >> 31U;
            return value == 0 ? 1 : value;
        }

        [[nodiscard]] Keire::AudioMixerBusDefinition& RequireBus(Keire::AudioMixerDefinition& definition,
                                                                 const Keire::AssetId id)
        {
            const auto found = std::ranges::find(definition.Buses, id, &Keire::AudioMixerBusDefinition::Id);
            if (found == definition.Buses.end())
                throw std::invalid_argument("Audio Mixer bus is unavailable.");
            return *found;
        }

        [[nodiscard]] std::size_t BusDepth(const Keire::AudioMixerDefinition& definition,
                                           const Keire::AudioMixerBusDefinition& bus)
        {
            std::size_t result = 0;
            auto parent = bus.Parent;
            while (parent)
            {
                const auto found = std::ranges::find(definition.Buses, parent, &Keire::AudioMixerBusDefinition::Id);
                if (found == definition.Buses.end())
                    throw std::logic_error("Validated Audio Mixer hierarchy became unavailable.");
                ++result;
                parent = found->Parent;
            }
            return result;
        }
    } // namespace

    AudioMixerDocument::AudioMixerDocument(AudioMixerDocumentSpecification specification)
        : m_Host(
              {.Validate = [](const Keire::AudioMixerDefinition& definition) { Keire::ValidateAudioMixer(definition); },
               .Encode = [](const Keire::AudioMixerDefinition& definition)
               { return Keire::AudioMixerAsset::Encode(definition); },
               .Preview = std::move(specification.Preview),
               .CancelPreview = std::move(specification.StopPreview),
               .Persist = std::move(specification.Persist)})
    {
    }

    void AudioMixerDocument::Open(const Keire::AssetId asset, const std::span<const std::byte> bytes,
                                  const std::uint64_t revision, Keire::Ref<Keire::UndoContext> undo)
    {
        Open(asset, Keire::AudioMixerAsset::Decode(bytes)->Definition(), revision, std::move(undo));
    }

    void AudioMixerDocument::Open(const Keire::AssetId asset, Keire::AudioMixerDefinition definition,
                                  const std::uint64_t revision, Keire::Ref<Keire::UndoContext> undo)
    {
        m_Host.Open(asset, std::move(definition), revision, std::move(undo));
        m_SelectedBus = m_Host.Draft().MasterBus;
        ReconcileSelection();
    }

    void AudioMixerDocument::Create(const Keire::AssetId asset, Keire::AudioMixerDefinition definition,
                                    Keire::Ref<Keire::UndoContext> undo)
    {
        m_Host.Create(asset, std::move(definition), std::move(undo));
        m_SelectedBus = m_Host.Draft().MasterBus;
        ReconcileSelection();
    }

    void AudioMixerDocument::Save() { m_Host.Save(); }

    void AudioMixerDocument::Discard()
    {
        m_Host.Discard();
        ReconcileSelection();
    }

    AssetDocumentReloadResult AudioMixerDocument::Reload(const std::span<const std::byte> bytes,
                                                         const std::uint64_t revision)
    {
        return Reload(Keire::AudioMixerAsset::Decode(bytes)->Definition(), revision);
    }

    AssetDocumentReloadResult AudioMixerDocument::Reload(const Keire::AudioMixerDefinition& definition,
                                                         const std::uint64_t revision)
    {
        if (definition == m_Host.Draft())
        {
            m_Host.AcknowledgeRevision(revision);
            return AssetDocumentReloadResult::Unchanged;
        }
        const auto result = m_Host.Reload(definition, revision);
        if (result == AssetDocumentReloadResult::Applied)
            ReconcileSelection();
        return result;
    }

    bool AudioMixerDocument::Undo()
    {
        const bool changed = m_Host.Undo();
        if (changed)
            ReconcileSelection();
        return changed;
    }

    bool AudioMixerDocument::Redo()
    {
        const bool changed = m_Host.Redo();
        if (changed)
            ReconcileSelection();
        return changed;
    }

    void AudioMixerDocument::Close() noexcept
    {
        m_Host.Close();
        m_Canvas.Select(std::nullopt);
        m_SelectedBus.reset();
    }

    bool AudioMixerDocument::Edit(const std::string_view name,
                                  const std::function<void(Keire::AudioMixerDefinition&)>& operation)
    {
        if (!operation)
            throw std::invalid_argument("Audio Mixer edits require an operation.");
        auto candidate = m_Host.Draft();
        operation(candidate);
        const bool changed = m_Host.Edit(name, std::move(candidate));
        if (changed)
            ReconcileSelection();
        return changed;
    }

    bool AudioMixerDocument::AddBus(Keire::AudioMixerBusDefinition bus)
    {
        return Edit("Add Audio Mixer bus", [bus = std::move(bus)](Keire::AudioMixerDefinition& definition) mutable
                    { definition.Buses.push_back(std::move(bus)); });
    }

    bool AudioMixerDocument::RenameBus(const Keire::AssetId bus, std::string name)
    {
        return Edit("Rename Audio Mixer bus",
                    [bus, name = std::move(name)](Keire::AudioMixerDefinition& definition) mutable
                    { RequireBus(definition, bus).Name = std::move(name); });
    }

    bool AudioMixerDocument::RemoveBus(const Keire::AssetId bus)
    {
        return Edit("Remove Audio Mixer bus",
                    [bus](Keire::AudioMixerDefinition& definition)
                    {
                        if (bus == definition.MasterBus)
                            throw std::invalid_argument("The Audio Mixer Master bus cannot be removed.");
                        const auto found =
                            std::ranges::find(definition.Buses, bus, &Keire::AudioMixerBusDefinition::Id);
                        if (found == definition.Buses.end())
                            throw std::invalid_argument("Audio Mixer bus is unavailable.");
                        const auto parent = found->Parent;
                        std::set<Keire::AssetId> removedTargets{bus};
                        for (const auto& effect : found->Effects)
                            removedTargets.insert(effect.Id);
                        for (const auto& send : found->Sends)
                            removedTargets.insert(send.Id);
                        definition.Buses.erase(found);
                        for (auto& candidate : definition.Buses)
                        {
                            if (candidate.Parent == bus)
                                candidate.Parent = parent;
                            std::erase_if(candidate.Sends,
                                          [&](const Keire::AudioMixerSendDefinition& send)
                                          {
                                              if (send.DestinationBus != bus)
                                                  return false;
                                              removedTargets.insert(send.Id);
                                              return true;
                                          });
                        }
                        for (auto& snapshot : definition.Snapshots)
                        {
                            std::erase_if(snapshot.Parameters,
                                          [&](const Keire::AudioMixerSnapshotParameterDefinition& parameter)
                                          { return removedTargets.contains(parameter.Target); });
                        }
                        std::erase_if(definition.Ducking, [bus](const Keire::AudioMixerDuckingDefinition& ducking)
                                      { return ducking.SidechainBus == bus || ducking.TargetBus == bus; });
                    });
    }

    bool AudioMixerDocument::AddEffect(const Keire::AssetId bus, Keire::AudioMixerEffectDefinition effect)
    {
        return Edit("Add Audio Mixer effect",
                    [bus, effect = std::move(effect)](Keire::AudioMixerDefinition& definition) mutable
                    { RequireBus(definition, bus).Effects.push_back(std::move(effect)); });
    }

    bool AudioMixerDocument::RenameEffect(const Keire::AssetId effect, std::string name)
    {
        return Edit("Rename Audio Mixer effect",
                    [effect, name = std::move(name)](Keire::AudioMixerDefinition& definition) mutable
                    {
                        for (auto& bus : definition.Buses)
                        {
                            const auto found =
                                std::ranges::find(bus.Effects, effect, &Keire::AudioMixerEffectDefinition::Id);
                            if (found != bus.Effects.end())
                            {
                                found->Name = std::move(name);
                                return;
                            }
                        }
                        throw std::invalid_argument("Audio Mixer effect is unavailable.");
                    });
    }

    bool AudioMixerDocument::MoveEffect(const Keire::AssetId bus, const Keire::AssetId effect,
                                        const std::size_t destination)
    {
        return Edit("Reorder Audio Mixer effect",
                    [bus, effect, destination](Keire::AudioMixerDefinition& definition)
                    {
                        auto& effects = RequireBus(definition, bus).Effects;
                        const auto found = std::ranges::find(effects, effect, &Keire::AudioMixerEffectDefinition::Id);
                        if (found == effects.end() || destination >= effects.size())
                            throw std::invalid_argument("Audio Mixer effect or destination is unavailable.");
                        const auto source = static_cast<std::size_t>(std::distance(effects.begin(), found));
                        if (source == destination)
                            return;
                        auto moved = std::move(*found);
                        effects.erase(found);
                        effects.insert(effects.begin() + static_cast<std::ptrdiff_t>(destination), std::move(moved));
                    });
    }

    bool AudioMixerDocument::RemoveEffect(const Keire::AssetId effect)
    {
        return Edit("Remove Audio Mixer effect",
                    [effect](Keire::AudioMixerDefinition& definition)
                    {
                        bool removed = false;
                        for (auto& bus : definition.Buses)
                            removed =
                                std::erase_if(bus.Effects, [effect](const Keire::AudioMixerEffectDefinition& entry)
                                              { return entry.Id == effect; }) != 0 ||
                                removed;
                        if (!removed)
                            throw std::invalid_argument("Audio Mixer effect is unavailable.");
                        for (auto& snapshot : definition.Snapshots)
                            std::erase_if(snapshot.Parameters,
                                          [effect](const Keire::AudioMixerSnapshotParameterDefinition& parameter)
                                          { return parameter.Target == effect; });
                    });
    }

    bool AudioMixerDocument::AddSend(const Keire::AssetId bus, Keire::AudioMixerSendDefinition send)
    {
        return Edit("Add Audio Mixer send", [bus, send](Keire::AudioMixerDefinition& definition)
                    { RequireBus(definition, bus).Sends.push_back(send); });
    }

    bool AudioMixerDocument::RemoveSend(const Keire::AssetId send)
    {
        return Edit("Remove Audio Mixer send",
                    [send](Keire::AudioMixerDefinition& definition)
                    {
                        bool removed = false;
                        for (auto& bus : definition.Buses)
                            removed = std::erase_if(bus.Sends, [send](const Keire::AudioMixerSendDefinition& entry)
                                                    { return entry.Id == send; }) != 0 ||
                                      removed;
                        if (!removed)
                            throw std::invalid_argument("Audio Mixer send is unavailable.");
                        for (auto& snapshot : definition.Snapshots)
                            std::erase_if(snapshot.Parameters,
                                          [send](const Keire::AudioMixerSnapshotParameterDefinition& parameter)
                                          { return parameter.Target == send; });
                    });
    }

    bool AudioMixerDocument::AddSnapshot(Keire::AudioMixerSnapshotDefinition snapshot)
    {
        return Edit("Add Audio Mixer snapshot",
                    [snapshot = std::move(snapshot)](Keire::AudioMixerDefinition& definition) mutable
                    { definition.Snapshots.push_back(std::move(snapshot)); });
    }

    bool AudioMixerDocument::RenameSnapshot(const Keire::AssetId snapshot, std::string name)
    {
        return Edit("Rename Audio Mixer snapshot",
                    [snapshot, name = std::move(name)](Keire::AudioMixerDefinition& definition) mutable
                    {
                        const auto found =
                            std::ranges::find(definition.Snapshots, snapshot, &Keire::AudioMixerSnapshotDefinition::Id);
                        if (found == definition.Snapshots.end())
                            throw std::invalid_argument("Audio Mixer snapshot is unavailable.");
                        found->Name = std::move(name);
                    });
    }

    bool AudioMixerDocument::RemoveSnapshot(const Keire::AssetId snapshot)
    {
        return Edit("Remove Audio Mixer snapshot",
                    [snapshot](Keire::AudioMixerDefinition& definition)
                    {
                        if (std::erase_if(definition.Snapshots,
                                          [snapshot](const Keire::AudioMixerSnapshotDefinition& entry)
                                          { return entry.Id == snapshot; }) == 0)
                            throw std::invalid_argument("Audio Mixer snapshot is unavailable.");
                    });
    }

    bool AudioMixerDocument::AddDucking(Keire::AudioMixerDuckingDefinition ducking)
    {
        return Edit("Add Audio Mixer ducking",
                    [ducking = std::move(ducking)](Keire::AudioMixerDefinition& definition) mutable
                    { definition.Ducking.push_back(std::move(ducking)); });
    }

    bool AudioMixerDocument::RemoveDucking(const Keire::AssetId ducking)
    {
        return Edit("Remove Audio Mixer ducking",
                    [ducking](Keire::AudioMixerDefinition& definition)
                    {
                        if (std::erase_if(definition.Ducking, [ducking](const Keire::AudioMixerDuckingDefinition& entry)
                                          { return entry.Id == ducking; }) == 0)
                            throw std::invalid_argument("Audio Mixer ducking rule is unavailable.");
                    });
    }

    AudioMixerRoutingGraph AudioMixerDocument::RoutingGraph() const
    {
        const auto& definition = m_Host.Draft();
        Keire::ValidateAudioMixer(definition);
        AudioMixerRoutingGraph result;
        result.Nodes.reserve(definition.Buses.size());
        std::map<std::size_t, std::size_t> rows;
        for (const auto& bus : definition.Buses)
        {
            const auto id = StableCanvasId(bus.Id, 0x4255534e4f444501ULL);
            if (!result.Buses.emplace(id, bus.Id).second)
                throw std::invalid_argument("Audio Mixer bus IDs collide in the node canvas projection.");
            const auto depth = BusDepth(definition, bus);
            const auto row = rows[depth]++;
            result.Nodes.push_back({.Id = id,
                                    .Label = bus.Name,
                                    .Position = {static_cast<float>(depth) * 240.0F, static_cast<float>(row) * 112.0F},
                                    .Color = bus.Id == definition.MasterBus ? Keire::UiColor{0.24F, 0.42F, 0.65F, 1.0F}
                                                                            : Keire::UiColor{0.2F, 0.24F, 0.3F, 1.0F}});
        }

        for (const auto& bus : definition.Buses)
        {
            const auto source = StableCanvasId(bus.Id, 0x4255534e4f444501ULL);
            if (bus.Parent)
                result.Connections.push_back({.Id = StableCanvasId(bus.Id, 0x504152454e544501ULL),
                                              .Source = source,
                                              .Target = StableCanvasId(bus.Parent, 0x4255534e4f444501ULL),
                                              .Label = "Output"});
            for (const auto& send : bus.Sends)
            {
                const auto id = StableCanvasId(send.Id, 0x53454e4445444745ULL);
                if (!result.Sends.emplace(id, send.Id).second)
                    throw std::invalid_argument("Audio Mixer send IDs collide in the node canvas projection.");
                result.Connections.push_back(
                    {.Id = id,
                     .Source = source,
                     .Target = StableCanvasId(send.DestinationBus, 0x4255534e4f444501ULL),
                     .Label = send.Stage == Keire::AudioMixerSendStage::PreFader ? "Pre" : "Post"});
            }
        }
        StableNodeGraphCanvas::Validate(result.Nodes, result.Connections);
        return result;
    }

    NodeGraphCanvasResult AudioMixerDocument::DrawRouting(Keire::UiFrame& ui, const std::string_view id)
    {
        auto graph = RoutingGraph();
        auto result = m_Canvas.Draw(ui, id, graph.Nodes, graph.Connections, false);
        if (result.ActivatedNode)
        {
            const auto found = graph.Buses.find(*result.ActivatedNode);
            if (found != graph.Buses.end())
                m_SelectedBus = found->second;
        }
        return result;
    }

    void AudioMixerDocument::FocusRouting(const Keire::UiSize size)
    {
        const auto graph = RoutingGraph();
        m_Canvas.Focus(graph.Nodes, size);
    }

    void AudioMixerDocument::SelectBus(const std::optional<Keire::AssetId> bus)
    {
        if (!bus)
        {
            m_SelectedBus.reset();
            m_Canvas.Select(std::nullopt);
            return;
        }
        const auto& definition = m_Host.Draft();
        if (std::ranges::find(definition.Buses, *bus, &Keire::AudioMixerBusDefinition::Id) == definition.Buses.end())
            throw std::invalid_argument("Audio Mixer selected bus is unavailable.");
        m_SelectedBus = bus;
        m_Canvas.Select(StableCanvasId(*bus, 0x4255534e4f444501ULL));
    }

    void AudioMixerDocument::ReconcileSelection()
    {
        if (!m_Host.IsOpen())
        {
            m_SelectedBus.reset();
            m_Canvas.Select(std::nullopt);
            return;
        }
        const auto& definition = m_Host.Draft();
        if (m_SelectedBus && std::ranges::find(definition.Buses, *m_SelectedBus, &Keire::AudioMixerBusDefinition::Id) !=
                                 definition.Buses.end())
        {
            m_Canvas.Select(StableCanvasId(*m_SelectedBus, 0x4255534e4f444501ULL));
            return;
        }
        m_SelectedBus = definition.MasterBus;
        m_Canvas.Select(StableCanvasId(definition.MasterBus, 0x4255534e4f444501ULL));
    }
} // namespace KeireEditor
