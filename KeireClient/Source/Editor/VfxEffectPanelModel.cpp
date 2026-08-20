#include "KeireClient/Editor/VfxEffectPanelModel.h"

#include <algorithm>
#include <bit>
#include <ranges>
#include <variant>

namespace KeireEditor
{
    namespace Detail
    {
        namespace
        {
            template <typename... Ts> struct Overloaded : Ts...
            {
                using Ts::operator()...;
            };
            template <typename... Ts> Overloaded(Ts...) -> Overloaded<Ts...>;

            [[nodiscard]] std::string_view ContextName(const Keire::VfxContextType context) noexcept
            {
                switch (context)
                {
                case Keire::VfxContextType::Spawn:
                    return "Spawn";
                case Keire::VfxContextType::Initialize:
                    return "Initialize";
                case Keire::VfxContextType::Update:
                    return "Update";
                case Keire::VfxContextType::Output:
                    return "Output";
                case Keire::VfxContextType::Event:
                    return "Event";
                }
                return "Unknown";
            }
        } // namespace

        [[nodiscard]] std::string_view ModuleName(const Keire::VfxModulePayload& payload)
        {
            return std::visit(
                Overloaded{
                    [](const Keire::VfxEmissionRateModule&) -> std::string_view { return "Emission Rate"; },
                    [](const Keire::VfxBurstModule&) -> std::string_view { return "Burst"; },
                    [](const Keire::VfxShapeModule&) -> std::string_view { return "Shape"; },
                    [](const Keire::VfxInitializeModule&) -> std::string_view { return "Initialize"; },
                    [](const Keire::VfxForceModule&) -> std::string_view { return "Forces"; },
                    [](const Keire::VfxSizeOverLifetimeModule&) -> std::string_view { return "Size over Lifetime"; },
                    [](const Keire::VfxColorOverLifetimeModule&) -> std::string_view { return "Color over Lifetime"; },
                    [](const Keire::VfxCollisionModule&) -> std::string_view { return "Collision"; },
                    [](const Keire::VfxRendererModule&) -> std::string_view { return "Renderer"; },
                    [](const Keire::VfxKillShapeModule&) -> std::string_view { return "Kill Shape"; },
                },
                payload);
        }

        [[nodiscard]] std::size_t BurstCount(const Keire::VfxEffectDefinition& definition)
        {
            return static_cast<std::size_t>(
                std::ranges::count_if(definition.Modules, [](const Keire::VfxModuleDefinition& module)
                                      { return std::holds_alternative<Keire::VfxBurstModule>(module.Payload); }));
        }

        [[nodiscard]] StableNodeId PreferredCanvasId(const Keire::AssetId id, const std::uint64_t salt) noexcept
        {
            return id.High() ^ std::rotl(id.Low(), 17) ^ salt;
        }

        [[nodiscard]] Keire::UiColor ContextColor(const Keire::VfxContextType context) noexcept
        {
            switch (context)
            {
            case Keire::VfxContextType::Spawn:
                return {0.10F, 0.48F, 0.48F, 1.0F};
            case Keire::VfxContextType::Initialize:
                return {0.42F, 0.27F, 0.62F, 1.0F};
            case Keire::VfxContextType::Update:
                return {0.18F, 0.38F, 0.68F, 1.0F};
            case Keire::VfxContextType::Output:
                return {0.72F, 0.38F, 0.14F, 1.0F};
            case Keire::VfxContextType::Event:
                return {0.64F, 0.20F, 0.30F, 1.0F};
            }
            return {0.2F, 0.24F, 0.3F, 1.0F};
        }

        [[nodiscard]] Keire::UiColor NodeColor(const Keire::VfxGraphNode& node) noexcept
        {
            switch (node.Kind)
            {
            case Keire::VfxGraphNodeKind::Context:
                return ContextColor(node.Context);
            case Keire::VfxGraphNodeKind::Module:
                return {0.14F, 0.42F, 0.68F, 1.0F};
            case Keire::VfxGraphNodeKind::Parameter:
                return {0.12F, 0.52F, 0.36F, 1.0F};
            case Keire::VfxGraphNodeKind::CustomHlsl:
                return {0.56F, 0.24F, 0.62F, 1.0F};
            case Keire::VfxGraphNodeKind::Operator:
                return {0.2F, 0.36F, 0.54F, 1.0F};
            case Keire::VfxGraphNodeKind::Attribute:
                return {0.16F, 0.48F, 0.38F, 1.0F};
            case Keire::VfxGraphNodeKind::Subgraph:
                return {0.42F, 0.28F, 0.58F, 1.0F};
            }
            return {0.2F, 0.24F, 0.3F, 1.0F};
        }

        [[nodiscard]] Keire::UiColor PinColor(const Keire::VfxValueType type) noexcept
        {
            switch (type)
            {
            case Keire::VfxValueType::Boolean:
                return {0.82F, 0.30F, 0.35F, 1.0F};
            case Keire::VfxValueType::Integer:
                return {0.30F, 0.74F, 0.48F, 1.0F};
            case Keire::VfxValueType::Scalar:
                return {0.42F, 0.82F, 0.52F, 1.0F};
            case Keire::VfxValueType::Vector2:
                return {0.95F, 0.62F, 0.22F, 1.0F};
            case Keire::VfxValueType::Vector3:
                return {0.96F, 0.46F, 0.24F, 1.0F};
            case Keire::VfxValueType::Color:
                return {0.86F, 0.38F, 0.88F, 1.0F};
            case Keire::VfxValueType::Texture:
                return {0.55F, 0.43F, 0.94F, 1.0F};
            case Keire::VfxValueType::Mesh:
                return {0.30F, 0.72F, 0.88F, 1.0F};
            case Keire::VfxValueType::Asset:
                return {0.45F, 0.58F, 0.86F, 1.0F};
            case Keire::VfxValueType::ParticleStream:
                return {0.28F, 0.72F, 1.0F, 1.0F};
            default:
                break;
            }
            return {0.56F, 0.62F, 0.72F, 1.0F};
        }

        [[nodiscard]] Keire::VfxGraphNode NewContextNode(const Keire::VfxContextType context,
                                                         const Keire::Vector2 position)
        {
            Keire::VfxGraphNode result;
            result.Id = Keire::AssetId::Generate();
            result.Type =
                context == Keire::VfxContextType::Event ? "OnPlay" : std::string(ContextName(context)) + " Context";
            result.Context = context;
            result.EditorPosition = position;
            if (context != Keire::VfxContextType::Spawn && context != Keire::VfxContextType::Event)
            {
                result.Pins.push_back(
                    {Keire::AssetId::Generate(), "Particles", Keire::VfxValueType::ParticleStream, true, "particles"});
            }
            if (context != Keire::VfxContextType::Output)
            {
                result.Pins.push_back(
                    {Keire::AssetId::Generate(), "Particles", Keire::VfxValueType::ParticleStream, false, "particles"});
            }
            return result;
        }

        [[nodiscard]] Keire::VfxGraphNode NewParameterNode(const Keire::VfxBlackboardParameter& parameter,
                                                           const Keire::Vector2 position)
        {
            Keire::VfxGraphNode result;
            result.Id = Keire::AssetId::Generate();
            result.Type = "Blackboard Parameter";
            result.TypeId = {"keire.parameter"};
            result.EditorPosition = position;
            result.Kind = Keire::VfxGraphNodeKind::Parameter;
            result.Reference = parameter.Id;
            result.Pins.push_back(
                {Keire::AssetId::Generate(), parameter.Name, parameter.Type, false, "value", std::nullopt});
            return result;
        }

        [[nodiscard]] Keire::VfxGraphNode NewCustomHlslNode(const Keire::Vector2 position)
        {
            Keire::VfxGraphNode result;
            result.Id = Keire::AssetId::Generate();
            result.Type = "Custom HLSL";
            result.TypeId = {"keire.operator.portable-hlsl"};
            result.Context = Keire::VfxContextType::Update;
            result.EditorPosition = position;
            result.Kind = Keire::VfxGraphNodeKind::CustomHlsl;
            result.Pins = {
                {Keire::AssetId::Generate(), "Particles", Keire::VfxValueType::ParticleStream, true, "particles"},
                {Keire::AssetId::Generate(), "Particles", Keire::VfxValueType::ParticleStream, false, "particles"},
            };
            result.CustomHlsl = "Size *= 1.0;";
            return result;
        }

        [[nodiscard]] std::string NodeLabel(const Keire::VfxEffectDefinition& definition,
                                            const Keire::VfxGraphNode& node)
        {
            if (node.Kind == Keire::VfxGraphNodeKind::Module)
            {
                const auto module =
                    std::ranges::find(definition.Modules, node.Reference, &Keire::VfxModuleDefinition::Id);
                return module == definition.Modules.end() ? "Missing Runtime Module"
                                                          : std::string(ModuleName(module->Payload));
            }
            if (node.Kind == Keire::VfxGraphNodeKind::Parameter)
            {
                const auto parameter =
                    std::ranges::find(definition.Blackboard, node.Reference, &Keire::VfxBlackboardParameter::Id);
                return parameter == definition.Blackboard.end() ? "Missing Blackboard Parameter" : parameter->Name;
            }
            return node.Type;
        }

        [[nodiscard]] bool ModuleRunsInContext(const Keire::VfxModulePayload& payload,
                                               const Keire::VfxContextType context)
        {
            return std::visit(
                Overloaded{
                    [context](const Keire::VfxEmissionRateModule&) { return context == Keire::VfxContextType::Spawn; },
                    [context](const Keire::VfxBurstModule&) { return context == Keire::VfxContextType::Spawn; },
                    [context](const Keire::VfxShapeModule&) { return context == Keire::VfxContextType::Initialize; },
                    [context](const Keire::VfxInitializeModule&)
                    { return context == Keire::VfxContextType::Initialize; },
                    [context](const Keire::VfxForceModule&) { return context == Keire::VfxContextType::Update; },
                    [context](const Keire::VfxSizeOverLifetimeModule&)
                    { return context == Keire::VfxContextType::Update; },
                    [context](const Keire::VfxColorOverLifetimeModule&)
                    { return context == Keire::VfxContextType::Update; },
                    [context](const Keire::VfxCollisionModule&) { return context == Keire::VfxContextType::Update; },
                    [context](const Keire::VfxRendererModule&) { return context == Keire::VfxContextType::Output; },
                    [context](const Keire::VfxKillShapeModule&) { return context == Keire::VfxContextType::Update; },
                },
                payload);
        }
    } // namespace Detail
} // namespace KeireEditor
