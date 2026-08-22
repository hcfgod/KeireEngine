#pragma once

#include "Keire/Vfx/VfxSystem.h"
#include "KeireClient/Editor/AssetDocumentHost.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace KeireEditor
{
    enum class VfxGraphConnectionStatus : std::uint8_t
    {
        Accepted,
        AcceptedWithWarning,
        Rejected
    };

    struct VfxGraphConnectionCheck
    {
        VfxGraphConnectionStatus Status = VfxGraphConnectionStatus::Rejected;
        bool ReplacesInput = false;
        std::string Diagnostic;
    };

    struct VfxEffectDocumentSpecification
    {
        // Preview is transient and must not persist the asset or dirty scene state.
        std::function<void(Keire::AssetId, const Keire::VfxEffectDefinition&)> Preview;
        // Stops every effect instance owned by this document's preview session.
        std::function<void(Keire::AssetId)> StopPreview;
        std::function<void(Keire::AssetId, std::span<const std::byte>)> Persist;
    };

    class VfxEffectDocument final
    {
      public:
        using Host = AssetDocumentHost<Keire::VfxEffectDefinition>;

        explicit VfxEffectDocument(VfxEffectDocumentSpecification specification);
        ~VfxEffectDocument() = default;

        VfxEffectDocument(const VfxEffectDocument&) = delete;
        VfxEffectDocument& operator=(const VfxEffectDocument&) = delete;

        void Open(Keire::AssetId asset, std::span<const std::byte> bytes, std::uint64_t revision,
                  Keire::Ref<Keire::UndoContext> undo = {});
        void Open(Keire::AssetId asset, Keire::VfxEffectDefinition definition, std::uint64_t revision,
                  Keire::Ref<Keire::UndoContext> undo = {});
        void Create(Keire::AssetId asset,
                    Keire::VfxEffectDefinition definition = Keire::VfxEffectAsset::DefaultDefinition(),
                    Keire::Ref<Keire::UndoContext> undo = {});
        void Save();
        void Discard();
        [[nodiscard]] AssetDocumentReloadResult Reload(std::span<const std::byte> bytes, std::uint64_t revision);
        [[nodiscard]] AssetDocumentReloadResult Reload(const Keire::VfxEffectDefinition& definition,
                                                       std::uint64_t revision);
        [[nodiscard]] bool Undo();
        [[nodiscard]] bool Redo();
        void Close() noexcept;

        [[nodiscard]] bool IsOpen() const noexcept { return m_Host.IsOpen(); }
        [[nodiscard]] Keire::AssetId Asset() const noexcept { return m_Host.Asset(); }
        [[nodiscard]] std::uint64_t Revision() const noexcept { return m_Host.Revision(); }
        [[nodiscard]] bool Dirty() const noexcept { return m_Host.Dirty(); }
        [[nodiscard]] std::string_view Diagnostic() const noexcept;
        [[nodiscard]] bool Publishable() const noexcept { return m_GraphDiagnostic.empty(); }
        [[nodiscard]] std::string_view GraphDiagnostic() const noexcept { return m_GraphDiagnostic; }
        [[nodiscard]] const Keire::VfxEffectDefinition& Definition() const { return m_Host.Draft(); }
        [[nodiscard]] Keire::Ref<Keire::UndoContext> UndoContext() const noexcept { return m_Host.UndoContext(); }

        [[nodiscard]] bool Edit(std::string_view name,
                                const std::function<void(Keire::VfxEffectDefinition&)>& operation);
        [[nodiscard]] bool ConvertToGraph();
        [[nodiscard]] bool AddModule(Keire::VfxModuleDefinition module);
        [[nodiscard]] bool EditModule(Keire::AssetId module,
                                      const std::function<void(Keire::VfxModuleDefinition&)>& operation);
        [[nodiscard]] bool RemoveModule(Keire::AssetId module);
        [[nodiscard]] bool MoveModule(Keire::AssetId module, std::size_t destination);
        [[nodiscard]] bool AddSystem(Keire::VfxGraphSystem system);
        [[nodiscard]] bool EditSystem(Keire::AssetId system,
                                      const std::function<void(Keire::VfxGraphSystem&)>& operation);
        [[nodiscard]] bool RemoveSystem(Keire::AssetId system);
        [[nodiscard]] bool AddNode(Keire::AssetId system, Keire::VfxGraphNode node);
        [[nodiscard]] bool EditNode(Keire::AssetId system, Keire::AssetId node,
                                    const std::function<void(Keire::VfxGraphNode&)>& operation);
        [[nodiscard]] bool MoveNodes(Keire::AssetId system,
                                     std::span<const std::pair<Keire::AssetId, Keire::Vector2>> nodes);
        [[nodiscard]] bool RemoveNode(Keire::AssetId system, Keire::AssetId node);
        [[nodiscard]] bool RemoveNodes(Keire::AssetId system, std::span<const Keire::AssetId> nodes);
        [[nodiscard]] bool AddBlock(Keire::AssetId system, Keire::AssetId context, Keire::VfxGraphBlock block);
        [[nodiscard]] bool EditBlock(Keire::AssetId system, Keire::AssetId context, Keire::AssetId block,
                                     const std::function<void(Keire::VfxGraphBlock&)>& operation);
        [[nodiscard]] bool SetBlockEnabled(Keire::AssetId system, Keire::AssetId context, Keire::AssetId block,
                                           bool enabled);
        [[nodiscard]] bool RemoveBlock(Keire::AssetId system, Keire::AssetId context, Keire::AssetId block);
        [[nodiscard]] bool MoveBlock(Keire::AssetId system, Keire::AssetId context, Keire::AssetId block,
                                     std::size_t destination);
        [[nodiscard]] bool AddBlockPin(Keire::AssetId system, Keire::AssetId context, Keire::AssetId block,
                                       Keire::VfxGraphPin pin);
        [[nodiscard]] bool EditBlockPin(Keire::AssetId system, Keire::AssetId context, Keire::AssetId block,
                                        Keire::AssetId pin, const std::function<void(Keire::VfxGraphPin&)>& operation);
        [[nodiscard]] bool RemoveBlockPin(Keire::AssetId system, Keire::AssetId context, Keire::AssetId block,
                                          Keire::AssetId pin);
        [[nodiscard]] bool AddPin(Keire::AssetId system, Keire::AssetId node, Keire::VfxGraphPin pin);
        [[nodiscard]] bool EditPin(Keire::AssetId system, Keire::AssetId node, Keire::AssetId pin,
                                   const std::function<void(Keire::VfxGraphPin&)>& operation);
        [[nodiscard]] bool RemovePin(Keire::AssetId system, Keire::AssetId node, Keire::AssetId pin);
        [[nodiscard]] bool AddConnection(Keire::AssetId system, Keire::VfxGraphConnection connection);
        [[nodiscard]] bool EditConnection(Keire::AssetId system, Keire::AssetId connection,
                                          const std::function<void(Keire::VfxGraphConnection&)>& operation);
        [[nodiscard]] bool SetConnectionRouting(Keire::AssetId system, Keire::AssetId connection,
                                                std::vector<Keire::Vector2> routingPoints);
        [[nodiscard]] bool RemoveConnection(Keire::AssetId system, Keire::AssetId connection);
        [[nodiscard]] VfxGraphConnectionCheck CheckConnection(Keire::AssetId system, Keire::AssetId outputNode,
                                                              Keire::AssetId outputPin, Keire::AssetId inputNode,
                                                              Keire::AssetId inputPin) const;
        [[nodiscard]] VfxGraphConnectionCheck CheckConnection(Keire::AssetId system, Keire::VfxGraphEndpoint output,
                                                              Keire::VfxGraphEndpoint input) const;
        [[nodiscard]] bool AddBlackboardParameter(Keire::VfxBlackboardParameter parameter);
        [[nodiscard]] bool
        EditBlackboardParameter(Keire::AssetId parameter,
                                const std::function<void(Keire::VfxBlackboardParameter&)>& operation);
        [[nodiscard]] bool RemoveBlackboardParameter(Keire::AssetId parameter);

      private:
        Host m_Host;
        std::string m_GraphDiagnostic;
    };
} // namespace KeireEditor
