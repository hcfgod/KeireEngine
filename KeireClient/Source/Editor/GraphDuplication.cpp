#include "KeireClient/Editor/GraphDuplication.h"

#include <algorithm>
#include <cctype>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <unordered_map>

namespace KeireEditor
{
    namespace
    {
        using IdentityMap = std::map<Keire::AssetId, Keire::AssetId>;

        [[nodiscard]] Keire::AssetId Remapped(const IdentityMap& identities, const Keire::AssetId source)
        {
            const auto found = identities.find(source);
            if (found == identities.end())
                throw std::logic_error("Duplicated graph fragment is missing an identity mapping.");
            return found->second;
        }

        void Offset(Keire::Vector2& position, const Keire::Vector2 offset) noexcept
        {
            position.X += offset.X;
            position.Y += offset.Y;
        }

        void DuplicateAuthoring(Keire::GraphAuthoringMetadata& metadata, IdentityMap& identities,
                                const Keire::Vector2 offset)
        {
            const auto sourceAnnotations = metadata.NodeAnnotations;
            for (const auto& annotation : sourceAnnotations)
            {
                if (const auto node = identities.find(annotation.Node); node != identities.end())
                {
                    auto duplicate = annotation;
                    duplicate.Node = node->second;
                    metadata.NodeAnnotations.push_back(std::move(duplicate));
                }
            }

            const auto sourceComments = metadata.Comments;
            std::set<Keire::AssetId> includedComments;
            bool changed = true;
            while (changed)
            {
                changed = false;
                for (const auto& comment : sourceComments)
                {
                    if (comment.Members.empty() || includedComments.contains(comment.Id))
                        continue;
                    const bool contained = std::ranges::all_of(
                        comment.Members, [&](const Keire::AssetId member)
                        { return identities.contains(member) || includedComments.contains(member); });
                    if (contained)
                        changed = includedComments.insert(comment.Id).second || changed;
                }
            }

            for (const auto& comment : sourceComments)
                if (includedComments.contains(comment.Id))
                    identities.emplace(comment.Id, Keire::AssetId::Generate());
            for (const auto& comment : sourceComments)
            {
                if (!includedComments.contains(comment.Id))
                    continue;
                auto duplicate = comment;
                duplicate.Id = Remapped(identities, comment.Id);
                if (identities.contains(comment.Parent))
                    duplicate.Parent = Remapped(identities, comment.Parent);
                Offset(duplicate.Position, offset);
                for (auto& member : duplicate.Members)
                    member = Remapped(identities, member);
                metadata.Comments.push_back(std::move(duplicate));
            }
        }

        [[nodiscard]] bool Selected(const std::set<Keire::AssetId>& selection, const Keire::AssetId node) noexcept
        {
            return selection.contains(node);
        }

        void RemapShaderNode(Keire::ShaderGraphNode& node, IdentityMap& identities, const Keire::Vector2 offset)
        {
            const auto sourceNode = node.Id;
            node.Id = Keire::AssetId::Generate();
            identities.emplace(sourceNode, node.Id);
            Offset(node.EditorPosition, offset);
            for (auto& pin : node.Pins)
            {
                const auto sourcePin = pin.Id;
                pin.Id = Keire::AssetId::Generate();
                identities.emplace(sourcePin, pin.Id);
            }
        }

        void DuplicateShaderConnections(Keire::ShaderGraphDefinition& definition, const IdentityMap& identities)
        {
            const auto sourceConnections = definition.Connections;
            for (const auto& connection : sourceConnections)
            {
                if (!identities.contains(connection.Output.Node) || !identities.contains(connection.Input.Node))
                    continue;
                auto duplicate = connection;
                duplicate.Id = Keire::AssetId::Generate();
                duplicate.Output.Node = Remapped(identities, connection.Output.Node);
                duplicate.Output.Pin = Remapped(identities, connection.Output.Pin);
                duplicate.Input.Node = Remapped(identities, connection.Input.Node);
                duplicate.Input.Pin = Remapped(identities, connection.Input.Pin);
                definition.Connections.push_back(std::move(duplicate));
            }
        }

        [[nodiscard]] const Keire::ShaderGraphNode& RequireShaderNode(const Keire::ShaderGraphDefinition& definition,
                                                                      const Keire::AssetId id)
        {
            const auto found = std::ranges::find(definition.Nodes, id, &Keire::ShaderGraphNode::Id);
            if (found == definition.Nodes.end())
                throw std::invalid_argument("Graph extraction selection contains an unavailable node.");
            return *found;
        }

        [[nodiscard]] const Keire::ShaderGraphPin& RequireShaderPin(const Keire::ShaderGraphDefinition& definition,
                                                                    const Keire::ShaderGraphEndpoint endpoint)
        {
            const auto& node = RequireShaderNode(definition, endpoint.Node);
            const auto found = std::ranges::find(node.Pins, endpoint.Pin, &Keire::ShaderGraphPin::Id);
            if (found == node.Pins.end())
                throw std::logic_error("Graph extraction connection contains an unavailable pin.");
            return *found;
        }

        [[nodiscard]] std::string UniqueBoundaryName(const std::string_view source, const std::string_view fallback,
                                                     std::set<std::string, std::less<>>& used)
        {
            std::string base;
            base.reserve(source.size());
            for (const unsigned char character : source)
            {
                if (std::isalnum(character) || character == '_')
                    base.push_back(static_cast<char>(character));
            }
            if (base.empty() || std::isdigit(static_cast<unsigned char>(base.front())))
                base = std::string(fallback) + base;
            auto result = base;
            for (std::size_t suffix = 2; !used.insert(result).second; ++suffix)
                result = base + std::to_string(suffix);
            return result;
        }

        void RemoveExtractedAuthoring(Keire::GraphAuthoringMetadata& metadata, const std::set<Keire::AssetId>& selected,
                                      const Keire::AssetId call, const Keire::Vector2 callPosition)
        {
            std::erase_if(metadata.NodeAnnotations, [&](const Keire::GraphNodeAnnotation& annotation)
                          { return selected.contains(annotation.Node); });
            for (auto& comment : metadata.Comments)
                std::erase_if(comment.Members, [&](const Keire::AssetId member) { return selected.contains(member); });
            Keire::UpdateGraphCommentMembership(metadata, call, callPosition, {220.0F, 100.0F});
        }

        [[nodiscard]] ShaderGraphFunctionExtraction
        ExtractShaderGraphSelectionImpl(const Keire::ShaderGraphDefinition& definition,
                                        const std::span<const Keire::AssetId> selection,
                                        const Keire::AssetId functionAsset, const std::string_view functionName,
                                        const Keire::ShaderGraphPurpose purpose)
        {
            if (!functionAsset)
                throw std::invalid_argument("Graph extraction requires a reusable asset identity.");
            const std::set<Keire::AssetId> selected(selection.begin(), selection.end());
            if (selected.empty())
                throw std::invalid_argument("Select at least one compatible expression node to extract.");

            Keire::Vector2 minimum{std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
            Keire::Vector2 maximum{std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest()};
            for (const auto id : selected)
            {
                const auto& node = RequireShaderNode(definition, id);
                if (node.Kind == Keire::ShaderGraphNodeKind::Master ||
                    node.Kind == Keire::ShaderGraphNodeKind::Parameter)
                    throw std::invalid_argument("Output and exposed Parameter nodes cannot be extracted.");
                minimum.X = std::min(minimum.X, node.EditorPosition.X);
                minimum.Y = std::min(minimum.Y, node.EditorPosition.Y);
                maximum.X = std::max(maximum.X, node.EditorPosition.X);
                maximum.Y = std::max(maximum.Y, node.EditorPosition.Y);
            }

            std::vector<const Keire::ShaderGraphConnection*> incoming;
            std::vector<Keire::ShaderGraphEndpoint> outputs;
            for (const auto& connection : definition.Connections)
            {
                const bool sourceSelected = selected.contains(connection.Output.Node);
                const bool targetSelected = selected.contains(connection.Input.Node);
                if (!sourceSelected && targetSelected)
                    incoming.push_back(std::addressof(connection));
                else if (sourceSelected && !targetSelected)
                {
                    if (std::ranges::find(outputs, connection.Output) == outputs.end())
                        outputs.push_back(connection.Output);
                }
            }
            if (outputs.empty())
                throw std::invalid_argument("The selection must drive at least one node outside the selection.");

            ShaderGraphFunctionExtraction result;
            result.Parent = definition;
            result.Function.Description = "Extracted from an editable graph selection.";
            result.Function.Category = "Extracted";
            auto& body = result.Function.Body;
            body.SchemaVersion = Keire::ShaderGraphSourceSchemaVersion;
            body.Purpose = purpose;
            body.Output = definition.Output;
            body.Keywords = definition.Keywords;
            body.IncludeRoots = definition.IncludeRoots;
            body.Resources = definition.Resources;

            Keire::ShaderGraphNode master;
            master.Id = Keire::AssetId::Generate();
            master.Kind = Keire::ShaderGraphNodeKind::Master;
            master.TypeId = std::string(Keire::ShaderGraphNodeTypeId(master.Kind));
            master.Name = purpose == Keire::ShaderGraphPurpose::ShaderFunction ? "Shader Function Output"
                                                                               : "Material Function Output";
            master.EditorPosition = {maximum.X - minimum.X + 520.0F, 80.0F};
            body.Nodes.push_back(master);

            std::set<std::string, std::less<>> inputNames;
            for (std::size_t index = 0; index < incoming.size(); ++index)
            {
                const auto& targetPin = RequireShaderPin(definition, incoming[index]->Input);
                auto parameter = Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::Parameter, targetPin.Type);
                parameter.Name = UniqueBoundaryName(targetPin.Name, "Input", inputNames);
                parameter.Symbol = parameter.Name;
                parameter.Value = targetPin.DefaultValue;
                parameter.Pins.front().DefaultValue = targetPin.DefaultValue;
                parameter.ParameterMetadata.Category = "Inputs";
                parameter.EditorPosition = {0.0F, 80.0F + static_cast<float>(index) * 120.0F};
                const auto outputPin = parameter.Pins.front().Id;
                const auto parameterId = parameter.Id;
                body.Nodes.push_back(std::move(parameter));
                body.Connections.push_back(
                    {Keire::AssetId::Generate(), {parameterId, outputPin}, incoming[index]->Input, {}});
            }

            for (const auto& node : definition.Nodes)
            {
                if (!selected.contains(node.Id))
                    continue;
                auto extracted = node;
                extracted.EditorPosition.X += 240.0F - minimum.X;
                extracted.EditorPosition.Y += 80.0F - minimum.Y;
                body.Nodes.push_back(std::move(extracted));
            }
            for (const auto& connection : definition.Connections)
                if (selected.contains(connection.Output.Node) && selected.contains(connection.Input.Node))
                    body.Connections.push_back(connection);
            for (const auto& annotation : definition.Authoring.NodeAnnotations)
                if (selected.contains(annotation.Node))
                    body.Authoring.NodeAnnotations.push_back(annotation);

            std::set<std::string, std::less<>> outputNames;
            auto& outputNode = body.Nodes.front();
            for (const auto endpoint : outputs)
            {
                const auto& sourcePin = RequireShaderPin(definition, endpoint);
                auto name = UniqueBoundaryName(sourcePin.Name, "Result", outputNames);
                const auto outputPin = Keire::AssetId::Generate();
                outputNode.Pins.push_back({outputPin, std::move(name), sourcePin.Type,
                                           Keire::ShaderGraphPinDirection::Input, sourcePin.DefaultValue});
                body.Connections.push_back({Keire::AssetId::Generate(), endpoint, {outputNode.Id, outputPin}, {}});
            }
            Keire::ValidateGraphFunction(result.Function, purpose);

            auto call = Keire::CreateShaderGraphFunctionCallNode(functionAsset, body);
            call.Name = functionName.empty() ? "Extracted Function" : std::string(functionName);
            call.EditorPosition = minimum;
            result.CallNode = call.Id;
            std::vector<const Keire::ShaderGraphPin*> callInputs;
            std::vector<const Keire::ShaderGraphPin*> callOutputs;
            for (const auto& pin : call.Pins)
                (pin.Direction == Keire::ShaderGraphPinDirection::Input ? callInputs : callOutputs)
                    .push_back(std::addressof(pin));
            if (callInputs.size() != incoming.size() || callOutputs.size() != outputs.size())
                throw std::logic_error("Extracted function boundary does not match its call node.");

            for (auto& connection : result.Parent.Connections)
            {
                const bool sourceSelected = selected.contains(connection.Output.Node);
                const bool targetSelected = selected.contains(connection.Input.Node);
                if (!sourceSelected && targetSelected)
                {
                    const auto found =
                        std::ranges::find(incoming, connection.Id, [](const auto* candidate) { return candidate->Id; });
                    connection.Input = {call.Id, callInputs[static_cast<std::size_t>(found - incoming.begin())]->Id};
                }
                else if (sourceSelected && !targetSelected)
                {
                    const auto output = std::ranges::find(outputs, connection.Output);
                    connection.Output = {call.Id, callOutputs[static_cast<std::size_t>(output - outputs.begin())]->Id};
                }
            }
            std::erase_if(
                result.Parent.Connections, [&](const Keire::ShaderGraphConnection& connection)
                { return selected.contains(connection.Output.Node) && selected.contains(connection.Input.Node); });
            std::erase_if(result.Parent.Nodes,
                          [&](const Keire::ShaderGraphNode& node) { return selected.contains(node.Id); });
            result.Parent.Nodes.push_back(std::move(call));
            RemoveExtractedAuthoring(result.Parent.Authoring, selected, result.CallNode, minimum);
            Keire::ValidateShaderGraph(result.Parent);
            return result;
        }
    } // namespace

    std::vector<Keire::AssetId> DuplicateShaderGraphSelection(Keire::ShaderGraphDefinition& definition,
                                                              const std::span<const Keire::AssetId> selection,
                                                              const Keire::Vector2 offset)
    {
        const std::set selected(selection.begin(), selection.end());
        IdentityMap identities;
        std::vector<Keire::AssetId> result;
        const auto sourceNodes = definition.Nodes;
        for (auto node : sourceNodes)
        {
            if (!Selected(selected, node.Id) || node.Kind == Keire::ShaderGraphNodeKind::Master)
                continue;
            RemapShaderNode(node, identities, offset);
            result.push_back(node.Id);
            definition.Nodes.push_back(std::move(node));
        }
        DuplicateShaderConnections(definition, identities);
        DuplicateAuthoring(definition.Authoring, identities, offset);
        return result;
    }

    std::vector<Keire::AssetId> DuplicateMaterialGraphSelection(Keire::MaterialGraphDefinition& definition,
                                                                const std::span<const Keire::AssetId> selection,
                                                                const Keire::Vector2 offset)
    {
        const std::set selected(selection.begin(), selection.end());
        IdentityMap identities;
        std::vector<Keire::AssetId> result;
        const auto sourceNodes = definition.Nodes;
        for (auto node : sourceNodes)
        {
            if (!Selected(selected, node.Id))
                continue;
            const auto sourceNode = node.Id;
            const auto sourcePin = node.OutputPin;
            node.Id = Keire::AssetId::Generate();
            node.OutputPin = Keire::AssetId::Generate();
            identities.emplace(sourceNode, node.Id);
            identities.emplace(sourcePin, node.OutputPin);
            Offset(node.EditorPosition, offset);
            result.push_back(node.Id);
            definition.Nodes.push_back(std::move(node));
        }

        const auto sourceExpressions = definition.SurfaceGraph.Nodes;
        for (auto node : sourceExpressions)
        {
            if (!Selected(selected, node.Id) || node.Kind == Keire::ShaderGraphNodeKind::Master)
                continue;
            RemapShaderNode(node, identities, offset);
            result.push_back(node.Id);
            definition.SurfaceGraph.Nodes.push_back(std::move(node));
        }

        const auto sourceConnections = definition.Connections;
        for (const auto& connection : sourceConnections)
        {
            if (!identities.contains(connection.Output.Node) || !identities.contains(connection.Input.Node))
                continue;
            auto duplicate = connection;
            duplicate.Id = Keire::AssetId::Generate();
            duplicate.Output = {Remapped(identities, connection.Output.Node),
                                Remapped(identities, connection.Output.Pin)};
            duplicate.Input = {Remapped(identities, connection.Input.Node), Remapped(identities, connection.Input.Pin)};
            definition.Connections.push_back(std::move(duplicate));
        }
        DuplicateShaderConnections(definition.SurfaceGraph, identities);
        DuplicateAuthoring(definition.Authoring, identities, offset);
        DuplicateAuthoring(definition.SurfaceGraph.Authoring, identities, offset);
        return result;
    }

    std::vector<Keire::AssetId> DuplicateVfxGraphSelection(Keire::VfxEffectDefinition& definition,
                                                           const Keire::AssetId system,
                                                           const std::span<const Keire::AssetId> selection,
                                                           const Keire::Vector2 offset)
    {
        auto foundSystem = std::ranges::find(definition.Systems, system, &Keire::VfxGraphSystem::Id);
        if (foundSystem == definition.Systems.end())
            throw std::invalid_argument("VFX graph system does not exist.");
        const std::set selected(selection.begin(), selection.end());
        IdentityMap identities;
        std::vector<Keire::AssetId> result;
        const auto sourceNodes = foundSystem->Nodes;
        for (auto node : sourceNodes)
        {
            if (!Selected(selected, node.Id) || node.Kind == Keire::VfxGraphNodeKind::Context)
                continue;
            const auto sourceNode = node.Id;
            node.Id = Keire::AssetId::Generate();
            identities.emplace(sourceNode, node.Id);
            Offset(node.EditorPosition, offset);
            for (auto& pin : node.Pins)
            {
                const auto sourcePin = pin.Id;
                pin.Id = Keire::AssetId::Generate();
                identities.emplace(sourcePin, pin.Id);
            }
            for (auto& pin : node.DynamicPinOrder)
                pin = Remapped(identities, pin);
            for (auto& block : node.Blocks)
            {
                const auto sourceBlock = block.Id;
                block.Id = Keire::AssetId::Generate();
                identities.emplace(sourceBlock, block.Id);
                for (auto& pin : block.Pins)
                {
                    const auto sourcePin = pin.Id;
                    pin.Id = Keire::AssetId::Generate();
                    identities.emplace(sourcePin, pin.Id);
                }
            }
            result.push_back(node.Id);
            foundSystem->Nodes.push_back(std::move(node));
        }

        const auto sourceConnections = foundSystem->Connections;
        for (const auto& connection : sourceConnections)
        {
            if (!identities.contains(connection.OutputNode) || !identities.contains(connection.InputNode))
                continue;
            auto duplicate = connection;
            duplicate.Id = Keire::AssetId::Generate();
            duplicate.OutputNode = Remapped(identities, connection.OutputNode);
            duplicate.OutputPin = Remapped(identities, connection.OutputPin);
            duplicate.InputNode = Remapped(identities, connection.InputNode);
            duplicate.InputPin = Remapped(identities, connection.InputPin);
            if (connection.OutputBlock)
                duplicate.OutputBlock = Remapped(identities, connection.OutputBlock);
            if (connection.InputBlock)
                duplicate.InputBlock = Remapped(identities, connection.InputBlock);
            foundSystem->Connections.push_back(std::move(duplicate));
        }
        DuplicateAuthoring(foundSystem->Authoring, identities, offset);
        return result;
    }

    ShaderGraphFunctionExtraction ExtractShaderGraphSelection(const Keire::ShaderGraphDefinition& definition,
                                                              const std::span<const Keire::AssetId> selection,
                                                              const Keire::AssetId functionAsset,
                                                              const std::string_view functionName)
    {
        return ExtractShaderGraphSelectionImpl(definition, selection, functionAsset, functionName,
                                               Keire::ShaderGraphPurpose::ShaderFunction);
    }

    MaterialGraphFunctionExtraction ExtractMaterialGraphSelection(const Keire::MaterialGraphDefinition& definition,
                                                                  const std::span<const Keire::AssetId> selection,
                                                                  const Keire::AssetId functionAsset,
                                                                  const std::string_view functionName)
    {
        const std::set<Keire::AssetId> selected(selection.begin(), selection.end());
        if (selected.empty() ||
            std::ranges::any_of(definition.Nodes, [&](const Keire::MaterialGraphValueNode& node)
                                { return selected.contains(node.Id); }) ||
            selected.contains(definition.OutputNode))
            throw std::invalid_argument("Only Material expression nodes can be extracted to a reusable function.");
        for (const auto id : selected)
            if (std::ranges::find(definition.SurfaceGraph.Nodes, id, &Keire::ShaderGraphNode::Id) ==
                definition.SurfaceGraph.Nodes.end())
                throw std::invalid_argument("Material function extraction contains an unavailable expression node.");

        const auto extracted =
            ExtractShaderGraphSelectionImpl(definition.SurfaceGraph, selection, functionAsset, functionName,
                                            Keire::ShaderGraphPurpose::MaterialFunction);
        MaterialGraphFunctionExtraction result;
        result.Function = extracted.Function;
        result.Parent = definition;
        result.Parent.SurfaceGraph = extracted.Parent;
        result.CallNode = extracted.CallNode;
        const auto call =
            std::ranges::find(result.Parent.SurfaceGraph.Nodes, result.CallNode, &Keire::ShaderGraphNode::Id);
        RemoveExtractedAuthoring(result.Parent.Authoring, selected, result.CallNode, call->EditorPosition);
        Keire::ValidateMaterialGraph(result.Parent);
        return result;
    }
} // namespace KeireEditor
