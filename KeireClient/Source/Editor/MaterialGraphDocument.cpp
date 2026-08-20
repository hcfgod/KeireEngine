#include "KeireClient/Editor/MaterialGraphDocument.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <memory>
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

        [[nodiscard]] Keire::UiColor PinColor(const Keire::ShaderGraphValueType type) noexcept
        {
            if (type <= Keire::ShaderGraphValueType::Texture2D)
                return PinColor(static_cast<Keire::ShaderPropertyType>(type));
            if (type == Keire::ShaderGraphValueType::MaterialAttributes)
                return {0.96F, 0.72F, 0.22F, 1.0F};
            return {0.88F, 0.3F, 0.25F, 1.0F};
        }

        [[nodiscard]] StableNodeId CanvasPinType(const Keire::ShaderGraphValueType type) noexcept
        {
            return static_cast<StableNodeId>(type) + 1U;
        }

        [[nodiscard]] bool Compatible(const Keire::ShaderGraphValueType output,
                                      const Keire::ShaderGraphValueType input) noexcept
        {
            return output == input ||
                   ((output == Keire::ShaderGraphValueType::Color && input == Keire::ShaderGraphValueType::Vector4) ||
                    (output == Keire::ShaderGraphValueType::Vector4 && input == Keire::ShaderGraphValueType::Color) ||
                    ((output == Keire::ShaderGraphValueType::Vector4 || output == Keire::ShaderGraphValueType::Color) &&
                     input == Keire::ShaderGraphValueType::Vector3) ||
                    (output == Keire::ShaderGraphValueType::Vector3 &&
                     (input == Keire::ShaderGraphValueType::Vector4 || input == Keire::ShaderGraphValueType::Color))) ||
                   (output == Keire::ShaderGraphValueType::Scalar && input != Keire::ShaderGraphValueType::Texture2D &&
                    input != Keire::ShaderGraphValueType::MaterialAttributes &&
                    input != Keire::ShaderGraphValueType::Bsdf);
        }

        [[nodiscard]] const Keire::ShaderGraphNode* FindExpressionNode(const Keire::MaterialGraphDefinition& definition,
                                                                       const Keire::AssetId id) noexcept
        {
            const auto found = std::ranges::find(definition.SurfaceGraph.Nodes, id, &Keire::ShaderGraphNode::Id);
            return found == definition.SurfaceGraph.Nodes.end() ? nullptr : std::addressof(*found);
        }

        [[nodiscard]] const Keire::ShaderGraphPin* FindExpressionPin(const Keire::ShaderGraphNode& node,
                                                                     const Keire::AssetId id) noexcept
        {
            const auto found = std::ranges::find(node.Pins, id, &Keire::ShaderGraphPin::Id);
            return found == node.Pins.end() ? nullptr : std::addressof(*found);
        }

        [[nodiscard]] bool HasSurfaceExpressions(const Keire::MaterialGraphDefinition& definition) noexcept
        {
            const auto output = std::ranges::find(definition.SurfaceGraph.Nodes, Keire::ShaderGraphNodeKind::Master,
                                                  &Keire::ShaderGraphNode::Kind);
            return output != definition.SurfaceGraph.Nodes.end() &&
                   std::ranges::any_of(definition.SurfaceGraph.Connections,
                                       [&](const Keire::ShaderGraphConnection& connection)
                                       { return connection.Input.Node == output->Id; });
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
                      std::optional<Keire::MaterialAssetDefinition> material;
                      try
                      {
                          if (HasSurfaceExpressions(definition))
                          {
                              const auto shaderTemplate = m_Specification.ResolveTemplate
                                                              ? m_Specification.ResolveTemplate(definition.Shader)
                                                              : std::optional<Keire::ShaderGraphDefinition>{};
                              if (!shaderTemplate)
                                  return;
                              auto composed = Keire::ComposeMaterialGraphShader(definition, *shaderTemplate);
                              if (!Keire::ShaderGraphReferencedAssets(composed).empty())
                                  composed =
                                      Keire::ExpandShaderGraphFunctions(composed, m_Specification.ResolveFunction);
                              Keire::ShaderGraphInstanceDefinition defaults;
                              defaults.Parent = asset;
                              defaults.KeywordOverrides = definition.Shader.Keywords;
                              const std::array ancestry{defaults};
                              const auto resolved = Keire::ResolveShaderGraphInstance(composed, ancestry);
                              Keire::MaterialAssetDefinition live;
                              live.Shader = m_Specification.ResolveShader(definition.Shader);
                              if (!live.Shader)
                                  return;
                              live.Surface = definition.Surface;
                              live.ContributeEmissionToGI = definition.ContributeEmissionToGI;
                              live.EmissiveGIIntensity = definition.EmissiveGIIntensity;
                              live.Properties = resolved.Properties;
                              material = std::move(live);
                          }
                          else
                          {
                              material = Keire::BakeMaterialGraph(definition, m_Specification.ResolveShader);
                          }
                      }
                      catch (const std::exception&)
                      {
                          return;
                      }
                      m_Specification.Preview(asset, *material);
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
        m_AutosaveSeconds = 0.0;
        RefreshDiagnostics();
    }

    void MaterialGraphDocument::Save()
    {
        if (!Dirty())
        {
            m_AutosaveSeconds = 0.0;
            return;
        }
        m_Host.Save();
        m_AutosaveSeconds = 0.0;
    }

    bool MaterialGraphDocument::AdvanceAutosave(const double deltaSeconds)
    {
        if (!std::isfinite(deltaSeconds) || deltaSeconds < 0.0 || deltaSeconds > 60.0)
            throw std::invalid_argument("Material Graph autosave delta must be finite and in the range 0..60.");
        if (!Dirty())
        {
            m_AutosaveSeconds = 0.0;
            return false;
        }
        constexpr double autosaveDelaySeconds = 0.5;
        m_AutosaveSeconds += deltaSeconds;
        if (m_AutosaveSeconds < autosaveDelaySeconds)
            return false;
        m_AutosaveSeconds = 0.0;
        m_Host.Save();
        return true;
    }

    void MaterialGraphDocument::Discard()
    {
        m_Host.Discard();
        m_AutosaveSeconds = 0.0;
        RefreshDiagnostics();
    }
    void MaterialGraphDocument::Close() noexcept
    {
        m_Host.Close();
        m_Diagnostics.clear();
        m_AutosaveSeconds = 0.0;
    }
    bool MaterialGraphDocument::Undo()
    {
        const bool changed = m_Host.Undo();
        if (changed)
        {
            m_AutosaveSeconds = 0.0;
            RefreshDiagnostics();
        }
        return changed;
    }
    bool MaterialGraphDocument::Redo()
    {
        const bool changed = m_Host.Redo();
        if (changed)
        {
            m_AutosaveSeconds = 0.0;
            RefreshDiagnostics();
        }
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
        {
            m_AutosaveSeconds = 0.0;
            RefreshDiagnostics();
        }
        return changed;
    }

    bool MaterialGraphDocument::SetShader(Keire::MaterialShaderReference shader)
    {
        const auto shaderInterface = m_Specification.ResolveInterface(shader);
        if (!shaderInterface)
            throw std::invalid_argument("Selected shader does not expose a compatible material interface.");
        if (shader.Kind != Keire::MaterialShaderSourceKind::ShaderGraph && HasSurfaceExpressions(Definition()))
            throw std::invalid_argument(
                "Disconnect Material Output expressions before switching this Material Graph to a raw Shader.");
        std::optional<Keire::ShaderGraphDefinition> materialSurface;
        if (!HasSurfaceExpressions(Definition()) && shader.Kind == Keire::MaterialShaderSourceKind::ShaderGraph &&
            m_Specification.ResolveTemplate)
            if (const auto shaderTemplate = m_Specification.ResolveTemplate(shader))
                materialSurface = Keire::CreateMaterialSurfaceGraph(*shaderTemplate);
        return Edit("Change Material Graph shader",
                    [&](auto& definition)
                    {
                        definition.Shader = std::move(shader);
                        if (materialSurface)
                            definition.SurfaceGraph = std::move(*materialSurface);
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

    bool MaterialGraphDocument::AddExpressionNode(Keire::ShaderGraphNode node)
    {
        if (node.Kind == Keire::ShaderGraphNodeKind::Master)
            throw std::invalid_argument("Material Graph has one fixed Material Output node.");
        return Edit("Add Material Graph expression",
                    [node = std::move(node)](auto& definition) mutable
                    {
                        if (node.Kind == Keire::ShaderGraphNodeKind::Keyword)
                        {
                            if (node.Symbol.empty())
                                throw std::invalid_argument("Static parameters require a shader symbol.");
                            definition.SurfaceGraph.Keywords.push_back(
                                {.Name = node.Symbol, .DefaultOption = "false", .Exposed = true});
                        }
                        definition.SurfaceGraph.Nodes.push_back(std::move(node));
                    });
    }

    bool MaterialGraphDocument::EditExpressionNode(const Keire::AssetId node,
                                                   const std::function<void(Keire::ShaderGraphNode&)>& operation)
    {
        if (!operation)
            throw std::invalid_argument("Material Graph expression edits require an operation.");
        return Edit("Edit Material Graph expression",
                    [node, &operation](auto& definition)
                    {
                        const auto found =
                            std::ranges::find(definition.SurfaceGraph.Nodes, node, &Keire::ShaderGraphNode::Id);
                        if (found == definition.SurfaceGraph.Nodes.end())
                            throw std::invalid_argument("Material Graph expression node is unavailable.");
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
                        if (found != definition.Nodes.end())
                        {
                            found->EditorPosition = position;
                            return;
                        }
                        const auto expression =
                            std::ranges::find(definition.SurfaceGraph.Nodes, node, &Keire::ShaderGraphNode::Id);
                        if (expression == definition.SurfaceGraph.Nodes.end())
                            throw std::invalid_argument("Material Graph node is unavailable.");
                        expression->EditorPosition = position;
                    });
    }

    bool MaterialGraphDocument::RemoveNode(const Keire::AssetId node)
    {
        const auto expression = FindExpressionNode(Definition(), node);
        if (node == Definition().OutputNode || (expression && expression->Kind == Keire::ShaderGraphNodeKind::Master))
            throw std::invalid_argument("Material Output cannot be removed.");
        const auto keyword = expression && expression->Kind == Keire::ShaderGraphNodeKind::Keyword
                                 ? std::optional(expression->Symbol)
                                 : std::nullopt;
        return Edit("Remove Material Graph node",
                    [node, keyword](auto& definition)
                    {
                        const auto valueCount = definition.Nodes.size();
                        const auto expressionCount = definition.SurfaceGraph.Nodes.size();
                        std::erase_if(definition.Nodes, [node](const auto& candidate) { return candidate.Id == node; });
                        std::erase_if(definition.SurfaceGraph.Nodes,
                                      [node](const auto& candidate) { return candidate.Id == node; });
                        if (definition.Nodes.size() == valueCount &&
                            definition.SurfaceGraph.Nodes.size() == expressionCount)
                            throw std::invalid_argument("Material Graph node is unavailable.");
                        std::erase_if(definition.Connections,
                                      [node](const auto& connection) { return connection.Output.Node == node; });
                        std::erase_if(definition.SurfaceGraph.Connections, [node](const auto& connection)
                                      { return connection.Output.Node == node || connection.Input.Node == node; });
                        if (keyword)
                            std::erase_if(definition.SurfaceGraph.Keywords,
                                          [&](const auto& candidate) { return candidate.Name == *keyword; });
                    });
    }

    NodeGraphConnectionValidation MaterialGraphDocument::CheckConnection(const Keire::MaterialGraphEndpoint output,
                                                                         const Keire::MaterialGraphEndpoint input) const
    {
        const auto source = std::ranges::find(Definition().Nodes, output.Node, &Keire::MaterialGraphValueNode::Id);
        const auto target =
            std::ranges::find(Definition().Properties, input.Pin, &Keire::MaterialGraphPropertyBinding::Pin);
        if (source != Definition().Nodes.end() && source->OutputPin == output.Pin &&
            input.Node == Definition().OutputNode && target != Definition().Properties.end())
            return source->Type == target->Type
                       ? NodeGraphConnectionValidation{}
                       : NodeGraphConnectionValidation{NodeGraphConnectionValidationStatus::Reject,
                                                       "Material value type does not match this input."};

        const auto* expressionSource = FindExpressionNode(Definition(), output.Node);
        const auto* expressionTarget = FindExpressionNode(Definition(), input.Node);
        if (!expressionSource || !expressionTarget)
            return {NodeGraphConnectionValidationStatus::Reject,
                    "Connect an expression output to an expression or Material Output input."};
        const auto* outputPin = FindExpressionPin(*expressionSource, output.Pin);
        const auto* inputPin = FindExpressionPin(*expressionTarget, input.Pin);
        if (!outputPin || !inputPin || outputPin->Direction != Keire::ShaderGraphPinDirection::Output ||
            inputPin->Direction != Keire::ShaderGraphPinDirection::Input)
            return {NodeGraphConnectionValidationStatus::Reject, "Material expression pin direction is invalid."};
        if (!Compatible(outputPin->Type, inputPin->Type))
            return {NodeGraphConnectionValidationStatus::Reject, "Material expression types are incompatible."};
        return {};
    }

    bool MaterialGraphDocument::AddConnection(Keire::MaterialGraphConnection connection)
    {
        const auto validation = CheckConnection(connection.Output, connection.Input);
        if (!validation.CanConnect())
            throw std::invalid_argument(validation.Diagnostic);
        if (!connection.Id)
            connection.Id = Keire::AssetId::Generate();
        const bool surfaceConnection = FindExpressionNode(Definition(), connection.Output.Node) != nullptr;
        return Edit(surfaceConnection ? "Connect Material Graph expression" : "Connect Material Graph value",
                    [connection](auto& definition)
                    {
                        const bool expression = std::ranges::any_of(definition.SurfaceGraph.Nodes, [&](const auto& node)
                                                                    { return node.Id == connection.Output.Node; });
                        if (expression)
                        {
                            std::erase_if(definition.SurfaceGraph.Connections,
                                          [&](const auto& candidate)
                                          {
                                              return candidate.Input.Node == connection.Input.Node &&
                                                     candidate.Input.Pin == connection.Input.Pin;
                                          });
                            definition.SurfaceGraph.Connections.push_back(
                                {connection.Id,
                                 {connection.Output.Node, connection.Output.Pin},
                                 {connection.Input.Node, connection.Input.Pin}});
                        }
                        else
                        {
                            std::erase_if(definition.Connections,
                                          [&](const auto& candidate) { return candidate.Input == connection.Input; });
                            definition.Connections.push_back(connection);
                        }
                    });
    }

    bool MaterialGraphDocument::RemoveConnection(const Keire::AssetId connection)
    {
        return Edit("Disconnect Material Graph value",
                    [connection](auto& definition)
                    {
                        const auto valueCount = definition.Connections.size();
                        const auto expressionCount = definition.SurfaceGraph.Connections.size();
                        std::erase_if(definition.Connections,
                                      [connection](const auto& candidate) { return candidate.Id == connection; });
                        std::erase_if(definition.SurfaceGraph.Connections,
                                      [connection](const auto& candidate) { return candidate.Id == connection; });
                        if (valueCount == definition.Connections.size() &&
                            expressionCount == definition.SurfaceGraph.Connections.size())
                            throw std::invalid_argument("Material Graph connection is unavailable.");
                    });
    }

    bool MaterialGraphDocument::SetConnectionRouting(const Keire::AssetId connection,
                                                     std::vector<Keire::Vector2> routingPoints)
    {
        auto candidate = Definition();
        auto found = std::ranges::find(candidate.Connections, connection, &Keire::MaterialGraphConnection::Id);
        if (found != candidate.Connections.end())
            found->RoutingPoints = std::move(routingPoints);
        else
        {
            auto expression =
                std::ranges::find(candidate.SurfaceGraph.Connections, connection, &Keire::ShaderGraphConnection::Id);
            if (expression == candidate.SurfaceGraph.Connections.end())
                throw std::invalid_argument("Material Graph connection is unavailable.");
            expression->RoutingPoints = std::move(routingPoints);
        }
        return m_Host.EditMetadata("Route Material Graph connection", std::move(candidate));
    }

    MaterialGraphCanvasModel MaterialGraphDocument::BuildCanvasModel(const bool includeTemplateParameters) const
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

        if (includeTemplateParameters)
        {
            NodeGraphNode output;
            output.Id = addIdentity(result.NodeIdentities, Definition().OutputNode, 0x4d474f5554505554ULL);
            output.Label = "Template Defaults";
            output.Subtitle = Definition().Shader.Kind == Keire::MaterialShaderSourceKind::ShaderGraph
                                  ? "Compatibility overrides"
                                  : "Raw shader bindings";
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
                canvas.Subtitle = node.Type == Keire::ShaderPropertyType::Texture2D ? "Texture2D" : "Template value";
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
        }

        for (const auto& node : Definition().SurfaceGraph.Nodes)
        {
            NodeGraphNode canvas;
            canvas.Id = addIdentity(result.NodeIdentities, node.Id, 0x4d47535552460000ULL);
            canvas.Label = node.Kind == Keire::ShaderGraphNodeKind::Master ? "Material Output" : node.Name;
            const auto* descriptor = Keire::FindShaderGraphNodeDescriptor(
                node.TypeId.empty() ? Keire::ShaderGraphNodeTypeId(node.Kind) : std::string_view(node.TypeId));
            canvas.Subtitle = node.Kind == Keire::ShaderGraphNodeKind::Master ? "Compiled surface attributes"
                              : descriptor                                    ? std::string(descriptor->Category)
                                                                              : "Material expression";
            canvas.Position = node.EditorPosition;
            canvas.Size = {node.Kind == Keire::ShaderGraphNodeKind::Master ? 290.0F : 220.0F,
                           std::max(88.0F, 48.0F + static_cast<float>(node.Pins.size()) * 22.0F)};
            canvas.Color = node.Kind == Keire::ShaderGraphNodeKind::Master ? Keire::UiColor{0.48F, 0.2F, 0.08F, 1.0F}
                                                                           : Keire::UiColor{0.12F, 0.25F, 0.36F, 1.0F};
            for (const auto& graphPin : node.Pins)
            {
                NodeGraphPin pin;
                pin.Id = addIdentity(result.PinIdentities, graphPin.Id, 0x4d4750494e000000ULL);
                pin.Label = graphPin.Name;
                pin.Direction = graphPin.Direction == Keire::ShaderGraphPinDirection::Input
                                    ? NodeGraphPinDirection::Input
                                    : NodeGraphPinDirection::Output;
                pin.Type = CanvasPinType(graphPin.Type);
                pin.Color = PinColor(graphPin.Type);
                canvas.Pins.push_back(std::move(pin));
            }
            result.Nodes.push_back(std::move(canvas));
        }

        if (includeTemplateParameters)
            for (const auto& connection : Definition().Connections)
            {
                NodeGraphConnection canvas;
                canvas.Id = addIdentity(result.ConnectionIdentities, connection.Id, 0x4d47434f4e4e0000ULL);
                canvas.Source = *FindCanvasIdentity(result.NodeIdentities, connection.Output.Node);
                canvas.SourcePin = *FindCanvasIdentity(result.PinIdentities, connection.Output.Pin);
                canvas.Target = *FindCanvasIdentity(result.NodeIdentities, connection.Input.Node);
                canvas.TargetPin = *FindCanvasIdentity(result.PinIdentities, connection.Input.Pin);
                canvas.RoutingPoints = connection.RoutingPoints;
                result.Connections.push_back(std::move(canvas));
            }
        for (const auto& connection : Definition().SurfaceGraph.Connections)
        {
            NodeGraphConnection canvas;
            canvas.Id = addIdentity(result.ConnectionIdentities, connection.Id, 0x4d4753434f4e0000ULL);
            canvas.Source = *FindCanvasIdentity(result.NodeIdentities, connection.Output.Node);
            canvas.SourcePin = *FindCanvasIdentity(result.PinIdentities, connection.Output.Pin);
            canvas.Target = *FindCanvasIdentity(result.NodeIdentities, connection.Input.Node);
            canvas.TargetPin = *FindCanvasIdentity(result.PinIdentities, connection.Input.Pin);
            canvas.RoutingPoints = connection.RoutingPoints;
            result.Connections.push_back(std::move(canvas));
        }
        return result;
    }

    void MaterialGraphDocument::RefreshDiagnostics()
    {
        const auto shaderInterface = m_Specification.ResolveInterface(Definition().Shader);
        if (shaderInterface)
        {
            m_Diagnostics = Keire::ValidateMaterialGraphAgainstInterface(Definition(), *shaderInterface);
        }
        else
        {
            m_Diagnostics = {{Keire::MaterialGraphDiagnosticSeverity::Error,
                              "MAT1000",
                              "Selected shader interface is unavailable.",
                              {},
                              {},
                              {}}};
        }
        if (Definition().Shader.Kind != Keire::MaterialShaderSourceKind::ShaderGraph ||
            !HasSurfaceExpressions(Definition()))
            return;
        const auto shaderTemplate =
            m_Specification.ResolveTemplate ? m_Specification.ResolveTemplate(Definition().Shader) : std::nullopt;
        if (!shaderTemplate)
        {
            m_Diagnostics.push_back({Keire::MaterialGraphDiagnosticSeverity::Error,
                                     "MAT2001",
                                     "Selected Shader Graph template source is unavailable.",
                                     {},
                                     {},
                                     {}});
            return;
        }
        try
        {
            const auto composed = Keire::ComposeMaterialGraphShader(Definition(), *shaderTemplate);
            if (!Keire::ShaderGraphReferencedAssets(composed).empty())
                (void)Keire::ExpandShaderGraphFunctions(composed, m_Specification.ResolveFunction);
        }
        catch (const std::exception& error)
        {
            m_Diagnostics.push_back(
                {Keire::MaterialGraphDiagnosticSeverity::Error,
                 "MAT2002",
                 std::string("Material surface cannot compile with this Shader Graph: ") + error.what(),
                 {},
                 {},
                 {}});
        }
    }
} // namespace KeireEditor
