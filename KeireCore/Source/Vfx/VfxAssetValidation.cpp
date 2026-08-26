#include "Keire/Vfx/VfxSubgraph.h"
#include "Keire/Vfx/VfxSystem.h"

#include "KeireInternal/Vfx/VfxAssetCompilerInternal.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

namespace Keire
{
    namespace Detail
    {
        template <typename T> [[nodiscard]] bool FiniteRange(const T minimum, const T maximum) noexcept
        {
            return std::isfinite(minimum) && std::isfinite(maximum) && minimum <= maximum;
        }

        [[nodiscard]] bool OrderedRange(const Vector3 minimum, const Vector3 maximum) noexcept
        {
            return Math::IsFinite(minimum) && Math::IsFinite(maximum) && minimum.X <= maximum.X &&
                   minimum.Y <= maximum.Y && minimum.Z <= maximum.Z;
        }

        [[nodiscard]] bool BoundedVector(const Vector3 value) noexcept
        {
            return Math::IsFinite(value) && std::abs(value.X) <= MaximumAuthoredScalar &&
                   std::abs(value.Y) <= MaximumAuthoredScalar && std::abs(value.Z) <= MaximumAuthoredScalar;
        }

        [[nodiscard]] bool ValidSizeCurve(const Curve1D& curve) noexcept
        {
            if (curve.Keys().empty())
                return false;
            return std::ranges::all_of(curve.Keys(),
                                       [](const CurveKey& key)
                                       {
                                           return key.Time >= 0.0F && key.Time <= 1.0F && key.Value >= 0.0F &&
                                                  key.Value <= MaximumAuthoredScalar &&
                                                  std::abs(key.InTangent) <= MaximumAuthoredScalar &&
                                                  std::abs(key.OutTangent) <= MaximumAuthoredScalar;
                                       });
        }

        [[nodiscard]] bool ValidColorGradient(const ColorGradient& gradient) noexcept
        {
            if (gradient.Keys().empty())
                return false;
            return std::ranges::all_of(gradient.Keys(),
                                       [](const ColorGradientKey& key)
                                       {
                                           return key.Time >= 0.0F && key.Time <= 1.0F && key.Value.Red >= 0.0F &&
                                                  key.Value.Red <= MaximumAuthoredScalar && key.Value.Green >= 0.0F &&
                                                  key.Value.Green <= MaximumAuthoredScalar && key.Value.Blue >= 0.0F &&
                                                  key.Value.Blue <= MaximumAuthoredScalar && key.Value.Alpha >= 0.0F &&
                                                  key.Value.Alpha <= MaximumAuthoredScalar;
                                       });
        }

        [[nodiscard]] bool ValueMatchesType(const VfxValueType type, const VfxParameterValue& value) noexcept
        {
            switch (type)
            {
            case VfxValueType::Boolean:
                return std::holds_alternative<bool>(value);
            case VfxValueType::Integer:
                return std::holds_alternative<std::int64_t>(value);
            case VfxValueType::Scalar:
                return std::holds_alternative<float>(value) && std::isfinite(std::get<float>(value));
            case VfxValueType::Vector2:
                return std::holds_alternative<Vector2>(value) && Math::IsFinite(std::get<Vector2>(value));
            case VfxValueType::Vector3:
                return std::holds_alternative<Vector3>(value) && Math::IsFinite(std::get<Vector3>(value));
            case VfxValueType::Color:
                return std::holds_alternative<Color>(value) && Math::IsFinite(std::get<Color>(value));
            case VfxValueType::Texture:
            case VfxValueType::Mesh:
            case VfxValueType::Asset:
            case VfxValueType::Texture2DArray:
            case VfxValueType::Texture3D:
            case VfxValueType::TextureCube:
            case VfxValueType::Buffer:
            case VfxValueType::PointCache:
            case VfxValueType::SignedDistanceField:
                return std::holds_alternative<AssetId>(value);
            case VfxValueType::ParticleStream:
                return false;
            case VfxValueType::UnsignedInteger:
                return std::holds_alternative<std::uint64_t>(value);
            case VfxValueType::Vector4:
                return std::holds_alternative<Vector4>(value) && Math::IsFinite(std::get<Vector4>(value));
            case VfxValueType::Quaternion:
                return std::holds_alternative<Quaternion>(value) && Math::IsFinite(std::get<Quaternion>(value));
            case VfxValueType::Matrix:
                return std::holds_alternative<Matrix4>(value) && Math::IsFinite(std::get<Matrix4>(value));
            case VfxValueType::Curve:
                return std::holds_alternative<Curve1D>(value);
            case VfxValueType::Gradient:
                return std::holds_alternative<ColorGradient>(value);
            case VfxValueType::ScalarRange:
                if (const auto* range = std::get_if<VfxScalarRange>(&value))
                    return FiniteRange(range->Minimum, range->Maximum);
                return false;
            case VfxValueType::IntegerRange:
                if (const auto* range = std::get_if<VfxIntegerRange>(&value))
                    return range->Minimum <= range->Maximum;
                return false;
            case VfxValueType::UnsignedIntegerRange:
                if (const auto* range = std::get_if<VfxUnsignedIntegerRange>(&value))
                    return range->Minimum <= range->Maximum;
                return false;
            case VfxValueType::Vector2Range:
                if (const auto* range = std::get_if<VfxVector2Range>(&value))
                {
                    return Math::IsFinite(range->Minimum) && Math::IsFinite(range->Maximum) &&
                           range->Minimum.X <= range->Maximum.X && range->Minimum.Y <= range->Maximum.Y;
                }
                return false;
            case VfxValueType::Vector3Range:
                if (const auto* range = std::get_if<VfxVector3Range>(&value))
                    return OrderedRange(range->Minimum, range->Maximum);
                return false;
            case VfxValueType::Vector4Range:
                if (const auto* range = std::get_if<VfxVector4Range>(&value))
                {
                    return Math::IsFinite(range->Minimum) && Math::IsFinite(range->Maximum) &&
                           range->Minimum.X <= range->Maximum.X && range->Minimum.Y <= range->Maximum.Y &&
                           range->Minimum.Z <= range->Maximum.Z && range->Minimum.W <= range->Maximum.W;
                }
                return false;
            case VfxValueType::ColorRange:
                if (const auto* range = std::get_if<VfxColorRange>(&value))
                {
                    return Math::IsFinite(range->Minimum) && Math::IsFinite(range->Maximum) &&
                           range->Minimum.Red <= range->Maximum.Red && range->Minimum.Green <= range->Maximum.Green &&
                           range->Minimum.Blue <= range->Maximum.Blue && range->Minimum.Alpha <= range->Maximum.Alpha;
                }
                return false;
            }
            return false;
        }

        [[nodiscard]] bool IsPersistableValueType(const VfxValueType type) noexcept
        {
            return type <= VfxValueType::SignedDistanceField && type != VfxValueType::ParticleStream;
        }

        [[nodiscard]] bool IsPortableCustomValueType(const VfxValueType type) noexcept
        {
            return type == VfxValueType::Scalar || type == VfxValueType::Vector2 || type == VfxValueType::Vector3 ||
                   type == VfxValueType::Vector4 || type == VfxValueType::Color;
        }

        [[nodiscard]] bool ValidGraphPropertyValue(const VfxGraphPropertyValue& value,
                                                   const std::size_t maximumStringBytes) noexcept
        {
            return std::visit(
                Overloaded{
                    [](const bool) { return true; },
                    [](const std::int64_t) { return true; },
                    [](const std::uint64_t) { return true; },
                    [](const float scalar) { return std::isfinite(scalar); },
                    [maximumStringBytes](const std::string& text) { return text.size() <= maximumStringBytes; },
                    [](const Vector2 vector) { return Math::IsFinite(vector); },
                    [](const Vector3 vector) { return Math::IsFinite(vector); },
                    [](const Vector4 vector) { return Math::IsFinite(vector); },
                    [](const Quaternion quaternion) { return Math::IsFinite(quaternion); },
                    [](const Color color) { return Math::IsFinite(color); },
                    [](const Matrix4& matrix) { return Math::IsFinite(matrix); },
                    [](const AssetId) { return true; },
                },
                value);
        }

        [[nodiscard]] bool ValidGraphProperties(const std::span<const VfxGraphProperty> properties,
                                                const std::size_t maximumStringBytes = MaximumNameBytes)
        {
            std::set<std::string> names;
            return std::ranges::all_of(properties,
                                       [&names, maximumStringBytes](const VfxGraphProperty& property)
                                       {
                                           return !property.Name.empty() && property.Name.size() <= MaximumNameBytes &&
                                                  names.insert(property.Name).second &&
                                                  ValidGraphPropertyValue(property.Value, maximumStringBytes);
                                       });
        }

        [[nodiscard]] bool ValidTypeId(const VfxNodeTypeId& typeId) noexcept
        {
            if (typeId.Empty() || typeId.Value.size() > MaximumNameBytes || typeId.Value.front() == '.' ||
                typeId.Value.back() == '.')
            {
                return false;
            }
            return std::ranges::all_of(typeId.Value,
                                       [](const char character)
                                       {
                                           return (character >= 'a' && character <= 'z') ||
                                                  (character >= '0' && character <= '9') || character == '.' ||
                                                  character == '-';
                                       });
        }
    } // namespace Detail

    using Detail::BoundedVector;
    using Detail::FiniteRange;
    using Detail::IsPersistableValueType;
    using Detail::LowerGraph;
    using Detail::MaximumAuthoredScalar;
    using Detail::MaximumBlackboardParameters;
    using Detail::MaximumBurstCycles;
    using Detail::MaximumBursts;
    using Detail::MaximumDocumentBytes;
    using Detail::MaximumGraphConnections;
    using Detail::MaximumGraphNodes;
    using Detail::MaximumGraphRoutingPointsPerConnection;
    using Detail::MaximumModules;
    using Detail::MaximumNameBytes;
    using Detail::MaximumSystems;
    using Detail::OrderedRange;
    using Detail::Overloaded;
    using Detail::ValidColorGradient;
    using Detail::ValidGraphProperties;
    using Detail::ValidSizeCurve;
    using Detail::ValidTypeId;
    using Detail::ValueMatchesType;

    void ValidateVfxEffectAuthoring(const VfxEffectDefinition& definition)
    {
        if ((definition.SchemaVersion < 1 || definition.SchemaVersion > CurrentVfxSchemaVersion) ||
            !definition.EmitterId || definition.Name.empty() || definition.Name.size() > MaximumNameBytes ||
            !std::isfinite(definition.Duration) || definition.Duration < 0.001F || definition.Duration > 3600.0F ||
            definition.Capacity == 0 || definition.Capacity > 1'000'000 || definition.Modules.empty() ||
            definition.Modules.size() > MaximumModules || definition.ExecutionSource > VfxExecutionSource::Graph ||
            definition.CompatibilityMode > VfxCompatibilityMode::MigratedLegacyModules ||
            (definition.SchemaVersion < 3 && definition.ExecutionSource != VfxExecutionSource::LegacyModules))
        {
            throw std::invalid_argument("VFX effect header is invalid.");
        }
        if (definition.Space > VfxSimulationSpace::World)
            throw std::invalid_argument("VFX effect simulation space is invalid.");

        std::set<AssetId> stableIds{definition.EmitterId};
        std::size_t bursts = 0;

        for (const auto& module : definition.Modules)
        {
            if (!module.Id || !stableIds.insert(module.Id).second)
                throw std::invalid_argument("VFX effect contains an empty or duplicate stable ID.");
            std::visit(
                Overloaded{
                    [&](const VfxEmissionRateModule& value)
                    {
                        if (!std::isfinite(value.ParticlesPerSecond) || value.ParticlesPerSecond < 0.0F ||
                            value.ParticlesPerSecond > 1'000'000.0F)
                        {
                            throw std::invalid_argument("VFX emission rate is invalid.");
                        }
                    },
                    [&](const VfxBurstModule& value)
                    {
                        ++bursts;
                        if (!std::isfinite(value.Time) || value.Time < 0.0F || value.Time >= definition.Duration ||
                            value.Count == 0 || value.Count > 1'000'000 || value.Cycles == 0 ||
                            value.Cycles > MaximumBurstCycles || !std::isfinite(value.Interval) ||
                            value.Interval < 0.0F || (value.Cycles > 1 && value.Interval <= 0.0F) ||
                            value.Time + static_cast<float>(value.Cycles - 1) * value.Interval >= definition.Duration)
                        {
                            throw std::invalid_argument("VFX burst is invalid.");
                        }
                    },
                    [&](const VfxShapeModule& value)
                    {
                        if (value.Shape > VfxShape::Volume || !Math::IsFinite(value.BoxHalfExtent) ||
                            value.BoxHalfExtent.X <= 0.0F || value.BoxHalfExtent.Y <= 0.0F ||
                            value.BoxHalfExtent.Z <= 0.0F || value.BoxHalfExtent.X > MaximumAuthoredScalar ||
                            value.BoxHalfExtent.Y > MaximumAuthoredScalar ||
                            value.BoxHalfExtent.Z > MaximumAuthoredScalar || !std::isfinite(value.Radius) ||
                            value.Radius <= 0.0F || value.Radius > MaximumAuthoredScalar ||
                            !std::isfinite(value.ConeAngleDegrees) || value.ConeAngleDegrees <= 0.0F ||
                            value.ConeAngleDegrees >= 90.0F || !std::isfinite(value.ConeLength) ||
                            value.ConeLength <= 0.0F || value.ConeLength > MaximumAuthoredScalar ||
                            (value.Shape == VfxShape::Mesh && !value.Mesh) ||
                            (value.Shape == VfxShape::Volume && !value.Volume))
                        {
                            throw std::invalid_argument("VFX shape module is invalid.");
                        }
                    },
                    [&](const VfxInitializeModule& value)
                    {
                        if (!FiniteRange(value.LifetimeMinimum, value.LifetimeMaximum) ||
                            value.LifetimeMinimum <= 0.0F || value.LifetimeMaximum > 86'400.0F ||
                            !OrderedRange(value.VelocityMinimum, value.VelocityMaximum) ||
                            !OrderedRange(value.RotationMinimum, value.RotationMaximum) ||
                            !BoundedVector(value.VelocityMinimum) || !BoundedVector(value.VelocityMaximum) ||
                            !BoundedVector(value.RotationMinimum) || !BoundedVector(value.RotationMaximum))
                        {
                            throw std::invalid_argument("VFX initialize module is invalid.");
                        }
                    },
                    [&](const VfxForceModule& value)
                    {
                        if (!BoundedVector(value.Force) || !std::isfinite(value.GravityMultiplier) ||
                            std::abs(value.GravityMultiplier) > 1000.0F)
                        {
                            throw std::invalid_argument("VFX force module is invalid.");
                        }
                    },
                    [&](const VfxSizeOverLifetimeModule& value)
                    {
                        if (!ValidSizeCurve(value.Size))
                            throw std::invalid_argument("VFX size curve is invalid.");
                    },
                    [&](const VfxColorOverLifetimeModule& value)
                    {
                        if (!ValidColorGradient(value.Color))
                            throw std::invalid_argument("VFX color gradient is invalid.");
                    },
                    [&](const VfxCollisionModule& value)
                    {
                        if (value.Mode > VfxCollisionMode::ScenePhysics || !std::isfinite(value.Restitution) ||
                            value.Restitution < 0.0F || value.Restitution > 1.0F)
                        {
                            throw std::invalid_argument("VFX collision module is invalid.");
                        }
                    },
                    [&](const VfxKillShapeModule& value)
                    {
                        if ((value.Shape != VfxShape::Box && value.Shape != VfxShape::Sphere) ||
                            value.Mode > VfxKillShapeMode::Inverted || !BoundedVector(value.Center) ||
                            !Math::IsFinite(value.BoxHalfExtent) || value.BoxHalfExtent.X <= 0.0F ||
                            value.BoxHalfExtent.Y <= 0.0F || value.BoxHalfExtent.Z <= 0.0F ||
                            value.BoxHalfExtent.X > MaximumAuthoredScalar ||
                            value.BoxHalfExtent.Y > MaximumAuthoredScalar ||
                            value.BoxHalfExtent.Z > MaximumAuthoredScalar || !std::isfinite(value.Radius) ||
                            value.Radius <= 0.0F || value.Radius > MaximumAuthoredScalar)
                        {
                            throw std::invalid_argument("VFX kill-shape module is invalid.");
                        }
                    },
                    [&](const VfxRendererModule& value)
                    {
                        if (value.Type > VfxRendererType::Volumetric ||
                            (value.Type == VfxRendererType::Mesh && !value.Mesh))
                        {
                            throw std::invalid_argument("VFX renderer module is invalid.");
                        }
                    },
                },
                module.Payload);
        }

        if (bursts > MaximumBursts)
        {
            throw std::invalid_argument("VFX effect contains an invalid module multiplicity.");
        }

        if (!definition.Systems.empty() || !definition.Blackboard.empty() ||
            definition.ExecutionSource == VfxExecutionSource::Graph)
        {
            if ((definition.ExecutionSource == VfxExecutionSource::Graph && definition.Systems.empty()) ||
                definition.Systems.size() > MaximumSystems ||
                definition.Blackboard.size() > MaximumBlackboardParameters)
                throw std::invalid_argument("VFX graph system or blackboard count is invalid.");
            std::size_t nodeCount = 0;
            std::size_t connectionCount = 0;
            std::set<std::string> parameterNames;
            for (const auto& parameter : definition.Blackboard)
            {
                if (!parameter.Id || !stableIds.insert(parameter.Id).second || parameter.Name.empty() ||
                    parameter.Name.size() > MaximumNameBytes || !parameterNames.insert(parameter.Name).second ||
                    !IsPersistableValueType(parameter.Type) ||
                    !ValueMatchesType(parameter.Type, parameter.DefaultValue))
                    throw std::invalid_argument("VFX blackboard contains an invalid parameter.");
            }
            for (const auto& system : definition.Systems)
            {
                if (!system.Id || !stableIds.insert(system.Id).second || system.Name.empty() ||
                    system.Name.size() > MaximumNameBytes || system.DataType > VfxParticleDataType::ParticleStrip ||
                    system.ParticlesPerStrip == 0 ||
                    (system.DataType == VfxParticleDataType::ParticleStrip &&
                     system.ParticlesPerStrip > definition.Capacity))
                    throw std::invalid_argument("VFX graph contains an invalid system.");
                nodeCount += system.Nodes.size();
                connectionCount += system.Connections.size();
                std::set<AssetId> nodeIds;
                std::set<AssetId> pinIds;
                for (const auto& node : system.Nodes)
                {
                    if (!node.Id || !stableIds.insert(node.Id).second || !nodeIds.insert(node.Id).second ||
                        node.Type.empty() || node.Type.size() > MaximumNameBytes ||
                        !Math::IsFinite(node.EditorPosition) || node.CustomHlsl.size() > MaximumDocumentBytes ||
                        node.Context > VfxContextType::Event || node.Kind > VfxGraphNodeKind::Subgraph ||
                        (definition.SchemaVersion >= 4 && (!ValidTypeId(node.TypeId) || node.DefinitionVersion == 0 ||
                                                           !ValidGraphProperties(node.Properties))))
                        throw std::invalid_argument("VFX graph contains an invalid node.");
                    for (const auto& pin : node.Pins)
                    {
                        if (!pin.Id || !stableIds.insert(pin.Id).second || !pinIds.insert(pin.Id).second ||
                            pin.Name.empty() || pin.Name.size() > MaximumNameBytes ||
                            pin.Semantic.size() > MaximumNameBytes || pin.Type > VfxValueType::SignedDistanceField ||
                            (pin.DefaultValue && (pin.Type == VfxValueType::ParticleStream ||
                                                  !ValueMatchesType(pin.Type, *pin.DefaultValue))))
                            throw std::invalid_argument("VFX graph contains an invalid pin.");
                    }
                    if (definition.SchemaVersion >= 4)
                    {
                        if (std::ranges::any_of(node.ResolvedSignature, [](const VfxValueType type)
                                                { return type > VfxValueType::SignedDistanceField; }))
                        {
                            throw std::invalid_argument("VFX graph contains an invalid resolved signature.");
                        }
                        std::set<AssetId> dynamicPins;
                        for (const auto pin : node.DynamicPinOrder)
                        {
                            if (!pin || !dynamicPins.insert(pin).second ||
                                std::ranges::find(node.Pins, pin, &VfxGraphPin::Id) == node.Pins.end())
                            {
                                throw std::invalid_argument("VFX graph contains an invalid dynamic pin order.");
                            }
                        }
                        if (node.Kind != VfxGraphNodeKind::Context && !node.Blocks.empty())
                            throw std::invalid_argument("Only VFX Context nodes may own ordered blocks.");
                        nodeCount += node.Blocks.size();
                        for (const auto& block : node.Blocks)
                        {
                            if (!block.Id || !stableIds.insert(block.Id).second || !ValidTypeId(block.TypeId) ||
                                block.Type.empty() || block.Type.size() > MaximumNameBytes ||
                                block.DefinitionVersion == 0 ||
                                !ValidGraphProperties(block.Properties,
                                                      block.TypeId.View() == "keire.block.portable-hlsl"
                                                          ? MaximumDocumentBytes
                                                          : MaximumNameBytes))
                            {
                                throw std::invalid_argument("VFX graph contains an invalid block.");
                            }
                            for (const auto& pin : block.Pins)
                            {
                                if (!pin.Id || !stableIds.insert(pin.Id).second || !pinIds.insert(pin.Id).second ||
                                    pin.Name.empty() || pin.Name.size() > MaximumNameBytes ||
                                    pin.Semantic.size() > MaximumNameBytes ||
                                    pin.Type > VfxValueType::SignedDistanceField ||
                                    (pin.DefaultValue && (pin.Type == VfxValueType::ParticleStream ||
                                                          !ValueMatchesType(pin.Type, *pin.DefaultValue))))
                                {
                                    throw std::invalid_argument("VFX graph contains an invalid block pin.");
                                }
                            }
                        }
                    }
                }
                const auto findPin = [&system](const AssetId nodeId, const AssetId blockId,
                                               const AssetId pinId) -> const VfxGraphPin*
                {
                    const auto node = std::ranges::find(system.Nodes, nodeId, &VfxGraphNode::Id);
                    if (node == system.Nodes.end())
                        return nullptr;
                    if (blockId)
                    {
                        const auto block = std::ranges::find(node->Blocks, blockId, &VfxGraphBlock::Id);
                        if (block == node->Blocks.end())
                            return nullptr;
                        const auto pin = std::ranges::find(block->Pins, pinId, &VfxGraphPin::Id);
                        return pin == block->Pins.end() ? nullptr : std::addressof(*pin);
                    }
                    const auto pin = std::ranges::find(node->Pins, pinId, &VfxGraphPin::Id);
                    return pin == node->Pins.end() ? nullptr : std::addressof(*pin);
                };
                for (const auto& connection : system.Connections)
                {
                    const auto* output = findPin(connection.OutputNode, connection.OutputBlock, connection.OutputPin);
                    const auto* input = findPin(connection.InputNode, connection.InputBlock, connection.InputPin);
                    if (!connection.Id || !stableIds.insert(connection.Id).second || !output || !input ||
                        output->Input || !input->Input || output->Type != input->Type ||
                        connection.RoutingPoints.size() > MaximumGraphRoutingPointsPerConnection ||
                        std::ranges::any_of(connection.RoutingPoints,
                                            [](const Vector2 point) { return !Math::IsFinite(point); }))
                        throw std::invalid_argument("VFX graph contains an invalid connection.");
                }
                std::vector<AssetId> authoringNodeIds;
                authoringNodeIds.reserve(system.Nodes.size());
                std::ranges::transform(system.Nodes, std::back_inserter(authoringNodeIds), &VfxGraphNode::Id);
                ValidateGraphAuthoringMetadata(system.Authoring, authoringNodeIds);
            }
            if (nodeCount > MaximumGraphNodes || connectionCount > MaximumGraphConnections)
                throw std::invalid_argument("VFX graph exceeds its bounded complexity limits.");
        }

        const auto hasEnabledEmission = std::ranges::any_of(
            definition.Modules,
            [](const VfxModuleDefinition& module)
            {
                return module.Enabled && (std::holds_alternative<VfxEmissionRateModule>(module.Payload) ||
                                          std::holds_alternative<VfxBurstModule>(module.Payload));
            });
        const auto hasEnabledRenderer = std::ranges::any_of(
            definition.Modules, [](const VfxModuleDefinition& module)
            { return module.Enabled && std::holds_alternative<VfxRendererModule>(module.Payload); });
        const auto hasRendererPayload =
            std::ranges::any_of(definition.Modules, [](const VfxModuleDefinition& module)
                                { return std::holds_alternative<VfxRendererModule>(module.Payload); });
        if (definition.ExecutionSource == VfxExecutionSource::LegacyModules &&
            (!hasEnabledEmission || !hasEnabledRenderer))
            throw std::invalid_argument("VFX effect requires enabled emission and renderer modules.");
        if (definition.ExecutionSource == VfxExecutionSource::Graph && !hasRendererPayload)
            throw std::invalid_argument("VFX graph requires renderer backing modules.");
        if (definition.ExecutionSource == VfxExecutionSource::Graph && !HasVfxSubgraphCalls(definition))
            for (const auto& system : definition.Systems)
                (void)LowerGraph(definition, system, false);
    }
    void ValidateVfxEffect(const VfxEffectDefinition& definition)
    {
        ValidateVfxEffectAuthoring(definition);
        if (definition.ExecutionSource == VfxExecutionSource::Graph && !HasVfxSubgraphCalls(definition))
            for (const auto& system : definition.Systems)
                (void)LowerGraph(definition, system, true);
    }
} // namespace Keire
