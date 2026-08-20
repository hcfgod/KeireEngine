#pragma once

#include "Keire/Rendering/MaterialGraph.h"
#include "KeireClient/Editor/AssetDocumentHost.h"
#include "KeireClient/Editor/AuthoringWidgets.h"

#include <functional>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace KeireEditor
{
    struct MaterialGraphDocumentSpecification
    {
        std::function<std::optional<Keire::ShaderInterfaceDefinition>(const Keire::MaterialShaderReference&)>
            ResolveInterface;
        std::function<std::optional<Keire::ShaderGraphDefinition>(const Keire::MaterialShaderReference&)>
            ResolveTemplate;
        std::function<std::optional<Keire::ShaderGraphDefinition>(Keire::AssetId)> ResolveFunction;
        std::function<Keire::AssetId(const Keire::MaterialShaderReference&)> ResolveShader;
        std::function<void(Keire::AssetId, const Keire::MaterialAssetDefinition&)> Preview;
        std::function<void(Keire::AssetId)> StopPreview;
        std::function<void(Keire::AssetId, std::span<const std::byte>)> Persist;
    };

    struct MaterialGraphCanvasModel
    {
        std::vector<NodeGraphNode> Nodes;
        std::vector<NodeGraphConnection> Connections;
        std::vector<std::pair<StableNodeId, Keire::AssetId>> NodeIdentities;
        std::vector<std::pair<StableNodeId, Keire::AssetId>> PinIdentities;
        std::vector<std::pair<StableNodeId, Keire::AssetId>> ConnectionIdentities;

        [[nodiscard]] std::optional<Keire::AssetId> Node(StableNodeId id) const noexcept;
        [[nodiscard]] std::optional<Keire::AssetId> Pin(StableNodeId id) const noexcept;
        [[nodiscard]] std::optional<Keire::AssetId> Connection(StableNodeId id) const noexcept;
    };

    class MaterialGraphDocument final
    {
      public:
        using Host = AssetDocumentHost<Keire::MaterialGraphDefinition>;

        explicit MaterialGraphDocument(MaterialGraphDocumentSpecification specification);

        void Open(Keire::AssetId asset, std::span<const std::byte> bytes, std::uint64_t revision,
                  Keire::Ref<Keire::UndoContext> undo = {});
        void Save();
        [[nodiscard]] bool AdvanceAutosave(double deltaSeconds);
        void Discard();
        void Close() noexcept;
        [[nodiscard]] bool Undo();
        [[nodiscard]] bool Redo();

        [[nodiscard]] bool IsOpen() const noexcept { return m_Host.IsOpen(); }
        [[nodiscard]] bool Dirty() const noexcept { return m_Host.Dirty(); }
        [[nodiscard]] Keire::AssetId Asset() const noexcept { return m_Host.Asset(); }
        [[nodiscard]] const Keire::MaterialGraphDefinition& Definition() const { return m_Host.Draft(); }
        [[nodiscard]] Keire::Ref<Keire::UndoContext> UndoContext() const noexcept { return m_Host.UndoContext(); }
        [[nodiscard]] std::span<const Keire::MaterialGraphDiagnostic> Diagnostics() const noexcept
        {
            return m_Diagnostics;
        }

        [[nodiscard]] bool Edit(std::string_view name,
                                const std::function<void(Keire::MaterialGraphDefinition&)>& operation);
        [[nodiscard]] bool SetShader(Keire::MaterialShaderReference shader);
        [[nodiscard]] bool SetSurface(Keire::MaterialSurfaceState surface);
        [[nodiscard]] bool SetInputValue(Keire::AssetId pin, Keire::MaterialPropertyValue value);
        [[nodiscard]] bool AddNode(Keire::MaterialGraphValueNode node);
        [[nodiscard]] bool EditNode(Keire::AssetId node,
                                    const std::function<void(Keire::MaterialGraphValueNode&)>& operation);
        [[nodiscard]] bool AddExpressionNode(Keire::ShaderGraphNode node);
        [[nodiscard]] bool EditExpressionNode(Keire::AssetId node,
                                              const std::function<void(Keire::ShaderGraphNode&)>& operation);
        [[nodiscard]] bool MoveNode(Keire::AssetId node, Keire::Vector2 position);
        [[nodiscard]] bool RemoveNode(Keire::AssetId node);
        [[nodiscard]] bool AddConnection(Keire::MaterialGraphConnection connection);
        [[nodiscard]] bool SetConnectionRouting(Keire::AssetId connection, std::vector<Keire::Vector2> routingPoints);
        [[nodiscard]] bool RemoveConnection(Keire::AssetId connection);
        [[nodiscard]] NodeGraphConnectionValidation CheckConnection(Keire::MaterialGraphEndpoint output,
                                                                    Keire::MaterialGraphEndpoint input) const;
        [[nodiscard]] MaterialGraphCanvasModel BuildCanvasModel(bool includeTemplateParameters = false) const;

      private:
        void RefreshDiagnostics();

        MaterialGraphDocumentSpecification m_Specification;
        Host m_Host;
        std::vector<Keire::MaterialGraphDiagnostic> m_Diagnostics;
        double m_AutosaveSeconds = 0.0;
    };
} // namespace KeireEditor
