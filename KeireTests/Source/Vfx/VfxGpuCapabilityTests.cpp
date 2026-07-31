#include "Keire/Vfx/VfxSystem.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace
{
    template <typename T> [[nodiscard]] T& Payload(Keire::VfxEffectDefinition& definition)
    {
        const auto module = std::ranges::find_if(definition.Modules, [](const Keire::VfxModuleDefinition& candidate)
                                                 { return std::holds_alternative<T>(candidate.Payload); });
        REQUIRE(module != definition.Modules.end());
        return std::get<T>(module->Payload);
    }

    template <typename T> [[nodiscard]] Keire::AssetId ModuleId(const Keire::VfxEffectDefinition& definition)
    {
        const auto module = std::ranges::find_if(definition.Modules, [](const Keire::VfxModuleDefinition& candidate)
                                                 { return std::holds_alternative<T>(candidate.Payload); });
        REQUIRE(module != definition.Modules.end());
        return module->Id;
    }

    [[nodiscard]] Keire::VfxGraphNode& Context(Keire::VfxEffectDefinition& definition, const Keire::VfxContextType type)
    {
        REQUIRE(definition.Systems.size() == 1);
        const auto context =
            std::ranges::find_if(definition.Systems.front().Nodes, [type](const Keire::VfxGraphNode& node)
                                 { return node.Kind == Keire::VfxGraphNodeKind::Context && node.Context == type; });
        REQUIRE(context != definition.Systems.front().Nodes.end());
        return *context;
    }

    [[nodiscard]] Keire::AssetId ExecutionNode(const Keire::VfxEffectDefinition& definition,
                                               const Keire::AssetId module)
    {
        REQUIRE(definition.Systems.size() == 1);
        for (const auto& node : definition.Systems.front().Nodes)
        {
            const auto block = std::ranges::find(node.Blocks, module, &Keire::VfxGraphBlock::Reference);
            if (block != node.Blocks.end())
                return block->Id;
        }
        REQUIRE_MESSAGE(false, "The module's schema-4 execution Block was not found.");
        return {};
    }

    [[nodiscard]] Keire::VfxGraphPin& BlockPin(Keire::VfxEffectDefinition& definition, const Keire::AssetId module,
                                               const std::string_view semantic)
    {
        REQUIRE(definition.Systems.size() == 1);
        for (auto& node : definition.Systems.front().Nodes)
        {
            const auto block = std::ranges::find(node.Blocks, module, &Keire::VfxGraphBlock::Reference);
            if (block == node.Blocks.end())
                continue;
            const auto pin = std::ranges::find(block->Pins, semantic, &Keire::VfxGraphPin::Semantic);
            REQUIRE(pin != block->Pins.end());
            return *pin;
        }
        throw std::logic_error("The module's schema-4 execution Block was not found.");
    }

    template <typename T>
    Keire::AssetId AppendModule(Keire::VfxEffectDefinition& definition, const Keire::VfxContextType context, T payload)
    {
        Keire::VfxModuleDefinition module{Keire::AssetId::Generate(), true, std::move(payload)};
        auto block = Keire::CreateVfxGraphBlock(module);
        const auto blockId = block.Id;
        definition.Modules.push_back(std::move(module));
        Context(definition, context).Blocks.push_back(std::move(block));
        return blockId;
    }

    [[nodiscard]] bool HasErrorAt(const Keire::VfxCompiledProgram& program, const Keire::AssetId node,
                                  const std::string_view messageFragment)
    {
        return std::ranges::any_of(program.Diagnostics,
                                   [node, messageFragment](const Keire::VfxCompileDiagnostic& diagnostic)
                                   {
                                       return diagnostic.Severity == Keire::VfxCompileDiagnosticSeverity::Error &&
                                              diagnostic.Node == node &&
                                              diagnostic.Message.find(messageFragment) != std::string::npos;
                                   });
    }

    void CheckGpuError(const Keire::VfxEffectDefinition& definition, const Keire::AssetId node,
                       const std::string_view messageFragment)
    {
        const auto program = Keire::CompileVfxEffect(definition, Keire::VfxBackend::Gpu);
        for (const auto& diagnostic : program.Diagnostics)
            INFO("GPU diagnostic node " << diagnostic.Node.ToString() << ": " << diagnostic.Message);
        CHECK_FALSE(program.Valid);
        CHECK(HasErrorAt(program, node, messageFragment));
    }

    void CheckCpuError(const Keire::VfxEffectDefinition& definition, const Keire::AssetId node,
                       const std::string_view messageFragment)
    {
        const auto program = Keire::CompileVfxEffect(definition, Keire::VfxBackend::Cpu);
        for (const auto& diagnostic : program.Diagnostics)
            INFO("CPU diagnostic node " << diagnostic.Node.ToString() << ": " << diagnostic.Message);
        CHECK_FALSE(program.Valid);
        CHECK(HasErrorAt(program, node, messageFragment));
    }

    [[nodiscard]] std::string SizeInstructions(const std::size_t count)
    {
        std::string result;
        for (std::size_t index = 0; index < count; ++index)
            result += "Size += 0.0;";
        return result;
    }
} // namespace

TEST_CASE("GPU VFX capability validation preserves the exact supported baseline with dynamic execution records")
{
    auto definition = Keire::VfxEffectAsset::DefaultDefinition();
    Payload<Keire::VfxShapeModule>(definition).Shape = Keire::VfxShape::Cone;
    auto& initialize = Payload<Keire::VfxInitializeModule>(definition);
    initialize.RotationMinimum = {0.0F, 0.0F, -30.0F};
    initialize.RotationMaximum = {0.0F, 0.0F, 30.0F};
    const auto initializeId = ModuleId<Keire::VfxInitializeModule>(definition);
    BlockPin(definition, initializeId, "rotationMinimum").DefaultValue = initialize.RotationMinimum;
    BlockPin(definition, initializeId, "rotationMaximum").DefaultValue = initialize.RotationMaximum;
    Payload<Keire::VfxSizeOverLifetimeModule>(definition).Size =
        Keire::Curve1D({{0.0F, 1.0F}, {0.5F, 1.5F}, {1.0F, 2.0F}});
    Payload<Keire::VfxColorOverLifetimeModule>(definition).Color = Keire::ColorGradient(
        {{0.0F, {1.0F, 1.0F, 1.0F, 1.0F}}, {0.5F, {0.5F, 0.75F, 0.625F, 0.5F}}, {1.0F, {0.0F, 0.5F, 0.25F, 0.0F}}});
    AppendModule(definition, Keire::VfxContextType::Update, Keire::VfxForceModule{});
    AppendModule(definition, Keire::VfxContextType::Update, Keire::VfxCollisionModule{});
    Context(definition, Keire::VfxContextType::Update)
        .Blocks.push_back(Keire::CreateVfxGraphPortableHlslBlock(SizeInstructions(8)));

    const auto program = Keire::CompileVfxEffect(definition, Keire::VfxBackend::Gpu);
    CHECK(program.Valid);
    CHECK(program.CustomInstructions.size() == Keire::VfxGpuEmitter::MaximumCustomInstructions);
    CHECK(program.Operations.size() == Keire::VfxGpuEmitter::MaximumParticleOperations + 1);

    auto world = Keire::CreateRef<Keire::VfxWorld>(
        Keire::VfxWorldSpecification{.MaximumEffects = 1, .MaximumParticles = 32, .Backend = Keire::VfxBackend::Gpu});
    REQUIRE(world->Activate({Keire::CreateRef<Keire::VfxEffectAsset>(definition)}));
    world->Update(0.1F);
    const auto snapshot = world->CaptureRenderSnapshot();
    REQUIRE(snapshot.GpuEmitters().size() == 1);
    const auto& emitter = snapshot.GpuEmitters().front();
    REQUIRE(emitter.Execution);
    CHECK(emitter.Execution->CustomInstructions.size() == program.CustomInstructions.size());
    CHECK(emitter.Execution->ParticleOperations.size() >= emitter.ParticleOperationCount);
    CHECK(emitter.Shape == Keire::VfxShape::Cone);
    CHECK(emitter.RotationMinimum.Z == doctest::Approx(-30.0F));
    CHECK(emitter.RotationMaximum.Z == doctest::Approx(30.0F));
    CHECK(emitter.ConeAngleDegrees == doctest::Approx(25.0F));
    CHECK(emitter.ConeLength == doctest::Approx(1.0F));
}

TEST_CASE("GPU VFX rejects resource-backed spawn shapes at their schema-4 Block")
{
    SUBCASE("Mesh")
    {
        auto definition = Keire::VfxEffectAsset::DefaultDefinition();
        auto& shape = Payload<Keire::VfxShapeModule>(definition);
        shape.Shape = Keire::VfxShape::Mesh;
        shape.Mesh = Keire::AssetId::Generate();
        const auto shapeId = ModuleId<Keire::VfxShapeModule>(definition);
        BlockPin(definition, shapeId, "mesh").DefaultValue = shape.Mesh;
        const auto block = ExecutionNode(definition, shapeId);

        CheckGpuError(definition, block, "Mesh or Volume shape sampling");
    }

    SUBCASE("Volume")
    {
        auto definition = Keire::VfxEffectAsset::DefaultDefinition();
        auto& shape = Payload<Keire::VfxShapeModule>(definition);
        shape.Shape = Keire::VfxShape::Volume;
        shape.Volume = Keire::AssetId::Generate();
        const auto shapeId = ModuleId<Keire::VfxShapeModule>(definition);
        BlockPin(definition, shapeId, "volume").DefaultValue = shape.Volume;
        const auto block = ExecutionNode(definition, shapeId);

        CheckGpuError(definition, block, "Mesh or Volume shape sampling");
    }
}

TEST_CASE("GPU VFX rejects Sprite initialization rotations it cannot represent")
{
    auto definition = Keire::VfxEffectAsset::DefaultDefinition();
    auto& initialize = Payload<Keire::VfxInitializeModule>(definition);
    initialize.RotationMinimum = {10.0F, 0.0F, -30.0F};
    initialize.RotationMaximum = {20.0F, 0.0F, 30.0F};
    const auto initializeId = ModuleId<Keire::VfxInitializeModule>(definition);
    BlockPin(definition, initializeId, "rotationMinimum").DefaultValue = initialize.RotationMinimum;
    BlockPin(definition, initializeId, "rotationMaximum").DefaultValue = initialize.RotationMaximum;

    CheckGpuError(definition, ExecutionNode(definition, initializeId), "Z-axis initialization rotation only");
}

TEST_CASE("GPU VFX rejects curves and gradients that its endpoint ABI cannot represent")
{
    SUBCASE("Size curve")
    {
        auto definition = Keire::VfxEffectAsset::DefaultDefinition();
        Payload<Keire::VfxSizeOverLifetimeModule>(definition).Size =
            Keire::Curve1D({{0.0F, 1.0F}, {0.5F, 2.0F}, {1.0F, 1.0F}});
        const auto block = ExecutionNode(definition, ModuleId<Keire::VfxSizeOverLifetimeModule>(definition));

        CheckGpuError(definition, block, "size curves must be constant or an exactly linear");
    }

    SUBCASE("Color gradient")
    {
        auto definition = Keire::VfxEffectAsset::DefaultDefinition();
        Payload<Keire::VfxColorOverLifetimeModule>(definition).Color = Keire::ColorGradient(
            {{0.0F, {1.0F, 1.0F, 1.0F, 1.0F}}, {0.5F, {0.0F, 1.0F, 0.0F, 1.0F}}, {1.0F, {0.0F, 0.0F, 1.0F, 1.0F}}});
        const auto block = ExecutionNode(definition, ModuleId<Keire::VfxColorOverLifetimeModule>(definition));

        CheckGpuError(definition, block, "color gradients must be constant or exactly linear");
    }
}

TEST_CASE("GPU VFX rejects every unavailable collision mode at its schema-4 Block")
{
    constexpr std::array modes{Keire::VfxCollisionMode::Cpu, Keire::VfxCollisionMode::GpuDepth,
                               Keire::VfxCollisionMode::ScenePhysics};
    for (const auto mode : modes)
    {
        auto definition = Keire::VfxEffectAsset::DefaultDefinition();
        const auto block =
            AppendModule(definition, Keire::VfxContextType::Update, Keire::VfxCollisionModule{mode, 0.5F, false});

        CheckGpuError(definition, block, "collision mode must be None");
    }
}

TEST_CASE("GPU VFX capability validation ignores unscheduled compatibility payloads")
{
    auto definition = Keire::VfxEffectAsset::DefaultDefinition();
    const Keire::VfxModuleDefinition inertCollision{
        Keire::AssetId::Generate(), true, Keire::VfxCollisionModule{Keire::VfxCollisionMode::Cpu, 0.5F, false}};
    definition.Modules.insert(definition.Modules.begin(), inertCollision);

    const auto program = Keire::CompileVfxEffect(definition, Keire::VfxBackend::Gpu);
    REQUIRE(program.Valid);
    CHECK(std::ranges::find(program.Modules, inertCollision.Id, &Keire::VfxCompiledModule::Module) ==
          program.Modules.end());
    CHECK(std::ranges::none_of(program.Diagnostics, [](const Keire::VfxCompileDiagnostic& diagnostic)
                               { return diagnostic.Severity == Keire::VfxCompileDiagnosticSeverity::Error; }));
}

TEST_CASE("GPU VFX sums multiple scheduled emission-rate Blocks")
{
    auto definition = Keire::VfxEffectAsset::DefaultDefinition();
    Payload<Keire::VfxEmissionRateModule>(definition).ParticlesPerSecond = 10.0F;
    AppendModule(definition, Keire::VfxContextType::Spawn, Keire::VfxEmissionRateModule{15.0F});
    BlockPin(definition, ModuleId<Keire::VfxEmissionRateModule>(definition), "particlesPerSecond").DefaultValue = 10.0F;

    const auto program = Keire::CompileVfxEffect(definition, Keire::VfxBackend::Gpu);
    REQUIRE(program.Valid);
    auto world = Keire::CreateRef<Keire::VfxWorld>(
        Keire::VfxWorldSpecification{.MaximumEffects = 1, .MaximumParticles = 32, .Backend = Keire::VfxBackend::Gpu});
    REQUIRE(world->Activate({Keire::CreateRef<Keire::VfxEffectAsset>(definition)}));
    world->Update(0.2F);
    const auto snapshot = world->CaptureRenderSnapshot();
    REQUIRE(snapshot.GpuEmitters().size() == 1);
    CHECK(snapshot.GpuEmitters().front().SpawnSequence == 5);
}

TEST_CASE("GPU VFX rejects duplicate per-particle Blocks that share one fixed payload slot")
{
    auto definition = Keire::VfxEffectAsset::DefaultDefinition();
    AppendModule(definition, Keire::VfxContextType::Update, Keire::VfxForceModule{{1.0F, 0.0F, 0.0F}, 0.0F});
    const auto duplicate =
        AppendModule(definition, Keire::VfxContextType::Update, Keire::VfxForceModule{{0.0F, 1.0F, 0.0F}, 0.0F});

    CheckGpuError(definition, duplicate, "one Force Block");
}

TEST_CASE("GPU VFX rejects unavailable renderer configurations at the Output Block")
{
    SUBCASE("Mesh output")
    {
        auto definition = Keire::VfxEffectAsset::DefaultDefinition();
        auto& renderer = Payload<Keire::VfxRendererModule>(definition);
        renderer.Type = Keire::VfxRendererType::Mesh;
        renderer.Mesh = Keire::AssetId::Generate();
        const auto rendererId = ModuleId<Keire::VfxRendererModule>(definition);
        BlockPin(definition, rendererId, "mesh").DefaultValue = renderer.Mesh;
        const auto block = ExecutionNode(definition, rendererId);

        CheckGpuError(definition, block, "supports Sprite output only");
    }

    SUBCASE("Custom Sprite texture")
    {
        auto definition = Keire::VfxEffectAsset::DefaultDefinition();
        auto& renderer = Payload<Keire::VfxRendererModule>(definition);
        renderer.Sprite = Keire::AssetId::Generate();
        const auto rendererId = ModuleId<Keire::VfxRendererModule>(definition);
        BlockPin(definition, rendererId, "sprite").DefaultValue = renderer.Sprite;
        const auto block = ExecutionNode(definition, rendererId);

        CheckGpuError(definition, block, "does not support a custom texture");
    }
}

TEST_CASE("Portable Custom HLSL uses dynamic GPU records and reports its compiler safety bound")
{
    SUBCASE("Programs larger than the legacy cbuffer execute through the dynamic payload")
    {
        auto definition = Keire::VfxEffectAsset::DefaultDefinition();
        constexpr auto instructionCount = Keire::VfxGpuEmitter::MaximumParticleOperations + 1;
        auto block = Keire::CreateVfxGraphPortableHlslBlock(SizeInstructions(instructionCount));
        Context(definition, Keire::VfxContextType::Update).Blocks.push_back(std::move(block));

        const auto program = Keire::CompileVfxEffect(definition, Keire::VfxBackend::Gpu);
        REQUIRE(program.Valid);
        REQUIRE(program.CustomInstructions.size() == instructionCount);

        auto world = Keire::CreateRef<Keire::VfxWorld>(Keire::VfxWorldSpecification{
            .MaximumEffects = 1, .MaximumParticles = 32, .Backend = Keire::VfxBackend::Gpu});
        REQUIRE(world->Activate({Keire::CreateRef<Keire::VfxEffectAsset>(definition)}));
        world->Update(0.1F);
        const auto snapshot = world->CaptureRenderSnapshot();
        REQUIRE(snapshot.GpuEmitters().size() == 1);
        REQUIRE(snapshot.GpuEmitters().front().Execution);
        CHECK(snapshot.GpuEmitters().front().Execution->CustomInstructions.size() == instructionCount);
        CHECK(snapshot.GpuEmitters().front().Execution->ParticleOperations.size() >
              Keire::VfxGpuEmitter::MaximumParticleOperations);
    }

    SUBCASE("The compiler safety limit identifies the overflowing Block")
    {
        auto definition = Keire::VfxEffectAsset::DefaultDefinition();
        auto overflow = Keire::CreateVfxGraphPortableHlslBlock(SizeInstructions(4097));
        const auto overflowId = overflow.Id;
        Context(definition, Keire::VfxContextType::Update).Blocks.push_back(std::move(overflow));

        CheckGpuError(definition, overflowId, "4096-instruction compiler safety limit");
    }
}

TEST_CASE("CPU VFX rejects renderer inputs its built-in output cannot consume")
{
    SUBCASE("Schema-4 custom Sprite")
    {
        auto definition = Keire::VfxEffectAsset::DefaultDefinition();
        auto& renderer = Payload<Keire::VfxRendererModule>(definition);
        renderer.Sprite = Keire::AssetId::Generate();
        const auto rendererId = ModuleId<Keire::VfxRendererModule>(definition);
        BlockPin(definition, rendererId, "sprite").DefaultValue = renderer.Sprite;

        CheckCpuError(definition, ExecutionNode(definition, rendererId), "does not yet sample a custom texture");
    }

    SUBCASE("Legacy custom Sprite remains loadable with a warning")
    {
        auto definition = Keire::VfxEffectAsset::DefaultDefinition();
        definition.ExecutionSource = Keire::VfxExecutionSource::LegacyModules;
        Payload<Keire::VfxRendererModule>(definition).Sprite = Keire::AssetId::Generate();

        const auto program = Keire::CompileVfxEffect(definition, Keire::VfxBackend::Cpu);
        CHECK(program.Valid);
        CHECK(std::ranges::any_of(program.Diagnostics,
                                  [](const Keire::VfxCompileDiagnostic& diagnostic)
                                  {
                                      return diagnostic.Severity == Keire::VfxCompileDiagnosticSeverity::Warning &&
                                             diagnostic.Message.find("does not yet sample a custom texture") !=
                                                 std::string::npos;
                                  }));
    }
}

TEST_CASE("CPU VFX keeps Sprite rotation honest while accepting full Mesh rotation")
{
    auto definition = Keire::VfxEffectAsset::DefaultDefinition();
    auto& initialize = Payload<Keire::VfxInitializeModule>(definition);
    initialize.RotationMinimum = {10.0F, 20.0F, -30.0F};
    initialize.RotationMaximum = {20.0F, 30.0F, 30.0F};
    const auto initializeId = ModuleId<Keire::VfxInitializeModule>(definition);
    BlockPin(definition, initializeId, "rotationMinimum").DefaultValue = initialize.RotationMinimum;
    BlockPin(definition, initializeId, "rotationMaximum").DefaultValue = initialize.RotationMaximum;

    CheckCpuError(definition, ExecutionNode(definition, initializeId), "Z-axis initialization rotation only");

    auto& renderer = Payload<Keire::VfxRendererModule>(definition);
    renderer.Type = Keire::VfxRendererType::Mesh;
    renderer.Mesh = Keire::AssetId::Generate();
    const auto rendererId = ModuleId<Keire::VfxRendererModule>(definition);
    BlockPin(definition, rendererId, "mesh").DefaultValue = renderer.Mesh;
    const auto meshProgram = Keire::CompileVfxEffect(definition, Keire::VfxBackend::Cpu);
    CHECK(meshProgram.Valid);
}

TEST_CASE("live VFX overrides transactionally revalidate backend capabilities")
{
    auto definition = Keire::VfxEffectAsset::DefaultDefinition();
    const auto parameterId = Keire::AssetId::Generate();
    const auto parameterNodeId = Keire::AssetId::Generate();
    const auto parameterPinId = Keire::AssetId::Generate();
    definition.Blackboard.push_back({parameterId, "Sprite", Keire::VfxValueType::Texture, Keire::AssetId{}, true});

    Keire::VfxGraphNode parameter;
    parameter.Id = parameterNodeId;
    parameter.Type = "Sprite";
    parameter.Context = Keire::VfxContextType::Output;
    parameter.Kind = Keire::VfxGraphNodeKind::Parameter;
    parameter.Reference = parameterId;
    parameter.TypeId.Value = "keire.parameter";
    parameter.Pins.push_back({parameterPinId, "Sprite", Keire::VfxValueType::Texture, false, "value", std::nullopt});
    definition.Systems.front().Nodes.push_back(std::move(parameter));

    const auto rendererId = ModuleId<Keire::VfxRendererModule>(definition);
    auto& output = Context(definition, Keire::VfxContextType::Output);
    const auto renderer = std::ranges::find(output.Blocks, rendererId, &Keire::VfxGraphBlock::Reference);
    REQUIRE(renderer != output.Blocks.end());
    const auto sprite = std::ranges::find(renderer->Pins, std::string("sprite"), &Keire::VfxGraphPin::Semantic);
    REQUIRE(sprite != renderer->Pins.end());
    Keire::VfxGraphConnection connection;
    connection.Id = Keire::AssetId::Generate();
    connection.OutputNode = parameterNodeId;
    connection.OutputPin = parameterPinId;
    connection.InputNode = output.Id;
    connection.InputBlock = renderer->Id;
    connection.InputPin = sprite->Id;
    definition.Systems.front().Connections.push_back(connection);

    const auto effect = Keire::CreateRef<Keire::VfxEffectAsset>(definition);
    for (const auto backend : {Keire::VfxBackend::Cpu, Keire::VfxBackend::Gpu})
    {
        auto world = Keire::CreateRef<Keire::VfxWorld>(
            Keire::VfxWorldSpecification{.MaximumEffects = 1, .MaximumParticles = 16, .Backend = backend});
        const auto handle = world->Activate({effect});
        REQUIRE(handle);
        CHECK_THROWS_WITH_AS(world->SetParameter(handle, parameterId, Keire::AssetId::Generate()),
                             doctest::Contains("custom texture"), std::invalid_argument);
        CHECK(world->IsAlive(handle));
    }
}
