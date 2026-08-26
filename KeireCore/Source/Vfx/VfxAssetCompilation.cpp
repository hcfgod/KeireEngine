#include "Keire/Vfx/VfxSubgraph.h"
#include "Keire/Vfx/VfxSystem.h"

#include "KeireInternal/Vfx/VfxAssetCompilerInternal.h"
#include "KeireInternal/Vfx/VfxExecutionInternal.h"
#include "KeireInternal/Vfx/VfxExpressionInternal.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>

namespace Keire
{
    using Detail::BuildCanonicalIr;
    using Detail::BuildStateLayoutHash;
    using Detail::HashBytes;
    using Detail::LowerEffect;
    using Detail::MaximumBurstCycles;
    using Detail::Overloaded;
    using Detail::UsesStrictSchemaFourCapabilities;
    using Detail::VfxNodeCompileError;

    namespace
    {
        [[nodiscard]] bool IsGpuValueType(const VfxValueType type) noexcept
        {
            return IsVfxGpuExpressionValueType(type);
        }

        [[nodiscard]] std::array<std::uint32_t, 4> GpuIdentity(const AssetId id) noexcept
        {
            return {static_cast<std::uint32_t>(id.High()), static_cast<std::uint32_t>(id.High() >> 32U),
                    static_cast<std::uint32_t>(id.Low()), static_cast<std::uint32_t>(id.Low() >> 32U)};
        }

        [[nodiscard]] VfxCompiledGpuValueProgram
        BuildGpuValueProgram(const std::span<const VfxCompiledValueInstruction> instructions,
                             const std::uint32_t registerCount, const std::uint32_t parameterCount,
                             const AssetId system)
        {
            VfxCompiledGpuValueProgram result;
            result.SystemIdentity = GpuIdentity(system);
            result.RegisterCount = registerCount;
            if (registerCount > VfxCompiledGpuValueProgram::MaximumRegisters)
            {
                const auto instruction = std::ranges::find_if(
                    instructions, [](const VfxCompiledValueInstruction& candidate)
                    { return candidate.OutputRegister >= VfxCompiledGpuValueProgram::MaximumRegisters; });
                throw VfxNodeCompileError(instruction == instructions.end() ? AssetId{} : instruction->Node,
                                          "VFX GPU expression program exceeds the 64-register shader limit.");
            }
            result.Instructions.reserve(instructions.size());
            result.Sources.reserve(
                std::min<std::size_t>(instructions.size() * 2, VfxCompiledGpuValueProgram::MaximumSources));

            for (const auto& instruction : instructions)
            {
                if (result.Instructions.size() >= VfxCompiledGpuValueProgram::MaximumInstructions)
                {
                    throw VfxNodeCompileError(instruction.Node,
                                              "VFX GPU expression program exceeds the 64-instruction shader limit.");
                }
                if (!IsGpuValueType(instruction.Type))
                {
                    throw VfxNodeCompileError(instruction.Node,
                                              "This VFX value type has no packed GPU register representation.");
                }
                if (instruction.OutputRegister >= registerCount || instruction.Inputs.size() > 8)
                    throw VfxNodeCompileError(instruction.Node, "VFX GPU expression instruction layout is invalid.");

                VfxGpuValueInstruction packed;
                packed.Header = {
                    static_cast<std::uint32_t>(instruction.Opcode), static_cast<std::uint32_t>(instruction.Type),
                    static_cast<std::uint32_t>(instruction.Context), static_cast<std::uint32_t>(instruction.Domain)};
                packed.Output = {instruction.OutputRegister, instruction.OutputIndex,
                                 static_cast<std::uint32_t>(result.Sources.size()),
                                 static_cast<std::uint32_t>(instruction.Inputs.size())};
                std::uint32_t flags = 0;
                if (instruction.ConstantRandom)
                    flags |= static_cast<std::uint32_t>(VfxGpuValueInstructionFlag::ConstantRandom);
                if (instruction.IndependentRandomChannels)
                    flags |= static_cast<std::uint32_t>(VfxGpuValueInstructionFlag::IndependentRandomChannels);
                if (instruction.InclusiveMaximum)
                    flags |= static_cast<std::uint32_t>(VfxGpuValueInstructionFlag::InclusiveMaximum);
                if (instruction.ClampRemap)
                    flags |= static_cast<std::uint32_t>(VfxGpuValueInstructionFlag::ClampRemap);
                packed.Settings = {instruction.ChannelSalt, static_cast<std::uint32_t>(instruction.RandomScope), flags,
                                   static_cast<std::uint32_t>(instruction.Comparison)};
                packed.NodeIdentity = GpuIdentity(instruction.Node);

                for (const auto& source : instruction.Inputs)
                {
                    if (result.Sources.size() >= VfxCompiledGpuValueProgram::MaximumSources)
                    {
                        throw VfxNodeCompileError(instruction.Node,
                                                  "VFX GPU expression program exceeds the 256-source shader limit.");
                    }
                    if (!IsGpuValueType(source.Type))
                    {
                        throw VfxNodeCompileError(instruction.Node,
                                                  "This VFX input type has no packed GPU value representation.");
                    }

                    VfxGpuValueSource packedSource;
                    packedSource.Type = static_cast<std::uint32_t>(source.Type);
                    switch (source.Kind)
                    {
                    case VfxCompiledValueSourceKind::Literal:
                    {
                        VfxGpuValue value;
                        if (!Internal::PackVfxGpuValue(source.Type, source.Literal, value))
                        {
                            throw VfxNodeCompileError(instruction.Node,
                                                      "VFX literal cannot be represented by the GPU value ABI.");
                        }
                        const auto found = std::ranges::find(result.Constants, value);
                        if (found == result.Constants.end())
                        {
                            if (result.Constants.size() >= VfxCompiledGpuValueProgram::MaximumConstants)
                            {
                                throw VfxNodeCompileError(
                                    instruction.Node,
                                    "VFX GPU expression program exceeds the 256-constant shader limit.");
                            }
                            packedSource.Index = static_cast<std::uint32_t>(result.Constants.size());
                            result.Constants.push_back(value);
                        }
                        else
                        {
                            packedSource.Index =
                                static_cast<std::uint32_t>(std::distance(result.Constants.begin(), found));
                        }
                        packedSource.Kind = static_cast<std::uint32_t>(VfxGpuValueSourceKind::Literal);
                        break;
                    }
                    case VfxCompiledValueSourceKind::Parameter:
                        if (source.Index >= parameterCount)
                            throw VfxNodeCompileError(instruction.Node,
                                                      "VFX GPU expression parameter source is invalid.");
                        packedSource.Kind = static_cast<std::uint32_t>(VfxGpuValueSourceKind::Parameter);
                        packedSource.Index = source.Index;
                        break;
                    case VfxCompiledValueSourceKind::Register:
                        if (source.Index >= registerCount)
                            throw VfxNodeCompileError(instruction.Node,
                                                      "VFX GPU expression register source is invalid.");
                        packedSource.Kind = static_cast<std::uint32_t>(VfxGpuValueSourceKind::Register);
                        packedSource.Index = source.Index;
                        break;
                    }
                    result.Sources.push_back(packedSource);
                }
                result.Instructions.push_back(packed);
            }
            return result;
        }

        void AppendGpuCapabilityDiagnostics(const VfxEffectDefinition& executable, const VfxCompiledProgram& program,
                                            const bool strictSchemaFour, std::vector<VfxCompileDiagnostic>& diagnostics)
        {
            const auto error = [&diagnostics, strictSchemaFour](const AssetId node, std::string message)
            {
                diagnostics.push_back(
                    {strictSchemaFour ? VfxCompileDiagnosticSeverity::Error : VfxCompileDiagnosticSeverity::Warning,
                     node, std::move(message)});
            };
            const auto hardError = [&diagnostics](const AssetId node, std::string message)
            { diagnostics.push_back({VfxCompileDiagnosticSeverity::Error, node, std::move(message)}); };
            std::size_t rendererCount = 0;
            const auto resolveModule = [&executable](const VfxCompiledModule& compiled) -> const VfxModuleDefinition&
            {
                const auto module = std::ranges::find(executable.Modules, compiled.Node, &VfxModuleDefinition::Id);
                if (module == executable.Modules.end())
                    throw std::logic_error("VFX resolved GPU module layout is invalid.");
                return *module;
            };
            const VfxRendererModule* renderer = nullptr;
            for (const auto& compiledModule : program.Modules)
            {
                const auto& executableModule = resolveModule(compiledModule);
                const auto node = compiledModule.Node;
                std::visit(
                    Overloaded{
                        [](const VfxShapeModule&) {},
                        [](const VfxInitializeModule&) {},
                        [](const VfxSizeOverLifetimeModule&) {},
                        [](const VfxColorOverLifetimeModule&) {},
                        [](const VfxForceModule&) {},
                        [&](const VfxCollisionModule& value)
                        {
                            if (value.Mode == VfxCollisionMode::Cpu || value.Mode == VfxCollisionMode::ScenePhysics)
                            {
                                error(node, "GPU VFX supports None or GPU Depth collision; CPU and Scene Physics "
                                            "queries require the CPU backend.");
                            }
                        },
                        [](const VfxKillShapeModule&) {},
                        [&](const VfxRendererModule& value)
                        {
                            if (!renderer)
                                renderer = std::addressof(value);
                            if (++rendererCount > 1)
                            {
                                hardError(compiledModule.Node,
                                          "GPU VFX currently supports one Renderer Block per particle system.");
                            }
                            if (value.Type == VfxRendererType::Ribbon &&
                                program.DataType != VfxParticleDataType::ParticleStrip)
                            {
                                hardError(compiledModule.Node, "Ribbon output requires a Particle Strip system.");
                            }
                            (void)node;
                        },
                        [](const auto&) {},
                    },
                    executableModule.Payload);
            }

            const auto spriteOutput = renderer && renderer->Type == VfxRendererType::Sprite;
            if (spriteOutput)
            {
                for (const auto& binding : program.Bindings)
                {
                    const auto rotation = binding.Property == VfxModuleProperty::InitializeRotationMinimum ||
                                          binding.Property == VfxModuleProperty::InitializeRotationMaximum;
                    if (rotation && binding.ValueRegister != ~std::uint32_t{0})
                    {
                        error(binding.Node,
                              "GPU VFX cannot prove that a dynamic Sprite rotation contains only a Z component; "
                              "use literal or Blackboard Z-only values.");
                    }
                }
                for (const auto& compiledModule : program.Modules)
                {
                    const auto& executableModule = resolveModule(compiledModule);
                    const auto* initialize = std::get_if<VfxInitializeModule>(&executableModule.Payload);
                    if (!initialize)
                        continue;
                    if (initialize->RotationMinimum.X != 0.0F || initialize->RotationMinimum.Y != 0.0F ||
                        initialize->RotationMaximum.X != 0.0F || initialize->RotationMaximum.Y != 0.0F)
                    {
                        error(compiledModule.Node,
                              "GPU VFX Sprite output supports Z-axis initialization rotation only; X/Y rotation "
                              "requires Mesh output.");
                    }
                }
            }
        }

        void AppendCpuCapabilityDiagnostics(const VfxEffectDefinition& executable, const VfxCompiledProgram& program,
                                            const bool strictSchemaFour, std::vector<VfxCompileDiagnostic>& diagnostics)
        {
            const auto unsupported = [&diagnostics, strictSchemaFour](const AssetId node, std::string message)
            {
                diagnostics.push_back(
                    {strictSchemaFour ? VfxCompileDiagnosticSeverity::Error : VfxCompileDiagnosticSeverity::Warning,
                     node, std::move(message)});
            };
            const auto hardError = [&diagnostics](const AssetId node, std::string message)
            { diagnostics.push_back({VfxCompileDiagnosticSeverity::Error, node, std::move(message)}); };

            const VfxRendererModule* renderer = nullptr;
            std::size_t rendererCount = 0;
            if (executable.Modules.size() != program.Modules.size())
                throw std::logic_error("VFX resolved CPU module layout is invalid.");
            for (std::size_t index = 0; index < program.Modules.size(); ++index)
            {
                const auto& compiledModule = program.Modules[index];
                if (executable.Modules[index].Id != compiledModule.Node)
                    throw std::logic_error("VFX resolved CPU module layout is invalid.");
                const auto* candidate = std::get_if<VfxRendererModule>(&executable.Modules[index].Payload);
                if (!candidate)
                    continue;
                ++rendererCount;
                if (!renderer)
                    renderer = candidate;
                if (rendererCount > 1)
                {
                    hardError(compiledModule.Node,
                              "CPU VFX currently supports one Renderer Block per particle system.");
                }
                if (candidate->Type == VfxRendererType::Ribbon &&
                    program.DataType != VfxParticleDataType::ParticleStrip)
                    hardError(compiledModule.Node, "Ribbon output requires a Particle Strip system.");
            }

            if (!renderer || renderer->Type != VfxRendererType::Sprite)
                return;
            for (const auto& binding : program.Bindings)
            {
                const auto rotation = binding.Property == VfxModuleProperty::InitializeRotationMinimum ||
                                      binding.Property == VfxModuleProperty::InitializeRotationMaximum;
                if (rotation && binding.ValueRegister != ~std::uint32_t{0})
                {
                    unsupported(binding.Node,
                                "CPU VFX cannot prove that a dynamic Sprite rotation contains only a Z component; "
                                "use literal or Blackboard Z-only values.");
                }
            }
            for (std::size_t index = 0; index < program.Modules.size(); ++index)
            {
                const auto& compiledModule = program.Modules[index];
                const auto* initialize = std::get_if<VfxInitializeModule>(&executable.Modules[index].Payload);
                if (!initialize)
                    continue;
                if (initialize->RotationMinimum.X != 0.0F || initialize->RotationMinimum.Y != 0.0F ||
                    initialize->RotationMaximum.X != 0.0F || initialize->RotationMaximum.Y != 0.0F)
                {
                    unsupported(compiledModule.Node,
                                "CPU VFX Sprite output supports Z-axis initialization rotation only; X/Y rotation "
                                "requires Mesh output.");
                }
            }
        }
    } // namespace

    namespace Internal
    {
        void ValidateVfxResolvedBackendCapabilities(const VfxEffectDefinition& definition,
                                                    const VfxCompiledProgram& program, const VfxBackend backend,
                                                    const bool strictSchemaFour)
        {
            std::vector<VfxCompileDiagnostic> diagnostics;
            if (backend == VfxBackend::Gpu)
            {
                AppendGpuCapabilityDiagnostics(definition, program, strictSchemaFour, diagnostics);
            }
            else
            {
                AppendCpuCapabilityDiagnostics(definition, program, strictSchemaFour, diagnostics);
            }
            const auto failure =
                std::ranges::find(diagnostics, VfxCompileDiagnosticSeverity::Error, &VfxCompileDiagnostic::Severity);
            if (failure != diagnostics.end())
                throw std::invalid_argument(failure->Message);
        }

        void ApplyVfxModuleProperty(VfxModuleDefinition& module, const VfxModuleProperty property,
                                    const VfxParameterValue& value)
        {
            switch (property)
            {
            case VfxModuleProperty::EmissionParticlesPerSecond:
                std::get<VfxEmissionRateModule>(module.Payload).ParticlesPerSecond = std::get<float>(value);
                break;
            case VfxModuleProperty::BurstTime:
                std::get<VfxBurstModule>(module.Payload).Time = std::get<float>(value);
                break;
            case VfxModuleProperty::BurstCount:
            {
                const auto integer = std::get<std::int64_t>(value);
                if (integer < 1 || integer > 1'000'000)
                    throw std::invalid_argument("VFX burst is invalid.");
                std::get<VfxBurstModule>(module.Payload).Count = static_cast<std::uint32_t>(integer);
                break;
            }
            case VfxModuleProperty::BurstCycles:
            {
                const auto integer = std::get<std::int64_t>(value);
                if (integer < 1 || integer > static_cast<std::int64_t>(MaximumBurstCycles))
                    throw std::invalid_argument("VFX burst is invalid.");
                std::get<VfxBurstModule>(module.Payload).Cycles = static_cast<std::uint32_t>(integer);
                break;
            }
            case VfxModuleProperty::BurstInterval:
                std::get<VfxBurstModule>(module.Payload).Interval = std::get<float>(value);
                break;
            case VfxModuleProperty::ShapeBoxHalfExtent:
                std::get<VfxShapeModule>(module.Payload).BoxHalfExtent = std::get<Vector3>(value);
                break;
            case VfxModuleProperty::ShapeRadius:
                std::get<VfxShapeModule>(module.Payload).Radius = std::get<float>(value);
                break;
            case VfxModuleProperty::ShapeConeAngleDegrees:
                std::get<VfxShapeModule>(module.Payload).ConeAngleDegrees = std::get<float>(value);
                break;
            case VfxModuleProperty::ShapeConeLength:
                std::get<VfxShapeModule>(module.Payload).ConeLength = std::get<float>(value);
                break;
            case VfxModuleProperty::ShapeMesh:
                std::get<VfxShapeModule>(module.Payload).Mesh = std::get<AssetId>(value);
                break;
            case VfxModuleProperty::ShapeVolume:
                std::get<VfxShapeModule>(module.Payload).Volume = std::get<AssetId>(value);
                break;
            case VfxModuleProperty::InitializeLifetimeMinimum:
                std::get<VfxInitializeModule>(module.Payload).LifetimeMinimum = std::get<float>(value);
                break;
            case VfxModuleProperty::InitializeLifetimeMaximum:
                std::get<VfxInitializeModule>(module.Payload).LifetimeMaximum = std::get<float>(value);
                break;
            case VfxModuleProperty::InitializeVelocityMinimum:
                std::get<VfxInitializeModule>(module.Payload).VelocityMinimum = std::get<Vector3>(value);
                break;
            case VfxModuleProperty::InitializeVelocityMaximum:
                std::get<VfxInitializeModule>(module.Payload).VelocityMaximum = std::get<Vector3>(value);
                break;
            case VfxModuleProperty::InitializeRotationMinimum:
                std::get<VfxInitializeModule>(module.Payload).RotationMinimum = std::get<Vector3>(value);
                break;
            case VfxModuleProperty::InitializeRotationMaximum:
                std::get<VfxInitializeModule>(module.Payload).RotationMaximum = std::get<Vector3>(value);
                break;
            case VfxModuleProperty::ForceVector:
                std::get<VfxForceModule>(module.Payload).Force = std::get<Vector3>(value);
                break;
            case VfxModuleProperty::ForceGravityMultiplier:
                std::get<VfxForceModule>(module.Payload).GravityMultiplier = std::get<float>(value);
                break;
            case VfxModuleProperty::SizeConstant:
                std::get<VfxSizeOverLifetimeModule>(module.Payload).Size = Curve1D::Constant(std::get<float>(value));
                break;
            case VfxModuleProperty::ColorConstant:
                std::get<VfxColorOverLifetimeModule>(module.Payload).Color =
                    ColorGradient::Constant(std::get<Color>(value));
                break;
            case VfxModuleProperty::CollisionRestitution:
                std::get<VfxCollisionModule>(module.Payload).Restitution = std::get<float>(value);
                break;
            case VfxModuleProperty::CollisionKillOnCollision:
                std::get<VfxCollisionModule>(module.Payload).KillOnCollision = std::get<bool>(value);
                break;
            case VfxModuleProperty::KillShapeCenter:
                std::get<VfxKillShapeModule>(module.Payload).Center = std::get<Vector3>(value);
                break;
            case VfxModuleProperty::KillShapeBoxHalfExtent:
                std::get<VfxKillShapeModule>(module.Payload).BoxHalfExtent = std::get<Vector3>(value);
                break;
            case VfxModuleProperty::KillShapeRadius:
                std::get<VfxKillShapeModule>(module.Payload).Radius = std::get<float>(value);
                break;
            case VfxModuleProperty::KillShapeInverted:
                std::get<VfxKillShapeModule>(module.Payload).Mode =
                    std::get<bool>(value) ? VfxKillShapeMode::Inverted : VfxKillShapeMode::Solid;
                break;
            case VfxModuleProperty::RendererSprite:
                std::get<VfxRendererModule>(module.Payload).Sprite = std::get<AssetId>(value);
                break;
            case VfxModuleProperty::RendererMesh:
                std::get<VfxRendererModule>(module.Payload).Mesh = std::get<AssetId>(value);
                break;
            case VfxModuleProperty::RendererMaterial:
                std::get<VfxRendererModule>(module.Payload).Material = std::get<AssetId>(value);
                break;
            case VfxModuleProperty::None:
                throw std::invalid_argument("VFX graph binding has no executable module property.");
            }
        }

        VfxEffectDefinition ResolveVfxExecutableDefinition(const VfxEffectDefinition& source,
                                                           const VfxCompiledProgram& program,
                                                           const std::span<const VfxParameterValue> parameters)
        {
            auto result = source;
            result.ExecutionSource = VfxExecutionSource::LegacyModules;
            result.Systems.clear();
            result.Blackboard.clear();
            result.Modules.clear();
            result.Modules.reserve(program.Modules.size());
            for (const auto& compiled : program.Modules)
            {
                if (compiled.ModuleIndex >= source.Modules.size())
                    throw std::invalid_argument("VFX compiled module index is invalid.");
                const auto& module = source.Modules[compiled.ModuleIndex];
                if (module.Id != compiled.Module)
                    throw std::invalid_argument("VFX compiled module identity is invalid.");
                auto executableModule = module;
                // Runtime copies are keyed by the stable execution node, not their shared authoring payload. This
                // gives duplicate Blocks independent literals, expression inputs, ordering, and hot-reload identity.
                executableModule.Id = compiled.Node;
                // A compiled operation is already filtered by its graph Block or legacy-node enabled state. Runtime
                // execution must therefore follow the compiled graph, not the compatibility payload's authoring flag.
                executableModule.Enabled = true;
                result.Modules.push_back(std::move(executableModule));
            }

            for (const auto& binding : program.Bindings)
            {
                const auto module = std::ranges::find(result.Modules, binding.Node, &VfxModuleDefinition::Id);
                if (module == result.Modules.end())
                    throw std::invalid_argument("VFX compiled binding is invalid.");
                if (binding.ValueRegister != ~std::uint32_t{0})
                    continue;
                const auto* valuePointer = binding.LiteralValue ? std::addressof(*binding.LiteralValue)
                                           : binding.ParameterSlot < parameters.size()
                                               ? std::addressof(parameters[binding.ParameterSlot])
                                               : nullptr;
                if (!valuePointer || !VfxValueMatchesType(binding.Type, *valuePointer))
                    throw std::invalid_argument("VFX compiled binding value is invalid.");
                const auto& value = *valuePointer;
                ApplyVfxModuleProperty(*module, binding.Property, value);
            }

            const auto hasEmission =
                std::ranges::any_of(result.Modules,
                                    [](const VfxModuleDefinition& module)
                                    {
                                        return std::holds_alternative<VfxEmissionRateModule>(module.Payload) ||
                                               std::holds_alternative<VfxBurstModule>(module.Payload);
                                    });
            if (!hasEmission && !program.EventName.empty())
            {
                auto validationId = AssetId(program.System.High() ^ 0x4556454e54535041ULL,
                                            program.System.Low() ^ 0x574e000000000001ULL);
                while (!validationId || std::ranges::find(result.Modules, validationId, &VfxModuleDefinition::Id) !=
                                            result.Modules.end())
                {
                    validationId = AssetId(validationId.High(), validationId.Low() + 1U);
                }
                result.Modules.push_back({validationId, true, VfxEmissionRateModule{0.0F}});
                ValidateVfxEffect(result);
                result.Modules.pop_back();
            }
            else
            {
                ValidateVfxEffect(result);
            }
            return result;
        }
    } // namespace Internal

    namespace
    {
        VfxCompiledProgram CompileVfxEffectSystem(const VfxEffectDefinition& definition, const VfxBackend backend,
                                                  const VfxGraphSystem* system)
        {
            VfxCompiledProgram result;
            result.Backend = backend;
            try
            {
                ValidateVfxEffect(definition);
                auto plan = LowerEffect(definition, system);
                result.System = plan.System;
                result.DataType = plan.DataType;
                result.ParticlesPerStrip = plan.ParticlesPerStrip;
                result.EventName = plan.EventName;
                std::set<AssetId> reportedBackendNodes;
                for (const auto& instruction : plan.ValueInstructions)
                {
                    const VfxGraphNode* sourceNode = nullptr;
                    for (const auto& graphSystem : definition.Systems)
                    {
                        const auto node = std::ranges::find(graphSystem.Nodes, instruction.Node, &VfxGraphNode::Id);
                        if (node != graphSystem.Nodes.end())
                        {
                            sourceNode = std::addressof(*node);
                            break;
                        }
                    }
                    const auto* descriptor = sourceNode ? FindVfxNodeDescriptor(sourceNode->TypeId.View()) : nullptr;
                    if (!descriptor || descriptor->Class != VfxNodeClass::Operator)
                        throw std::logic_error("VFX value instruction has no catalog descriptor.");
                    const auto unsupported =
                        (backend == VfxBackend::Gpu && descriptor->BackendTier == VfxNodeBackendTier::CpuOnly) ||
                        (backend == VfxBackend::Cpu && descriptor->BackendTier == VfxNodeBackendTier::GpuRequired);
                    if (!unsupported || !reportedBackendNodes.insert(instruction.Node).second)
                        continue;
                    result.Diagnostics.push_back(
                        {VfxCompileDiagnosticSeverity::Error, instruction.Node,
                         backend == VfxBackend::Gpu
                             ? "This VFX Operator is CPU-only until its GPU semantics pass differential validation."
                             : "This VFX Operator requires the GPU backend."});
                }
                if (backend == VfxBackend::Gpu)
                {
                    const auto shaderModuleProperty = [](const VfxModuleProperty property) noexcept
                    {
                        switch (property)
                        {
                        case VfxModuleProperty::ShapeBoxHalfExtent:
                        case VfxModuleProperty::ShapeRadius:
                        case VfxModuleProperty::ShapeConeAngleDegrees:
                        case VfxModuleProperty::ShapeConeLength:
                        case VfxModuleProperty::InitializeLifetimeMinimum:
                        case VfxModuleProperty::InitializeLifetimeMaximum:
                        case VfxModuleProperty::InitializeVelocityMinimum:
                        case VfxModuleProperty::InitializeVelocityMaximum:
                        case VfxModuleProperty::InitializeRotationMinimum:
                        case VfxModuleProperty::InitializeRotationMaximum:
                        case VfxModuleProperty::ForceVector:
                        case VfxModuleProperty::ForceGravityMultiplier:
                        case VfxModuleProperty::SizeConstant:
                        case VfxModuleProperty::ColorConstant:
                        case VfxModuleProperty::CollisionRestitution:
                        case VfxModuleProperty::CollisionKillOnCollision:
                        case VfxModuleProperty::KillShapeCenter:
                        case VfxModuleProperty::KillShapeBoxHalfExtent:
                        case VfxModuleProperty::KillShapeRadius:
                        case VfxModuleProperty::KillShapeInverted:
                            return true;
                        case VfxModuleProperty::None:
                        case VfxModuleProperty::EmissionParticlesPerSecond:
                        case VfxModuleProperty::BurstTime:
                        case VfxModuleProperty::BurstCount:
                        case VfxModuleProperty::BurstCycles:
                        case VfxModuleProperty::BurstInterval:
                        case VfxModuleProperty::ShapeMesh:
                        case VfxModuleProperty::ShapeVolume:
                        case VfxModuleProperty::RendererSprite:
                        case VfxModuleProperty::RendererMesh:
                        case VfxModuleProperty::RendererMaterial:
                            return false;
                        }
                        return false;
                    };
                    std::set<AssetId> reportedNodes;
                    for (const auto& binding : plan.Bindings)
                    {
                        if (binding.ValueRegister == ~std::uint32_t{0})
                            continue;
                        const auto instruction = std::ranges::find(plan.ValueInstructions, binding.ValueRegister,
                                                                   &VfxCompiledValueInstruction::OutputRegister);
                        if (instruction == plan.ValueInstructions.end())
                            throw std::logic_error("VFX Block binding references an unknown value register.");
                        if (instruction->Domain <= VfxEvaluationDomain::PerFrame ||
                            shaderModuleProperty(binding.Property) || !reportedNodes.insert(instruction->Node).second)
                        {
                            continue;
                        }
                        result.Diagnostics.push_back(
                            {VfxCompileDiagnosticSeverity::Error, instruction->Node,
                             "This per-particle GPU expression targets a host-evaluated Block property. Particle-stage "
                             "bindings are supported for Shape, Initialize, Force, Size, Color, Collision, and Kill "
                             "Shape inputs; emission schedules and resource selections remain per-effect values."});
                    }
                }
                result.CanonicalIr = BuildCanonicalIr(definition, plan);
                result.Hash = HashBytes(result.CanonicalIr);
                result.StateLayoutHash = BuildStateLayoutHash(definition, plan);
                result.Parameters = std::move(plan.Parameters);
                result.Modules = std::move(plan.Modules);
                result.Bindings = std::move(plan.Bindings);
                result.ValueInstructions = std::move(plan.ValueInstructions);
                result.ValueRegisterCount = plan.ValueRegisterCount;
                if (backend == VfxBackend::Gpu)
                {
                    result.GpuValueProgram =
                        BuildGpuValueProgram(result.ValueInstructions, result.ValueRegisterCount,
                                             static_cast<std::uint32_t>(result.Parameters.size()), result.System);
                }
                result.CustomInstructions = std::move(plan.CustomInstructions);
                result.Operations = std::move(plan.Operations);
                std::vector<VfxParameterValue> defaultParameters(result.Parameters.size());
                std::vector<bool> assignedDefaults(result.Parameters.size());
                for (const auto& parameter : result.Parameters)
                {
                    if (parameter.Slot >= defaultParameters.size() || assignedDefaults[parameter.Slot])
                        throw std::invalid_argument("VFX compiled parameter layout is invalid.");
                    defaultParameters[parameter.Slot] = parameter.DefaultValue;
                    assignedDefaults[parameter.Slot] = true;
                }
                const auto executable = Internal::ResolveVfxExecutableDefinition(definition, result, defaultParameters);
                if (backend == VfxBackend::Gpu)
                    AppendGpuCapabilityDiagnostics(executable, result, UsesStrictSchemaFourCapabilities(definition),
                                                   result.Diagnostics);
                if (backend == VfxBackend::Cpu)
                {
                    AppendCpuCapabilityDiagnostics(executable, result, UsesStrictSchemaFourCapabilities(definition),
                                                   result.Diagnostics);
                    for (const auto& compiled : result.Modules)
                    {
                        const auto& module = definition.Modules.at(compiled.ModuleIndex);
                        if (const auto* collision = std::get_if<VfxCollisionModule>(&module.Payload);
                            collision && collision->Mode == VfxCollisionMode::GpuDepth)
                            result.Diagnostics.push_back(
                                {VfxCompileDiagnosticSeverity::Warning, compiled.Node,
                                 "GPU depth collision degrades to the configured CPU collision query."});
                    }
                }
                if (std::ranges::any_of(result.Diagnostics, [](const VfxCompileDiagnostic& diagnostic)
                                        { return diagnostic.Severity == VfxCompileDiagnosticSeverity::Error; }))
                    return result;
                result.Valid = true;
            }
            catch (const VfxNodeCompileError& error)
            {
                result.Diagnostics.push_back({VfxCompileDiagnosticSeverity::Error, error.Node(), error.what()});
            }
            catch (const std::exception& error)
            {
                result.Diagnostics.push_back({VfxCompileDiagnosticSeverity::Error, {}, error.what()});
            }
            return result;
        }
    } // namespace

    VfxCompiledProgram CompileVfxEffect(const VfxEffectDefinition& definition, const VfxBackend backend)
    {
        if (definition.ExecutionSource == VfxExecutionSource::Graph && definition.Systems.size() != 1)
        {
            VfxCompiledProgram result;
            result.Backend = backend;
            result.Diagnostics.push_back(
                {VfxCompileDiagnosticSeverity::Error,
                 {},
                 "CompileVfxEffect requires one graph system; use CompileVfxEffectSystems for multi-system assets."});
            return result;
        }
        const auto* system =
            definition.ExecutionSource == VfxExecutionSource::Graph ? &definition.Systems.front() : nullptr;
        return CompileVfxEffectSystem(definition, backend, system);
    }

    std::vector<VfxCompiledProgram> CompileVfxEffectSystems(const VfxEffectDefinition& definition,
                                                            const VfxBackend backend)
    {
        if (definition.ExecutionSource != VfxExecutionSource::Graph)
            return {CompileVfxEffectSystem(definition, backend, nullptr)};
        std::vector<VfxCompiledProgram> result;
        result.reserve(definition.Systems.size());
        for (const auto& system : definition.Systems)
            result.push_back(CompileVfxEffectSystem(definition, backend, std::addressof(system)));
        return result;
    }
} // namespace Keire
