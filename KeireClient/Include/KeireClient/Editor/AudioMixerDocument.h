#pragma once

#include "Keire/Audio/AudioAssets.h"
#include "KeireClient/Editor/AssetDocumentHost.h"
#include "KeireClient/Editor/AuthoringWidgets.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace KeireEditor
{
    struct AudioMixerRoutingGraph
    {
        std::vector<NodeGraphNode> Nodes;
        std::vector<NodeGraphConnection> Connections;
        std::map<StableNodeId, Keire::AssetId> Buses;
        std::map<StableNodeId, Keire::AssetId> Sends;
    };

    struct AudioMixerDocumentSpecification
    {
        // Preview is a transient audition path and must not persist or dirty scene state.
        std::function<void(Keire::AssetId, const Keire::AudioMixerDefinition&)> Preview;
        // Stops every voice or graph instance owned by the document's preview session.
        std::function<void(Keire::AssetId)> StopPreview;
        std::function<void(Keire::AssetId, std::span<const std::byte>)> Persist;
    };

    class AudioMixerDocument final
    {
      public:
        using Host = AssetDocumentHost<Keire::AudioMixerDefinition>;

        explicit AudioMixerDocument(AudioMixerDocumentSpecification specification);
        ~AudioMixerDocument() = default;

        AudioMixerDocument(const AudioMixerDocument&) = delete;
        AudioMixerDocument& operator=(const AudioMixerDocument&) = delete;

        void Open(Keire::AssetId asset, std::span<const std::byte> bytes, std::uint64_t revision,
                  Keire::Ref<Keire::UndoContext> undo = {});
        void Open(Keire::AssetId asset, Keire::AudioMixerDefinition definition, std::uint64_t revision,
                  Keire::Ref<Keire::UndoContext> undo = {});
        void Create(Keire::AssetId asset,
                    Keire::AudioMixerDefinition definition = Keire::AudioMixerAsset::DefaultDefinition(),
                    Keire::Ref<Keire::UndoContext> undo = {});
        void Save();
        void Discard();
        [[nodiscard]] AssetDocumentReloadResult Reload(std::span<const std::byte> bytes, std::uint64_t revision);
        [[nodiscard]] AssetDocumentReloadResult Reload(const Keire::AudioMixerDefinition& definition,
                                                       std::uint64_t revision);
        [[nodiscard]] bool Undo();
        [[nodiscard]] bool Redo();
        void Close() noexcept;

        [[nodiscard]] bool IsOpen() const noexcept { return m_Host.IsOpen(); }
        [[nodiscard]] Keire::AssetId Asset() const noexcept { return m_Host.Asset(); }
        [[nodiscard]] std::uint64_t Revision() const noexcept { return m_Host.Revision(); }
        [[nodiscard]] bool Dirty() const noexcept { return m_Host.Dirty(); }
        [[nodiscard]] std::string_view Diagnostic() const noexcept { return m_Host.Diagnostic(); }
        [[nodiscard]] const Keire::AudioMixerDefinition& Definition() const { return m_Host.Draft(); }
        [[nodiscard]] Keire::Ref<Keire::UndoContext> UndoContext() const noexcept { return m_Host.UndoContext(); }
        [[nodiscard]] std::optional<Keire::AssetId> SelectedBus() const noexcept { return m_SelectedBus; }

        [[nodiscard]] bool Edit(std::string_view name,
                                const std::function<void(Keire::AudioMixerDefinition&)>& operation);
        [[nodiscard]] bool AddBus(Keire::AudioMixerBusDefinition bus);
        [[nodiscard]] bool RenameBus(Keire::AssetId bus, std::string name);
        [[nodiscard]] bool RemoveBus(Keire::AssetId bus);
        [[nodiscard]] bool AddEffect(Keire::AssetId bus, Keire::AudioMixerEffectDefinition effect);
        [[nodiscard]] bool RenameEffect(Keire::AssetId effect, std::string name);
        [[nodiscard]] bool MoveEffect(Keire::AssetId bus, Keire::AssetId effect, std::size_t destination);
        [[nodiscard]] bool RemoveEffect(Keire::AssetId effect);
        [[nodiscard]] bool AddSend(Keire::AssetId bus, Keire::AudioMixerSendDefinition send);
        [[nodiscard]] bool RemoveSend(Keire::AssetId send);
        [[nodiscard]] bool AddSnapshot(Keire::AudioMixerSnapshotDefinition snapshot);
        [[nodiscard]] bool RenameSnapshot(Keire::AssetId snapshot, std::string name);
        [[nodiscard]] bool RemoveSnapshot(Keire::AssetId snapshot);
        [[nodiscard]] bool AddDucking(Keire::AudioMixerDuckingDefinition ducking);
        [[nodiscard]] bool RemoveDucking(Keire::AssetId ducking);

        [[nodiscard]] AudioMixerRoutingGraph RoutingGraph() const;
        [[nodiscard]] NodeGraphCanvasResult DrawRouting(Keire::UiFrame& ui, std::string_view id);
        void FocusRouting(Keire::UiSize size);
        void SelectBus(std::optional<Keire::AssetId> bus);

      private:
        void ReconcileSelection();

        Host m_Host;
        StableNodeGraphCanvas m_Canvas;
        std::optional<Keire::AssetId> m_SelectedBus;
    };
} // namespace KeireEditor
