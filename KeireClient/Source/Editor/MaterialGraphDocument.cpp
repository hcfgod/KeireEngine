#include "KeireClient/Editor/MaterialGraphDocument.h"

#include <algorithm>
#include <bit>
#include <ranges>
#include <stdexcept>
#include <utility>

namespace KeireEditor
{
    namespace
    {
        [[nodiscard]] StableNodeId PreferredCanvasId(const Keire::AssetId id, const std::uint64_t salt) noexcept
        {
            std::uint64_t value = id.High() ^ std::rotl(id.Low(), 27) ^ salt;
            value += 0x9e3779b97f4a7c15ULL;
            value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
            value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
            value ^= value >> 31U;
            return value == 0 ? 1 : value;
        }

        [[nodiscard]] Keire::UiColor PinColor(const Keire::ShaderPropertyType type) noexcept
        {
            switch (type)
            {
            case Keire::ShaderPropertyType::Scalar:
                return {0.48F, 0.78F, 0.42F, 1.0F};
            case Keire::ShaderPropertyType::Vector2:
                return {0.3F, 0.72F, 0.75F, 1.0F};
            case Keire::ShaderPropertyType::Vector3:
                return {0.3F, 0.58F, 0.9F, 1.0F};
            case Keire::ShaderPropertyType::Vector4:
                return {0.56F, 0.48F, 0.9F, 1.0F};
            case Keire::ShaderPropertyType::Color:
                return {0.94F, 0.58F, 0.28F, 1.0F};
            case Keire::ShaderPropertyType::Texture2D:
                return {0.87F, 0.36F, 0.62F, 1.0F};
            }
            return {};
        }

        [[nodiscard]] StableNodeId CanvasPinType(const Keire::ShaderPropertyType type) noexcept
        {
            return static_cast<StableNodeId>(type) + 1U;
        }

        [[nodiscard]] std::optional<Keire::AssetId>
        FindIdentity(const std::span<const std::pair<StableNodeId, Keire::AssetId>> identities,
                     const StableNodeId id) noexcept
        {
            const auto found = std::ranges::find(identities, id, &std::pair<StableNodeId, Keire::AssetId>::first);
            return found == identities.end() ? std::nullopt : std::optional(found->second);
        }

        [[nodiscard]] std::optional<StableNodeId>
        FindCanvasIdentity(const std::span<const std::pair<StableNodeId, Keire::AssetId>> identities,
                           const Keire::AssetId id) noexcept
        {
            const auto found = std::ranges::find(identities, id, &std::pair<StableNodeId, Keire::AssetId>::second);
            return found == identities.end() ? std::nullopt : std::optional(found->first);
        }
    } // namespace

    std::optional<Keire::AssetId> MaterialGraphCanvasModel::Node(const StableNodeId id) const noexcept
    {
        return FindIdentity(NodeIdentities, id);
    }

    std::optional<Keire::AssetId> MaterialGraphCanvasModel::Pin(const StableNodeId id) const noexcept
    {
        return FindIdentity(PinIdentities, id);
    }

    std::optional<Keire::AssetId> MaterialGraphCanvasModel::Connection(const StableNodeId id) const noexcept
    {
        return FindIdentity(ConnectionIdentities, id);
    }

    MaterialGraphDocument::MaterialGraphDocument(MaterialGraphDocumentSpecification specification)
        : m_Specification(std::move(specification)),
          m_Host({.Validate = [](const Keire::MaterialGraphDefinition& definition)
                  { Keire::ValidateMaterialGraph(definition); },
                  .Encode = [](const Keire::MaterialGraphDefinition& definition)
                  { return Keire::MaterialGraphAsset::EncodeSource(definition); },
                  .EstimateSize = [](const Keire::MaterialGraphDefinition& definition)
                  { return Keire::MaterialGraphAsset::EncodeSource(definition).size(); },
                  .Preview =
                      [this](const Keire::AssetId asset, const Keire::MaterialGraphDefinition& definition)
                  {
                      if (!m_Specification.Preview || !m_Specification.ResolveShader)
                          return;
                      m_Specification.Preview(asset,
                                              Keire::BakeMaterialGraph(definition, m_Specification.ResolveShader));
                  },
                  .CancelPreview = m_Specification.StopPreview,
                  .Persist = m_Specification.Persist})
    {
        if (!m_Specification.ResolveInterface || !m_Specification.ResolveShader || !m_Specification.Persist)
            throw std::invalid_argument(
                "Material Graph documents require shader-interface, runtime-shader, and persistence services.");
    }

    void MaterialGraphDocument::Open(const Keire::AssetId asset, const std::span<const std::byte> bytes,
                                     const std::uint64_t revision, Keire::Ref<Keire::UndoContext> undo)
    {
        auto definition = Keire::MaterialGraphAsset::DecodeSource(bytes);
        const auto shaderInterface = m_Specification.ResolveInterface(definition.Shader);
        if (!shaderInterface)
            throw std::invalid_argument("Material Graph shader interface is unavailable.");
        Keire::SynchronizeMaterialGraphInterface(definition, *shaderInterface);
        m_Host.Open(asset, std::move(definition), revision, std::move(undo));
        RefreshDiagnostics();
    }

    void MaterialGraphDocument::Save() { m_Host.Save(); }
    void MaterialGraphDocument::Discard()
    {
        m_Host.Discard();
        RefreshDiagnostics();
    }
    void MaterialGraphDocument::Close() noexcept
    {
        m_Host.Close();
        m_Diagnostics.clear();
    }
    bool MaterialGraphDocument::Undo()
    {
        const bool changed = m_Host.Undo();
        if (changed)
            RefreshDiagnostics();
        return changed;
    }
    bool MaterialGraphDocument::Redo()
    {
        const bool changed = m_Host.Redo();
        if (changed)
            RefreshDiagnostics();
        return changed;
    }

    bool MaterialGraphDocument::Edit(const std::string_view name,
                                     const std::function<void(Keire::MaterialGraphDefinition&)>& operation)
    {
        if (!operation)
            throw std::invalid_argument("Material Graph edits require an operation.");
        auto candidate = Definition();
        operation(candidate);
        const bool changed = m_Host.Edit(name, std::move(candidate));
        if (changed)
            RefreshDiagnostics();
        return changed;
    }

    bool MaterialGraphDocument::SetShader(Keire::MaterialShaderReference shader)
    {
        const auto shaderInterface = m_Specification.ResolveInterface(shader);
        if (!shaderInterface)
            throw std::invalid_argument("Selected shader does not expose a compatible material interface.");
        return Edit("Change Material Graph shader",
                    [&](auto& definition)
                    {
                        definition.Shader = std::move(shader);
                        Keire::SynchronizeMaterialGraphInterface(definition, *shaderInterface);
                    });
    }

    bool MaterialGraphDocument::SetSurface(const Keire::MaterialSurfaceState surface)
    {
        return Edit("Change Material Graph surface", [surface](auto& definition) { definition.Surface = surface; });
    }

    bool MaterialGraphDocument::SetInputValue(const Keire::AssetId pin, Keire::MaterialPropertyValue value)
    {
        return Edit("Edit Material Graph input",
                    [pin, value = std::move(value)](auto& definition) mutable
                    {
                        const auto property =
                            std::ranges::find(definition.Properties, pin, &Keire::MaterialGraphPropertyBinding::Pin);
                        if (property == definition.Properties.end())
                            throw std::invalid_argument("Material Graph output input is unavailable.");
                        property->Value = std::move(value);
                    });
    }

    bool MaterialGraphDocument::AddNode(Keire::MaterialGraphValueNode node)
    {
        return Edit("Add Material Graph value", [node = std::move(node)](auto& definition) mutable
                    { definition.Nodes.push_back(std::move(node)); });
    }

    bool MaterialGraphDocument::EditNode(const Keire::AssetId node,
                                         const std::function<void(Keire::MaterialGraphValueNode&)>& operation)
    {
        return Edit("Edit Material Graph value",
                    [node, &operation](auto& definition)
                    {
                        const auto found =
                            std::ranges::find(definition.Nodes, node, &Keire::MaterialGraphValueNode::Id);
                        if (found == definition.Nodes.end())
                            throw std::invalid_argument("Material Graph value node is unavailable.");
                        operation(*found);
                    });
    }

    bool MaterialGraphDocument::MoveNode(const Keire::AssetId node, const Keire::Vector2 position)
    {
        return Edit("Move Material Graph node",
                    [node, position](auto& definition)
                    {
                        if (node == definition.OutputNode)
                        {
                            definition.OutputPosition = position;
                            return;
                        }
                        const auto found =
                            std::ranges::find(definition.Nodes, node, &Keire::MaterialGraphValueNode::Id);
                        if (found == definition.Nodes.end())
                            throw std::invalid_argument("Material Graph node is unavailable.");
                        found->EditorPosition = position;
                    });
    }

    bool MaterialGraphDocument::RemoveNode(const Keire::AssetId node)
    {
        if (node == Definition().OutputNode)
            throw std::invalid_argument("Material Output cannot be removed.");
        return Edit("Remove Material Graph value",
                    [node](auto& definition)
                    {
                        const auto before = definition.Nodes.size();
                        std::erase_if(definition.Nodes, [node](const auto& candidate) { return candidate.Id == node; });
                        if (definition.Nodes.size() == before)
                            throw std::invalid_argument("Material Graph value node is unavailable.");
                        std::erase_if(definition.Connections,
                                      [node](const auto& connection) { return connection.Output.Node == node; });
                    });
    }

    NodeGraphConnectionValidation MaterialGraphDocument::CheckConnection(const Keire::MaterialGraphEndpoint output,
                                                                         const Keire::MaterialGraphEndpoint input) const
    {
        const auto source = std::ranges::find(Definition().Nodes, output.Node, &Keire::MaterialGraphValueNode::Id);
        const auto target =
            std::ranges::find(Definition().Properties, input.Pin, &Keire::MaterialGraphPropertyBinding::Pin);
        if (source == Definition().Nodes.end() || source->OutputPin != output.Pin ||
            input.Node != Definition().OutputNode || target == Definition().Properties.end())
            return {NodeGraphConnectionValidationStatus::Reject, "Connect a value node to Material Output."};
        if (source->Type != target->Type)
            return {NodeGraphConnectionValidationStatus::Reject, "Material value type does not match this input."};
        return {};
    }

    bool MaterialGraphDocument::AddConnection(Keire::MaterialGraphConnection connection)
    {
        const auto validation = CheckConnection(connection.Output, connection.Input);
        if (!validation.CanConnect())
            throw std::invalid_argument(validation.Diagnostic);
        if (!connection.Id)
            connection.Id = Keire::AssetId::Generate();
        return Edit("Connect Material Graph value",
                    [connection](auto& definition)
                    {
                        std::erase_if(definition.Connections,
                                      [&](const auto& candidate) { return candidate.Input == connection.Input; });
                        definition.Connections.push_back(connection);
                    });
    }

    bool MaterialGraphDocument::RemoveConnection(const Keire::AssetId connection)
    {
        return Edit("Disconnect Material Graph value",
                    [connection](auto& definition)
                    {
                        const auto before = definition.Connections.size();
                        std::erase_if(definition.Connections,
                                      [connection](const auto& candidate) { return candidate.Id == connection; });
                        if (before == definition.Connections.size())
                            throw std::invalid_argument("Material Graph connection is unavailable.");
                    });
    }

    MaterialGraphCanvasModel MaterialGraphDocument::BuildCanvasModel() const
    {
        MaterialGraphCanvasModel result;
        StableNodeGraphIdMap ids;
        const auto addIdentity = [&](std::vector<std::pair<StableNodeId, Keire::AssetId>>& identities,
                                     const Keire::AssetId id, const std::uint64_t salt)
        {
            const auto canvas = ids.Assign(id, PreferredCanvasId(id, salt));
            identities.emplace_back(canvas, id);
            return canvas;
        };

        NodeGraphNode output;
        output.Id = addIdentity(result.NodeIdentities, Definition().OutputNode, 0x4d474f5554505554ULL);
        output.Label = "Material Output";
        output.Subtitle = Definition().Shader.Kind == Keire::MaterialShaderSourceKind::ShaderGraph
                              ? "Shader Graph interface"
                              : "Shader interface";
        output.Position = Definition().OutputPosition;
        output.Size = {260.0F, std::max(96.0F, 48.0F + static_cast<float>(Definition().Properties.size()) * 24.0F)};
        output.Color = {0.42F, 0.22F, 0.12F, 1.0F};
        for (const auto& property : Definition().Properties)
        {
            NodeGraphPin pin;
            pin.Id = addIdentity(result.PinIdentities, property.Pin, 0x4d47494e50555400ULL);
            pin.Label = property.Name;
            pin.Direction = NodeGraphPinDirection::Input;
            pin.Type = CanvasPinType(property.Type);
            pin.Color = PinColor(property.Type);
            output.Pins.push_back(std::move(pin));
        }
        result.Nodes.push_back(std::move(output));

        for (const auto& node : Definition().Nodes)
        {
            NodeGraphNode canvas;
            canvas.Id = addIdentity(result.NodeIdentities, node.Id, 0x4d474e4f44450000ULL);
            canvas.Label = node.Name;
            canvas.Subtitle = node.Type == Keire::ShaderPropertyType::Texture2D ? "Texture2D" : "Material value";
            canvas.Position = node.EditorPosition;
            canvas.Color = {0.16F, 0.27F, 0.34F, 1.0F};
            NodeGraphPin pin;
            pin.Id = addIdentity(result.PinIdentities, node.OutputPin, 0x4d474f555450494eULL);
            pin.Label = "Value";
            pin.Direction = NodeGraphPinDirection::Output;
            pin.Type = CanvasPinType(node.Type);
            pin.Color = PinColor(node.Type);
            canvas.Pins.push_back(std::move(pin));
            result.Nodes.push_back(std::move(canvas));
        }

        for (const auto& connection : Definition().Connections)
        {
            NodeGraphConnection canvas;
            canvas.Id = addIdentity(result.ConnectionIdentities, connection.Id, 0x4d47434f4e4e0000ULL);
            canvas.Source = *FindCanvasIdentity(result.NodeIdentities, connection.Output.Node);
            canvas.SourcePin = *FindCanvasIdentity(result.PinIdentities, connection.Output.Pin);
            canvas.Target = *FindCanvasIdentity(result.NodeIdentities, connection.Input.Node);
            canvas.TargetPin = *FindCanvasIdentity(result.PinIdentities, connection.Input.Pin);
            result.Connections.push_back(std::move(canvas));
        }
        return result;
    }

    void MaterialGraphDocument::RefreshDiagnostics()
    {
        const auto shaderInterface = m_Specification.ResolveInterface(Definition().Shader);
        m_Diagnostics =
            shaderInterface
                ? Keire::ValidateMaterialGraphAgainstInterface(Definition(), *shaderInterface)
                : std::vector<Keire::MaterialGraphDiagnostic>{{Keire::MaterialGraphDiagnosticSeverity::Error,
                                                               "MAT1000",
                                                               "Selected shader interface is unavailable.",
                                                               {},
                                                               {},
                                                               {}}};
    }
} // namespace KeireEditor
