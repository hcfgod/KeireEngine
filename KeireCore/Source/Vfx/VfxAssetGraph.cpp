#include "Keire/Vfx/VfxSubgraph.h"
#include "Keire/Vfx/VfxSystem.h"

#include "KeireInternal/Vfx/VfxAssetCompilerInternal.h"
#include "KeireInternal/Vfx/VfxAssetValueCodec.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace Keire
{
    namespace Detail
    {
        [[nodiscard]] VfxContextType ModuleContext(const VfxModulePayload& payload) noexcept
        {
            return std::visit(
                Overloaded{
                    [](const VfxEmissionRateModule&) { return VfxContextType::Spawn; },
                    [](const VfxBurstModule&) { return VfxContextType::Spawn; },
                    [](const VfxShapeModule&) { return VfxContextType::Initialize; },
                    [](const VfxInitializeModule&) { return VfxContextType::Initialize; },
                    [](const VfxForceModule&) { return VfxContextType::Update; },
                    [](const VfxSizeOverLifetimeModule&) { return VfxContextType::Update; },
                    [](const VfxColorOverLifetimeModule&) { return VfxContextType::Update; },
                    [](const VfxCollisionModule&) { return VfxContextType::Update; },
                    [](const VfxKillShapeModule&) { return VfxContextType::Update; },
                    [](const VfxRendererModule&) { return VfxContextType::Output; },
                },
                payload);
        }

        [[nodiscard]] std::string_view ContextTypeId(const VfxContextType context)
        {
            switch (context)
            {
            case VfxContextType::Spawn:
                return "keire.context.spawn";
            case VfxContextType::Initialize:
                return "keire.context.initialize";
            case VfxContextType::Update:
                return "keire.context.update";
            case VfxContextType::Output:
                return "keire.context.output";
            case VfxContextType::Event:
                return "keire.context.event";
            }
            throw std::invalid_argument("VFX context type is unsupported.");
        }

        [[nodiscard]] std::string_view ModuleTypeId(const VfxModulePayload& payload)
        {
            return std::visit(
                Overloaded{
                    [](const VfxEmissionRateModule&) -> std::string_view { return "keire.block.emission-rate"; },
                    [](const VfxBurstModule&) -> std::string_view { return "keire.block.burst"; },
                    [](const VfxShapeModule&) -> std::string_view { return "keire.block.shape"; },
                    [](const VfxInitializeModule&) -> std::string_view { return "keire.block.initialize"; },
                    [](const VfxForceModule&) -> std::string_view { return "keire.block.force"; },
                    [](const VfxSizeOverLifetimeModule&) -> std::string_view
                    { return "keire.block.size-over-lifetime"; },
                    [](const VfxColorOverLifetimeModule&) -> std::string_view
                    { return "keire.block.color-over-lifetime"; },
                    [](const VfxCollisionModule&) -> std::string_view { return "keire.block.collision"; },
                    [](const VfxKillShapeModule&) -> std::string_view { return "keire.block.kill-shape"; },
                    [](const VfxRendererModule&) -> std::string_view { return "keire.output.renderer"; },
                },
                payload);
        }

        [[nodiscard]] std::string TypeSlug(const std::string_view value)
        {
            std::string result;
            result.reserve(value.size());
            bool separator = false;
            for (const auto character : value)
            {
                const auto byte = static_cast<unsigned char>(character);
                if (std::isalnum(byte) != 0)
                {
                    if (separator && !result.empty())
                        result.push_back('-');
                    result.push_back(static_cast<char>(std::tolower(byte)));
                    separator = false;
                }
                else
                    separator = true;
            }
            return result;
        }

        [[nodiscard]] VfxNodeTypeId MigratedNodeTypeId(const VfxEffectDefinition& definition, const VfxGraphNode& node)
        {
            switch (node.Kind)
            {
            case VfxGraphNodeKind::Context:
                return {std::string(ContextTypeId(node.Context))};
            case VfxGraphNodeKind::Module:
            {
                const auto module = std::ranges::find(definition.Modules, node.Reference, &VfxModuleDefinition::Id);
                if (module != definition.Modules.end())
                    return {std::string(ModuleTypeId(module->Payload))};
                const auto slug = TypeSlug(node.Type);
                return {slug == "renderer" ? "keire.output.renderer" : "keire.block." + slug};
            }
            case VfxGraphNodeKind::Parameter:
                return {"keire.parameter"};
            case VfxGraphNodeKind::CustomHlsl:
                return {"keire.operator.portable-hlsl"};
            case VfxGraphNodeKind::Operator:
                return {"keire.operator." + TypeSlug(node.Type)};
            case VfxGraphNodeKind::Attribute:
                return {"keire.attribute." + TypeSlug(node.Type)};
            case VfxGraphNodeKind::Subgraph:
                return {"keire.subgraph." + TypeSlug(node.Type)};
            }
            throw std::invalid_argument("VFX graph node kind is unsupported.");
        }

        [[nodiscard]] std::vector<ModulePinSpecification> ModulePinSpecifications(const VfxModulePayload& payload)
        {
            return std::visit(
                Overloaded{
                    [](const VfxEmissionRateModule& value)
                    {
                        return std::vector<ModulePinSpecification>{
                            {"Particles Per Second", "particlesPerSecond", VfxValueType::Scalar,
                             VfxModuleProperty::EmissionParticlesPerSecond, value.ParticlesPerSecond}};
                    },
                    [](const VfxBurstModule& value)
                    {
                        return std::vector<ModulePinSpecification>{
                            {"Time", "time", VfxValueType::Scalar, VfxModuleProperty::BurstTime, value.Time},
                            {"Count", "count", VfxValueType::Integer, VfxModuleProperty::BurstCount,
                             static_cast<std::int64_t>(value.Count)},
                            {"Cycles", "cycles", VfxValueType::Integer, VfxModuleProperty::BurstCycles,
                             static_cast<std::int64_t>(value.Cycles)},
                            {"Interval", "interval", VfxValueType::Scalar, VfxModuleProperty::BurstInterval,
                             value.Interval}};
                    },
                    [](const VfxShapeModule& value)
                    {
                        return std::vector<ModulePinSpecification>{
                            {"Box Half Extent", "boxHalfExtent", VfxValueType::Vector3,
                             VfxModuleProperty::ShapeBoxHalfExtent, value.BoxHalfExtent},
                            {"Radius", "radius", VfxValueType::Scalar, VfxModuleProperty::ShapeRadius, value.Radius},
                            {"Cone Angle", "coneAngleDegrees", VfxValueType::Scalar,
                             VfxModuleProperty::ShapeConeAngleDegrees, value.ConeAngleDegrees},
                            {"Cone Length", "coneLength", VfxValueType::Scalar, VfxModuleProperty::ShapeConeLength,
                             value.ConeLength},
                            {"Mesh", "mesh", VfxValueType::Mesh, VfxModuleProperty::ShapeMesh, value.Mesh},
                            {"Volume", "volume", VfxValueType::Asset, VfxModuleProperty::ShapeVolume, value.Volume}};
                    },
                    [](const VfxInitializeModule& value)
                    {
                        return std::vector<ModulePinSpecification>{
                            {"Lifetime Minimum", "lifetimeMinimum", VfxValueType::Scalar,
                             VfxModuleProperty::InitializeLifetimeMinimum, value.LifetimeMinimum},
                            {"Lifetime Maximum", "lifetimeMaximum", VfxValueType::Scalar,
                             VfxModuleProperty::InitializeLifetimeMaximum, value.LifetimeMaximum},
                            {"Velocity Minimum", "velocityMinimum", VfxValueType::Vector3,
                             VfxModuleProperty::InitializeVelocityMinimum, value.VelocityMinimum},
                            {"Velocity Maximum", "velocityMaximum", VfxValueType::Vector3,
                             VfxModuleProperty::InitializeVelocityMaximum, value.VelocityMaximum},
                            {"Rotation Minimum", "rotationMinimum", VfxValueType::Vector3,
                             VfxModuleProperty::InitializeRotationMinimum, value.RotationMinimum},
                            {"Rotation Maximum", "rotationMaximum", VfxValueType::Vector3,
                             VfxModuleProperty::InitializeRotationMaximum, value.RotationMaximum}};
                    },
                    [](const VfxForceModule& value)
                    {
                        return std::vector<ModulePinSpecification>{
                            {"Force", "force", VfxValueType::Vector3, VfxModuleProperty::ForceVector, value.Force},
                            {"Gravity Multiplier", "gravityMultiplier", VfxValueType::Scalar,
                             VfxModuleProperty::ForceGravityMultiplier, value.GravityMultiplier}};
                    },
                    [](const VfxSizeOverLifetimeModule& value)
                    {
                        return std::vector<ModulePinSpecification>{{"Size", "size", VfxValueType::Scalar,
                                                                    VfxModuleProperty::SizeConstant,
                                                                    value.Size.Evaluate(0.0F)}};
                    },
                    [](const VfxColorOverLifetimeModule& value)
                    {
                        return std::vector<ModulePinSpecification>{{"Color", "color", VfxValueType::Color,
                                                                    VfxModuleProperty::ColorConstant,
                                                                    value.Color.Evaluate(0.0F)}};
                    },
                    [](const VfxCollisionModule& value)
                    {
                        return std::vector<ModulePinSpecification>{
                            {"Restitution", "restitution", VfxValueType::Scalar,
                             VfxModuleProperty::CollisionRestitution, value.Restitution},
                            {"Kill On Collision", "killOnCollision", VfxValueType::Boolean,
                             VfxModuleProperty::CollisionKillOnCollision, value.KillOnCollision}};
                    },
                    [](const VfxKillShapeModule& value)
                    {
                        return std::vector<ModulePinSpecification>{
                            {"Center", "center", VfxValueType::Vector3, VfxModuleProperty::KillShapeCenter,
                             value.Center},
                            {"Box Half Extent", "boxHalfExtent", VfxValueType::Vector3,
                             VfxModuleProperty::KillShapeBoxHalfExtent, value.BoxHalfExtent},
                            {"Radius", "radius", VfxValueType::Scalar, VfxModuleProperty::KillShapeRadius,
                             value.Radius},
                            {"Inverted", "inverted", VfxValueType::Boolean, VfxModuleProperty::KillShapeInverted,
                             value.Mode == VfxKillShapeMode::Inverted}};
                    },
                    [](const VfxRendererModule& value)
                    {
                        return std::vector<ModulePinSpecification>{
                            {"Sprite", "sprite", VfxValueType::Texture, VfxModuleProperty::RendererSprite,
                             value.Sprite},
                            {"Mesh", "mesh", VfxValueType::Mesh, VfxModuleProperty::RendererMesh, value.Mesh},
                            {"Material", "material", VfxValueType::Asset, VfxModuleProperty::RendererMaterial,
                             value.Material}};
                    },
                },
                payload);
        }

        [[nodiscard]] std::uint32_t ModuleDefinitionVersion(const VfxModulePayload& payload) noexcept
        {
            return std::visit(
                Overloaded{
                    [](const VfxShapeModule&) { return 2U; },
                    [](const VfxRendererModule&) { return 2U; },
                    [](const auto&) { return 1U; },
                },
                payload);
        }

        [[nodiscard]] std::uint32_t ContextOrder(const VfxContextType context)
        {
            switch (context)
            {
            case VfxContextType::Spawn:
                return 0;
            case VfxContextType::Initialize:
                return 1;
            case VfxContextType::Update:
                return 2;
            case VfxContextType::Output:
                return 3;
            case VfxContextType::Event:
                return 0;
            }
            throw std::invalid_argument("VFX context is invalid.");
        }

        [[nodiscard]] const VfxGraphPin* FindPin(const VfxGraphNode& node, const bool input, const VfxValueType type,
                                                 const std::string_view semantic) noexcept
        {
            const auto found =
                std::ranges::find_if(node.Pins, [input, type, semantic](const VfxGraphPin& pin)
                                     { return pin.Input == input && pin.Type == type && pin.Semantic == semantic; });
            return found == node.Pins.end() ? nullptr : std::addressof(*found);
        }

        [[nodiscard]] std::uint64_t HashBytes(const std::span<const std::byte> bytes) noexcept
        {
            std::uint64_t hash = 1469598103934665603ULL;
            for (const auto value : bytes)
            {
                hash ^= std::to_integer<std::uint8_t>(value);
                hash *= 1099511628211ULL;
            }
            return hash;
        }

        [[nodiscard]] AssetId AllocateDerivedId(const AssetId source, std::uint64_t salt, std::set<AssetId>& used)
        {
            for (;; ++salt)
            {
                const auto candidate = DerivedVfxGraphId(source, salt);
                if (candidate && used.insert(candidate).second)
                    return candidate;
            }
        }
    } // namespace Detail

    using Detail::AllocateDerivedId;
    using Detail::ContextName;
    using Detail::ContextOrder;
    using Detail::ContextTypeId;
    using Detail::FindPin;
    using Detail::MaximumDocumentBytes;
    using Detail::MigratedNodeTypeId;
    using Detail::ModuleContext;
    using Detail::ModuleDefinitionVersion;
    using Detail::ModulePinSpecification;
    using Detail::ModulePinSpecifications;
    using Detail::ModuleTypeId;
    using Detail::ModuleTypeName;
    using Detail::TypeSlug;
    using Detail::ValueMatchesType;

    namespace
    {
        void MigrateLegacyExecutableNodesToBlocks(VfxEffectDefinition& definition)
        {
            if (definition.ExecutionSource != VfxExecutionSource::Graph)
                return;

            for (auto& system : definition.Systems)
            {
                if (std::ranges::none_of(
                        system.Nodes, [](const VfxGraphNode& node)
                        { return node.Kind == VfxGraphNodeKind::Module || node.Kind == VfxGraphNodeKind::CustomHlsl; }))
                {
                    continue;
                }

                std::map<AssetId, VfxGraphNode*> nodes;
                std::array<VfxGraphNode*, 4> contexts{};
                for (auto& node : system.Nodes)
                {
                    nodes.emplace(node.Id, std::addressof(node));
                    if (node.Kind == VfxGraphNodeKind::Context && node.Context != VfxContextType::Event)
                    {
                        const auto index = ContextOrder(node.Context);
                        if (contexts[index])
                            throw std::invalid_argument(
                                "Historical VFX graph migration found duplicate executable contexts.");
                        contexts[index] = std::addressof(node);
                    }
                }
                if (std::ranges::any_of(contexts, [](const VfxGraphNode* context) { return context == nullptr; }))
                {
                    throw std::invalid_argument(
                        "Historical VFX graph migration requires Spawn, Initialize, Update, and Output contexts.");
                }

                const auto graphPin = [&nodes](const AssetId nodeId, const AssetId pinId) -> const VfxGraphPin*
                {
                    const auto node = nodes.find(nodeId);
                    if (node == nodes.end())
                        return nullptr;
                    const auto pin = std::ranges::find(node->second->Pins, pinId, &VfxGraphPin::Id);
                    return pin == node->second->Pins.end() ? nullptr : std::addressof(*pin);
                };

                std::map<AssetId, std::size_t> flowIndegree;
                std::map<AssetId, std::vector<AssetId>> flowAdjacency;
                std::map<AssetId, std::vector<AssetId>> reverseFlowAdjacency;
                for (const auto& node : system.Nodes)
                    flowIndegree.emplace(node.Id, 0);
                for (const auto& connection : system.Connections)
                {
                    const auto* output = graphPin(connection.OutputNode, connection.OutputPin);
                    const auto* input = graphPin(connection.InputNode, connection.InputPin);
                    if (!output || !input || output->Type != VfxValueType::ParticleStream ||
                        input->Type != VfxValueType::ParticleStream)
                    {
                        continue;
                    }
                    flowAdjacency[connection.OutputNode].push_back(connection.InputNode);
                    reverseFlowAdjacency[connection.InputNode].push_back(connection.OutputNode);
                    ++flowIndegree.at(connection.InputNode);
                }

                const auto visitFlow = [](const AssetId start, const std::map<AssetId, std::vector<AssetId>>& adjacency)
                {
                    std::set<AssetId> visited{start};
                    std::vector<AssetId> pending{start};
                    while (!pending.empty())
                    {
                        const auto current = pending.back();
                        pending.pop_back();
                        if (const auto found = adjacency.find(current); found != adjacency.end())
                            for (const auto destination : found->second)
                                if (visited.insert(destination).second)
                                    pending.push_back(destination);
                    }
                    return visited;
                };
                const auto fromSpawn = visitFlow(contexts.front()->Id, flowAdjacency);
                const auto toOutput = visitFlow(contexts.back()->Id, reverseFlowAdjacency);

                std::set<AssetId> ready;
                for (const auto& [node, count] : flowIndegree)
                    if (count == 0)
                        ready.insert(node);
                std::vector<AssetId> flowOrder;
                flowOrder.reserve(system.Nodes.size());
                while (!ready.empty())
                {
                    const auto node = *ready.begin();
                    ready.erase(ready.begin());
                    flowOrder.push_back(node);
                    for (const auto destination : flowAdjacency[node])
                        if (--flowIndegree.at(destination) == 0)
                            ready.insert(destination);
                }
                if (flowOrder.size() != system.Nodes.size())
                    throw std::invalid_argument("Historical VFX graph migration found a particle-stream cycle.");

                std::map<AssetId, AssetId> migratedContexts;
                for (const auto nodeId : flowOrder)
                {
                    auto& node = *nodes.at(nodeId);
                    if (node.Kind != VfxGraphNodeKind::Module && node.Kind != VfxGraphNodeKind::CustomHlsl)
                        continue;
                    if (!fromSpawn.contains(node.Id) || !toOutput.contains(node.Id))
                        continue;
                    auto& context = *contexts[ContextOrder(node.Context)];
                    VfxGraphBlock block;
                    if (node.Kind == VfxGraphNodeKind::Module)
                    {
                        const auto module =
                            std::ranges::find(definition.Modules, node.Reference, &VfxModuleDefinition::Id);
                        if (module == definition.Modules.end())
                            throw std::invalid_argument(
                                "Historical VFX Module node references an unknown compatibility payload.");
                        block = CreateVfxGraphBlock(*module);
                    }
                    else
                    {
                        block = CreateVfxGraphPortableHlslBlock(node.CustomHlsl);
                    }
                    block.Id = node.Id;
                    block.DefinitionVersion = node.DefinitionVersion;
                    block.Pins.clear();
                    for (const auto& pin : node.Pins)
                        if (pin.Input && pin.Type != VfxValueType::ParticleStream)
                            block.Pins.push_back(pin);
                    context.Blocks.push_back(std::move(block));
                    migratedContexts.emplace(node.Id, context.Id);
                }

                std::vector<VfxGraphConnection> migratedConnections;
                migratedConnections.reserve(system.Connections.size());
                for (auto connection : system.Connections)
                {
                    const auto* output = graphPin(connection.OutputNode, connection.OutputPin);
                    const auto* input = graphPin(connection.InputNode, connection.InputPin);
                    if (!output || !input)
                        throw std::invalid_argument("Historical VFX graph migration found an invalid connection.");
                    const bool stream =
                        output->Type == VfxValueType::ParticleStream || input->Type == VfxValueType::ParticleStream;
                    const auto outputContext = migratedContexts.find(connection.OutputNode);
                    const auto inputContext = migratedContexts.find(connection.InputNode);
                    if (!stream)
                    {
                        if (outputContext != migratedContexts.end())
                            throw std::invalid_argument(
                                "Historical executable VFX nodes may not expose data-output pins.");
                        if (inputContext != migratedContexts.end())
                        {
                            connection.InputNode = inputContext->second;
                            connection.InputBlock = inputContext->first;
                        }
                        migratedConnections.push_back(connection);
                        continue;
                    }

                    if (outputContext == migratedContexts.end() && inputContext == migratedContexts.end())
                    {
                        migratedConnections.push_back(connection);
                        continue;
                    }
                    if (outputContext != migratedContexts.end() && inputContext == migratedContexts.end() &&
                        nodes.at(connection.InputNode)->Kind == VfxGraphNodeKind::Context)
                    {
                        const auto inputStage = ContextOrder(nodes.at(connection.InputNode)->Context);
                        if (inputStage == 0)
                            throw std::invalid_argument(
                                "Historical VFX graph migration found a stream entering the Spawn context.");
                        auto& context = *contexts[inputStage - 1];
                        const auto* contextOutput = FindPin(context, false, VfxValueType::ParticleStream, "particles");
                        if (!contextOutput)
                            throw std::invalid_argument(
                                "Historical VFX graph migration found a context without a stream output.");
                        connection.OutputNode = context.Id;
                        connection.OutputBlock = {};
                        connection.OutputPin = contextOutput->Id;
                        migratedConnections.push_back(connection);
                    }
                }
                system.Connections = std::move(migratedConnections);
                std::erase_if(system.Nodes, [&migratedContexts](const VfxGraphNode& node)
                              { return migratedContexts.contains(node.Id); });
            }
        }

        void CollectVfxStableIds(const VfxEffectDefinition& definition, std::set<AssetId>& used)
        {
            used.insert(definition.EmitterId);
            for (const auto& module : definition.Modules)
                used.insert(module.Id);
            for (const auto& parameter : definition.Blackboard)
                used.insert(parameter.Id);
            for (const auto& system : definition.Systems)
            {
                used.insert(system.Id);
                for (const auto& node : system.Nodes)
                {
                    used.insert(node.Id);
                    for (const auto& pin : node.Pins)
                        used.insert(pin.Id);
                    for (const auto& block : node.Blocks)
                    {
                        used.insert(block.Id);
                        for (const auto& pin : block.Pins)
                            used.insert(pin.Id);
                    }
                }
                for (const auto& connection : system.Connections)
                    used.insert(connection.Id);
            }
        }

        [[nodiscard]] VfxGraphPin UpgradeModulePin(const VfxGraphPin& pin, const ModulePinSpecification& specification)
        {
            if (!pin.Input || pin.Type != specification.Type || !pin.DefaultValue ||
                !ValueMatchesType(pin.Type, *pin.DefaultValue))
            {
                throw std::invalid_argument("Historical VFX module input cannot be upgraded safely.");
            }
            auto result = pin;
            result.Name = specification.Name;
            result.Semantic = specification.Semantic;
            return result;
        }

        void UpgradeModuleBlockLayout(VfxGraphBlock& block, const VfxModuleDefinition& module, std::set<AssetId>& used)
        {
            const auto targetVersion = ModuleDefinitionVersion(module.Payload);
            if (block.DefinitionVersion >= targetVersion)
                return;
            if (block.DefinitionVersion != 1 || targetVersion != 2 ||
                block.TypeId.View() != ModuleTypeId(module.Payload) || block.Type != ModuleTypeName(module.Payload))
            {
                return;
            }

            const auto specifications = ModulePinSpecifications(module.Payload);
            std::set<std::string> semantics;
            for (const auto& pin : block.Pins)
            {
                const auto specification =
                    std::ranges::find(specifications, pin.Semantic, &ModulePinSpecification::Semantic);
                if (!semantics.insert(pin.Semantic).second || specification == specifications.end())
                    throw std::invalid_argument("Historical VFX Block contains an input that cannot be upgraded.");
                (void)UpgradeModulePin(pin, *specification);
            }

            std::vector<VfxGraphPin> upgraded;
            upgraded.reserve(specifications.size());
            for (std::size_t index = 0; index < specifications.size(); ++index)
            {
                const auto& specification = specifications[index];
                const auto pin = std::ranges::find(block.Pins, specification.Semantic, &VfxGraphPin::Semantic);
                if (pin != block.Pins.end())
                    upgraded.push_back(UpgradeModulePin(*pin, specification));
                else
                {
                    upgraded.push_back({AllocateDerivedId(block.Id, 0x50494e0000000000ULL + index, used),
                                        std::string(specification.Name), specification.Type, true,
                                        std::string(specification.Semantic), specification.DefaultValue});
                }
            }
            block.Pins = std::move(upgraded);
            block.DefinitionVersion = targetVersion;
        }

        void UpgradeModuleNodeLayout(VfxGraphNode& node, const VfxModuleDefinition& module, std::set<AssetId>& used)
        {
            const auto targetVersion = ModuleDefinitionVersion(module.Payload);
            if (node.DefinitionVersion >= targetVersion)
                return;
            if (node.DefinitionVersion != 1 || targetVersion != 2 || node.TypeId.View() != ModuleTypeId(module.Payload))
                return;

            const auto specifications = ModulePinSpecifications(module.Payload);
            const VfxGraphPin* inputFlow = nullptr;
            const VfxGraphPin* outputFlow = nullptr;
            std::set<std::string> semantics;
            for (const auto& pin : node.Pins)
            {
                if (pin.Type == VfxValueType::ParticleStream)
                {
                    auto*& flow = pin.Input ? inputFlow : outputFlow;
                    if (flow || pin.Semantic != "particles" || pin.DefaultValue)
                        throw std::invalid_argument("Historical VFX module flow cannot be upgraded safely.");
                    flow = std::addressof(pin);
                    continue;
                }
                const auto specification =
                    std::ranges::find(specifications, pin.Semantic, &ModulePinSpecification::Semantic);
                if (!semantics.insert(pin.Semantic).second || specification == specifications.end())
                    throw std::invalid_argument(
                        "Historical VFX module node contains an input that cannot be upgraded.");
                const auto upgraded = UpgradeModulePin(pin, *specification);
                if (*upgraded.DefaultValue != specification->DefaultValue)
                    throw std::invalid_argument("Historical VFX module node default cannot be upgraded safely.");
            }
            if (!inputFlow || !outputFlow)
                throw std::invalid_argument("Historical VFX module flow cannot be upgraded safely.");

            std::vector<VfxGraphPin> upgraded;
            upgraded.reserve(specifications.size() + 2);
            upgraded.push_back(*inputFlow);
            for (std::size_t index = 0; index < specifications.size(); ++index)
            {
                const auto& specification = specifications[index];
                const auto pin = std::ranges::find(node.Pins, specification.Semantic, &VfxGraphPin::Semantic);
                if (pin != node.Pins.end())
                    upgraded.push_back(UpgradeModulePin(*pin, specification));
                else
                {
                    upgraded.push_back({AllocateDerivedId(node.Id, 0x50494e0000000000ULL + index, used),
                                        std::string(specification.Name), specification.Type, true,
                                        std::string(specification.Semantic), specification.DefaultValue});
                }
            }
            upgraded.push_back(*outputFlow);
            node.Pins = std::move(upgraded);
            node.DefinitionVersion = targetVersion;
        }

        void UpgradeModuleGraphLayouts(VfxEffectDefinition& definition)
        {
            std::map<AssetId, const VfxModuleDefinition*> modules;
            for (const auto& module : definition.Modules)
                modules.emplace(module.Id, std::addressof(module));
            std::set<AssetId> used;
            CollectVfxStableIds(definition, used);

            for (auto& system : definition.Systems)
            {
                for (auto& node : system.Nodes)
                {
                    if (node.Kind == VfxGraphNodeKind::Module)
                    {
                        if (const auto module = modules.find(node.Reference); module != modules.end())
                            UpgradeModuleNodeLayout(node, *module->second, used);
                    }
                    if (node.Kind != VfxGraphNodeKind::Context)
                        continue;
                    for (auto& block : node.Blocks)
                    {
                        if (const auto module = modules.find(block.Reference); module != modules.end())
                            UpgradeModuleBlockLayout(block, *module->second, used);
                    }
                }
            }
        }
    } // namespace

    VfxGraphNode CreateVfxGraphModuleNode(const VfxModuleDefinition& module, const Vector2 editorPosition)
    {
        if (!module.Id)
            throw std::invalid_argument("VFX module nodes require a valid Runtime Module reference.");
        VfxGraphNode result;
        result.Id = AssetId::Generate();
        result.Type = std::string(ModuleTypeName(module.Payload));
        result.Context = ModuleContext(module.Payload);
        result.EditorPosition = editorPosition;
        result.Kind = VfxGraphNodeKind::Module;
        result.Reference = module.Id;
        result.TypeId.Value = ModuleTypeId(module.Payload);
        result.DefinitionVersion = ModuleDefinitionVersion(module.Payload);
        result.Pins.push_back(
            {AssetId::Generate(), "Particles", VfxValueType::ParticleStream, true, "particles", std::nullopt});
        for (const auto& specification : ModulePinSpecifications(module.Payload))
        {
            result.Pins.push_back({AssetId::Generate(), std::string(specification.Name), specification.Type, true,
                                   std::string(specification.Semantic), specification.DefaultValue});
        }
        result.Pins.push_back(
            {AssetId::Generate(), "Particles", VfxValueType::ParticleStream, false, "particles", std::nullopt});
        return result;
    }

    VfxGraphBlock CreateVfxGraphBlock(const VfxModuleDefinition& module)
    {
        if (!module.Id)
            throw std::invalid_argument("VFX Blocks require a valid Runtime Module payload reference.");
        VfxGraphBlock result;
        result.Id = AssetId::Generate();
        result.TypeId.Value = ModuleTypeId(module.Payload);
        result.Type = std::string(ModuleTypeName(module.Payload));
        result.Enabled = module.Enabled;
        result.Reference = module.Id;
        result.DefinitionVersion = ModuleDefinitionVersion(module.Payload);
        const auto specifications = ModulePinSpecifications(module.Payload);
        result.Pins.reserve(specifications.size());
        for (const auto& specification : specifications)
        {
            result.Pins.push_back({AssetId::Generate(), std::string(specification.Name), specification.Type, true,
                                   std::string(specification.Semantic), specification.DefaultValue});
        }
        return result;
    }

    VfxGraphBlock CreateVfxGraphPortableHlslBlock(std::string source)
    {
        if (source.empty() || source.size() > MaximumDocumentBytes)
            throw std::invalid_argument("Portable Custom HLSL Blocks require bounded non-empty source.");
        VfxGraphBlock result;
        result.Id = AssetId::Generate();
        result.TypeId.Value = "keire.block.portable-hlsl";
        result.Type = "Portable Custom HLSL";
        result.Properties.push_back({"Source", std::move(source)});
        return result;
    }
    VfxEffectDefinition MigrateVfxEffectToCurrentSchema(const VfxEffectDefinition& definition)
    {
        if (definition.SchemaVersion < 1 || definition.SchemaVersion > CurrentVfxSchemaVersion)
            throw std::invalid_argument("VFX effect migration source schema is unsupported.");
        auto result = definition;
        if (result.SchemaVersion < CurrentVfxSchemaVersion)
        {
            if (result.SchemaVersion < 4)
            {
                result.CompatibilityMode = VfxCompatibilityMode::MigratedLegacyModules;
                if (result.SchemaVersion < 3)
                    result.ExecutionSource = VfxExecutionSource::LegacyModules;
                for (auto& system : result.Systems)
                {
                    for (auto& node : system.Nodes)
                    {
                        node.TypeId = MigratedNodeTypeId(result, node);
                        node.DefinitionVersion = 1;
                        for (auto& block : node.Blocks)
                        {
                            if (const auto module =
                                    std::ranges::find(result.Modules, block.Reference, &VfxModuleDefinition::Id);
                                module != result.Modules.end())
                            {
                                block.TypeId.Value = ModuleTypeId(module->Payload);
                            }
                            else
                                block.TypeId.Value = "keire.block." + TypeSlug(block.Type);
                            block.DefinitionVersion = 1;
                        }
                    }
                }
                MigrateLegacyExecutableNodesToBlocks(result);
            }
            result.SchemaVersion = CurrentVfxSchemaVersion;
        }
        UpgradeModuleGraphLayouts(result);
        return result;
    }
    VfxEffectDefinition ConvertVfxEffectToGraph(const VfxEffectDefinition& definition)
    {
        auto result = MigrateVfxEffectToCurrentSchema(definition);
        ValidateVfxEffect(result);
        if (result.ExecutionSource == VfxExecutionSource::Graph)
            return result;
        result.ExecutionSource = VfxExecutionSource::Graph;
        result.CompatibilityMode = VfxCompatibilityMode::MigratedLegacyModules;
        result.Systems.clear();

        std::set<AssetId> used{result.EmitterId};
        for (const auto& module : result.Modules)
            used.insert(module.Id);
        for (const auto& parameter : result.Blackboard)
            used.insert(parameter.Id);
        VfxGraphSystem system;
        system.Id = AllocateDerivedId(result.EmitterId, 0x1000, used);
        system.Name = "Particle System";
        float cursorX = 0.0F;
        std::uint64_t connectionSalt = 0x8000;
        AssetId previousNode;
        AssetId previousOutput;

        const auto connect = [&](const AssetId inputNode, const AssetId inputPin)
        {
            if (!previousNode || !previousOutput)
                throw std::logic_error("VFX graph conversion has no particle-stream source.");
            system.Connections.push_back({AllocateDerivedId(result.EmitterId, connectionSalt++, used), previousNode,
                                          previousOutput, inputNode, inputPin});
        };

        const auto appendContext = [&](const VfxContextType context)
        {
            VfxGraphNode node;
            node.Id = AllocateDerivedId(result.EmitterId, 0x2000 + ContextOrder(context) * 0x100, used);
            node.Type = std::string(ContextName(context)) + " Context";
            node.Context = context;
            node.EditorPosition = {cursorX, 0.0F};
            node.Kind = VfxGraphNodeKind::Context;
            node.TypeId.Value = ContextTypeId(context);
            cursorX += 280.0F;
            if (context != VfxContextType::Spawn)
            {
                node.Pins.push_back({AllocateDerivedId(node.Id, 1, used), "Particles", VfxValueType::ParticleStream,
                                     true, "particles", std::nullopt});
                connect(node.Id, node.Pins.back().Id);
            }
            if (context != VfxContextType::Output)
            {
                node.Pins.push_back({AllocateDerivedId(node.Id, 2, used), "Particles", VfxValueType::ParticleStream,
                                     false, "particles", std::nullopt});
                previousNode = node.Id;
                previousOutput = node.Pins.back().Id;
            }
            for (const auto& module : result.Modules)
            {
                if (ModuleContext(module.Payload) != context)
                    continue;
                auto block = CreateVfxGraphBlock(module);
                block.Id = AllocateDerivedId(module.Id, 0x3000, used);
                for (std::size_t pinIndex = 0; pinIndex < block.Pins.size(); ++pinIndex)
                    block.Pins[pinIndex].Id = AllocateDerivedId(module.Id, 0x3101 + pinIndex, used);
                node.Blocks.push_back(std::move(block));
            }
            system.Nodes.push_back(std::move(node));
        };

        appendContext(VfxContextType::Spawn);
        appendContext(VfxContextType::Initialize);
        appendContext(VfxContextType::Update);
        appendContext(VfxContextType::Output);

        std::vector<const VfxBlackboardParameter*> sortedParameters;
        sortedParameters.reserve(result.Blackboard.size());
        for (const auto& parameter : result.Blackboard)
            sortedParameters.push_back(std::addressof(parameter));
        std::ranges::sort(sortedParameters, {}, [](const VfxBlackboardParameter* parameter) { return parameter->Id; });
        float parameterY = 0.0F;
        for (const auto* parameter : sortedParameters)
        {
            VfxGraphNode node;
            node.Id = AllocateDerivedId(parameter->Id, 0x4000, used);
            node.Type = parameter->Name;
            node.Context = VfxContextType::Update;
            node.EditorPosition = {-360.0F, parameterY};
            node.Kind = VfxGraphNodeKind::Parameter;
            node.Reference = parameter->Id;
            node.TypeId.Value = "keire.parameter";
            node.Pins.push_back({AllocateDerivedId(parameter->Id, 0x4100, used), parameter->Name, parameter->Type,
                                 false, "value", std::nullopt});
            system.Nodes.push_back(std::move(node));
            parameterY += 150.0F;
        }
        result.Systems.push_back(std::move(system));
        ValidateVfxEffect(result);
        return result;
    }
} // namespace Keire
