#include "Keire/Vfx/VfxSubgraph.h"
#include "Keire/Vfx/VfxSystem.h"

#include "KeireInternal/Vfx/VfxAssetCompilerInternal.h"
#include "KeireInternal/Vfx/VfxExpressionInternal.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <iterator>
#include <limits>
#include <locale>
#include <map>
#include <queue>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace Keire
{
    using Detail::ContextOrder;
    using Detail::FindPin;
    using Detail::IsPortableCustomValueType;
    using Detail::LoweredPlan;
    using Detail::MaximumAuthoredScalar;
    using Detail::MaximumDocumentBytes;
    using Detail::MaximumPortableCustomInstructions;
    using Detail::ModuleContext;
    using Detail::ModuleDefinitionVersion;
    using Detail::ModulePinSpecification;
    using Detail::ModulePinSpecifications;
    using Detail::ModuleTypeId;
    using Detail::ModuleTypeName;
    using Detail::ValueMatchesType;
    using Detail::VfxNodeCompileError;

    namespace
    {
        [[nodiscard]] std::string_view Trim(std::string_view value) noexcept
        {
            while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())) != 0)
                value.remove_prefix(1);
            while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())) != 0)
                value.remove_suffix(1);
            return value;
        }

        [[nodiscard]] bool IsIdentifier(const std::string_view value) noexcept
        {
            if (value.empty() || (std::isalpha(static_cast<unsigned char>(value.front())) == 0 && value.front() != '_'))
                return false;
            return std::ranges::all_of(
                value.substr(1), [](const char character)
                { return std::isalnum(static_cast<unsigned char>(character)) != 0 || character == '_'; });
        }
        [[nodiscard]] float ParsePortableFloat(std::string_view value)
        {
            value = Trim(value);
            if (!value.empty() && (value.back() == 'f' || value.back() == 'F'))
                value.remove_suffix(1);
            float result = 0.0F;
            std::istringstream parser{std::string(value)};
            parser.imbue(std::locale::classic());
            if (value.empty() || !(parser >> std::noskipws >> result) ||
                parser.peek() != std::char_traits<char>::eof() || !std::isfinite(result) ||
                std::abs(result) > MaximumAuthoredScalar)
                throw std::invalid_argument("Portable Custom HLSL contains an invalid numeric literal.");
            return result;
        }
        struct PortableLiteral
        {
            VfxValueType Type = VfxValueType::Scalar;
            VfxParameterValue Value = 0.0F;
        };

        [[nodiscard]] PortableLiteral ParsePortableLiteral(std::string_view value)
        {
            value = Trim(value);
            std::size_t componentCount = 0;
            if (value.starts_with("float2(") && value.ends_with(')'))
                componentCount = 2;
            else if (value.starts_with("float3(") && value.ends_with(')'))
                componentCount = 3;
            else if (value.starts_with("float4(") && value.ends_with(')'))
                componentCount = 4;
            else
                return {VfxValueType::Scalar, ParsePortableFloat(value)};

            value.remove_prefix(7);
            value.remove_suffix(1);
            std::array<float, 4> components{};
            for (std::size_t index = 0; index < componentCount; ++index)
            {
                const auto comma = value.find(',');
                if ((index + 1 < componentCount && comma == std::string_view::npos) ||
                    (index + 1 == componentCount && comma != std::string_view::npos))
                {
                    throw std::invalid_argument("Portable Custom HLSL vector literals have invalid arity.");
                }
                const auto component = index + 1 < componentCount ? value.substr(0, comma) : value;
                components[index] = ParsePortableFloat(component);
                if (index + 1 < componentCount)
                    value.remove_prefix(comma + 1);
            }
            if (componentCount == 2)
                return {VfxValueType::Vector2, Vector2{components[0], components[1]}};
            if (componentCount == 3)
                return {VfxValueType::Vector3, Vector3{components[0], components[1], components[2]}};
            return {VfxValueType::Color, Color{components[0], components[1], components[2], components[3]}};
        }

        struct PortableTarget
        {
            VfxCustomTarget Target = VfxCustomTarget::Velocity;
            VfxValueType Type = VfxValueType::Vector3;
        };

        [[nodiscard]] PortableTarget ParsePortableTarget(const std::string_view value)
        {
            if (value == "Position")
                return {VfxCustomTarget::Position, VfxValueType::Vector3};
            if (value == "Velocity")
                return {VfxCustomTarget::Velocity, VfxValueType::Vector3};
            if (value == "Rotation")
                return {VfxCustomTarget::Rotation, VfxValueType::Scalar};
            if (value == "Tint")
                return {VfxCustomTarget::Tint, VfxValueType::Color};
            if (value == "Size")
                return {VfxCustomTarget::Size, VfxValueType::Scalar};
            throw std::invalid_argument("Portable Custom HLSL targets Position, Velocity, Rotation, Tint, or Size.");
        }

        struct PortableInput
        {
            VfxValueType Type = VfxValueType::Scalar;
            std::uint32_t ParameterSlot = ~std::uint32_t{0};
            std::uint32_t ValueRegister = ~std::uint32_t{0};
            std::optional<VfxParameterValue> DefaultValue;
        };

        [[nodiscard]] bool PortableOperandMatches(const VfxValueType target, const VfxValueType operand) noexcept
        {
            return operand == VfxValueType::Scalar || operand == target;
        }

        [[nodiscard]] std::vector<VfxCompiledCustomInstruction>
        CompilePortableCustomHlsl(const VfxGraphNode& node, const std::map<std::string, PortableInput>& inputs)
        {
            std::vector<VfxCompiledCustomInstruction> result;
            std::size_t start = 0;
            while (start <= node.CustomHlsl.size())
            {
                const auto separator = node.CustomHlsl.find_first_of(";\r\n", start);
                auto statement =
                    Trim(std::string_view(node.CustomHlsl)
                             .substr(start, separator == std::string::npos ? std::string::npos : separator - start));
                start = separator == std::string::npos ? node.CustomHlsl.size() + 1 : separator + 1;
                if (statement.empty())
                    continue;
                if (result.size() >= MaximumPortableCustomInstructions)
                {
                    throw VfxNodeCompileError(
                        node.Id, "Portable Custom HLSL exceeds the 4096-instruction compiler safety limit.");
                }

                std::size_t operationPosition = statement.find("+=");
                auto operation = VfxCustomOperation::Add;
                std::size_t operationLength = 2;
                if (operationPosition == std::string_view::npos)
                {
                    operationPosition = statement.find("*=");
                    operation = VfxCustomOperation::Multiply;
                }
                if (operationPosition == std::string_view::npos)
                {
                    operationPosition = statement.find('=');
                    operation = VfxCustomOperation::Assign;
                    operationLength = 1;
                }
                if (operationPosition == std::string_view::npos)
                    throw std::invalid_argument("Portable Custom HLSL statement has no supported assignment.");

                const auto target = ParsePortableTarget(Trim(statement.substr(0, operationPosition)));
                auto operandText = Trim(statement.substr(operationPosition + operationLength));
                bool scaleByDeltaTime = false;
                constexpr std::string_view deltaTime = "DeltaTime";
                if (operandText.ends_with(deltaTime))
                {
                    auto prefix = Trim(operandText.substr(0, operandText.size() - deltaTime.size()));
                    if (prefix.empty() || prefix.back() != '*')
                        throw std::invalid_argument(
                            "Portable Custom HLSL uses DeltaTime in an unsupported expression.");
                    prefix.remove_suffix(1);
                    operandText = Trim(prefix);
                    scaleByDeltaTime = true;
                }
                if (operandText.empty())
                    throw std::invalid_argument("Portable Custom HLSL statement has an empty operand.");

                VfxCompiledCustomInstruction instruction;
                instruction.Node = node.Id;
                instruction.Context = node.Context;
                instruction.Target = target.Target;
                instruction.Operation = operation;
                instruction.ScaleByDeltaTime = scaleByDeltaTime;
                if (IsIdentifier(operandText))
                {
                    const auto input = inputs.find(std::string(operandText));
                    if (input == inputs.end())
                        throw std::invalid_argument("Portable Custom HLSL references an unknown input semantic.");
                    instruction.OperandType = input->second.Type;
                    instruction.ParameterSlot = input->second.ParameterSlot;
                    instruction.ValueRegister = input->second.ValueRegister;
                    if (instruction.ParameterSlot != ~std::uint32_t{0} &&
                        instruction.ValueRegister != ~std::uint32_t{0})
                    {
                        throw std::logic_error("Portable Custom HLSL input has multiple compiled value sources.");
                    }
                    if (instruction.ParameterSlot == ~std::uint32_t{0} &&
                        instruction.ValueRegister == ~std::uint32_t{0})
                    {
                        if (!input->second.DefaultValue)
                            throw std::invalid_argument(
                                "Portable Custom HLSL input must be connected or have a typed default.");
                        instruction.Literal = *input->second.DefaultValue;
                    }
                }
                else
                {
                    const auto literal = ParsePortableLiteral(operandText);
                    instruction.OperandType = literal.Type;
                    instruction.Literal = literal.Value;
                }
                if (!PortableOperandMatches(target.Type, instruction.OperandType))
                    throw std::invalid_argument("Portable Custom HLSL operand type does not match its target.");
                result.push_back(std::move(instruction));
            }
            if (result.empty())
                throw std::invalid_argument("Portable Custom HLSL nodes require at least one instruction.");
            return result;
        }

        [[nodiscard]] std::vector<VfxCompiledParameter>
        CompileParameters(const std::span<const VfxBlackboardParameter> blackboard)
        {
            std::vector<const VfxBlackboardParameter*> sorted;
            sorted.reserve(blackboard.size());
            for (const auto& parameter : blackboard)
                sorted.push_back(std::addressof(parameter));
            std::ranges::sort(sorted, {}, [](const VfxBlackboardParameter* parameter) { return parameter->Id; });

            std::vector<VfxCompiledParameter> result;
            result.reserve(sorted.size());
            for (std::uint32_t slot = 0; slot < sorted.size(); ++slot)
            {
                const auto& parameter = *sorted[slot];
                result.push_back({parameter.Id, parameter.Type, parameter.DefaultValue, slot, parameter.Exposed});
            }
            return result;
        }

        [[nodiscard]] LoweredPlan LowerLegacyModules(const VfxEffectDefinition& definition)
        {
            LoweredPlan result;
            result.Parameters = CompileParameters(definition.Blackboard);
            for (std::uint32_t index = 0; index < definition.Modules.size(); ++index)
            {
                const auto& module = definition.Modules[index];
                if (module.Enabled)
                {
                    const auto context = ModuleContext(module.Payload);
                    const auto operationIndex = static_cast<std::uint32_t>(result.Modules.size());
                    result.Modules.push_back({module.Id, module.Id, context, index});
                    result.Operations.push_back({module.Id, context, VfxCompiledOperationKind::Module, operationIndex});
                }
            }
            return result;
        }

        struct LocatedPin
        {
            const VfxGraphNode* Node = nullptr;
            const VfxGraphBlock* Block = nullptr;
            const VfxGraphPin* Pin = nullptr;
        };

        [[nodiscard]] std::size_t CountFlowPins(const VfxGraphNode& node, const bool input) noexcept
        {
            return static_cast<std::size_t>(
                std::ranges::count_if(node.Pins, [input](const VfxGraphPin& pin)
                                      { return pin.Input == input && pin.Type == VfxValueType::ParticleStream; }));
        }
        void ValidateContextNode(const VfxGraphNode& node)
        {
            const auto expectedInputs =
                node.Context == VfxContextType::Spawn || node.Context == VfxContextType::Event ? 0U : 1U;
            const auto expectedOutputs = node.Context == VfxContextType::Output ? 0U : 1U;
            if (node.Reference || !node.CustomHlsl.empty() || node.Pins.size() != expectedInputs + expectedOutputs ||
                CountFlowPins(node, true) != expectedInputs || CountFlowPins(node, false) != expectedOutputs)
            {
                throw std::invalid_argument("VFX context nodes require canonical particle-stream pins.");
            }
            for (const auto& pin : node.Pins)
                if (pin.Semantic != "particles")
                    throw std::invalid_argument("VFX context nodes require the particles pin semantic.");
        }

        void ValidateModuleNode(const VfxGraphNode& node, const VfxModuleDefinition& module)
        {
            const auto specifications = ModulePinSpecifications(module.Payload);
            if (!node.Reference || node.Context != ModuleContext(module.Payload) || !node.CustomHlsl.empty() ||
                CountFlowPins(node, true) != 1 || CountFlowPins(node, false) != 1 ||
                node.Pins.size() != specifications.size() + 2 ||
                node.DefinitionVersion != ModuleDefinitionVersion(module.Payload))
            {
                throw std::invalid_argument("VFX module node does not match its referenced Runtime Module.");
            }
            std::set<std::string> semantics;
            for (const auto& pin : node.Pins)
            {
                if (!semantics.insert(pin.Semantic).second && pin.Type != VfxValueType::ParticleStream)
                    throw std::invalid_argument("VFX module node contains a duplicate input semantic.");
                if (pin.Type == VfxValueType::ParticleStream)
                {
                    if (pin.Semantic != "particles" || pin.DefaultValue)
                        throw std::invalid_argument("VFX module flow pins are malformed.");
                    continue;
                }
                if (!pin.Input)
                    throw std::invalid_argument("VFX module property pins must be inputs.");
                const auto specification =
                    std::ranges::find(specifications, pin.Semantic, &ModulePinSpecification::Semantic);
                if (specification == specifications.end() || specification->Type != pin.Type || !pin.DefaultValue ||
                    !ValueMatchesType(pin.Type, *pin.DefaultValue) || *pin.DefaultValue != specification->DefaultValue)
                {
                    throw std::invalid_argument(
                        "VFX module node contains an unknown, stale, or type-mismatched property pin.");
                }
            }
            for (const auto& specification : specifications)
            {
                if (!FindPin(node, true, specification.Type, specification.Semantic))
                    throw std::invalid_argument("VFX module node is missing a canonical property pin.");
            }
        }

        void ValidateBlock(const VfxGraphNode& context, const VfxGraphBlock& block, const VfxModuleDefinition& module)
        {
            const auto specifications = ModulePinSpecifications(module.Payload);
            if (context.Kind != VfxGraphNodeKind::Context || !block.Reference ||
                context.Context != ModuleContext(module.Payload) ||
                block.TypeId.View() != ModuleTypeId(module.Payload) ||
                block.DefinitionVersion != ModuleDefinitionVersion(module.Payload) ||
                block.Type != ModuleTypeName(module.Payload) || block.Pins.size() != specifications.size())
            {
                throw std::invalid_argument("VFX Block does not match its referenced Runtime Module payload.");
            }
            std::set<std::string> semantics;
            for (const auto& pin : block.Pins)
            {
                if (!pin.Input || pin.Type == VfxValueType::ParticleStream || pin.DefaultValue == std::nullopt ||
                    !semantics.insert(pin.Semantic).second)
                {
                    throw std::invalid_argument("VFX Block contains a malformed data-input pin.");
                }
                const auto specification =
                    std::ranges::find(specifications, pin.Semantic, &ModulePinSpecification::Semantic);
                if (specification == specifications.end() || specification->Type != pin.Type ||
                    !ValueMatchesType(pin.Type, *pin.DefaultValue))
                {
                    throw std::invalid_argument("VFX Block contains an unknown or type-mismatched input pin.");
                }
            }
            for (const auto& specification : specifications)
            {
                const auto pin = std::ranges::find(block.Pins, specification.Semantic, &VfxGraphPin::Semantic);
                if (pin == block.Pins.end() || pin->Type != specification.Type)
                    throw std::invalid_argument("VFX Block is missing a canonical property input.");
            }
        }

        [[nodiscard]] const std::string& PortableBlockSource(const VfxGraphBlock& block)
        {
            if (block.Properties.size() != 1 || block.Properties.front().Name != "Source" ||
                !std::holds_alternative<std::string>(block.Properties.front().Value))
            {
                throw std::invalid_argument("Portable Custom HLSL Block source is missing or malformed.");
            }
            return std::get<std::string>(block.Properties.front().Value);
        }

        void ValidatePortableBlock(const VfxGraphNode& context, const VfxGraphBlock& block)
        {
            const auto& source = PortableBlockSource(block);
            if (context.Kind != VfxGraphNodeKind::Context || block.Reference ||
                block.TypeId.View() != "keire.block.portable-hlsl" || block.Type != "Portable Custom HLSL" ||
                block.DefinitionVersion != 1 || source.empty() || source.size() > MaximumDocumentBytes)
            {
                throw std::invalid_argument("Portable Custom HLSL Block is not canonical.");
            }
            std::set<std::string> semantics;
            for (const auto& pin : block.Pins)
            {
                if (!pin.Input || pin.Type == VfxValueType::ParticleStream || !IsIdentifier(pin.Semantic) ||
                    !semantics.insert(pin.Semantic).second || !IsPortableCustomValueType(pin.Type) ||
                    (pin.DefaultValue && !ValueMatchesType(pin.Type, *pin.DefaultValue)))
                {
                    throw std::invalid_argument("Portable Custom HLSL Block contains an invalid typed input pin.");
                }
            }
        }

        void ValidateParameterNode(const VfxGraphNode& node, const VfxBlackboardParameter& parameter)
        {
            if (!node.Reference || !node.CustomHlsl.empty() || node.Pins.size() != 1)
                throw std::invalid_argument("VFX parameter nodes require one canonical output pin.");
            const auto& pin = node.Pins.front();
            if (pin.Input || pin.Type != parameter.Type || pin.Type == VfxValueType::ParticleStream ||
                pin.Semantic != "value" || pin.DefaultValue)
            {
                throw std::invalid_argument("VFX parameter node output does not match its Blackboard parameter.");
            }
        }

        void ValidateOperatorNode(const VfxGraphNode& node, const VfxNodeDescriptor& descriptor,
                                  const VfxParticleDataType dataType)
        {
            if (node.Reference || !node.CustomHlsl.empty() || node.Type != descriptor.Label ||
                node.DefinitionVersion != descriptor.DefinitionVersion || node.Pins.size() != descriptor.Pins.size() ||
                node.Properties.size() != descriptor.Settings.size() ||
                std::ranges::find(descriptor.ValidContexts, node.Context) == descriptor.ValidContexts.end())
            {
                throw std::invalid_argument("VFX Operator node does not match its catalog descriptor.");
            }

            std::vector<VfxValueType> resolvedSignature;
            std::vector<AssetId> dynamicPinOrder;
            resolvedSignature.reserve(node.Pins.size());
            for (std::size_t index = 0; index < node.Pins.size(); ++index)
            {
                const auto& pin = node.Pins[index];
                const auto& expected = descriptor.Pins[index];
                if (pin.Name != expected.Name || pin.Semantic != expected.Semantic || pin.Type != expected.Type ||
                    pin.Input != expected.Input ||
                    (pin.Input && (!pin.DefaultValue || !ValueMatchesType(pin.Type, *pin.DefaultValue))) ||
                    (!pin.Input && pin.DefaultValue))
                {
                    throw std::invalid_argument("VFX Operator node pin signature is not canonical.");
                }
                resolvedSignature.push_back(pin.Type);
                if (descriptor.TypeBehavior == VfxNodeTypeBehavior::Cascaded && pin.Input)
                    dynamicPinOrder.push_back(pin.Id);
            }
            if (node.ResolvedSignature != resolvedSignature || node.DynamicPinOrder != dynamicPinOrder)
                throw std::invalid_argument("VFX Operator resolved signature is not canonical.");

            for (std::size_t index = 0; index < node.Properties.size(); ++index)
            {
                const auto& property = node.Properties[index];
                const auto& expected = descriptor.Settings[index];
                if (property.Name != expected.Name || property.Value.index() != expected.DefaultValue.index())
                    throw std::invalid_argument("VFX Operator settings do not match the catalog descriptor.");
                if (property.Name == "Scope")
                {
                    const auto scope = std::get<std::uint64_t>(property.Value);
                    if (scope > static_cast<std::uint64_t>(VfxRandomScope::PerParticleStrip))
                        throw std::invalid_argument("VFX Random scope is unsupported.");
                    if (scope == static_cast<std::uint64_t>(VfxRandomScope::PerParticleStrip) &&
                        dataType != VfxParticleDataType::ParticleStrip)
                    {
                        throw std::invalid_argument(
                            "VFX Random Per Particle Strip scope requires a Particle Strip system.");
                    }
                }
                if (property.Name == "Condition")
                {
                    static constexpr std::array<std::string_view, 6> conditions{
                        "Less", "Less Or Equal", "Equal", "Not Equal", "Greater Or Equal", "Greater"};
                    const auto& condition = std::get<std::string>(property.Value);
                    if (std::ranges::find(conditions, condition) == conditions.end())
                        throw std::invalid_argument("VFX Compare operator condition is unsupported.");
                }
            }
        }

        void ValidateCustomNode(const VfxGraphNode& node)
        {
            if (node.Reference || node.CustomHlsl.empty() || CountFlowPins(node, true) != 1 ||
                CountFlowPins(node, false) != 1)
            {
                throw std::invalid_argument("Portable Custom HLSL nodes require particle-stream input and output.");
            }
            std::set<std::string> semantics;
            for (const auto& pin : node.Pins)
            {
                if (pin.Type == VfxValueType::ParticleStream)
                {
                    if (pin.Semantic != "particles" || pin.DefaultValue)
                        throw std::invalid_argument("Portable Custom HLSL flow pins are malformed.");
                    continue;
                }
                if (!pin.Input || !IsIdentifier(pin.Semantic) || !semantics.insert(pin.Semantic).second ||
                    !IsPortableCustomValueType(pin.Type) ||
                    (pin.DefaultValue && !ValueMatchesType(pin.Type, *pin.DefaultValue)))
                {
                    throw std::invalid_argument("Portable Custom HLSL contains an invalid typed input pin.");
                }
            }
        }

        [[nodiscard]] LoweredPlan LowerGraphImpl(const VfxEffectDefinition& definition, const VfxGraphSystem& system,
                                                 const bool requirePublishable)
        {
            std::map<AssetId, std::pair<const VfxModuleDefinition*, std::uint32_t>> modules;
            for (std::uint32_t index = 0; index < definition.Modules.size(); ++index)
                modules.emplace(definition.Modules[index].Id,
                                std::pair{std::addressof(definition.Modules[index]), index});
            std::map<AssetId, const VfxBlackboardParameter*> parameters;
            for (const auto& parameter : definition.Blackboard)
                parameters.emplace(parameter.Id, std::addressof(parameter));

            LoweredPlan result;
            result.System = system.Id;
            result.DataType = system.DataType;
            result.ParticlesPerStrip =
                system.DataType == VfxParticleDataType::ParticleStrip ? system.ParticlesPerStrip : 1U;
            result.Parameters = CompileParameters(definition.Blackboard);
            std::map<AssetId, std::uint32_t> parameterSlots;
            for (const auto& parameter : result.Parameters)
                parameterSlots.emplace(parameter.Parameter, parameter.Slot);

            std::map<AssetId, const VfxGraphNode*> nodes;
            std::map<AssetId, LocatedPin> pins;
            std::array<const VfxGraphNode*, 4> contexts{};
            bool eventDriven = false;
            for (const auto& node : system.Nodes)
            {
                nodes.emplace(node.Id, std::addressof(node));
                for (const auto& pin : node.Pins)
                    pins.emplace(pin.Id, LocatedPin{std::addressof(node), nullptr, std::addressof(pin)});
                switch (node.Kind)
                {
                case VfxGraphNodeKind::Context:
                {
                    ValidateContextNode(node);
                    const auto index = ContextOrder(node.Context);
                    if (contexts[index])
                        throw std::invalid_argument(
                            "Executable VFX graphs require one Spawn or Event source and one context per later stage.");
                    contexts[index] = std::addressof(node);
                    if (node.Context == VfxContextType::Event)
                    {
                        if (!node.Blocks.empty())
                            throw std::invalid_argument("VFX Event contexts cannot contain particle Blocks.");
                        eventDriven = true;
                        result.EventName = node.Type;
                        if (result.EventName.empty())
                            throw std::invalid_argument("VFX Event contexts require a non-empty event name.");
                    }
                    for (const auto& block : node.Blocks)
                    {
                        if (block.TypeId.View() == "keire.block.portable-hlsl")
                        {
                            ValidatePortableBlock(node, block);
                        }
                        else
                        {
                            const auto module = modules.find(block.Reference);
                            if (module == modules.end())
                                throw std::invalid_argument("VFX Block has an unknown Runtime Module reference.");
                            ValidateBlock(node, block, *module->second.first);
                        }
                        for (const auto& pin : block.Pins)
                            pins.emplace(pin.Id,
                                         LocatedPin{std::addressof(node), std::addressof(block), std::addressof(pin)});
                    }
                    break;
                }
                case VfxGraphNodeKind::Module:
                {
                    const auto module = modules.find(node.Reference);
                    if (module == modules.end())
                        throw std::invalid_argument("VFX module node has an unknown Runtime Module reference.");
                    ValidateModuleNode(node, *module->second.first);
                    break;
                }
                case VfxGraphNodeKind::Parameter:
                {
                    const auto parameter = parameters.find(node.Reference);
                    if (parameter == parameters.end())
                        throw std::invalid_argument("VFX parameter node references an unknown Blackboard parameter.");
                    ValidateParameterNode(node, *parameter->second);
                    break;
                }
                case VfxGraphNodeKind::CustomHlsl:
                    ValidateCustomNode(node);
                    break;
                case VfxGraphNodeKind::Operator:
                {
                    const auto* descriptor = FindVfxNodeDescriptor(node.TypeId.View());
                    if (!descriptor || descriptor->Class != VfxNodeClass::Operator || !descriptor->Lowering)
                        throw std::invalid_argument("VFX graph contains an unknown executable Operator type ID.");
                    if (descriptor->SupportTier == VfxNodeSupportTier::Disabled)
                        throw std::invalid_argument("VFX Operator is disabled: " + descriptor->DisabledReason);
                    ValidateOperatorNode(node, *descriptor, system.DataType);
                    break;
                }
                case VfxGraphNodeKind::Attribute:
                    throw std::invalid_argument("VFX Attribute nodes are not executable in this production tier.");
                case VfxGraphNodeKind::Subgraph:
                    throw std::invalid_argument("VFX Subgraph nodes are not executable in this production tier.");
                }
            }
            if (std::ranges::any_of(contexts, [](const VfxGraphNode* context) { return context == nullptr; }))
                throw std::invalid_argument(
                    "Executable VFX graphs require Spawn, Initialize, Update, and Output contexts.");

            std::map<AssetId, const VfxGraphConnection*> inputDrivers;
            std::map<AssetId, std::size_t> indegree;
            std::map<AssetId, std::vector<AssetId>> adjacency;
            std::map<AssetId, std::vector<AssetId>> flowAdjacency;
            std::map<AssetId, std::vector<AssetId>> reverseFlowAdjacency;
            for (const auto& node : system.Nodes)
                indegree.emplace(node.Id, 0);
            for (const auto& connection : system.Connections)
            {
                const auto output = pins.find(connection.OutputPin);
                const auto input = pins.find(connection.InputPin);
                if (output == pins.end() || input == pins.end() || output->second.Node->Id != connection.OutputNode ||
                    input->second.Node->Id != connection.InputNode ||
                    (output->second.Block ? output->second.Block->Id : AssetId{}) != connection.OutputBlock ||
                    (input->second.Block ? input->second.Block->Id : AssetId{}) != connection.InputBlock)
                {
                    throw std::invalid_argument("VFX graph connection references a mismatched node or pin.");
                }
                if (!inputDrivers.emplace(connection.InputPin, std::addressof(connection)).second)
                    throw std::invalid_argument("VFX graph input pins may have at most one cable.");
                ++indegree.at(connection.InputNode);
                adjacency[connection.OutputNode].push_back(connection.InputNode);
                if (output->second.Pin->Type == VfxValueType::ParticleStream)
                {
                    if (ContextOrder(output->second.Node->Context) > ContextOrder(input->second.Node->Context))
                        throw std::invalid_argument(
                            "VFX particle-stream cables cannot travel backwards across contexts.");
                    flowAdjacency[connection.OutputNode].push_back(connection.InputNode);
                    reverseFlowAdjacency[connection.InputNode].push_back(connection.OutputNode);
                }
                else if ((output->second.Node->Kind == VfxGraphNodeKind::Operator ||
                          input->second.Node->Kind == VfxGraphNodeKind::Operator) &&
                         output->second.Node->Kind != VfxGraphNodeKind::Parameter &&
                         output->second.Node->Context != input->second.Node->Context)
                {
                    throw std::invalid_argument(
                        "VFX Operator cables must remain in one evaluation context in this production tier.");
                }
            }

            std::set<AssetId> ready;
            for (const auto& [node, count] : indegree)
                if (count == 0)
                    ready.insert(node);
            std::vector<AssetId> topologicalOrder;
            topologicalOrder.reserve(nodes.size());
            while (!ready.empty())
            {
                const auto node = *ready.begin();
                ready.erase(ready.begin());
                topologicalOrder.push_back(node);
                for (const auto destination : adjacency[node])
                {
                    auto& count = indegree.at(destination);
                    if (--count == 0)
                        ready.insert(destination);
                }
            }
            if (topologicalOrder.size() != nodes.size())
                throw std::invalid_argument("VFX graph must be a directed acyclic graph.");

            const auto visitFlow = [](const AssetId start, const std::map<AssetId, std::vector<AssetId>>& edges)
            {
                std::set<AssetId> visited;
                std::queue<AssetId> pending;
                visited.insert(start);
                pending.push(start);
                while (!pending.empty())
                {
                    const auto node = pending.front();
                    pending.pop();
                    const auto destinations = edges.find(node);
                    if (destinations == edges.end())
                        continue;
                    for (const auto destination : destinations->second)
                    {
                        if (visited.insert(destination).second)
                            pending.push(destination);
                    }
                }
                return visited;
            };
            const auto fromSpawn = visitFlow(contexts[0]->Id, flowAdjacency);
            const auto toOutput = visitFlow(contexts[3]->Id, reverseFlowAdjacency);
            if (requirePublishable)
            {
                for (const auto* context : contexts)
                {
                    if (!fromSpawn.contains(context->Id) || !toOutput.contains(context->Id))
                    {
                        throw std::invalid_argument("VFX contexts must share one connected particle-stream path.");
                    }
                }
            }

            std::vector<AssetId> requiredExpressionOutputs;
            for (const auto& connection : system.Connections)
            {
                const auto& outputNode = *nodes.at(connection.OutputNode);
                const auto& inputNode = *nodes.at(connection.InputNode);
                const auto& input = pins.at(connection.InputPin);
                const bool connected = fromSpawn.contains(inputNode.Id) && toOutput.contains(inputNode.Id);
                bool executableConsumer = false;
                if (input.Block)
                    executableConsumer = connected && input.Block->Enabled;
                else if (inputNode.Kind == VfxGraphNodeKind::Module)
                    executableConsumer = connected && modules.at(inputNode.Reference).first->Enabled;
                if (outputNode.Kind == VfxGraphNodeKind::Operator && executableConsumer)
                    requiredExpressionOutputs.push_back(connection.OutputPin);
            }
            std::ranges::sort(requiredExpressionOutputs);
            requiredExpressionOutputs.erase(
                std::unique(requiredExpressionOutputs.begin(), requiredExpressionOutputs.end()),
                requiredExpressionOutputs.end());
            auto expressions = Internal::CompileVfxExpressions(system, parameterSlots, requiredExpressionOutputs);
            result.ValueInstructions = std::move(expressions.Instructions);
            result.ValueRegisterCount = expressions.RegisterCount;

            bool hasEmission = false;
            bool hasRenderer = false;
            const auto lowerModule = [&](const VfxModuleDefinition& module, const std::uint32_t moduleIndex,
                                         const AssetId executionId, const VfxContextType context,
                                         const std::span<const VfxGraphPin> propertyPins, const bool connected,
                                         const bool enabled)
            {
                if (!enabled || !connected)
                    return;
                const auto operationIndex = static_cast<std::uint32_t>(result.Modules.size());
                result.Modules.push_back({executionId, module.Id, context, moduleIndex});
                result.Operations.push_back({executionId, context, VfxCompiledOperationKind::Module, operationIndex});
                hasEmission |= std::holds_alternative<VfxEmissionRateModule>(module.Payload) ||
                               std::holds_alternative<VfxBurstModule>(module.Payload);
                hasRenderer |= std::holds_alternative<VfxRendererModule>(module.Payload);

                const auto specifications = ModulePinSpecifications(module.Payload);
                for (const auto& specification : specifications)
                {
                    const auto input = std::ranges::find(propertyPins, specification.Semantic, &VfxGraphPin::Semantic);
                    if (input == propertyPins.end())
                        throw std::logic_error("Canonical VFX module property input is unavailable.");
                    const auto driver = inputDrivers.find(input->Id);
                    VfxCompiledBinding binding;
                    binding.Node = executionId;
                    binding.Module = module.Id;
                    binding.Property = specification.Property;
                    binding.Type = specification.Type;
                    if (driver == inputDrivers.end())
                    {
                        if (!input->DefaultValue)
                            throw std::logic_error("Canonical VFX module property input has no inline value.");
                        if (*input->DefaultValue == specification.DefaultValue)
                            continue;
                        binding.LiteralValue = *input->DefaultValue;
                    }
                    else
                    {
                        const auto& source = *nodes.at(driver->second->OutputNode);
                        const auto& output = *pins.at(driver->second->OutputPin).Pin;
                        if (output.Type != specification.Type)
                            throw std::invalid_argument("VFX module binding type does not match its property.");
                        if (source.Kind == VfxGraphNodeKind::Parameter)
                            binding.ParameterSlot = parameterSlots.at(source.Reference);
                        else if (source.Kind == VfxGraphNodeKind::Operator)
                        {
                            const auto expression = expressions.SourcesByOutputPin.find(driver->second->OutputPin);
                            if (expression == expressions.SourcesByOutputPin.end())
                                throw std::logic_error("VFX Operator binding was not lowered.");
                            if (expression->second.Kind == VfxCompiledValueSourceKind::Parameter)
                                binding.ParameterSlot = expression->second.Index;
                            else if (expression->second.Kind == VfxCompiledValueSourceKind::Register)
                                binding.ValueRegister = expression->second.Index;
                            else
                                binding.LiteralValue = expression->second.Literal;
                        }
                        else
                            throw std::invalid_argument(
                                "VFX module properties require a Blackboard or executable Operator source.");
                    }
                    result.Bindings.push_back(std::move(binding));
                }
            };
            const auto lowerPortable = [&](const AssetId executionId, const VfxContextType context,
                                           const std::span<const VfxGraphPin> inputPins, const std::string_view source,
                                           const bool connected, const bool enabled)
            {
                if (!enabled || !connected)
                    return;
                std::map<std::string, PortableInput> inputs;
                for (const auto& input : inputPins)
                {
                    if (!input.Input || input.Type == VfxValueType::ParticleStream)
                        continue;
                    PortableInput value;
                    value.Type = input.Type;
                    value.DefaultValue = input.DefaultValue;
                    const auto driver = inputDrivers.find(input.Id);
                    if (driver != inputDrivers.end())
                    {
                        const auto& outputNode = *nodes.at(driver->second->OutputNode);
                        if (outputNode.Kind == VfxGraphNodeKind::Parameter)
                        {
                            value.ParameterSlot = parameterSlots.at(outputNode.Reference);
                        }
                        else if (outputNode.Kind == VfxGraphNodeKind::Operator)
                        {
                            const auto expression = expressions.SourcesByOutputPin.find(driver->second->OutputPin);
                            if (expression == expressions.SourcesByOutputPin.end())
                                throw std::logic_error("Portable Custom HLSL Operator input was not lowered.");
                            switch (expression->second.Kind)
                            {
                            case VfxCompiledValueSourceKind::Literal:
                                value.DefaultValue = expression->second.Literal;
                                break;
                            case VfxCompiledValueSourceKind::Parameter:
                                value.ParameterSlot = expression->second.Index;
                                break;
                            case VfxCompiledValueSourceKind::Register:
                                value.ValueRegister = expression->second.Index;
                                break;
                            }
                        }
                        else
                        {
                            throw std::invalid_argument(
                                "Portable Custom HLSL inputs require a Blackboard or executable Operator source.");
                        }
                    }
                    inputs.emplace(input.Semantic, std::move(value));
                }
                VfxGraphNode portable;
                portable.Id = executionId;
                portable.Context = context;
                portable.CustomHlsl = source;
                std::vector<VfxCompiledCustomInstruction> instructions;
                try
                {
                    instructions = CompilePortableCustomHlsl(portable, inputs);
                }
                catch (const VfxNodeCompileError&)
                {
                    throw;
                }
                catch (const std::exception& error)
                {
                    throw VfxNodeCompileError(executionId, error.what());
                }
                if (instructions.size() > MaximumPortableCustomInstructions - result.CustomInstructions.size())
                {
                    throw VfxNodeCompileError(executionId,
                                              "VFX graph exceeds the 4096-instruction Portable Custom HLSL compiler "
                                              "safety limit.");
                }
                for (auto& instruction : instructions)
                {
                    const auto operationIndex = static_cast<std::uint32_t>(result.CustomInstructions.size());
                    result.CustomInstructions.push_back(std::move(instruction));
                    result.Operations.push_back(
                        {executionId, context, VfxCompiledOperationKind::CustomHlsl, operationIndex});
                }
            };
            for (const auto nodeId : topologicalOrder)
            {
                const auto& node = *nodes.at(nodeId);
                if (node.Kind == VfxGraphNodeKind::Context)
                {
                    const bool connected = fromSpawn.contains(node.Id) && toOutput.contains(node.Id);
                    for (const auto& block : node.Blocks)
                    {
                        if (block.TypeId.View() == "keire.block.portable-hlsl")
                            lowerPortable(block.Id, node.Context, block.Pins, PortableBlockSource(block), connected,
                                          block.Enabled);
                        else
                        {
                            const auto [module, index] = modules.at(block.Reference);
                            lowerModule(*module, index, block.Id, node.Context, block.Pins, connected, block.Enabled);
                        }
                    }
                    continue;
                }
                if (node.Kind != VfxGraphNodeKind::Module && node.Kind != VfxGraphNodeKind::CustomHlsl)
                    continue;
                const bool connected = fromSpawn.contains(node.Id) && toOutput.contains(node.Id);
                if (requirePublishable && !connected)
                    throw std::invalid_argument("Executable VFX nodes must be connected to the main particle stream.");
                if (node.Kind == VfxGraphNodeKind::Module)
                {
                    const auto [module, index] = modules.at(node.Reference);
                    lowerModule(*module, index, node.Id, node.Context, node.Pins, connected, module->Enabled);
                }
                else
                {
                    lowerPortable(node.Id, node.Context, node.Pins, node.CustomHlsl, connected, true);
                }
            }
            if (requirePublishable && ((!eventDriven && !hasEmission) || !hasRenderer))
            {
                throw std::invalid_argument(
                    "Executable VFX graphs require connected emission (unless Event-driven) and renderer modules.");
            }
            return result;
        }

        [[nodiscard]] LoweredPlan LowerEffectImpl(const VfxEffectDefinition& definition, const VfxGraphSystem* system)
        {
            if (definition.ExecutionSource == VfxExecutionSource::Graph)
            {
                if (!system)
                    throw std::invalid_argument("VFX graph compilation requires a particle system.");
                return LowerGraphImpl(definition, *system, true);
            }
            return LowerLegacyModules(definition);
        }
    } // namespace

    namespace Detail
    {
        LoweredPlan LowerGraph(const VfxEffectDefinition& definition, const VfxGraphSystem& system,
                               const bool requirePublishable)
        {
            return LowerGraphImpl(definition, system, requirePublishable);
        }

        LoweredPlan LowerEffect(const VfxEffectDefinition& definition, const VfxGraphSystem* system)
        {
            return LowerEffectImpl(definition, system);
        }
    } // namespace Detail
} // namespace Keire
