#include "KeireClient/Editor/MaterialGraphDocument.h"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <ranges>
#include <stdexcept>
#include <utility>

namespace KeireEditor
{
    namespace
    {
        constexpr double LiveCompilationIntervalSeconds = 0.075;

        [[nodiscard]] std::vector<std::byte> TextBytes(const std::string_view text)
        {
            std::vector<std::byte> result(text.size());
            if (!text.empty())
                std::memcpy(result.data(), text.data(), text.size());
            return result;
        }

        [[nodiscard]] bool HasEquivalentRuntimeShaders(const Keire::MaterialGraphDefinition& previousDefinition,
                                                       const Keire::MaterialGraphCompilation& previous,
                                                       const Keire::MaterialGraphDefinition& definition,
                                                       const Keire::MaterialGraphCompilation& compilation)
        {
            if (previousDefinition.Output != definition.Output ||
                previous.Variants.size() != compilation.Variants.size() ||
                previous.Properties.size() != compilation.Properties.size())
            {
                return false;
            }
            for (std::size_t index = 0; index < compilation.Variants.size(); ++index)
            {
                const auto& left = previous.Variants[index];
                const auto& right = compilation.Variants[index];
                if (left.Keywords != right.Keywords || left.Hlsl != right.Hlsl)
                    return false;
            }
            for (std::size_t index = 0; index < compilation.Properties.size(); ++index)
            {
                const auto& left = previous.Properties[index];
                const auto& right = compilation.Properties[index];
                if (left.Name != right.Name || left.Type != right.Type || left.TextureSemantic != right.TextureSemantic)
                    return false;
            }
            return true;
        }

        [[nodiscard]] bool ApplyParameterDefault(const Keire::MaterialGraphNode& node,
                                                 Keire::ShaderPropertyDefinition& property)
        {
            switch (node.ValueType)
            {
            case Keire::MaterialGraphValueType::Scalar:
                if (const auto* value = std::get_if<float>(&node.Value))
                {
                    property.DefaultValue = {*value, 0.0F, 0.0F, 0.0F};
                    return true;
                }
                break;
            case Keire::MaterialGraphValueType::Vector2:
                if (const auto* value = std::get_if<Keire::Vector2>(&node.Value))
                {
                    property.DefaultValue = {value->X, value->Y, 0.0F, 0.0F};
                    return true;
                }
                break;
            case Keire::MaterialGraphValueType::Vector3:
                if (const auto* value = std::get_if<Keire::Vector3>(&node.Value))
                {
                    property.DefaultValue = {value->X, value->Y, value->Z, 0.0F};
                    return true;
                }
                break;
            case Keire::MaterialGraphValueType::Vector4:
                if (const auto* value = std::get_if<Keire::Vector4>(&node.Value))
                {
                    property.DefaultValue = *value;
                    return true;
                }
                break;
            case Keire::MaterialGraphValueType::Color:
                if (const auto* value = std::get_if<Keire::Color>(&node.Value))
                {
                    property.DefaultValue = {value->Red, value->Green, value->Blue, value->Alpha};
                    return true;
                }
                break;
            case Keire::MaterialGraphValueType::Texture2D:
                if (const auto* value = std::get_if<Keire::AssetId>(&node.Value))
                {
                    property.DefaultTexture = *value;
                    return true;
                }
                break;
            case Keire::MaterialGraphValueType::MaterialAttributes:
            case Keire::MaterialGraphValueType::Bsdf:
                break;
            }
            return false;
        }

        [[nodiscard]] bool BuildParameterOnlyCompilation(const Keire::MaterialGraphDefinition& previousDefinition,
                                                         const Keire::MaterialGraphCompilation& previousCompilation,
                                                         const Keire::MaterialGraphDefinition& definition,
                                                         Keire::MaterialGraphCompilation& compilation)
        {
            if (previousDefinition.Nodes.size() != definition.Nodes.size())
                return false;

            auto normalized = definition;
            std::vector<std::size_t> changedParameters;
            for (std::size_t index = 0; index < definition.Nodes.size(); ++index)
            {
                const auto& before = previousDefinition.Nodes[index];
                auto& after = normalized.Nodes[index];
                if (before.Id == after.Id && before.Kind == Keire::MaterialGraphNodeKind::Parameter &&
                    after.Kind == Keire::MaterialGraphNodeKind::Parameter && before.Value != after.Value)
                {
                    after.Value = before.Value;
                    changedParameters.push_back(index);
                }
            }
            if (changedParameters.empty() || normalized != previousDefinition)
                return false;

            compilation = previousCompilation;
            for (const auto index : changedParameters)
            {
                const auto& node = definition.Nodes[index];
                const auto property =
                    std::ranges::find(compilation.Properties, node.Symbol, &Keire::ShaderPropertyDefinition::Name);
                if (property == compilation.Properties.end() || !ApplyParameterDefault(node, *property))
                    return false;
            }
            return true;
        }

        [[nodiscard]] std::vector<Keire::Ref<Keire::ShaderAsset>>
        CompileDevelopmentShaders(const Keire::MaterialGraphCompilation& compilation,
                                  const Keire::MaterialGraphCompileOptions& options)
        {
            Keire::ShaderImporterSpecification importerSpecification;
#if defined(_WIN32)
            importerSpecification.Formats = {Keire::ShaderBinaryFormat::Dxil, Keire::ShaderBinaryFormat::SpirV};
#elif defined(__APPLE__)
            importerSpecification.Formats = {Keire::ShaderBinaryFormat::SpirV, Keire::ShaderBinaryFormat::Msl};
#else
            importerSpecification.Formats = {Keire::ShaderBinaryFormat::SpirV};
#endif
            const auto importer = Keire::CreateShaderAssetImporter(std::move(importerSpecification));
            if (!importer.ContextualImport)
                throw std::logic_error("Live Material Graph compilation requires the contextual shader importer.");

            std::vector<Keire::Ref<Keire::ShaderAsset>> result;
            result.reserve(compilation.Variants.size());
            for (const auto& variant : compilation.Variants)
            {
                Keire::AssetImportContext context;
                context.Asset = Keire::AssetId::Generate();
                context.ProjectRoot = std::filesystem::current_path();
                context.SourceRoot = context.ProjectRoot / "Assets";
                context.RelativePath = variant.GeneratedSource;
                context.RelativePath.replace_extension(".keireshader");
                context.SourcePath = context.ProjectRoot / context.RelativePath;
                context.MetadataPath = context.SourcePath;
                context.MetadataPath += ".keiremeta";
                context.ReadProjectFile = [generatedSource = variant.GeneratedSource,
                                           generatedBytes = TextBytes(variant.Hlsl),
                                           readInclude = options.ReadInclude](const std::filesystem::path& requested)
                {
                    if (requested.lexically_normal() == generatedSource.lexically_normal())
                        return generatedBytes;
                    if (readInclude)
                        if (const auto include = readInclude(requested))
                            return TextBytes(*include);
                    throw std::runtime_error("Live Material Graph shader dependency is unavailable: " +
                                             requested.generic_string());
                };
                const auto imported = importer.ContextualImport(context, TextBytes(variant.Manifest));
                result.push_back(Keire::ShaderAsset::Decode(imported.Bytes));
            }
            return result;
        }

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
            case Keire::MaterialGraphValueType::MaterialAttributes:
                return {0.96F, 0.74F, 0.22F, 1.0F};
            case Keire::MaterialGraphValueType::Bsdf:
                return {0.88F, 0.38F, 0.2F, 1.0F};
            }
            return {};
        }

        [[nodiscard]] StableNodeId CanvasPinType(const Keire::MaterialGraphValueType type) noexcept
        {
            // The shared canvas performs an equality pre-check before the material-specific validator. Material
            // graphs intentionally support scalar broadcasts and Color/Vector4 interchange, so all numeric pins
            // share one canvas family while CheckConnection remains authoritative for directional coercion rules.
            if (type == Keire::MaterialGraphValueType::Texture2D)
                return 2U;
            if (type == Keire::MaterialGraphValueType::MaterialAttributes)
                return 3U;
            if (type == Keire::MaterialGraphValueType::Bsdf)
                return 4U;
            return 1U;
        }

        [[nodiscard]] std::string_view NodeCategory(const Keire::MaterialGraphNodeKind kind) noexcept
        {
            const auto* descriptor = Keire::FindMaterialGraphNodeDescriptor(Keire::MaterialGraphNodeTypeId(kind));
            return descriptor ? descriptor->Category : std::string_view("Unknown");
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
            case Keire::MaterialGraphNodeKind::TextureSampleLevel:
            case Keire::MaterialGraphNodeKind::TriplanarSample:
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
            case Keire::MaterialGraphNodeKind::GradientNoise:
            case Keire::MaterialGraphNodeKind::VoronoiNoise:
                return {0.28F, 0.19F, 0.08F, 1.0F};
            case Keire::MaterialGraphNodeKind::MakeMaterialAttributes:
            case Keire::MaterialGraphNodeKind::BreakMaterialAttributes:
            case Keire::MaterialGraphNodeKind::BlendMaterialAttributes:
            case Keire::MaterialGraphNodeKind::BsdfToMaterialAttributes:
                return {0.33F, 0.22F, 0.08F, 1.0F};
            case Keire::MaterialGraphNodeKind::StandardSurfaceBsdf:
            case Keire::MaterialGraphNodeKind::ClearCoatBsdf:
            case Keire::MaterialGraphNodeKind::SheenBsdf:
            case Keire::MaterialGraphNodeKind::SubsurfaceBsdf:
            case Keire::MaterialGraphNodeKind::TransmissionBsdf:
                return {0.39F, 0.14F, 0.08F, 1.0F};
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
                  { QueueCompilation(definition); },
                  .CancelPreview = m_Specification.StopPreview,
                  .Persist = m_Specification.Persist})
    {
        if (!m_Specification.Persist)
            throw std::invalid_argument("Material Graph documents require a persistence callback.");
    }

    MaterialGraphDocument::~MaterialGraphDocument() noexcept
    {
        CancelBackgroundCompilation();
        if (m_JobScope)
        {
            m_JobScope->Cancel();
            m_JobScope->Wait();
        }
        if (m_OwnJobSystem && m_JobSystem)
            m_JobSystem->Close();
    }

    void MaterialGraphDocument::SetJobSystem(Keire::Ref<Keire::JobSystem> jobs)
    {
        if (m_BackgroundCompilation || m_JobScope)
            throw std::logic_error("Material Graph jobs are already configured.");
        if (!jobs)
            throw std::invalid_argument("Material Graph job system is unavailable.");
        m_JobSystem = std::move(jobs);
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
        RecompileCurrent();
    }

    void MaterialGraphDocument::Create(const Keire::AssetId asset, Keire::MaterialGraphDefinition definition,
                                       Keire::Ref<Keire::UndoContext> undo)
    {
        m_LastGoodCompilation.reset();
        m_LastGoodDefinition.reset();
        m_Host.Create(asset, std::move(definition), std::move(undo));
        RecompileCurrent();
    }

    void MaterialGraphDocument::Save()
    {
        RecompileCurrent(true);
        if (!Publishable())
            throw std::logic_error("Material Graph cannot be saved until its generated-shader diagnostics are clear.");
        m_Host.Save();
    }

    void MaterialGraphDocument::Discard() { m_Host.Discard(); }

    void MaterialGraphDocument::Close() noexcept
    {
        ++m_RequestedGeneration;
        m_PendingDefinition.reset();
        CancelBackgroundCompilation();
        m_Host.Close();
        m_Compilation = {};
        m_LastGoodCompilation.reset();
        m_LastGoodDefinition.reset();
        m_Diagnostic.clear();
        m_CompileDebounceSeconds = 0.0;
        m_InFlightGeneration = 0;
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

    bool MaterialGraphDocument::MoveNode(const Keire::AssetId node, const Keire::Vector2 position)
    {
        auto candidate = m_Host.Draft();
        const auto found = std::ranges::find(candidate.Nodes, node, &Keire::MaterialGraphNode::Id);
        if (found == candidate.Nodes.end())
            throw std::invalid_argument("Material Graph node is unavailable.");
        found->EditorPosition = position;
        return m_Host.EditMetadata("Move Material Graph node", std::move(candidate));
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
                    [connection](auto& definition)
                    {
                        std::erase_if(definition.Connections,
                                      [&](const auto& existing) { return existing.Input == connection.Input; });
                        definition.Connections.push_back(connection);
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
                                     CanvasPinType(sourcePin.Type), PinColor(sourcePin.Type)});
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

    bool MaterialGraphDocument::CompilationPending() const noexcept
    {
        return m_PendingDefinition.has_value() || static_cast<bool>(m_BackgroundCompilation);
    }

    void MaterialGraphDocument::AdvanceCompilation(const double deltaSeconds)
    {
        if (!std::isfinite(deltaSeconds) || deltaSeconds < 0.0 || deltaSeconds > 60.0)
            throw std::invalid_argument("Material Graph compilation delta must be finite and in the range 0..60.");
        ConsumeBackgroundCompilation(false);
        m_CompileDebounceSeconds = std::max(0.0, m_CompileDebounceSeconds - deltaSeconds);
        if (m_PendingDefinition && m_CompileDebounceSeconds <= 0.0 && !m_BackgroundCompilation)
            StartPendingCompilation();
        ConsumeBackgroundCompilation(false);
    }

    void MaterialGraphDocument::QueueCompilation(const Keire::MaterialGraphDefinition& definition)
    {
        if (!m_PendingDefinition && !m_BackgroundCompilation && m_LastGoodDefinition && m_LastGoodCompilation)
        {
            Keire::MaterialGraphCompilation compilation;
            if (BuildParameterOnlyCompilation(*m_LastGoodDefinition, *m_LastGoodCompilation, definition, compilation))
            {
                if (++m_RequestedGeneration == 0)
                    ++m_RequestedGeneration;
                m_CompileDebounceSeconds = 0.0;
                ApplyCompilation(definition, std::move(compilation));
                return;
            }
        }
        const bool compilationAlreadyQueued = m_PendingDefinition.has_value();
        m_PendingDefinition = definition;
        if (!compilationAlreadyQueued)
            m_CompileDebounceSeconds = LiveCompilationIntervalSeconds;
        if (++m_RequestedGeneration == 0)
            ++m_RequestedGeneration;
    }

    void MaterialGraphDocument::StartPendingCompilation()
    {
        if (!m_PendingDefinition || m_BackgroundCompilation)
            return;
        EnsureJobScope();
        auto definition = std::move(*m_PendingDefinition);
        m_PendingDefinition.reset();
        auto options = m_Specification.CompileOptions;
        auto previousCompilation = m_LastGoodCompilation;
        auto previousDefinition = m_LastGoodDefinition;
        m_InFlightGeneration = m_RequestedGeneration;
        const auto generation = m_InFlightGeneration;
        m_BackgroundCompilationState = std::make_shared<BackgroundCompilationState>();
        const auto state = m_BackgroundCompilationState;
        m_BackgroundCompilation = m_JobScope->Submit(
            {.Name = "Compile Material Graph",
             .Priority = Keire::JobPriority::High,
             .Class = Keire::JobClass::Compute,
             .Domain = Keire::JobDomain::Tooling},
            [generation, definition = std::move(definition), options = std::move(options),
             previousCompilation = std::move(previousCompilation), previousDefinition = std::move(previousDefinition),
             state](Keire::JobContext& context) mutable
            {
                if (context.StopRequested())
                    return;
                auto compilation = Keire::CompileMaterialGraph(definition, options);
                std::vector<Keire::Ref<Keire::ShaderAsset>> developmentShaders;
                if (compilation.Succeeded() &&
                    (!previousCompilation || !previousDefinition ||
                     !HasEquivalentRuntimeShaders(*previousDefinition, *previousCompilation, definition, compilation)))
                {
                    try
                    {
                        developmentShaders = CompileDevelopmentShaders(compilation, options);
                    }
                    catch (const std::exception& error)
                    {
                        compilation.Diagnostics.push_back(
                            {Keire::MaterialGraphDiagnosticSeverity::Error, "MG2001",
                             std::string("Live shader compilation failed: ") + error.what()});
                    }
                }
                if (context.StopRequested())
                    return;
                std::scoped_lock lock(state->Mutex);
                state->Result.emplace(BackgroundCompilation{generation, std::move(definition), std::move(compilation),
                                                            std::move(developmentShaders)});
            });
    }

    void MaterialGraphDocument::ConsumeBackgroundCompilation(const bool wait)
    {
        if (!m_BackgroundCompilation)
            return;
        if (!wait && !m_BackgroundCompilation.IsComplete())
            return;
        (void)m_BackgroundCompilation.Wait();
        m_BackgroundCompilation.RethrowIfFailed();
        std::optional<BackgroundCompilation> result;
        {
            std::scoped_lock lock(m_BackgroundCompilationState->Mutex);
            result = std::move(m_BackgroundCompilationState->Result);
        }
        m_BackgroundCompilation = {};
        m_BackgroundCompilationState.reset();
        m_InFlightGeneration = 0;
        if (result && result->Generation == m_RequestedGeneration && !m_PendingDefinition && IsOpen())
            ApplyCompilation(std::move(result->Definition), std::move(result->Compilation),
                             std::move(result->DevelopmentShaders));
    }

    void MaterialGraphDocument::EnsureJobScope()
    {
        if (m_JobScope)
            return;
        if (!m_JobSystem)
        {
            Keire::JobSystemSpecification specification;
            specification.WorkerCount = 1;
            specification.BlockingWorkerCount = 1;
            m_JobSystem = Keire::CreateRef<Keire::JobSystem>(specification);
            m_OwnJobSystem = true;
        }
        m_JobScope = m_JobSystem->CreateScope("Material Graph compilation");
    }

    void MaterialGraphDocument::CancelBackgroundCompilation() noexcept
    {
        if (m_BackgroundCompilation)
        {
            m_BackgroundCompilation.Cancel();
            (void)m_BackgroundCompilation.Wait();
            m_BackgroundCompilation = {};
        }
        m_BackgroundCompilationState.reset();
        m_InFlightGeneration = 0;
    }

    void MaterialGraphDocument::ApplyCompilation(Keire::MaterialGraphDefinition definition,
                                                 Keire::MaterialGraphCompilation compilation,
                                                 std::vector<Keire::Ref<Keire::ShaderAsset>> developmentShaders)
    {
        m_Compilation = std::move(compilation);
        m_Diagnostic.clear();
        if (!m_Compilation.Succeeded())
        {
            if (!m_Compilation.Diagnostics.empty())
            {
                const auto error =
                    std::ranges::find(m_Compilation.Diagnostics, Keire::MaterialGraphDiagnosticSeverity::Error,
                                      &Keire::MaterialGraphDiagnostic::Severity);
                m_Diagnostic =
                    (error == m_Compilation.Diagnostics.end() ? m_Compilation.Diagnostics.front() : *error).Message;
            }
            return;
        }
        m_LastGoodCompilation = m_Compilation;
        m_LastGoodDefinition = std::move(definition);
        if (m_Specification.LiveApply)
            m_Specification.LiveApply(m_Host.Asset(), *m_LastGoodDefinition, m_Compilation, developmentShaders);
        if (m_Specification.Preview)
            m_Specification.Preview(m_Host.Asset(), m_Compilation, m_PreviewSettings);
    }

    void MaterialGraphDocument::CompileAndPreview(const Keire::MaterialGraphDefinition& definition,
                                                  const bool compileDevelopmentShaders)
    {
        auto compilation = Keire::CompileMaterialGraph(definition, m_Specification.CompileOptions);
        std::vector<Keire::Ref<Keire::ShaderAsset>> developmentShaders;
        if (compileDevelopmentShaders && compilation.Succeeded() &&
            (!m_LastGoodCompilation || !m_LastGoodDefinition ||
             !HasEquivalentRuntimeShaders(*m_LastGoodDefinition, *m_LastGoodCompilation, definition, compilation)))
        {
            try
            {
                developmentShaders = CompileDevelopmentShaders(compilation, m_Specification.CompileOptions);
            }
            catch (const std::exception& error)
            {
                compilation.Diagnostics.push_back({Keire::MaterialGraphDiagnosticSeverity::Error, "MG2001",
                                                   std::string("Live shader compilation failed: ") + error.what()});
            }
        }
        ApplyCompilation(definition, std::move(compilation), std::move(developmentShaders));
    }

    void MaterialGraphDocument::RecompileCurrent(const bool compileDevelopmentShaders)
    {
        if (m_Host.IsOpen())
        {
            if (++m_RequestedGeneration == 0)
                ++m_RequestedGeneration;
            m_PendingDefinition.reset();
            ConsumeBackgroundCompilation(true);
            CompileAndPreview(m_Host.Draft(), compileDevelopmentShaders);
        }
    }
} // namespace KeireEditor
