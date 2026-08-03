#include "KeireClient/Editor/MaterialGraphDocument.h"

#include <algorithm>
#include <bit>
#include <cmath>
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

        [[nodiscard]] Keire::UiColor PinColor(const Keire::MaterialGraphValueType type) noexcept
        {
            switch (type)
            {
            case Keire::MaterialGraphValueType::Scalar:
                return {0.48F, 0.78F, 0.42F, 1.0F};
            case Keire::MaterialGraphValueType::Vector2:
                return {0.3F, 0.72F, 0.75F, 1.0F};
            case Keire::MaterialGraphValueType::Vector3:
                return {0.3F, 0.58F, 0.9F, 1.0F};
            case Keire::MaterialGraphValueType::Vector4:
                return {0.56F, 0.48F, 0.9F, 1.0F};
            case Keire::MaterialGraphValueType::Color:
                return {0.94F, 0.58F, 0.28F, 1.0F};
            case Keire::MaterialGraphValueType::Texture2D:
                return {0.87F, 0.36F, 0.62F, 1.0F};
            }
            return {};
        }

        [[nodiscard]] std::string_view NodeCategory(const Keire::MaterialGraphNodeKind kind) noexcept
        {
            switch (kind)
            {
            case Keire::MaterialGraphNodeKind::Master:
                return "Material Output";
            case Keire::MaterialGraphNodeKind::Parameter:
            case Keire::MaterialGraphNodeKind::Constant:
            case Keire::MaterialGraphNodeKind::UV:
            case Keire::MaterialGraphNodeKind::VertexColor:
            case Keire::MaterialGraphNodeKind::WorldPosition:
            case Keire::MaterialGraphNodeKind::WorldNormal:
            case Keire::MaterialGraphNodeKind::ViewDirection:
                return "Input";
            case Keire::MaterialGraphNodeKind::TextureSample:
            case Keire::MaterialGraphNodeKind::UVTransform:
            case Keire::MaterialGraphNodeKind::RotateUV:
            case Keire::MaterialGraphNodeKind::Parallax:
                return "Texture & UV";
            case Keire::MaterialGraphNodeKind::NormalMap:
            case Keire::MaterialGraphNodeKind::DetailNormal:
            case Keire::MaterialGraphNodeKind::Fresnel:
            case Keire::MaterialGraphNodeKind::Desaturate:
                return "Surface";
            case Keire::MaterialGraphNodeKind::SimpleNoise:
                return "Procedural";
            case Keire::MaterialGraphNodeKind::Keyword:
            case Keire::MaterialGraphNodeKind::StaticSwitch:
                return "Logic & Variants";
            case Keire::MaterialGraphNodeKind::Custom:
                return "Advanced";
            default:
                return "Math";
            }
        }

        [[nodiscard]] Keire::UiColor NodeColor(const Keire::MaterialGraphNodeKind kind) noexcept
        {
            switch (kind)
            {
            case Keire::MaterialGraphNodeKind::Master:
                return {0.42F, 0.22F, 0.08F, 1.0F};
            case Keire::MaterialGraphNodeKind::Parameter:
            case Keire::MaterialGraphNodeKind::Constant:
            case Keire::MaterialGraphNodeKind::UV:
            case Keire::MaterialGraphNodeKind::VertexColor:
            case Keire::MaterialGraphNodeKind::WorldPosition:
            case Keire::MaterialGraphNodeKind::WorldNormal:
            case Keire::MaterialGraphNodeKind::ViewDirection:
                return {0.10F, 0.25F, 0.31F, 1.0F};
            case Keire::MaterialGraphNodeKind::TextureSample:
            case Keire::MaterialGraphNodeKind::UVTransform:
            case Keire::MaterialGraphNodeKind::RotateUV:
            case Keire::MaterialGraphNodeKind::Parallax:
                return {0.23F, 0.13F, 0.34F, 1.0F};
            case Keire::MaterialGraphNodeKind::NormalMap:
            case Keire::MaterialGraphNodeKind::DetailNormal:
            case Keire::MaterialGraphNodeKind::Fresnel:
            case Keire::MaterialGraphNodeKind::Desaturate:
                return {0.09F, 0.29F, 0.25F, 1.0F};
            case Keire::MaterialGraphNodeKind::SimpleNoise:
                return {0.28F, 0.19F, 0.08F, 1.0F};
            case Keire::MaterialGraphNodeKind::Keyword:
            case Keire::MaterialGraphNodeKind::StaticSwitch:
                return {0.31F, 0.12F, 0.17F, 1.0F};
            case Keire::MaterialGraphNodeKind::Custom:
                return {0.31F, 0.12F, 0.28F, 1.0F};
            default:
                return {0.12F, 0.18F, 0.28F, 1.0F};
            }
        }

        [[nodiscard]] const Keire::MaterialGraphNode& RequireNode(const Keire::MaterialGraphDefinition& definition,
                                                                  const Keire::AssetId id)
        {
            const auto found = std::ranges::find(definition.Nodes, id, &Keire::MaterialGraphNode::Id);
            if (found == definition.Nodes.end())
                throw std::invalid_argument("Material Graph node is unavailable.");
            return *found;
        }

        [[nodiscard]] const Keire::MaterialGraphPin& RequirePin(const Keire::MaterialGraphNode& node,
                                                                const Keire::AssetId id)
        {
            const auto found = std::ranges::find(node.Pins, id, &Keire::MaterialGraphPin::Id);
            if (found == node.Pins.end())
                throw std::invalid_argument("Material Graph pin is unavailable.");
            return *found;
        }

        template <typename Range>
        [[nodiscard]] std::optional<Keire::AssetId> ResolveCanvasIdentity(const Range& identities,
                                                                          const StableNodeId id) noexcept
        {
            const auto found =
                std::ranges::find_if(identities, [id](const auto& identity) { return identity.first == id; });
            return found == identities.end() ? std::nullopt : std::optional<Keire::AssetId>(found->second);
        }
    } // namespace

    std::optional<Keire::AssetId> MaterialGraphCanvasModel::Node(const StableNodeId id) const noexcept
    {
        return ResolveCanvasIdentity(NodeIdentities, id);
    }

    std::optional<Keire::AssetId> MaterialGraphCanvasModel::Pin(const StableNodeId id) const noexcept
    {
        return ResolveCanvasIdentity(PinIdentities, id);
    }

    std::optional<Keire::AssetId> MaterialGraphCanvasModel::Connection(const StableNodeId id) const noexcept
    {
        return ResolveCanvasIdentity(ConnectionIdentities, id);
    }

    MaterialGraphDocument::MaterialGraphDocument(MaterialGraphDocumentSpecification specification)
        : m_Specification(std::move(specification)),
          m_Host({.Validate = [](const Keire::MaterialGraphDefinition& definition)
                  { Keire::ValidateMaterialGraph(definition); },
                  .Encode = [](const Keire::MaterialGraphDefinition& definition)
                  { return Keire::MaterialGraphAsset::EncodeSource(definition); },
                  .Preview = [this](const Keire::AssetId, const Keire::MaterialGraphDefinition& definition)
                  { CompileAndPreview(definition); },
                  .CancelPreview = m_Specification.StopPreview,
                  .Persist = m_Specification.Persist})
    {
        if (!m_Specification.Persist)
            throw std::invalid_argument("Material Graph documents require a persistence callback.");
    }

    void MaterialGraphDocument::Open(const Keire::AssetId asset, const std::span<const std::byte> bytes,
                                     const std::uint64_t revision, Keire::Ref<Keire::UndoContext> undo)
    {
        Open(asset, Keire::MaterialGraphAsset::DecodeSource(bytes), revision, std::move(undo));
    }

    void MaterialGraphDocument::Open(const Keire::AssetId asset, Keire::MaterialGraphDefinition definition,
                                     const std::uint64_t revision, Keire::Ref<Keire::UndoContext> undo)
    {
        m_LastGoodCompilation.reset();
        m_LastGoodDefinition.reset();
        m_Host.Open(asset, std::move(definition), revision, std::move(undo));
    }

    void MaterialGraphDocument::Create(const Keire::AssetId asset, Keire::MaterialGraphDefinition definition,
                                       Keire::Ref<Keire::UndoContext> undo)
    {
        m_LastGoodCompilation.reset();
        m_LastGoodDefinition.reset();
        m_Host.Create(asset, std::move(definition), std::move(undo));
    }

    void MaterialGraphDocument::Save()
    {
        if (!Publishable())
            throw std::logic_error("Material Graph cannot be saved until its generated-shader diagnostics are clear.");
        m_Host.Save();
    }

    void MaterialGraphDocument::Discard() { m_Host.Discard(); }

    void MaterialGraphDocument::Close() noexcept
    {
        m_Host.Close();
        m_Compilation = {};
        m_LastGoodCompilation.reset();
        m_LastGoodDefinition.reset();
        m_Diagnostic.clear();
    }

    bool MaterialGraphDocument::Undo() { return m_Host.Undo(); }

    bool MaterialGraphDocument::Redo() { return m_Host.Redo(); }

    std::string_view MaterialGraphDocument::Diagnostic() const noexcept
    {
        return m_Host.Diagnostic().empty() ? std::string_view(m_Diagnostic) : m_Host.Diagnostic();
    }

    bool MaterialGraphDocument::Edit(const std::string_view name,
                                     const std::function<void(Keire::MaterialGraphDefinition&)>& operation)
    {
        if (!operation)
            throw std::invalid_argument("Material Graph edits require an operation.");
        auto candidate = m_Host.Draft();
        operation(candidate);
        return m_Host.Edit(name, std::move(candidate));
    }

    bool MaterialGraphDocument::AddNode(Keire::MaterialGraphNode node)
    {
        return Edit("Add Material Graph node", [node = std::move(node)](auto& definition) mutable
                    { definition.Nodes.push_back(std::move(node)); });
    }

    bool MaterialGraphDocument::EditNode(const Keire::AssetId node,
                                         const std::function<void(Keire::MaterialGraphNode&)>& operation)
    {
        if (!operation)
            throw std::invalid_argument("Material Graph node edits require an operation.");
        return Edit("Edit Material Graph node",
                    [node, &operation](auto& definition)
                    {
                        const auto found = std::ranges::find(definition.Nodes, node, &Keire::MaterialGraphNode::Id);
                        if (found == definition.Nodes.end())
                            throw std::invalid_argument("Material Graph node is unavailable.");
                        operation(*found);
                    });
    }

    bool MaterialGraphDocument::RemoveNode(const Keire::AssetId node)
    {
        return Edit("Remove Material Graph node",
                    [node](auto& definition)
                    {
                        const auto found = std::ranges::find(definition.Nodes, node, &Keire::MaterialGraphNode::Id);
                        if (found == definition.Nodes.end())
                            throw std::invalid_argument("Material Graph node is unavailable.");
                        if (found->Kind == Keire::MaterialGraphNodeKind::Master)
                            throw std::invalid_argument("Material Graph Master node cannot be removed.");
                        definition.Nodes.erase(found);
                        std::erase_if(definition.Connections, [node](const auto& connection)
                                      { return connection.Output.Node == node || connection.Input.Node == node; });
                    });
    }

    bool MaterialGraphDocument::AddConnection(Keire::MaterialGraphConnection connection)
    {
        const auto check = CheckConnection(connection.Output, connection.Input);
        if (!check.CanConnect())
            throw std::invalid_argument(check.Diagnostic);
        if (!connection.Id)
            connection.Id = Keire::AssetId::Generate();
        return Edit("Connect Material Graph pins",
                    [connection = std::move(connection)](auto& definition) mutable
                    {
                        std::erase_if(definition.Connections,
                                      [&](const auto& existing) { return existing.Input == connection.Input; });
                        definition.Connections.push_back(std::move(connection));
                    });
    }

    bool MaterialGraphDocument::RemoveConnection(const Keire::AssetId connection)
    {
        return Edit("Disconnect Material Graph pins",
                    [connection](auto& definition)
                    {
                        const auto found =
                            std::ranges::find(definition.Connections, connection, &Keire::MaterialGraphConnection::Id);
                        if (found == definition.Connections.end())
                            throw std::invalid_argument("Material Graph connection is unavailable.");
                        definition.Connections.erase(found);
                    });
    }

    NodeGraphConnectionValidation MaterialGraphDocument::CheckConnection(const Keire::MaterialGraphEndpoint output,
                                                                         const Keire::MaterialGraphEndpoint input) const
    {
        NodeGraphConnectionValidation result{NodeGraphConnectionValidationStatus::Reject, {}};
        try
        {
            const auto& outputNode = RequireNode(m_Host.Draft(), output.Node);
            const auto& inputNode = RequireNode(m_Host.Draft(), input.Node);
            const auto& outputPin = RequirePin(outputNode, output.Pin);
            const auto& inputPin = RequirePin(inputNode, input.Pin);
            if (output.Node == input.Node || outputPin.Direction != Keire::MaterialGraphPinDirection::Output ||
                inputPin.Direction != Keire::MaterialGraphPinDirection::Input)
                throw std::invalid_argument("Connect an output pin to an input pin on another node.");
            auto candidate = m_Host.Draft();
            const bool replaces = std::ranges::any_of(candidate.Connections, [&](const auto& connection)
                                                      { return connection.Input == input; });
            std::erase_if(candidate.Connections, [&](const auto& connection) { return connection.Input == input; });
            candidate.Connections.push_back({Keire::AssetId::Generate(), output, input});
            Keire::ValidateMaterialGraph(candidate);
            result.Status = replaces ? NodeGraphConnectionValidationStatus::AcceptWithWarning
                                     : NodeGraphConnectionValidationStatus::Accept;
            if (replaces)
                result.Diagnostic = "Connecting replaces the input's existing cable.";
        }
        catch (const std::exception& error)
        {
            result.Diagnostic = error.what();
        }
        return result;
    }

    MaterialGraphCanvasModel MaterialGraphDocument::BuildCanvasModel() const
    {
        MaterialGraphCanvasModel result;
        StableNodeGraphIdMap nodeIds;
        StableNodeGraphIdMap pinIds;
        StableNodeGraphIdMap connectionIds;
        for (const auto& source : m_Host.Draft().Nodes)
        {
            const auto nodeId = nodeIds.Assign(source.Id, PreferredCanvasId(source.Id, 0x4d41544e4f444501ULL));
            result.NodeIdentities.emplace_back(nodeId, source.Id);
            NodeGraphNode node;
            node.Id = nodeId;
            node.Label = source.Name;
            node.Subtitle = NodeCategory(source.Kind);
            node.Position = source.EditorPosition;
            node.Size = {220.0F, std::max(72.0F, 42.0F + static_cast<float>(source.Pins.size()) * 20.0F)};
            node.Color = NodeColor(source.Kind);
            for (const auto& sourcePin : source.Pins)
            {
                const auto pinId = pinIds.Assign(sourcePin.Id, PreferredCanvasId(sourcePin.Id, 0x4d415450494e0001ULL));
                result.PinIdentities.emplace_back(pinId, sourcePin.Id);
                node.Pins.push_back({pinId, sourcePin.Name,
                                     sourcePin.Direction == Keire::MaterialGraphPinDirection::Input
                                         ? NodeGraphPinDirection::Input
                                         : NodeGraphPinDirection::Output,
                                     static_cast<StableNodeId>(sourcePin.Type) + 1U, PinColor(sourcePin.Type)});
            }
            result.Nodes.push_back(std::move(node));
        }
        for (const auto& source : m_Host.Draft().Connections)
        {
            const auto connectionId =
                connectionIds.Assign(source.Id, PreferredCanvasId(source.Id, 0x4d41544c494e4b01ULL));
            result.ConnectionIdentities.emplace_back(connectionId, source.Id);
            const auto outputNode = nodeIds.Find(source.Output.Node);
            const auto inputNode = nodeIds.Find(source.Input.Node);
            const auto outputPin = pinIds.Find(source.Output.Pin);
            const auto inputPin = pinIds.Find(source.Input.Pin);
            if (!outputNode || !inputNode || !outputPin || !inputPin)
                throw std::logic_error("Validated Material Graph canvas identities became unavailable.");
            result.Connections.push_back({connectionId, *outputNode, *inputNode, {}, *outputPin, *inputPin});
        }
        StableNodeGraphCanvas::Validate(result.Nodes, result.Connections);
        return result;
    }

    void MaterialGraphDocument::SetCompileOptions(Keire::MaterialGraphCompileOptions options)
    {
        m_Specification.CompileOptions = std::move(options);
        if (IsOpen())
            RecompileCurrent();
    }

    void MaterialGraphDocument::SetPreviewSettings(MaterialGraphPreviewSettings settings)
    {
        if (settings.Mesh == Keire::MaterialGraphPreviewMesh::Custom && !settings.CustomMesh)
            throw std::invalid_argument("Custom Material Graph preview requires a mesh asset.");
        if (!std::isfinite(settings.Exposure) || settings.Exposure < 0.1F || settings.Exposure > 8.0F ||
            !std::isfinite(settings.EnvironmentIntensity) || settings.EnvironmentIntensity < 0.0F ||
            settings.EnvironmentIntensity > 8.0F || !std::isfinite(settings.RotationDegrees) ||
            settings.RotationDegrees < -180.0F || settings.RotationDegrees > 180.0F)
            throw std::invalid_argument("Material Graph preview lighting controls are outside their supported range.");
        if (settings == m_PreviewSettings)
            return;
        m_PreviewSettings = std::move(settings);
        if (m_LastGoodCompilation && m_Specification.Preview && m_Host.IsOpen())
            m_Specification.Preview(m_Host.Asset(), *m_LastGoodCompilation, m_PreviewSettings);
    }

    void MaterialGraphDocument::CompileAndPreview(const Keire::MaterialGraphDefinition& definition)
    {
        m_Compilation = Keire::CompileMaterialGraph(definition, m_Specification.CompileOptions);
        m_Diagnostic.clear();
        if (!m_Compilation.Succeeded())
        {
            if (!m_Compilation.Diagnostics.empty())
                m_Diagnostic = m_Compilation.Diagnostics.front().Message;
            return;
        }
        m_LastGoodCompilation = m_Compilation;
        m_LastGoodDefinition = definition;
        if (m_Specification.Preview)
            m_Specification.Preview(m_Host.Asset(), m_Compilation, m_PreviewSettings);
    }

    void MaterialGraphDocument::RecompileCurrent()
    {
        if (m_Host.IsOpen())
            CompileAndPreview(m_Host.Draft());
    }
} // namespace KeireEditor
