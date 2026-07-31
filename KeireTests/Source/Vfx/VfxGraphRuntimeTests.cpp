#include "Keire/ECS/Components/VfxEmitterComponent.h"
#include "Keire/Vfx/VfxSystem.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>

namespace
{
    [[nodiscard]] constexpr Keire::AssetId RuntimeId(const std::uint64_t value) noexcept
    {
        return Keire::AssetId(0x475241504852554eULL, value);
    }

    [[nodiscard]] Keire::VfxEffectDefinition ExecutableGraph()
    {
        Keire::VfxEffectDefinition definition;
        definition.EmitterId = RuntimeId(1);
        definition.Name = "Executable graph";
        definition.Loop = true;
        definition.Duration = 2.0F;
        definition.Capacity = 32;
        definition.Modules = {
            {RuntimeId(2), true, Keire::VfxEmissionRateModule{10.0F}},
            {RuntimeId(3), true, Keire::VfxShapeModule{}},
            {RuntimeId(4), true, Keire::VfxInitializeModule{5.0F, 5.0F, {}, {}, {}, {}}},
            {RuntimeId(5), true, Keire::VfxRendererModule{}},
        };
        definition = Keire::ConvertVfxEffectToGraph(definition);
        definition.Blackboard = {
            {RuntimeId(20), "Rate", Keire::VfxValueType::Scalar, 4.0F, true},
            {RuntimeId(21), "Force", Keire::VfxValueType::Vector3, Keire::Vector3{0.0F, 4.0F, 0.0F}, true},
        };

        auto& system = definition.Systems.front();
        system.Nodes.push_back({RuntimeId(30),
                                "Rate",
                                Keire::VfxContextType::Spawn,
                                {-300.0F, 0.0F},
                                {{RuntimeId(31), "Rate", Keire::VfxValueType::Scalar, false, "value", std::nullopt}},
                                {},
                                Keire::VfxGraphNodeKind::Parameter,
                                RuntimeId(20),
                                {"keire.parameter"}});
        system.Nodes.push_back({RuntimeId(32),
                                "Force",
                                Keire::VfxContextType::Update,
                                {-300.0F, 180.0F},
                                {{RuntimeId(33), "Force", Keire::VfxValueType::Vector3, false, "value", std::nullopt}},
                                {},
                                Keire::VfxGraphNodeKind::Parameter,
                                RuntimeId(21),
                                {"keire.parameter"}});

        const auto emissionContext =
            std::ranges::find_if(system.Nodes,
                                 [](const Keire::VfxGraphNode& node)
                                 {
                                     return std::ranges::find(node.Blocks, RuntimeId(2),
                                                              &Keire::VfxGraphBlock::Reference) != node.Blocks.end();
                                 });
        REQUIRE(emissionContext != system.Nodes.end());
        const auto emission =
            std::ranges::find(emissionContext->Blocks, RuntimeId(2), &Keire::VfxGraphBlock::Reference);
        REQUIRE(emission != emissionContext->Blocks.end());
        const auto rate =
            std::ranges::find(emission->Pins, std::string("particlesPerSecond"), &Keire::VfxGraphPin::Semantic);
        REQUIRE(rate != emission->Pins.end());
        Keire::VfxGraphConnection rateConnection;
        rateConnection.Id = RuntimeId(34);
        rateConnection.OutputNode = RuntimeId(30);
        rateConnection.OutputPin = RuntimeId(31);
        rateConnection.InputNode = emissionContext->Id;
        rateConnection.InputPin = rate->Id;
        rateConnection.InputBlock = emission->Id;
        system.Connections.push_back(rateConnection);

        const auto update = std::ranges::find_if(
            system.Nodes, [](const Keire::VfxGraphNode& node)
            { return node.Kind == Keire::VfxGraphNodeKind::Context && node.Context == Keire::VfxContextType::Update; });
        REQUIRE(update != system.Nodes.end());
        auto portable = Keire::CreateVfxGraphPortableHlslBlock("Velocity += Force * DeltaTime;");
        portable.Id = RuntimeId(40);
        portable.Pins.push_back({RuntimeId(42), "Force", Keire::VfxValueType::Vector3, true, "Force", std::nullopt});
        update->Blocks.push_back(std::move(portable));
        Keire::VfxGraphConnection forceConnection;
        forceConnection.Id = RuntimeId(45);
        forceConnection.OutputNode = RuntimeId(32);
        forceConnection.OutputPin = RuntimeId(33);
        forceConnection.InputNode = update->Id;
        forceConnection.InputPin = RuntimeId(42);
        forceConnection.InputBlock = RuntimeId(40);
        system.Connections.push_back(forceConnection);
        Keire::ValidateVfxEffect(definition);
        return definition;
    }

    [[nodiscard]] Keire::VfxEffectDefinition OrderedGraph(const bool customBeforeForce)
    {
        Keire::VfxEffectDefinition definition;
        definition.EmitterId = RuntimeId(customBeforeForce ? 100 : 200);
        definition.Name = customBeforeForce ? "Custom before Force" : "Custom after Collision";
        definition.Loop = true;
        definition.Duration = 2.0F;
        definition.Capacity = 32;
        definition.Modules = {
            {RuntimeId(customBeforeForce ? 101 : 201), true, Keire::VfxEmissionRateModule{10.0F}},
            {RuntimeId(customBeforeForce ? 102 : 202), true, Keire::VfxShapeModule{}},
            {RuntimeId(customBeforeForce ? 103 : 203), true, Keire::VfxInitializeModule{5.0F, 5.0F, {}, {}, {}, {}}},
            {RuntimeId(customBeforeForce ? 104 : 204), true, Keire::VfxForceModule{{0.0F, 2.0F, 0.0F}, 0.0F}},
            {RuntimeId(customBeforeForce ? 106 : 206), true, Keire::VfxCollisionModule{}},
            {RuntimeId(customBeforeForce ? 105 : 205), true, Keire::VfxRendererModule{}},
        };
        definition = Keire::ConvertVfxEffectToGraph(definition);

        auto& system = definition.Systems.front();
        const auto forceId = definition.Modules[3].Id;
        const auto collisionId = definition.Modules[4].Id;
        const auto update = std::ranges::find_if(
            system.Nodes, [](const Keire::VfxGraphNode& node)
            { return node.Kind == Keire::VfxGraphNodeKind::Context && node.Context == Keire::VfxContextType::Update; });
        REQUIRE(update != system.Nodes.end());
        const auto force = std::ranges::find(update->Blocks, forceId, &Keire::VfxGraphBlock::Reference);
        const auto collision = std::ranges::find(update->Blocks, collisionId, &Keire::VfxGraphBlock::Reference);
        REQUIRE(force != update->Blocks.end());
        REQUIRE(collision != update->Blocks.end());
        auto portable = Keire::CreateVfxGraphPortableHlslBlock("Velocity = float3(0.0, 1.0, 0.0);");
        portable.Id = RuntimeId(customBeforeForce ? 110 : 210);
        update->Blocks.insert(customBeforeForce ? force : std::next(collision), std::move(portable));
        Keire::ValidateVfxEffect(definition);
        return definition;
    }
} // namespace

TEST_CASE("VFX graph cables, Blackboard defaults, overrides, and Portable HLSL drive CPU execution")
{
    const auto definition = ExecutableGraph();
    const auto program = Keire::CompileVfxEffect(definition, Keire::VfxBackend::Cpu);
    REQUIRE(program.Valid);
    REQUIRE(program.Bindings.size() == 1);
    REQUIRE(program.CustomInstructions.size() == 1);

    auto world =
        Keire::CreateRef<Keire::VfxWorld>(Keire::VfxWorldSpecification{.MaximumEffects = 2, .MaximumParticles = 32});
    const auto effect = Keire::CreateRef<Keire::VfxEffectAsset>(definition);
    const auto defaults = world->Activate({effect});
    const auto overridden = world->Activate(
        {effect, 1, {}, {}, 0, {{RuntimeId(20), 8.0F}, {RuntimeId(21), Keire::Vector3{0.0F, 8.0F, 0.0F}}}});
    REQUIRE(defaults);
    REQUIRE(overridden);

    world->Update(0.25F);
    auto snapshot = world->CaptureDebugSnapshot();
    REQUIRE(snapshot.EffectCount == 2);
    CHECK(snapshot.Effects[defaults.Index()].ActiveParticles == 1);
    CHECK(snapshot.Effects[overridden.Index()].ActiveParticles == 2);

    world->SetParameter(defaults, RuntimeId(20), 8.0F);
    world->Update(0.25F);
    snapshot = world->CaptureDebugSnapshot();
    CHECK(snapshot.Effects[defaults.Index()].ActiveParticles == 3);
    CHECK(snapshot.Effects[overridden.Index()].ActiveParticles == 4);

    float defaultVelocity = 0.0F;
    float overriddenVelocity = 0.0F;
    for (std::size_t index = 0; index < snapshot.ParticleCount; ++index)
    {
        const auto& particle = snapshot.Particles[index];
        if (particle.Effect == defaults)
            defaultVelocity = std::max(defaultVelocity, particle.Velocity.Y);
        if (particle.Effect == overridden)
            overriddenVelocity = std::max(overriddenVelocity, particle.Velocity.Y);
    }
    CHECK(defaultVelocity == doctest::Approx(1.0F));
    CHECK(overriddenVelocity == doctest::Approx(2.0F));

    CHECK_THROWS_WITH_AS(world->SetParameter(defaults, RuntimeId(20), std::int64_t{8}),
                         "VFX parameter override is unknown, hidden, or type-mismatched.", std::invalid_argument);
    world->ResetParameter(defaults, RuntimeId(20));
    world->Update(0.25F);
    snapshot = world->CaptureDebugSnapshot();
    CHECK(snapshot.Effects[defaults.Index()].ActiveParticles == 4);
    CHECK(snapshot.Effects[overridden.Index()].ActiveParticles == 6);
}

TEST_CASE("VFX Operator results drive Portable HLSL inputs on CPU and the packed GPU runtime")
{
    auto definition = ExecutableGraph();
    auto& system = definition.Systems.front();

    auto age = Keire::CreateVfxGraphOperatorNode("keire.operator.age", {-360.0F, 320.0F});
    age.Context = Keire::VfxContextType::Update;
    const auto ageNode = age.Id;
    const auto ageOutput = age.Pins.front().Id;
    system.Nodes.push_back(std::move(age));

    const auto update = std::ranges::find_if(
        system.Nodes, [](const Keire::VfxGraphNode& node)
        { return node.Kind == Keire::VfxGraphNodeKind::Context && node.Context == Keire::VfxContextType::Update; });
    REQUIRE(update != system.Nodes.end());
    auto portable = Keire::CreateVfxGraphPortableHlslBlock("Size = DynamicSize;");
    portable.Id = RuntimeId(50);
    portable.Pins.push_back({RuntimeId(51), "Dynamic Size", Keire::VfxValueType::Scalar, true, "DynamicSize", 1.0F});
    update->Blocks.push_back(std::move(portable));

    Keire::VfxGraphConnection connection;
    connection.Id = RuntimeId(52);
    connection.OutputNode = ageNode;
    connection.OutputPin = ageOutput;
    connection.InputNode = update->Id;
    connection.InputBlock = RuntimeId(50);
    connection.InputPin = RuntimeId(51);
    system.Connections.push_back(connection);

    const auto cpu = Keire::CompileVfxEffect(definition, Keire::VfxBackend::Cpu);
    REQUIRE(cpu.Valid);
    REQUIRE(cpu.ValueInstructions.size() == 1);
    REQUIRE(cpu.CustomInstructions.size() == 2);
    const auto dynamicInstruction =
        std::ranges::find(cpu.CustomInstructions, RuntimeId(50), &Keire::VfxCompiledCustomInstruction::Node);
    REQUIRE(dynamicInstruction != cpu.CustomInstructions.end());
    CHECK(dynamicInstruction->ParameterSlot == ~std::uint32_t{0});
    CHECK(dynamicInstruction->ValueRegister == cpu.ValueInstructions.front().OutputRegister);

    const auto gpu = Keire::CompileVfxEffect(definition, Keire::VfxBackend::Gpu);
    REQUIRE(gpu.Valid);
    REQUIRE(gpu.GpuValueProgram.Instructions.size() == 1);
    CHECK(gpu.GpuValueProgram.RegisterCount == 1);

    auto gpuWorld = Keire::CreateRef<Keire::VfxWorld>(
        Keire::VfxWorldSpecification{.MaximumEffects = 1, .MaximumParticles = 32, .Backend = Keire::VfxBackend::Gpu});
    REQUIRE(gpuWorld->Activate({Keire::CreateRef<Keire::VfxEffectAsset>(definition)}));
    gpuWorld->Update(0.25F);
    const auto gpuSnapshot = gpuWorld->CaptureRenderSnapshot();
    REQUIRE(gpuSnapshot.GpuEmitters().size() == 1);
    const auto& gpuEmitter = gpuSnapshot.GpuEmitters().front();
    REQUIRE(gpuEmitter.Execution);
    REQUIRE(gpuEmitter.Execution->CustomInstructions.size() == gpu.CustomInstructions.size());
    CHECK(std::ranges::any_of(gpuEmitter.Execution->CustomInstructions,
                              [registerIndex = gpu.ValueInstructions.front().OutputRegister](
                                  const Keire::VfxGpuCustomInstruction& instruction)
                              { return instruction.ValueRegister == registerIndex; }));

    auto world =
        Keire::CreateRef<Keire::VfxWorld>(Keire::VfxWorldSpecification{.MaximumEffects = 1, .MaximumParticles = 32});
    const auto handle = world->Activate({Keire::CreateRef<Keire::VfxEffectAsset>(definition)});
    REQUIRE(handle);
    world->Update(0.25F);
    world->Update(0.25F);

    const auto snapshot = world->CaptureDebugSnapshot();
    REQUIRE(snapshot.EffectCount == 1);
    CHECK(snapshot.Effects[handle.Index()].ActiveParticles == 2);
    CHECK(std::ranges::any_of(snapshot.Particles, [handle](const Keire::VfxDebugParticle& particle)
                              { return particle.Effect == handle && particle.Size == doctest::Approx(0.25F); }));
}

TEST_CASE("GPU VFX descriptors receive graph-bound modules and resolved Portable HLSL operands")
{
    const auto effect = Keire::CreateRef<Keire::VfxEffectAsset>(ExecutableGraph());
    auto world = Keire::CreateRef<Keire::VfxWorld>(
        Keire::VfxWorldSpecification{.MaximumEffects = 1, .MaximumParticles = 32, .Backend = Keire::VfxBackend::Gpu});
    const auto handle = world->Activate(
        {effect, 1, {}, {}, 0, {{RuntimeId(20), 8.0F}, {RuntimeId(21), Keire::Vector3{0.0F, 8.0F, 0.0F}}}});
    REQUIRE(handle);
    world->Update(0.25F);

    const auto snapshot = world->CaptureRenderSnapshot();
    REQUIRE(snapshot.GpuEmitters().size() == 1);
    const auto& emitter = snapshot.GpuEmitters().front();
    REQUIRE(emitter.Execution);
    CHECK(emitter.SpawnSequence == 2);
    REQUIRE(emitter.CustomInstructionCount == 1);
    CHECK(emitter.CustomInstructions[0].Context == Keire::VfxContextType::Update);
    CHECK(emitter.CustomInstructions[0].Target == Keire::VfxCustomTarget::Velocity);
    CHECK(emitter.CustomInstructions[0].Operand.Y == doctest::Approx(8.0F));
    CHECK(emitter.CustomInstructions[0].ScaleByDeltaTime);

    const auto simulationRevision = emitter.SimulationRevision;
    const auto originalExecution = emitter.Execution;
    world->SetParameter(handle, RuntimeId(21), Keire::Vector3{0.0F, 4.0F, 0.0F});
    const auto updated = world->CaptureRenderSnapshot();
    REQUIRE(updated.GpuEmitters().size() == 1);
    CHECK(updated.GpuEmitters().front().SimulationRevision == simulationRevision);
    CHECK(updated.GpuEmitters().front().CustomInstructions[0].Operand.Y == doctest::Approx(4.0F));
    REQUIRE(updated.GpuEmitters().front().Execution);
    CHECK(updated.GpuEmitters().front().Execution != originalExecution);
    CHECK(emitter.Execution == originalExecution);
}

TEST_CASE("GPU VFX keeps per-frame Portable HLSL Operator inputs in the immutable shader program")
{
    auto definition = ExecutableGraph();
    auto& system = definition.Systems.front();

    auto time = Keire::CreateVfxGraphOperatorNode("keire.operator.time", {-360.0F, 320.0F});
    time.Context = Keire::VfxContextType::Update;
    const auto timeNode = time.Id;
    const auto timeOutput = time.Pins.front().Id;
    system.Nodes.push_back(std::move(time));

    const auto update = std::ranges::find_if(
        system.Nodes, [](const Keire::VfxGraphNode& node)
        { return node.Kind == Keire::VfxGraphNodeKind::Context && node.Context == Keire::VfxContextType::Update; });
    REQUIRE(update != system.Nodes.end());
    auto portable = Keire::CreateVfxGraphPortableHlslBlock("Size = DynamicSize;");
    portable.Id = RuntimeId(60);
    portable.Pins.push_back({RuntimeId(61), "Dynamic Size", Keire::VfxValueType::Scalar, true, "DynamicSize", 1.0F});
    update->Blocks.push_back(std::move(portable));

    Keire::VfxGraphConnection connection;
    connection.Id = RuntimeId(62);
    connection.OutputNode = timeNode;
    connection.OutputPin = timeOutput;
    connection.InputNode = update->Id;
    connection.InputBlock = RuntimeId(60);
    connection.InputPin = RuntimeId(61);
    system.Connections.push_back(connection);

    const auto program = Keire::CompileVfxEffect(definition, Keire::VfxBackend::Gpu);
    REQUIRE(program.Valid);
    const auto dynamicInstruction =
        std::ranges::find(program.CustomInstructions, RuntimeId(60), &Keire::VfxCompiledCustomInstruction::Node);
    REQUIRE(dynamicInstruction != program.CustomInstructions.end());
    CHECK(dynamicInstruction->ValueRegister != ~std::uint32_t{0});

    auto world = Keire::CreateRef<Keire::VfxWorld>(
        Keire::VfxWorldSpecification{.MaximumEffects = 1, .MaximumParticles = 32, .Backend = Keire::VfxBackend::Gpu});
    REQUIRE(world->Activate({Keire::CreateRef<Keire::VfxEffectAsset>(definition)}));
    world->Update(0.25F);
    const auto firstSnapshot = world->CaptureRenderSnapshot();
    REQUIRE(firstSnapshot.GpuEmitters().size() == 1);
    const auto firstExecution = firstSnapshot.GpuEmitters().front().Execution;
    REQUIRE(firstExecution);
    world->Update(0.25F);

    const auto snapshot = world->CaptureRenderSnapshot();
    REQUIRE(snapshot.GpuEmitters().size() == 1);
    const auto& emitter = snapshot.GpuEmitters().front();
    const auto uploaded =
        std::ranges::find_if(std::span(emitter.CustomInstructions).first(emitter.CustomInstructionCount),
                             [](const Keire::VfxGpuEmitter::CustomInstruction& instruction)
                             { return instruction.Target == Keire::VfxCustomTarget::Size; });
    REQUIRE(uploaded != std::span(emitter.CustomInstructions).first(emitter.CustomInstructionCount).end());
    CHECK(uploaded->ValueRegister == dynamicInstruction->ValueRegister);
    CHECK(emitter.Execution == firstExecution);
}

TEST_CASE("Saved VFX executable graphs preserve cables bindings and Portable HLSL at runtime")
{
    const auto source = ExecutableGraph();
    const auto encoded = Keire::VfxEffectAsset::Encode(source);
    const auto decoded = Keire::VfxEffectAsset::Decode(encoded);
    REQUIRE(decoded);
    CHECK(decoded->Definition() == source);

    const auto program = Keire::CompileVfxEffect(decoded->Definition(), Keire::VfxBackend::Cpu);
    REQUIRE(program.Valid);
    REQUIRE(program.Bindings.size() == 1);
    REQUIRE(program.CustomInstructions.size() == 1);
    CHECK_FALSE(program.Operations.empty());

    auto world =
        Keire::CreateRef<Keire::VfxWorld>(Keire::VfxWorldSpecification{.MaximumEffects = 1, .MaximumParticles = 32});
    const auto handle = world->Activate(
        {decoded, 1, {}, {}, 0, {{RuntimeId(20), 8.0F}, {RuntimeId(21), Keire::Vector3{0.0F, 8.0F, 0.0F}}}});
    REQUIRE(handle);
    world->Update(0.25F);
    world->Update(0.25F);

    const auto snapshot = world->CaptureDebugSnapshot();
    CHECK(snapshot.Statistics.ActiveParticles == 4);
    float maximumVelocity = 0.0F;
    for (std::size_t index = 0; index < snapshot.ParticleCount; ++index)
        maximumVelocity = std::max(maximumVelocity, snapshot.Particles[index].Velocity.Y);
    CHECK(maximumVelocity == doctest::Approx(2.0F));
}

TEST_CASE("Unreferenced VFX Runtime Module payloads are inert in Graph execution")
{
    auto definition = OrderedGraph(true);
    auto& system = definition.Systems.front();
    const auto forceModule = definition.Modules[3].Id;
    const auto update = std::ranges::find_if(
        system.Nodes, [](const Keire::VfxGraphNode& node)
        { return node.Kind == Keire::VfxGraphNodeKind::Context && node.Context == Keire::VfxContextType::Update; });
    REQUIRE(update != system.Nodes.end());
    CHECK(std::erase_if(update->Blocks, [forceModule](const Keire::VfxGraphBlock& block)
                        { return block.Reference == forceModule; }) == 1);
    Keire::ValidateVfxEffect(definition);

    const auto program = Keire::CompileVfxEffect(definition, Keire::VfxBackend::Cpu);
    REQUIRE(program.Valid);
    CHECK(std::ranges::find(program.Modules, forceModule, &Keire::VfxCompiledModule::Module) == program.Modules.end());

    auto cpu =
        Keire::CreateRef<Keire::VfxWorld>(Keire::VfxWorldSpecification{.MaximumEffects = 1, .MaximumParticles = 32});
    REQUIRE(cpu->Activate({Keire::CreateRef<Keire::VfxEffectAsset>(definition)}));
    cpu->Update(0.1F);
    cpu->Update(0.5F);
    const auto cpuSnapshot = cpu->CaptureDebugSnapshot();
    REQUIRE(cpuSnapshot.ParticleCount >= 1);
    CHECK(cpuSnapshot.Particles[0].Velocity.Y == doctest::Approx(1.0F));
    CHECK(cpuSnapshot.Particles[0].Position.Y == doctest::Approx(0.5F));

    auto gpu = Keire::CreateRef<Keire::VfxWorld>(
        Keire::VfxWorldSpecification{.MaximumEffects = 1, .MaximumParticles = 32, .Backend = Keire::VfxBackend::Gpu});
    REQUIRE(gpu->Activate({Keire::CreateRef<Keire::VfxEffectAsset>(definition)}));
    gpu->Update(0.1F);
    const auto gpuSnapshot = gpu->CaptureRenderSnapshot();
    REQUIRE(gpuSnapshot.GpuEmitters().size() == 1);
    const auto& emitter = gpuSnapshot.GpuEmitters().front();
    CHECK(emitter.Acceleration == Keire::Vector3{});
    const auto operations = std::span(emitter.ParticleOperations).first(emitter.ParticleOperationCount);
    CHECK(std::ranges::find(operations, Keire::VfxGpuEmitter::ParticleOperationKind::Force,
                            &Keire::VfxGpuEmitter::ParticleOperation::Kind) == operations.end());
}

TEST_CASE("VFX graph reload preserves compatible overrides and diagnoses rejected exposed values")
{
    const auto source = ExecutableGraph();
    const auto sourceProgram = Keire::CompileVfxEffect(source, Keire::VfxBackend::Cpu);
    REQUIRE(sourceProgram.Valid);
    auto world =
        Keire::CreateRef<Keire::VfxWorld>(Keire::VfxWorldSpecification{.MaximumEffects = 1, .MaximumParticles = 32});
    const auto handle = world->Activate({Keire::CreateRef<Keire::VfxEffectAsset>(source),
                                         1,
                                         {},
                                         {},
                                         0,
                                         {{RuntimeId(20), 8.0F}, {RuntimeId(21), Keire::Vector3{0.0F, 8.0F, 0.0F}}}});
    REQUIRE(handle);
    world->Update(0.25F);
    REQUIRE(world->Statistics().ActiveParticles == 2);

    auto reloaded = source;
    const auto force = std::ranges::find(reloaded.Blackboard, RuntimeId(21), &Keire::VfxBlackboardParameter::Id);
    REQUIRE(force != reloaded.Blackboard.end());
    force->Exposed = false;
    const auto reloadedProgram = Keire::CompileVfxEffect(reloaded, Keire::VfxBackend::Cpu);
    REQUIRE(reloadedProgram.Valid);
    CHECK(reloadedProgram.StateLayoutHash != sourceProgram.StateLayoutHash);
    REQUIRE(world->Reload(handle, Keire::CreateRef<Keire::VfxEffectAsset>(reloaded), 2));
    CHECK(world->Statistics().ActiveParticles == 0);

    auto debug = world->CaptureDebugSnapshot();
    REQUIRE(debug.EffectCount == 1);
    CHECK(
        Keire::HasVfxDiagnostic(debug.Effects[0].Diagnostics, Keire::VfxRuntimeDiagnostic::ParameterOverrideRejected));
    CHECK_THROWS_AS(world->SetParameter(handle, RuntimeId(21), Keire::Vector3{0.0F, 12.0F, 0.0F}),
                    std::invalid_argument);

    world->Update(0.25F);
    world->Update(0.25F);
    debug = world->CaptureDebugSnapshot();
    CHECK(debug.Statistics.ActiveParticles == 4);
    float maximumVelocity = 0.0F;
    for (std::size_t index = 0; index < debug.ParticleCount; ++index)
        maximumVelocity = std::max(maximumVelocity, debug.Particles[index].Velocity.Y);
    CHECK(maximumVelocity == doctest::Approx(1.0F));
}

TEST_CASE("VFX Emitter schema migration persists stable-ID parameter overrides")
{
    const auto registration = Keire::CreateVfxEmitterComponentRegistration();
    REQUIRE(registration.SchemaVersion == 2);
    REQUIRE(registration.Migrate);
    auto migrated = registration.Migrate({{"effect", RuntimeId(90)}}, 1);
    CHECK(std::get<std::string>(migrated.at("parameterOverrides")) == "[]");

    auto component = registration.Factory();
    registration.Deserialize(*component, migrated, 2);
    auto& emitter = dynamic_cast<Keire::VfxEmitterComponent&>(*component);
    emitter.SetParameterOverride({RuntimeId(20), 12.0F});
    emitter.SetParameterOverride({RuntimeId(21), Keire::Vector3{1.0F, 2.0F, 3.0F}});
    const auto encoded = registration.Serialize(*component);
    CHECK_FALSE(std::get<std::string>(encoded.at("parameterOverrides")).empty());

    auto decoded = registration.Factory();
    registration.Deserialize(*decoded, encoded, 2);
    const auto& decodedEmitter = dynamic_cast<const Keire::VfxEmitterComponent&>(*decoded);
    REQUIRE(decodedEmitter.ParameterOverrides().size() == 2);
    const Keire::VfxParameterOverride expectedRate{RuntimeId(20), 12.0F};
    const Keire::VfxParameterOverride expectedForce{RuntimeId(21), Keire::Vector3{1.0F, 2.0F, 3.0F}};
    CHECK(decodedEmitter.ParameterOverrides()[0] == expectedRate);
    CHECK(decodedEmitter.ParameterOverrides()[1] == expectedForce);
}

TEST_CASE("VFX particle cables define non-commutative CPU and GPU operation order")
{
    const auto beforeDefinition = OrderedGraph(true);
    const auto afterDefinition = OrderedGraph(false);
    const auto beforeProgram = Keire::CompileVfxEffect(beforeDefinition, Keire::VfxBackend::Cpu);
    const auto afterProgram = Keire::CompileVfxEffect(afterDefinition, Keire::VfxBackend::Cpu);
    REQUIRE(beforeProgram.Valid);
    REQUIRE(afterProgram.Valid);

    const auto simulate = [](const Keire::VfxEffectDefinition& definition)
    {
        auto world = Keire::CreateRef<Keire::VfxWorld>(
            Keire::VfxWorldSpecification{.MaximumEffects = 1, .MaximumParticles = 32});
        REQUIRE(world->Activate({Keire::CreateRef<Keire::VfxEffectAsset>(definition)}));
        world->Update(0.1F);
        world->Update(0.5F);
        return world->CaptureDebugSnapshot();
    };
    const auto before = simulate(beforeDefinition);
    const auto after = simulate(afterDefinition);
    REQUIRE(before.ParticleCount >= 1);
    REQUIRE(after.ParticleCount >= 1);
    CHECK(before.Particles[0].Velocity.Y == doctest::Approx(2.0F));
    CHECK(after.Particles[0].Velocity.Y == doctest::Approx(1.0F));
    CHECK(before.Particles[0].Position.Y == doctest::Approx(1.0F));
    CHECK(after.Particles[0].Position.Y == doctest::Approx(0.5F));

    const auto captureEmitter = [](const Keire::VfxEffectDefinition& definition)
    {
        auto world = Keire::CreateRef<Keire::VfxWorld>(Keire::VfxWorldSpecification{
            .MaximumEffects = 1, .MaximumParticles = 32, .Backend = Keire::VfxBackend::Gpu});
        REQUIRE(world->Activate({Keire::CreateRef<Keire::VfxEffectAsset>(definition)}));
        world->Update(0.1F);
        const auto snapshot = world->CaptureRenderSnapshot();
        REQUIRE(snapshot.GpuEmitters().size() == 1);
        return snapshot.GpuEmitters().front();
    };
    const auto beforeEmitter = captureEmitter(beforeDefinition);
    const auto afterEmitter = captureEmitter(afterDefinition);
    const auto operationPosition =
        [](const Keire::VfxGpuEmitter& emitter, const Keire::VfxGpuEmitter::ParticleOperationKind kind)
    {
        const auto operations = std::span(emitter.ParticleOperations).first(emitter.ParticleOperationCount);
        const auto found = std::ranges::find(operations, kind, &Keire::VfxGpuEmitter::ParticleOperation::Kind);
        REQUIRE(found != operations.end());
        return static_cast<std::size_t>(std::distance(operations.begin(), found));
    };
    CHECK(operationPosition(beforeEmitter, Keire::VfxGpuEmitter::ParticleOperationKind::CustomHlsl) <
          operationPosition(beforeEmitter, Keire::VfxGpuEmitter::ParticleOperationKind::Force));
    CHECK(operationPosition(beforeEmitter, Keire::VfxGpuEmitter::ParticleOperationKind::Force) <
          operationPosition(beforeEmitter, Keire::VfxGpuEmitter::ParticleOperationKind::Collision));
    CHECK(operationPosition(afterEmitter, Keire::VfxGpuEmitter::ParticleOperationKind::Force) <
          operationPosition(afterEmitter, Keire::VfxGpuEmitter::ParticleOperationKind::Collision));
    CHECK(operationPosition(afterEmitter, Keire::VfxGpuEmitter::ParticleOperationKind::Collision) <
          operationPosition(afterEmitter, Keire::VfxGpuEmitter::ParticleOperationKind::CustomHlsl));
}

TEST_CASE("Converting an executable VFX graph is idempotent")
{
    const auto definition = OrderedGraph(true);
    const auto converted = Keire::ConvertVfxEffectToGraph(definition);
    CHECK(converted == definition);
}

TEST_CASE("VFX authoring validation permits incomplete topology while runtime validation remains strict")
{
    auto definition = ExecutableGraph();
    auto& system = definition.Systems.front();
    const auto flowConnection = std::ranges::find_if(
        system.Connections,
        [&](const Keire::VfxGraphConnection& connection)
        {
            const auto node = std::ranges::find(system.Nodes, connection.OutputNode, &Keire::VfxGraphNode::Id);
            if (node == system.Nodes.end())
                return false;
            const auto pin = std::ranges::find(node->Pins, connection.OutputPin, &Keire::VfxGraphPin::Id);
            return pin != node->Pins.end() && pin->Type == Keire::VfxValueType::ParticleStream;
        });
    REQUIRE(flowConnection != system.Connections.end());
    system.Connections.erase(flowConnection);

    CHECK_NOTHROW(Keire::ValidateVfxEffectAuthoring(definition));
    CHECK_THROWS_AS(Keire::ValidateVfxEffect(definition), std::invalid_argument);
}

TEST_CASE("VFX authoring validation still rejects backward flow and cycles")
{
    SUBCASE("backward particle flow")
    {
        auto definition = ExecutableGraph();
        auto& system = definition.Systems.front();
        const auto initialize = std::ranges::find_if(system.Nodes,
                                                     [](const Keire::VfxGraphNode& node)
                                                     {
                                                         return node.Kind == Keire::VfxGraphNodeKind::Context &&
                                                                node.Context == Keire::VfxContextType::Initialize;
                                                     });
        const auto update = std::ranges::find_if(
            system.Nodes, [](const Keire::VfxGraphNode& node)
            { return node.Kind == Keire::VfxGraphNodeKind::Context && node.Context == Keire::VfxContextType::Update; });
        REQUIRE(initialize != system.Nodes.end());
        REQUIRE(update != system.Nodes.end());
        const auto updateOutput =
            std::ranges::find_if(update->Pins, [](const Keire::VfxGraphPin& pin)
                                 { return !pin.Input && pin.Type == Keire::VfxValueType::ParticleStream; });
        REQUIRE(updateOutput != update->Pins.end());
        const auto incoming =
            std::ranges::find(system.Connections, initialize->Id, &Keire::VfxGraphConnection::InputNode);
        REQUIRE(incoming != system.Connections.end());
        incoming->OutputNode = update->Id;
        incoming->OutputPin = updateOutput->Id;

        CHECK_THROWS_WITH_AS(Keire::ValidateVfxEffectAuthoring(definition),
                             "VFX particle-stream cables cannot travel backwards across contexts.",
                             std::invalid_argument);
    }

    SUBCASE("directed cycle")
    {
        auto definition = ExecutableGraph();
        auto& system = definition.Systems.front();
        const auto update = std::ranges::find_if(
            system.Nodes, [](const Keire::VfxGraphNode& node)
            { return node.Kind == Keire::VfxGraphNodeKind::Context && node.Context == Keire::VfxContextType::Update; });
        REQUIRE(update != system.Nodes.end());
        const auto updateInput =
            std::ranges::find_if(update->Pins, [](const Keire::VfxGraphPin& pin)
                                 { return pin.Input && pin.Type == Keire::VfxValueType::ParticleStream; });
        const auto updateOutput =
            std::ranges::find_if(update->Pins, [](const Keire::VfxGraphPin& pin)
                                 { return !pin.Input && pin.Type == Keire::VfxValueType::ParticleStream; });
        REQUIRE(updateInput != update->Pins.end());
        REQUIRE(updateOutput != update->Pins.end());

        const auto incoming = std::ranges::find(system.Connections, update->Id, &Keire::VfxGraphConnection::InputNode);
        REQUIRE(incoming != system.Connections.end());
        incoming->OutputNode = update->Id;
        incoming->OutputPin = updateOutput->Id;

        CHECK_THROWS_WITH_AS(Keire::ValidateVfxEffectAuthoring(definition),
                             "VFX graph must be a directed acyclic graph.", std::invalid_argument);
    }
}

TEST_CASE("VFX Emitter parameter overrides serialize canonically regardless of insertion order")
{
    const auto registration = Keire::CreateVfxEmitterComponentRegistration();
    auto ascending = registration.Factory();
    auto descending = registration.Factory();
    auto& ascendingEmitter = dynamic_cast<Keire::VfxEmitterComponent&>(*ascending);
    auto& descendingEmitter = dynamic_cast<Keire::VfxEmitterComponent&>(*descending);

    ascendingEmitter.SetParameterOverride({RuntimeId(20), 12.0F});
    ascendingEmitter.SetParameterOverride({RuntimeId(21), Keire::Vector3{1.0F, 2.0F, 3.0F}});
    descendingEmitter.SetParameterOverride({RuntimeId(21), Keire::Vector3{1.0F, 2.0F, 3.0F}});
    descendingEmitter.SetParameterOverride({RuntimeId(20), 12.0F});

    REQUIRE(descendingEmitter.ParameterOverrides().size() == 2);
    CHECK(descendingEmitter.ParameterOverrides()[0].Parameter == RuntimeId(20));
    CHECK(descendingEmitter.ParameterOverrides()[1].Parameter == RuntimeId(21));
    CHECK(std::get<std::string>(registration.Serialize(*ascending).at("parameterOverrides")) ==
          std::get<std::string>(registration.Serialize(*descending).at("parameterOverrides")));
}
