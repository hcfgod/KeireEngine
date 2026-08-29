#include "Keire/Rendering/ShaderGraph.h"

#include "KeireInternal/Authoring/GraphAuthoringSerialization.h"
#include "KeireInternal/Rendering/ShaderGraphCompilerInternal.h"
#include "KeireInternal/Rendering/ShaderGraphIdentity.h"

#include <algorithm>
#include <cmath>
#include <deque>
#include <iterator>
#include <map>
#include <ranges>
#include <set>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace Keire
{
    namespace
    {
        constexpr std::size_t MaximumGraphNodes = 1024;
        constexpr std::size_t MaximumGraphConnections = 4096;
        constexpr std::size_t MaximumGraphRoutingPointsPerConnection = 64;
        constexpr std::size_t MaximumGraphKeywords = 16;
        constexpr std::size_t MaximumGraphProperties = 80;
        constexpr std::size_t MaximumGraphPinsPerNode = 32;
        constexpr std::size_t MaximumGraphIncludeRoots = 16;
        constexpr std::size_t MaximumGraphText = 128;
        constexpr std::size_t MaximumGraphPath = 1024;
        [[nodiscard]] bool ValidIdentifier(const std::string_view value)
        {
            return Detail::IsValidShaderGraphIdentifier(value);
        }

        [[nodiscard]] bool SafeRelativePath(const std::filesystem::path& value)
        {
            return Detail::IsSafeShaderGraphRelativePath(value);
        }

        [[nodiscard]] ShaderGraphValue CoerceDefaultValue(const ShaderGraphValue& value,
                                                          const ShaderGraphValueType source,
                                                          const ShaderGraphValueType target)
        {
            if (source == target)
                return value;
            if (source == ShaderGraphValueType::Scalar)
            {
                const auto scalar = std::get<float>(value);
                if (target == ShaderGraphValueType::Vector2)
                    return Vector2{scalar, scalar};
                if (target == ShaderGraphValueType::Vector3)
                    return Vector3{scalar, scalar, scalar};
                if (target == ShaderGraphValueType::Vector4)
                    return Vector4{scalar, scalar, scalar, scalar};
                if (target == ShaderGraphValueType::Color)
                    return Color{scalar, scalar, scalar, scalar};
            }
            if (source == ShaderGraphValueType::Vector3 && target == ShaderGraphValueType::Vector4)
            {
                const auto vector = std::get<Vector3>(value);
                return Vector4{vector.X, vector.Y, vector.Z, 1.0F};
            }
            if (source == ShaderGraphValueType::Vector3 && target == ShaderGraphValueType::Color)
            {
                const auto vector = std::get<Vector3>(value);
                return Color{vector.X, vector.Y, vector.Z, 1.0F};
            }
            if (source == ShaderGraphValueType::Vector4 && target == ShaderGraphValueType::Vector3)
            {
                const auto vector = std::get<Vector4>(value);
                return Vector3{vector.X, vector.Y, vector.Z};
            }
            if (source == ShaderGraphValueType::Color && target == ShaderGraphValueType::Vector3)
            {
                const auto color = std::get<Color>(value);
                return Vector3{color.Red, color.Green, color.Blue};
            }
            if (source == ShaderGraphValueType::Vector4 && target == ShaderGraphValueType::Color)
            {
                const auto vector = std::get<Vector4>(value);
                return Color{vector.X, vector.Y, vector.Z, vector.W};
            }
            if (source == ShaderGraphValueType::Color && target == ShaderGraphValueType::Vector4)
            {
                const auto color = std::get<Color>(value);
                return Vector4{color.Red, color.Green, color.Blue, color.Alpha};
            }
            throw std::invalid_argument("A reusable graph default cannot be converted to the destination pin type.");
        }

        [[nodiscard]] const ShaderGraphPin* FindPin(const ShaderGraphNode& node, const std::string_view name,
                                                    const ShaderGraphPinDirection direction)
        {
            const auto found = std::ranges::find_if(node.Pins, [name, direction](const ShaderGraphPin& pin)
                                                    { return pin.Name == name && pin.Direction == direction; });
            return found == node.Pins.end() ? nullptr : &*found;
        }

        [[nodiscard]] bool Compatible(const ShaderGraphValueType output, const ShaderGraphValueType input)
        {
            return output == input ||
                   ((output == ShaderGraphValueType::Color && input == ShaderGraphValueType::Vector4) ||
                    (output == ShaderGraphValueType::Vector4 && input == ShaderGraphValueType::Color) ||
                    ((output == ShaderGraphValueType::Vector4 || output == ShaderGraphValueType::Color) &&
                     input == ShaderGraphValueType::Vector3) ||
                    (output == ShaderGraphValueType::Vector3 &&
                     (input == ShaderGraphValueType::Vector4 || input == ShaderGraphValueType::Color))) ||
                   (output == ShaderGraphValueType::Scalar && input != ShaderGraphValueType::Texture2D &&
                    input != ShaderGraphValueType::MaterialAttributes && input != ShaderGraphValueType::Bsdf);
        }

        [[nodiscard]] bool NumericNode(const ShaderGraphNodeKind kind) noexcept
        {
            switch (kind)
            {
            case ShaderGraphNodeKind::Add:
            case ShaderGraphNodeKind::Multiply:
            case ShaderGraphNodeKind::Lerp:
            case ShaderGraphNodeKind::OneMinus:
            case ShaderGraphNodeKind::Clamp:
            case ShaderGraphNodeKind::StaticSwitch:
            case ShaderGraphNodeKind::Subtract:
            case ShaderGraphNodeKind::Divide:
            case ShaderGraphNodeKind::Power:
            case ShaderGraphNodeKind::Minimum:
            case ShaderGraphNodeKind::Maximum:
            case ShaderGraphNodeKind::Absolute:
            case ShaderGraphNodeKind::Floor:
            case ShaderGraphNodeKind::Ceiling:
            case ShaderGraphNodeKind::Fraction:
            case ShaderGraphNodeKind::Sine:
            case ShaderGraphNodeKind::Cosine:
            case ShaderGraphNodeKind::Normalize:
            case ShaderGraphNodeKind::Remap:
            case ShaderGraphNodeKind::SmoothStep:
            case ShaderGraphNodeKind::Step:
            case ShaderGraphNodeKind::Posterize:
            case ShaderGraphNodeKind::Round:
            case ShaderGraphNodeKind::Truncate:
            case ShaderGraphNodeKind::Sign:
            case ShaderGraphNodeKind::Modulo:
            case ShaderGraphNodeKind::SquareRoot:
            case ShaderGraphNodeKind::ReciprocalSquareRoot:
            case ShaderGraphNodeKind::Exponential2:
            case ShaderGraphNodeKind::Logarithm2:
            case ShaderGraphNodeKind::Tangent:
            case ShaderGraphNodeKind::ArcSine:
            case ShaderGraphNodeKind::ArcCosine:
            case ShaderGraphNodeKind::ArcTangent2:
            case ShaderGraphNodeKind::DerivativeX:
            case ShaderGraphNodeKind::DerivativeY:
            case ShaderGraphNodeKind::FilterWidth:
            case ShaderGraphNodeKind::Reroute:
            case ShaderGraphNodeKind::If:
            case ShaderGraphNodeKind::ArcTangent:
            case ShaderGraphNodeKind::HyperbolicSine:
            case ShaderGraphNodeKind::HyperbolicCosine:
            case ShaderGraphNodeKind::HyperbolicTangent:
            case ShaderGraphNodeKind::DegreesToRadians:
            case ShaderGraphNodeKind::RadiansToDegrees:
            case ShaderGraphNodeKind::Negate:
            case ShaderGraphNodeKind::ScaleAndBias:
            case ShaderGraphNodeKind::Exponential:
            case ShaderGraphNodeKind::Logarithm:
                return true;
            default:
                return false;
            }
        }

        [[nodiscard]] std::size_t EstimatedNodeCost(const ShaderGraphNodeKind kind) noexcept
        {
            switch (kind)
            {
            case ShaderGraphNodeKind::Master:
                return 48;
            case ShaderGraphNodeKind::TextureSample:
            case ShaderGraphNodeKind::TextureSampleLevel:
                return 4;
            case ShaderGraphNodeKind::TriplanarSample:
                return 18;
            case ShaderGraphNodeKind::NormalMap:
                return 14;
            case ShaderGraphNodeKind::DetailNormal:
                return 10;
            case ShaderGraphNodeKind::Parallax:
                return 16;
            case ShaderGraphNodeKind::Divide:
            case ShaderGraphNodeKind::Power:
            case ShaderGraphNodeKind::Normalize:
            case ShaderGraphNodeKind::Length:
            case ShaderGraphNodeKind::Fresnel:
            case ShaderGraphNodeKind::Refract:
                return 8;
            case ShaderGraphNodeKind::SimpleNoise:
                return 28;
            case ShaderGraphNodeKind::VoronoiNoise:
                return 36;
            case ShaderGraphNodeKind::GradientNoise:
                return 30;
            case ShaderGraphNodeKind::RotateUV:
            case ShaderGraphNodeKind::Desaturate:
            case ShaderGraphNodeKind::Remap:
            case ShaderGraphNodeKind::SmoothStep:
                return 6;
            case ShaderGraphNodeKind::Sine:
            case ShaderGraphNodeKind::Cosine:
            case ShaderGraphNodeKind::Posterize:
            case ShaderGraphNodeKind::SquareRoot:
            case ShaderGraphNodeKind::ReciprocalSquareRoot:
                return 4;
            case ShaderGraphNodeKind::HueShift:
                return 12;
            case ShaderGraphNodeKind::Checkerboard:
                return 8;
            case ShaderGraphNodeKind::Parameter:
            case ShaderGraphNodeKind::Constant:
            case ShaderGraphNodeKind::UV:
            case ShaderGraphNodeKind::Keyword:
            case ShaderGraphNodeKind::VertexColor:
            case ShaderGraphNodeKind::WorldPosition:
            case ShaderGraphNodeKind::WorldNormal:
            case ShaderGraphNodeKind::ViewDirection:
                return 0;
            default:
                return 2;
            }
        }

        [[nodiscard]] ShaderGraphStatistics AnalyzeGraph(const ShaderGraphDefinition& definition)
        {
            ShaderGraphStatistics result;
            result.NodeCount = definition.Nodes.size();
            result.ConnectionCount = definition.Connections.size();
            const auto master =
                std::ranges::find(definition.Nodes, ShaderGraphNodeKind::Master, &ShaderGraphNode::Kind);
            if (master == definition.Nodes.end())
                return result;
            std::unordered_set<AssetId> reachable{master->Id};
            std::vector<AssetId> pending{master->Id};
            while (!pending.empty())
            {
                const auto inputNode = pending.back();
                pending.pop_back();
                for (const auto& connection : definition.Connections)
                    if (connection.Input.Node == inputNode && reachable.insert(connection.Output.Node).second)
                        pending.push_back(connection.Output.Node);
            }
            result.ReachableNodeCount = reachable.size();
            result.UnusedNodeCount = result.NodeCount - result.ReachableNodeCount;
            for (const auto& node : definition.Nodes)
            {
                if (!reachable.contains(node.Id))
                    continue;
                result.EstimatedAluInstructions += EstimatedNodeCost(node.Kind);
                if (node.Kind == ShaderGraphNodeKind::TriplanarSample)
                    result.TextureSampleCount += 3U;
                else if (node.Kind == ShaderGraphNodeKind::TextureSample ||
                         node.Kind == ShaderGraphNodeKind::TextureSampleLevel)
                    ++result.TextureSampleCount;
            }
            return result;
        }

        [[nodiscard]] const ShaderGraphNode& RequireNode(const ShaderGraphDefinition& definition, const AssetId id)
        {
            const auto found = std::ranges::find(definition.Nodes, id, &ShaderGraphNode::Id);
            if (found == definition.Nodes.end())
                throw std::invalid_argument("Shader Graph connection references an unknown node.");
            return *found;
        }

        [[nodiscard]] const ShaderGraphPin& RequirePin(const ShaderGraphNode& node, const AssetId id)
        {
            const auto found = std::ranges::find(node.Pins, id, &ShaderGraphPin::Id);
            if (found == node.Pins.end())
                throw std::invalid_argument("Shader Graph connection references an unknown pin.");
            return *found;
        }

        template <typename Variant> void ValidateFiniteValue(const Variant& value)
        {
            std::visit(
                [](const auto& typed)
                {
                    using T = std::decay_t<decltype(typed)>;
                    if constexpr (std::same_as<T, float>)
                    {
                        if (!std::isfinite(typed))
                            throw std::invalid_argument("Shader Graph values must be finite.");
                    }
                    else if constexpr (std::same_as<T, Vector2>)
                    {
                        if (!std::isfinite(typed.X) || !std::isfinite(typed.Y))
                            throw std::invalid_argument("Shader Graph values must be finite.");
                    }
                    else if constexpr (std::same_as<T, Vector3>)
                    {
                        if (!std::isfinite(typed.X) || !std::isfinite(typed.Y) || !std::isfinite(typed.Z))
                            throw std::invalid_argument("Shader Graph values must be finite.");
                    }
                    else if constexpr (std::same_as<T, Vector4>)
                    {
                        if (!Math::IsFinite(typed))
                            throw std::invalid_argument("Shader Graph values must be finite.");
                    }
                    else if constexpr (std::same_as<T, Color>)
                    {
                        if (!std::isfinite(typed.Red) || !std::isfinite(typed.Green) || !std::isfinite(typed.Blue) ||
                            !std::isfinite(typed.Alpha))
                            throw std::invalid_argument("Shader Graph values must be finite.");
                    }
                },
                value);
        }

        [[nodiscard]] bool ValueMatchesType(const ShaderGraphValue& value, const ShaderGraphValueType type)
        {
            return value.index() == static_cast<std::size_t>(type);
        }
    } // namespace

    ShaderGraphDefinition
    ExpandShaderGraphFunctions(const ShaderGraphDefinition& definition,
                               const std::function<std::optional<ShaderGraphDefinition>(AssetId)>& resolveFunction,
                               const std::size_t maximumDepth)
    {
        if (maximumDepth == 0)
            throw std::invalid_argument("Reusable graph expansion depth must be positive.");

        struct OutputBinding final
        {
            std::optional<ShaderGraphEndpoint> Source;
            ShaderGraphValue DefaultValue = 0.0F;
            ShaderGraphValueType Type = ShaderGraphValueType::Scalar;
        };

        std::function<ShaderGraphDefinition(const ShaderGraphDefinition&, std::vector<AssetId>&, std::size_t)> expand;
        expand = [&](const ShaderGraphDefinition& source, std::vector<AssetId>& stack,
                     const std::size_t depth) -> ShaderGraphDefinition
        {
            if (depth > maximumDepth)
                throw std::invalid_argument("Reusable graph dependency depth exceeds its configured limit.");
            auto result = source;
            result.SchemaVersion = ShaderGraphSourceSchemaVersion;
            ValidateShaderGraph(result);

            while (true)
            {
                const auto call =
                    std::ranges::find(result.Nodes, ShaderGraphNodeKind::FunctionCall, &ShaderGraphNode::Kind);
                if (call == result.Nodes.end())
                    break;
                if (!resolveFunction)
                    throw std::invalid_argument("Function Call nodes require a reusable graph resolver.");
                if (std::ranges::find(stack, call->ReferencedAsset) != stack.end())
                    throw std::invalid_argument("Reusable graph dependency cycle detected at " +
                                                call->ReferencedAsset.ToString() + '.');
                auto resolved = resolveFunction(call->ReferencedAsset);
                if (!resolved)
                    throw std::invalid_argument(
                        "Reusable graph asset is unavailable: " + call->ReferencedAsset.ToString() + '.');
                if (resolved->Purpose == ShaderGraphPurpose::Shader)
                    throw std::invalid_argument("Function Call nodes cannot reference Shader Graph templates.");

                stack.push_back(call->ReferencedAsset);
                auto function = expand(*resolved, stack, depth + 1U);
                stack.pop_back();
                const auto functionMaster =
                    std::ranges::find(function.Nodes, ShaderGraphNodeKind::Master, &ShaderGraphNode::Kind);
                if (functionMaster == function.Nodes.end())
                    throw std::logic_error("Validated reusable graph lost its output node.");

                const auto callId = call->Id;
                std::unordered_map<AssetId, ShaderGraphEndpoint> callInputs;
                for (const auto& connection : result.Connections)
                    if (connection.Input.Node == callId)
                        callInputs.emplace(connection.Input.Pin, connection.Output);

                std::unordered_map<AssetId, AssetId> nodeIds;
                std::unordered_map<AssetId, AssetId> pinIds;
                std::vector<ShaderGraphNode> clonedNodes;
                for (const auto& node : function.Nodes)
                {
                    if (node.Kind == ShaderGraphNodeKind::Master || node.Kind == ShaderGraphNodeKind::Parameter)
                        continue;
                    auto cloned = node;
                    cloned.Id = Detail::DerivedShaderFunctionElementId(callId, node.Id, "node");
                    nodeIds.emplace(node.Id, cloned.Id);
                    cloned.EditorPosition.X += call->EditorPosition.X;
                    cloned.EditorPosition.Y += call->EditorPosition.Y;
                    for (auto& pin : cloned.Pins)
                    {
                        const auto original = pin.Id;
                        pin.Id = Detail::DerivedShaderFunctionElementId(callId, original, "pin");
                        pinIds.emplace(original, pin.Id);
                    }
                    clonedNodes.push_back(std::move(cloned));
                }

                const auto mappedEndpoint = [&](const ShaderGraphEndpoint endpoint)
                {
                    const auto node = nodeIds.find(endpoint.Node);
                    const auto pin = pinIds.find(endpoint.Pin);
                    if (node == nodeIds.end() || pin == pinIds.end())
                        throw std::logic_error("Reusable graph expansion could not map an internal endpoint.");
                    return ShaderGraphEndpoint{node->second, pin->second};
                };
                const auto mutablePin = [&](const ShaderGraphEndpoint endpoint) -> ShaderGraphPin&
                {
                    const auto node = std::ranges::find(clonedNodes, endpoint.Node, &ShaderGraphNode::Id);
                    if (node == clonedNodes.end())
                        throw std::logic_error("Reusable graph expansion lost a cloned node.");
                    const auto pin = std::ranges::find(node->Pins, endpoint.Pin, &ShaderGraphPin::Id);
                    if (pin == node->Pins.end())
                        throw std::logic_error("Reusable graph expansion lost a cloned pin.");
                    return *pin;
                };

                std::unordered_map<AssetId, const ShaderGraphNode*> parameters;
                for (const auto& node : function.Nodes)
                    if (node.Kind == ShaderGraphNodeKind::Parameter)
                        parameters.emplace(node.Id, &node);
                const auto callInputPin = [&](const ShaderGraphNode& parameter) -> const ShaderGraphPin&
                {
                    const auto pin =
                        std::ranges::find_if(call->Pins,
                                             [&](const ShaderGraphPin& candidate)
                                             {
                                                 return candidate.Direction == ShaderGraphPinDirection::Input &&
                                                        candidate.Name == parameter.Symbol;
                                             });
                    if (pin == call->Pins.end())
                        throw std::invalid_argument("Function Call interface is stale for input " + parameter.Symbol +
                                                    '.');
                    const auto output =
                        std::ranges::find(parameter.Pins, ShaderGraphPinDirection::Output, &ShaderGraphPin::Direction);
                    if (output == parameter.Pins.end() || output->Type != pin->Type)
                        throw std::invalid_argument("Function Call input type is stale for " + parameter.Symbol + '.');
                    return *pin;
                };

                std::vector<ShaderGraphConnection> expandedConnections;
                std::map<std::string, OutputBinding, std::less<>> outputs;
                std::set<AssetId> connectedFunctionOutputs;
                for (const auto& connection : function.Connections)
                {
                    const auto parameter = parameters.find(connection.Output.Node);
                    if (connection.Input.Node == functionMaster->Id)
                    {
                        const auto& outputPin = RequirePin(*functionMaster, connection.Input.Pin);
                        connectedFunctionOutputs.insert(outputPin.Id);
                        OutputBinding binding;
                        binding.Type = outputPin.Type;
                        binding.DefaultValue = outputPin.DefaultValue;
                        if (parameter != parameters.end())
                        {
                            const auto& inputPin = callInputPin(*parameter->second);
                            if (const auto incoming = callInputs.find(inputPin.Id); incoming != callInputs.end())
                                binding.Source = incoming->second;
                            else
                                binding.DefaultValue =
                                    CoerceDefaultValue(inputPin.DefaultValue, inputPin.Type, outputPin.Type);
                        }
                        else
                            binding.Source = mappedEndpoint(connection.Output);
                        outputs.emplace(outputPin.Name, binding);
                        continue;
                    }

                    const auto destination = mappedEndpoint(connection.Input);
                    if (parameter != parameters.end())
                    {
                        const auto& inputPin = callInputPin(*parameter->second);
                        if (const auto incoming = callInputs.find(inputPin.Id); incoming != callInputs.end())
                        {
                            expandedConnections.push_back(
                                {Detail::DerivedShaderFunctionElementId(callId, connection.Id, "parameter-connection"),
                                 incoming->second, destination});
                        }
                        else
                        {
                            auto& target = mutablePin(destination);
                            target.DefaultValue = CoerceDefaultValue(inputPin.DefaultValue, inputPin.Type, target.Type);
                        }
                    }
                    else
                    {
                        expandedConnections.push_back(
                            {Detail::DerivedShaderFunctionElementId(callId, connection.Id, "connection"),
                             mappedEndpoint(connection.Output), destination});
                    }
                }
                for (const auto& pin : functionMaster->Pins)
                    if (!connectedFunctionOutputs.contains(pin.Id))
                        outputs.emplace(pin.Name, OutputBinding{std::nullopt, pin.DefaultValue, pin.Type});

                for (const auto& outputPin : call->Pins)
                {
                    if (outputPin.Direction != ShaderGraphPinDirection::Output)
                        continue;
                    const auto binding = outputs.find(outputPin.Name);
                    if (binding == outputs.end() || binding->second.Type != outputPin.Type)
                        throw std::invalid_argument("Function Call interface is stale for output " + outputPin.Name +
                                                    '.');
                }

                std::erase_if(result.Connections,
                              [&](ShaderGraphConnection& connection)
                              {
                                  if (connection.Input.Node == callId)
                                      return true;
                                  if (connection.Output.Node != callId)
                                      return false;
                                  const auto& callOutput = RequirePin(*call, connection.Output.Pin);
                                  const auto binding = outputs.find(callOutput.Name);
                                  if (binding == outputs.end())
                                      throw std::invalid_argument(
                                          "Function Call output no longer exists: " + callOutput.Name + '.');
                                  if (binding->second.Source)
                                  {
                                      connection.Output = *binding->second.Source;
                                      return false;
                                  }
                                  const auto destinationNode =
                                      std::ranges::find(result.Nodes, connection.Input.Node, &ShaderGraphNode::Id);
                                  if (destinationNode == result.Nodes.end())
                                      throw std::logic_error("Function Call destination node is unavailable.");
                                  const auto destinationPin = std::ranges::find(
                                      destinationNode->Pins, connection.Input.Pin, &ShaderGraphPin::Id);
                                  if (destinationPin == destinationNode->Pins.end())
                                      throw std::logic_error("Function Call destination pin is unavailable.");
                                  destinationPin->DefaultValue = CoerceDefaultValue(
                                      binding->second.DefaultValue, binding->second.Type, destinationPin->Type);
                                  return true;
                              });
                std::erase_if(result.Nodes, [&](const ShaderGraphNode& node) { return node.Id == callId; });
                result.Nodes.insert(result.Nodes.end(), std::make_move_iterator(clonedNodes.begin()),
                                    std::make_move_iterator(clonedNodes.end()));
                result.Connections.insert(result.Connections.end(),
                                          std::make_move_iterator(expandedConnections.begin()),
                                          std::make_move_iterator(expandedConnections.end()));
                for (const auto& keyword : function.Keywords)
                {
                    const auto existing = std::ranges::find(result.Keywords, keyword.Name, &ShaderGraphKeyword::Name);
                    if (existing == result.Keywords.end())
                        result.Keywords.push_back(keyword);
                    else if (*existing != keyword)
                        throw std::invalid_argument("Reusable graph keyword contract conflicts for " + keyword.Name +
                                                    '.');
                }
                for (const auto& root : function.IncludeRoots)
                    if (std::ranges::find(result.IncludeRoots, root) == result.IncludeRoots.end())
                        result.IncludeRoots.push_back(root);
                for (const auto& resource : function.Resources)
                {
                    const auto existing =
                        std::ranges::find(result.Resources, resource.Symbol, &ShaderGraphResourceDefinition::Symbol);
                    if (existing == result.Resources.end())
                        result.Resources.push_back(resource);
                    else if (*existing != resource)
                        throw std::invalid_argument("Reusable graph resource contract conflicts for " +
                                                    resource.Symbol + '.');
                }
            }
            ValidateShaderGraph(result);
            return result;
        };

        std::vector<AssetId> stack;
        return expand(definition, stack, 0);
    }

    void ValidateShaderGraph(const ShaderGraphDefinition& definition)
    {
        const auto stages = static_cast<std::uint8_t>(definition.Target.Stages);
        const auto allStages = static_cast<std::uint8_t>(ShaderGraphShaderStage::All);
        const auto graphicsStages = static_cast<std::uint8_t>(ShaderGraphShaderStage::Vertex) |
                                    static_cast<std::uint8_t>(ShaderGraphShaderStage::Fragment);
        const auto computeStage = static_cast<std::uint8_t>(ShaderGraphShaderStage::Compute);
        const auto threadCount = static_cast<std::uint64_t>(definition.Target.ThreadGroupSizeX) *
                                 definition.Target.ThreadGroupSizeY * definition.Target.ThreadGroupSizeZ;
        if (definition.SchemaVersion == 0 || definition.SchemaVersion > ShaderGraphSourceSchemaVersion ||
            definition.Purpose > ShaderGraphPurpose::MaterialLayerBlend ||
            definition.Target.Target > ShaderGraphTarget::Compute || stages == 0U || (stages & ~allStages) != 0U ||
            definition.Target.FullscreenInjectionPoint > ShaderGraphFullscreenInjectionPoint::AfterUi ||
            definition.Target.ThreadGroupSizeX == 0U || definition.Target.ThreadGroupSizeX > 1024U ||
            definition.Target.ThreadGroupSizeY == 0U || definition.Target.ThreadGroupSizeY > 1024U ||
            definition.Target.ThreadGroupSizeZ == 0U || definition.Target.ThreadGroupSizeZ > 64U ||
            threadCount > 1024U || definition.Output > ShaderGraphOutput::Fullscreen || definition.Nodes.empty() ||
            definition.Nodes.size() > MaximumGraphNodes || definition.Connections.size() > MaximumGraphConnections ||
            definition.Keywords.size() > MaximumGraphKeywords || definition.IncludeRoots.empty() ||
            definition.IncludeRoots.size() > MaximumGraphIncludeRoots ||
            !std::isfinite(definition.MaximumWorldPositionDisplacementRadius) ||
            definition.MaximumWorldPositionDisplacementRadius < 0.0F)
            throw std::invalid_argument("Shader Graph has an unsupported schema or exceeds a bounded collection.");

        if (definition.Purpose == ShaderGraphPurpose::Shader)
        {
            const bool validTarget =
                (definition.Target.Target == ShaderGraphTarget::LegacySurface &&
                 definition.Output != ShaderGraphOutput::Fullscreen && stages == graphicsStages) ||
                (definition.Target.Target == ShaderGraphTarget::Ui && definition.Output == ShaderGraphOutput::Unlit &&
                 stages == graphicsStages) ||
                (definition.Target.Target == ShaderGraphTarget::Fullscreen &&
                 definition.Output == ShaderGraphOutput::Fullscreen && stages == graphicsStages) ||
                (definition.Target.Target == ShaderGraphTarget::Vfx &&
                 definition.Output == ShaderGraphOutput::Transparent && stages == graphicsStages) ||
                (definition.Target.Target == ShaderGraphTarget::CustomGraphics &&
                 definition.Output == ShaderGraphOutput::Unlit && stages == graphicsStages) ||
                (definition.Target.Target == ShaderGraphTarget::Compute &&
                 definition.Output == ShaderGraphOutput::Unlit && stages == computeStage);
            if (!validTarget)
                throw std::invalid_argument("Shader Graph target, stages, and output contract are incompatible.");
        }

        ValidateShaderGraphResources(definition.Resources);
        std::set<AssetId> identities;
        std::set<std::string, std::less<>> properties;
        for (const auto& resource : definition.Resources)
        {
            identities.insert(resource.Id);
            properties.insert(resource.Symbol);
        }
        std::set<std::string, std::less<>> keywordNodeSymbols;
        std::size_t masters = 0;
        std::size_t propertyCount = 0;
        std::size_t texturePropertyCount = 0;
        for (const auto& root : definition.IncludeRoots)
            if (!SafeRelativePath(root))
                throw std::invalid_argument("Shader Graph include roots must be confined relative paths.");
        for (const auto& node : definition.Nodes)
        {
            if (!node.Id || !identities.insert(node.Id).second || node.Kind > ShaderGraphNodeKind::FuzzSlab ||
                node.ValueType > ShaderGraphValueType::Bsdf || node.Name.size() > MaximumGraphText ||
                node.TextureSemantic > ShaderTextureSemantic::Roughness || node.Pins.empty() ||
                node.Pins.size() > MaximumGraphPinsPerNode || !Math::IsFinite(node.EditorPosition) ||
                !ValueMatchesType(node.Value, node.ValueType))
                throw std::invalid_argument("Shader Graph node identity, type, position, or pins are invalid.");
            const auto expectedTypeId = ShaderGraphNodeTypeId(node.Kind);
            if (expectedTypeId.empty() || (definition.SchemaVersion >= 2 && node.TypeId != expectedTypeId) ||
                (!node.TypeId.empty() && node.TypeId != expectedTypeId))
                throw std::invalid_argument("Shader Graph node type ID does not match its node contract.");
            ValidateFiniteValue(node.Value);
            if (node.Kind == ShaderGraphNodeKind::Master)
                ++masters;
            if (node.Kind == ShaderGraphNodeKind::Parameter)
            {
                if (definition.Purpose == ShaderGraphPurpose::Shader &&
                    (node.ValueType == ShaderGraphValueType::MaterialAttributes ||
                     node.ValueType == ShaderGraphValueType::Bsdf))
                    throw std::invalid_argument("Shader Graph structured values cannot be exposed as parameters.");
                ++propertyCount;
                texturePropertyCount += node.ValueType == ShaderGraphValueType::Texture2D ? 1U : 0U;
                if (!ValidIdentifier(node.Symbol) || !properties.insert(node.Symbol).second)
                    throw std::invalid_argument("Shader Graph parameter symbols must be unique identifiers.");
                const auto& metadata = node.ParameterMetadata;
                const auto validOptional = [](const std::optional<float>& value)
                { return !value || std::isfinite(*value); };
                if (metadata.Description.size() > MaximumGraphText * 4U ||
                    metadata.Category.size() > MaximumGraphText || !validOptional(metadata.Minimum) ||
                    !validOptional(metadata.Maximum) || !validOptional(metadata.Step) ||
                    (metadata.Minimum && metadata.Maximum && *metadata.Minimum > *metadata.Maximum) ||
                    (metadata.Step && *metadata.Step <= 0.0F))
                    throw std::invalid_argument("Shader Graph parameter metadata is invalid.");
            }
            else if (node.ParameterMetadata != ShaderGraphParameterMetadata{})
                throw std::invalid_argument("Only Shader Graph Parameter nodes may contain parameter metadata.");
            if (node.Kind == ShaderGraphNodeKind::Keyword)
            {
                if (!ValidIdentifier(node.Symbol))
                    throw std::invalid_argument("Shader Graph Keyword node requires a valid symbol.");
                keywordNodeSymbols.insert(node.Symbol);
            }
            if (node.Kind == ShaderGraphNodeKind::Custom &&
                (!SafeRelativePath(node.Include) || !ValidIdentifier(node.Function)))
                throw std::invalid_argument("Custom Shader Graph nodes require a safe include and function name.");
            if (node.Kind == ShaderGraphNodeKind::FunctionCall && !node.ReferencedAsset)
                throw std::invalid_argument("Shader Graph Function Call nodes require a referenced function asset.");
            if (node.Kind != ShaderGraphNodeKind::FunctionCall && node.ReferencedAsset)
                throw std::invalid_argument("Only Shader Graph Function Call nodes may reference graph assets.");
            std::set<std::string, std::less<>> inputPinNames;
            std::set<std::string, std::less<>> outputPinNames;
            for (const auto& pin : node.Pins)
            {
                const auto qualifiedName = node.Name + "." + pin.Name;
                if (!pin.Id)
                    throw std::invalid_argument("Shader Graph pin has no identity: " + qualifiedName + '.');
                if (!identities.insert(pin.Id).second)
                    throw std::invalid_argument("Shader Graph pin identity is duplicated: " + qualifiedName + '.');
                auto& pinNames = pin.Direction == ShaderGraphPinDirection::Input ? inputPinNames : outputPinNames;
                if (pin.Name.empty() || pin.Name.size() > MaximumGraphText || !pinNames.insert(pin.Name).second)
                    throw std::invalid_argument("Shader Graph pin name is invalid or duplicated: " + qualifiedName +
                                                '.');
                if (pin.Type > ShaderGraphValueType::Bsdf || pin.Direction > ShaderGraphPinDirection::Output)
                    throw std::invalid_argument("Shader Graph pin type or direction is invalid: " + qualifiedName +
                                                '.');
                if (!ValueMatchesType(pin.DefaultValue, pin.Type))
                    throw std::invalid_argument("Shader Graph pin default has the wrong value type: " + qualifiedName +
                                                '.');
                ValidateFiniteValue(pin.DefaultValue);
            }
            if (node.Kind == ShaderGraphNodeKind::Master)
            {
                if (!outputPinNames.empty())
                    throw std::invalid_argument("Shader Output nodes cannot expose output pins.");
            }
            else if (node.Kind == ShaderGraphNodeKind::Custom)
            {
                if (outputPinNames.size() != 1 || node.Pins.size() == outputPinNames.size())
                    throw std::invalid_argument("Custom Shader Graph nodes require inputs and exactly one output.");
            }
            else if (node.Kind == ShaderGraphNodeKind::FunctionCall)
            {
                if (outputPinNames.empty())
                    throw std::invalid_argument("Shader Graph Function Call nodes require typed outputs.");
            }
            else
            {
                const auto canonical = CreateShaderGraphNode(node.Kind, node.ValueType);
                if (canonical.Pins.size() != node.Pins.size())
                    throw std::invalid_argument(
                        "Shader Graph node does not match its canonical pin contract: " + node.Name + '.');
                for (std::size_t index = 0; index < node.Pins.size(); ++index)
                {
                    const auto& expected = canonical.Pins[index];
                    const auto& actual = node.Pins[index];
                    if (actual.Name != expected.Name || actual.Type != expected.Type ||
                        actual.Direction != expected.Direction)
                        throw std::invalid_argument("Shader Graph node has a malformed canonical pin: " + node.Name +
                                                    "." + actual.Name + '.');
                }
            }
            if ((NumericNode(node.Kind) || node.Kind == ShaderGraphNodeKind::Constant) &&
                (node.ValueType == ShaderGraphValueType::Texture2D ||
                 node.ValueType == ShaderGraphValueType::MaterialAttributes ||
                 node.ValueType == ShaderGraphValueType::Bsdf))
                throw std::invalid_argument("Shader Graph numeric nodes require scalar, vector, or color values.");
        }
        if (masters != 1 || propertyCount > MaximumGraphProperties)
            throw std::invalid_argument("Shader Graph requires one Shader Output node and at most 80 properties.");
        const auto maximumTextures = Detail::IsUnlitShaderGraphOutput(definition.Output) ? 16U : 12U;
        const auto resourceStatistics = AnalyzeShaderGraphResources(definition.Resources).Statistics;
        if (definition.Purpose == ShaderGraphPurpose::Shader &&
            (texturePropertyCount + resourceStatistics.TextureCount > maximumTextures ||
             texturePropertyCount + resourceStatistics.SamplerCount > maximumTextures ||
             resourceStatistics.ReadOnlyBufferCount > (Detail::IsUnlitShaderGraphOutput(definition.Output) ? 8U : 5U)))
            throw std::invalid_argument("Shader Graph resources exceed the portable sampler or buffer budget.");
        const auto master = std::ranges::find(definition.Nodes, ShaderGraphNodeKind::Master, &ShaderGraphNode::Kind);
        if (definition.Purpose == ShaderGraphPurpose::Shader)
        {
            const auto required = Detail::IsUnlitShaderGraphOutput(definition.Output)
                                      ? std::array<std::string_view, 3>{"Color", "Emission", "Opacity"}
                                      : std::array<std::string_view, 3>{"BaseColor", "Emission", "Opacity"};
            for (const auto name : required)
                if (const auto* pin = FindPin(*master, name, ShaderGraphPinDirection::Input); !pin)
                    throw std::invalid_argument("Shader Output node is missing a required input.");
        }
        else if (master->Pins.empty())
            throw std::invalid_argument("Reusable graph functions require at least one output pin.");

        std::set<std::string, std::less<>> keywordNames;
        std::set<std::string, std::less<>> tokens;
        for (const auto& keyword : definition.Keywords)
        {
            if (!ValidIdentifier(keyword.Name) || !keywordNames.insert(keyword.Name).second ||
                keyword.Options.size() > 8)
                throw std::invalid_argument("Shader Graph keyword definitions are invalid or duplicated.");
            if (keyword.Options.empty())
            {
                if (!keyword.DefaultOption.empty() && keyword.DefaultOption != "true" &&
                    keyword.DefaultOption != "false")
                    throw std::invalid_argument("Boolean Shader Graph keyword defaults must be true or false.");
                if (!tokens.insert(keyword.Name).second)
                    throw std::invalid_argument("Shader Graph keyword tokens must be unique.");
                continue;
            }
            bool hasDefault = keyword.DefaultOption.empty();
            for (const auto& option : keyword.Options)
            {
                if (!ValidIdentifier(option))
                    throw std::invalid_argument("Shader Graph keyword options must be identifiers.");
                const auto token = keyword.Name + "_" + option;
                if (!tokens.insert(token).second)
                    throw std::invalid_argument("Shader Graph keyword tokens must be unique.");
                hasDefault |= option == keyword.DefaultOption;
            }
            if (!hasDefault)
                throw std::invalid_argument("Shader Graph enum keyword default is not one of its options.");
        }
        for (const auto& symbol : keywordNodeSymbols)
            if (!tokens.contains(symbol))
                throw std::invalid_argument("Shader Graph Keyword nodes must reference a declared keyword token.");

        std::set<std::pair<AssetId, AssetId>> inputs;
        for (const auto& connection : definition.Connections)
        {
            if (!connection.Id || !identities.insert(connection.Id).second || !connection.Output.Node ||
                !connection.Output.Pin || !connection.Input.Node || !connection.Input.Pin ||
                connection.Output.Node == connection.Input.Node ||
                !inputs.emplace(connection.Input.Node, connection.Input.Pin).second ||
                connection.RoutingPoints.size() > MaximumGraphRoutingPointsPerConnection ||
                std::ranges::any_of(connection.RoutingPoints,
                                    [](const Vector2 point) { return !Math::IsFinite(point); }))
                throw std::invalid_argument("Shader Graph connection identity or destination is invalid.");
            const auto& outputNode = RequireNode(definition, connection.Output.Node);
            const auto& inputNode = RequireNode(definition, connection.Input.Node);
            const auto& outputPin = RequirePin(outputNode, connection.Output.Pin);
            const auto& inputPin = RequirePin(inputNode, connection.Input.Pin);
            if (outputPin.Direction != ShaderGraphPinDirection::Output ||
                inputPin.Direction != ShaderGraphPinDirection::Input || !Compatible(outputPin.Type, inputPin.Type))
                throw std::invalid_argument("Shader Graph connection directions or value types are incompatible.");
        }

        std::unordered_map<AssetId, std::size_t> indegrees;
        std::unordered_map<AssetId, std::vector<AssetId>> adjacency;
        for (const auto& node : definition.Nodes)
            indegrees.emplace(node.Id, 0);
        for (const auto& connection : definition.Connections)
        {
            adjacency[connection.Output.Node].push_back(connection.Input.Node);
            ++indegrees[connection.Input.Node];
        }
        std::deque<AssetId> ready;
        for (const auto& [node, degree] : indegrees)
            if (degree == 0)
                ready.push_back(node);
        std::size_t visited = 0;
        while (!ready.empty())
        {
            const auto node = ready.front();
            ready.pop_front();
            ++visited;
            for (const auto target : adjacency[node])
                if (--indegrees[target] == 0)
                    ready.push_back(target);
        }
        if (visited != definition.Nodes.size())
            throw std::invalid_argument("Shader Graph contains a cycle.");

        std::vector<AssetId> nodeIds;
        nodeIds.reserve(definition.Nodes.size());
        std::ranges::transform(definition.Nodes, std::back_inserter(nodeIds), &ShaderGraphNode::Id);
        ValidateGraphAuthoringMetadata(definition.Authoring, nodeIds);
    }
} // namespace Keire
