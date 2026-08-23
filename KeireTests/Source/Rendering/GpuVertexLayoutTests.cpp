#include "KeireInternal/Rendering/RenderBackendInternal.h"

#include <doctest/doctest.h>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <limits>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

TEST_CASE("GPU skinning vertex storage uses explicit 16-byte lanes")
{
    using Keire::RenderBackend::GpuMeshVertex;
    using Keire::RenderBackend::GpuRenderVertex;

    CHECK(alignof(GpuMeshVertex) == 16);
    CHECK(sizeof(GpuMeshVertex) == 96);
    CHECK(offsetof(GpuMeshVertex, Position) == 0);
    CHECK(offsetof(GpuMeshVertex, Normal) == 16);
    CHECK(offsetof(GpuMeshVertex, UV0) == 32);
    CHECK(offsetof(GpuMeshVertex, VertexColor) == 48);
    CHECK(offsetof(GpuMeshVertex, Tangent) == 64);
    CHECK(offsetof(GpuMeshVertex, UV1) == 80);

    CHECK(alignof(GpuRenderVertex) == 16);
    CHECK(sizeof(GpuRenderVertex) == 48);
    CHECK(offsetof(GpuRenderVertex, Position) == 0);
    CHECK(offsetof(GpuRenderVertex, Color) == 16);
    CHECK(offsetof(GpuRenderVertex, Normal) == 32);
}

TEST_CASE("Editor grid shader reconstructs an infinite plane and writes scene depth")
{
    CHECK(sizeof(Keire::RenderBackend::GridUniforms) == sizeof(float) * 52);

    std::ifstream stream("KeireCore/Shaders/BuiltinGrid.hlsl", std::ios::binary);
    REQUIRE(stream.good());
    const std::string shader{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    CHECK(shader.find("SV_Depth") != std::string::npos);
    CHECK(shader.find("cameraWorld.xyz + rayDirection * rayDistance") != std::string::npos);
    CHECK(shader.find("GridLine(worldPosition.xz") != std::string::npos);
}

TEST_CASE("GPU skinning shader storage matches the mesh vertex lane layout")
{
    std::ifstream stream("KeireCore/Shaders/BuiltinSkinning.hlsl", std::ios::binary);
    REQUIRE(stream.good());
    const std::string shader{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    const auto structure = shader.find("struct AssetVertex");
    REQUIRE(structure != std::string::npos);
    const auto end = shader.find("};", structure);
    REQUIRE(end != std::string::npos);

    const std::array fields{"float4 Position;",    "float4 Normal;",  "float4 UV0;",
                            "float4 VertexColor;", "float4 Tangent;", "float4 UV1;"};
    auto cursor = structure;
    for (const auto field : fields)
    {
        cursor = shader.find(field, cursor);
        REQUIRE(cursor != std::string::npos);
        REQUIRE(cursor < end);
        cursor += std::string_view(field).size();
    }

    std::istringstream lines(shader.substr(structure, end - structure));
    std::size_t shaderLaneCount = 0;
    for (std::string line; std::getline(lines, line);)
        if (line.find("float4 ") != std::string::npos)
            ++shaderLaneCount;
    CHECK(shaderLaneCount == sizeof(Keire::RenderBackend::GpuMeshVertex) / sizeof(Keire::Vector4));
}

TEST_CASE("CPU mesh VFX particles enter material-aware scene rendering")
{
    Keire::VfxRenderParticle particle;
    particle.Renderer = Keire::VfxRendererType::Mesh;
    particle.Mesh = Keire::AssetId::Parse("e83e979d-d16f-4c8b-935d-29611f209a15");
    particle.Position = {1.0F, 2.0F, 3.0F};
    particle.Rotation = {15.0F, 30.0F, 45.0F};
    particle.Size = 0.75F;
    particle.Tint = {1.0F, 0.25F, 0.05F, 0.8F};

    const auto item = Keire::RenderBackend::VfxMeshDrawItem(particle);
    REQUIRE(item);
    CHECK(item->Mesh == particle.Mesh);
    CHECK(item->Materials.empty());
    CHECK(item->Tint == particle.Tint);
    CHECK_FALSE(item->CastShadows);
    CHECK(item->ReceiveShadows);
    Keire::Vector3 position;
    Keire::Quaternion rotation;
    Keire::Vector3 scale;
    REQUIRE(Keire::Math::DecomposeTransform(item->World, position, rotation, scale));
    CHECK(position == particle.Position);
    CHECK(scale.X == doctest::Approx(particle.Size));
    CHECK(scale.Y == doctest::Approx(particle.Size));
    CHECK(scale.Z == doctest::Approx(particle.Size));

    particle.Renderer = Keire::VfxRendererType::Sprite;
    CHECK_FALSE(Keire::RenderBackend::VfxMeshDrawItem(particle));
}

TEST_CASE("GPU linear blend skinning uses supported compute backends")
{
    using Keire::RenderBackend::SupportsComputeSkinning;

    CHECK(SupportsComputeSkinning("direct3d12", Keire::SkinningMethod::LinearBlend));
    CHECK(SupportsComputeSkinning("vulkan", Keire::SkinningMethod::LinearBlend));
    CHECK(SupportsComputeSkinning("metal", Keire::SkinningMethod::LinearBlend));
    CHECK_FALSE(SupportsComputeSkinning("vulkan", Keire::SkinningMethod::DualQuaternion));
    CHECK_FALSE(SupportsComputeSkinning({}, Keire::SkinningMethod::LinearBlend));
}

TEST_CASE("GPU skinning output slots stay bounded by frames in flight")
{
    using Keire::RenderBackend::SkinningOutputSlot;

    CHECK(SkinningOutputSlot(0, 3) == 0);
    CHECK(SkinningOutputSlot(1, 3) == 0);
    CHECK(SkinningOutputSlot(2, 3) == 1);
    CHECK(SkinningOutputSlot(3, 3) == 2);
    CHECK(SkinningOutputSlot(4, 3) == 0);
    CHECK(SkinningOutputSlot(10, 3) == 0);
    CHECK(SkinningOutputSlot(10, 0) == 0);
}

TEST_CASE("GPU skinning instances isolate writable outputs by render surface")
{
    using Keire::AssetId;
    using Keire::EntityId;
    using Keire::RenderBackend::GpuSkinInstanceKey;
    using Keire::RenderBackend::GpuSkinInstanceKeyHash;

    const auto scene = AssetId::Parse("711ace00-0000-4000-8000-000000000021");
    const auto entity = EntityId::Parse("711ace00-0000-4000-8000-000000000022");
    const GpuSkinInstanceKey sceneView{scene, entity, 17};
    const GpuSkinInstanceKey gameView{scene, entity, 18};

    CHECK(sceneView != gameView);
    CHECK(GpuSkinInstanceKeyHash{}(sceneView) != GpuSkinInstanceKeyHash{}(gameView));
}

TEST_CASE("GPU VFX sequencing distinguishes document snapshots from simulation steps")
{
    Keire::RenderBackend::GpuVfxWorldResources resources;

    CHECK(resources.ShouldApplySnapshot(1));
    CHECK(resources.ShouldConsumeSimulationStep(1));
    resources.MarkSnapshotApplied(1);
    resources.MarkSimulationStepConsumed(1);

    CHECK_FALSE(resources.ShouldApplySnapshot(1));
    CHECK_FALSE(resources.ShouldConsumeSimulationStep(1));
    CHECK(resources.ShouldApplySnapshot(2));
    CHECK_FALSE(resources.ShouldConsumeSimulationStep(1));
    resources.MarkSnapshotApplied(2);

    CHECK_FALSE(resources.ShouldApplySnapshot(1));
    CHECK(resources.ShouldConsumeSimulationStep(2));
    resources.MarkSimulationStepConsumed(2);
    resources.InvalidateSequencing();

    CHECK(resources.ShouldApplySnapshot(2));
    CHECK(resources.ShouldConsumeSimulationStep(2));
}

TEST_CASE("GPU VFX physical pools follow live system budgets without shrinking")
{
    using Keire::RenderBackend::GpuVfxActiveParticleBudget;
    using Keire::RenderBackend::SelectGpuVfxPoolCapacity;

    std::array<Keire::VfxGpuEmitter, 2> emitters;
    emitters[0].Capacity = 4096;
    emitters[1].Capacity = 3584;

    CHECK(SelectGpuVfxPoolCapacity(1'000'000, 0, {}) == 0);
    CHECK(GpuVfxActiveParticleBudget(1'000'000, emitters) == 7680);
    CHECK(SelectGpuVfxPoolCapacity(1'000'000, 0, emitters) == 16'384);
    CHECK(SelectGpuVfxPoolCapacity(32'768, 0, emitters) == 16'384);
    CHECK(SelectGpuVfxPoolCapacity(8192, 0, emitters) == 8192);

    emitters[0].Capacity = 60'000;
    emitters[1].Capacity = 10'000;
    CHECK(SelectGpuVfxPoolCapacity(1'000'000, 0, emitters) == 131'072);
    CHECK(SelectGpuVfxPoolCapacity(1'000'000, 262'144, emitters) == 262'144);

    emitters[0].Capacity = std::numeric_limits<std::uint32_t>::max();
    emitters[1].Capacity = std::numeric_limits<std::uint32_t>::max();
    CHECK(GpuVfxActiveParticleBudget(1'000'000, emitters) == 1'000'000);
    CHECK(SelectGpuVfxPoolCapacity(1'000'000, 0, emitters) == 1'000'000);
}

TEST_CASE("GPU VFX execution wire records remain explicit 16-byte lanes")
{
    using Keire::RenderBackend::VfxGpuCustomInstructionRecord;
    using Keire::RenderBackend::VfxGpuModulePropertyRecord;
    using Keire::RenderBackend::VfxGpuParticleOperationRecord;

    CHECK(alignof(VfxGpuCustomInstructionRecord) == 16);
    CHECK(sizeof(VfxGpuCustomInstructionRecord) == 32);
    CHECK(offsetof(VfxGpuCustomInstructionRecord, Metadata) == 0);
    CHECK(offsetof(VfxGpuCustomInstructionRecord, Operand) == 16);
    CHECK(alignof(VfxGpuParticleOperationRecord) == 16);
    CHECK(sizeof(VfxGpuParticleOperationRecord) == 32);
    CHECK(offsetof(VfxGpuParticleOperationRecord, Metadata) == 0);
    CHECK(offsetof(VfxGpuParticleOperationRecord, Payload) == 16);
    CHECK(alignof(VfxGpuModulePropertyRecord) == 16);
    CHECK(sizeof(VfxGpuModulePropertyRecord) == 48);
    CHECK(offsetof(VfxGpuModulePropertyRecord, Metadata) == 0);
    CHECK(offsetof(VfxGpuModulePropertyRecord, LiteralValue) == 16);
}

TEST_CASE("GPU VFX renderer validation accepts dynamic operation tables beyond the legacy cbuffer limits")
{
    Keire::VfxGpuExecutionPayload payload;
    payload.CustomInstructions.resize(9);
    for (auto& instruction : payload.CustomInstructions)
    {
        instruction.Context = Keire::VfxContextType::Update;
        instruction.Target = Keire::VfxCustomTarget::Size;
        instruction.Operation = Keire::VfxCustomOperation::Add;
        instruction.Operand = {0.25F, 0.0F, 0.0F, 0.0F};
    }
    payload.ParticleOperations.resize(16);
    for (std::size_t index = 0; index < payload.ParticleOperations.size(); ++index)
    {
        payload.ParticleOperations[index].Context = Keire::VfxContextType::Update;
        payload.ParticleOperations[index].Kind = Keire::VfxGpuParticleOperationKind::CustomHlsl;
        payload.ParticleOperations[index].Index = static_cast<std::uint32_t>(index % payload.CustomInstructions.size());
    }

    CHECK_FALSE(Keire::RenderBackend::ValidateGpuVfxExecutionPayload(payload).has_value());
}

TEST_CASE("GPU VFX renderer validation accepts the production built-in context schedule")
{
    const auto definition = Keire::VfxEffectAsset::DefaultDefinition();
    auto world = Keire::CreateRef<Keire::VfxWorld>(
        Keire::VfxWorldSpecification{.MaximumEffects = 1, .MaximumParticles = 16, .Backend = Keire::VfxBackend::Gpu});
    REQUIRE(world->Activate({Keire::CreateRef<Keire::VfxEffectAsset>(definition)}));
    world->Update(0.1F);
    const auto snapshot = world->CaptureRenderSnapshot();
    REQUIRE(snapshot.GpuEmitters().size() == 1);
    REQUIRE(snapshot.GpuEmitters().front().Execution);
    CHECK_FALSE(
        Keire::RenderBackend::ValidateGpuVfxExecutionPayload(*snapshot.GpuEmitters().front().Execution).has_value());
}

TEST_CASE("GPU VFX renderer validation accepts every packed Combine and Split vector signature")
{
    for (const auto type : {Keire::VfxValueType::Vector2, Keire::VfxValueType::Vector3, Keire::VfxValueType::Vector4,
                            Keire::VfxValueType::Color})
    {
        const std::uint32_t componentCount = type == Keire::VfxValueType::Vector2   ? 2U
                                             : type == Keire::VfxValueType::Vector3 ? 3U
                                                                                    : 4U;
        Keire::VfxGpuExecutionPayload payload;
        payload.ValueProgram.RegisterCount = componentCount + 1U;
        for (std::uint32_t component = 0; component < componentCount; ++component)
        {
            payload.ValueProgram.Constants.push_back(
                {{{std::bit_cast<std::uint32_t>(static_cast<float>(component + 1U)), 0U, 0U, 0U}}, {}});
            payload.ValueProgram.Sources.push_back({static_cast<std::uint32_t>(Keire::VfxGpuValueSourceKind::Literal),
                                                    static_cast<std::uint32_t>(Keire::VfxValueType::Scalar), component,
                                                    0U});
        }

        Keire::VfxGpuValueInstruction combine;
        combine.Header = {static_cast<std::uint32_t>(Keire::VfxValueOpcode::Combine), static_cast<std::uint32_t>(type),
                          static_cast<std::uint32_t>(Keire::VfxContextType::Update),
                          static_cast<std::uint32_t>(Keire::VfxEvaluationDomain::PerParticleUpdate)};
        combine.Output = {0U, 0U, 0U, componentCount};
        payload.ValueProgram.Instructions.push_back(combine);

        for (std::uint32_t component = 0; component < componentCount; ++component)
        {
            payload.ValueProgram.Sources.push_back({static_cast<std::uint32_t>(Keire::VfxGpuValueSourceKind::Register),
                                                    static_cast<std::uint32_t>(type), 0U, 0U});
            Keire::VfxGpuValueInstruction split;
            split.Header = {static_cast<std::uint32_t>(Keire::VfxValueOpcode::Split),
                            static_cast<std::uint32_t>(Keire::VfxValueType::Scalar),
                            static_cast<std::uint32_t>(Keire::VfxContextType::Update),
                            static_cast<std::uint32_t>(Keire::VfxEvaluationDomain::PerParticleUpdate)};
            split.Output = {component + 1U, component, componentCount + component, 1U};
            payload.ValueProgram.Instructions.push_back(split);
        }

        CHECK_FALSE(Keire::RenderBackend::ValidateGpuVfxExecutionPayload(payload).has_value());
    }
}

TEST_CASE("GPU VFX renderer validation accepts the Unity core utility opcode signatures")
{
    struct Signature
    {
        Keire::VfxValueOpcode Opcode;
        Keire::VfxValueType Output;
        std::vector<Keire::VfxValueType> Inputs;
        std::uint32_t OutputIndex = 0;
    };
    const std::array signatures{
        Signature{Keire::VfxValueOpcode::AgeOverLifetime, Keire::VfxValueType::Scalar, {}},
        Signature{Keire::VfxValueOpcode::BitwiseAnd,
                  Keire::VfxValueType::UnsignedInteger,
                  {Keire::VfxValueType::UnsignedInteger, Keire::VfxValueType::UnsignedInteger}},
        Signature{Keire::VfxValueOpcode::BitwiseComplement,
                  Keire::VfxValueType::UnsignedInteger,
                  {Keire::VfxValueType::UnsignedInteger}},
        Signature{Keire::VfxValueOpcode::BitwiseLeftShift,
                  Keire::VfxValueType::UnsignedInteger,
                  {Keire::VfxValueType::UnsignedInteger, Keire::VfxValueType::UnsignedInteger}},
        Signature{Keire::VfxValueOpcode::BitwiseOr,
                  Keire::VfxValueType::UnsignedInteger,
                  {Keire::VfxValueType::UnsignedInteger, Keire::VfxValueType::UnsignedInteger}},
        Signature{Keire::VfxValueOpcode::BitwiseRightShift,
                  Keire::VfxValueType::UnsignedInteger,
                  {Keire::VfxValueType::UnsignedInteger, Keire::VfxValueType::UnsignedInteger}},
        Signature{Keire::VfxValueOpcode::BitwiseXor,
                  Keire::VfxValueType::UnsignedInteger,
                  {Keire::VfxValueType::UnsignedInteger, Keire::VfxValueType::UnsignedInteger}},
        Signature{Keire::VfxValueOpcode::ColorLuma, Keire::VfxValueType::Scalar, {Keire::VfxValueType::Color}},
        Signature{Keire::VfxValueOpcode::HsvToRgb, Keire::VfxValueType::Vector4, {Keire::VfxValueType::Vector3}},
        Signature{Keire::VfxValueOpcode::RgbToHsv, Keire::VfxValueType::Vector3, {Keire::VfxValueType::Color}},
        Signature{Keire::VfxValueOpcode::Discretize,
                  Keire::VfxValueType::Scalar,
                  {Keire::VfxValueType::Scalar, Keire::VfxValueType::Scalar}},
        Signature{Keire::VfxValueOpcode::FrameIndex, Keire::VfxValueType::UnsignedInteger, {}},
        Signature{Keire::VfxValueOpcode::InverseLerp,
                  Keire::VfxValueType::Scalar,
                  {Keire::VfxValueType::Scalar, Keire::VfxValueType::Scalar, Keire::VfxValueType::Scalar}},
        Signature{Keire::VfxValueOpcode::Modulo,
                  Keire::VfxValueType::Scalar,
                  {Keire::VfxValueType::Scalar, Keire::VfxValueType::Scalar}},
        Signature{Keire::VfxValueOpcode::BooleanNand,
                  Keire::VfxValueType::Boolean,
                  {Keire::VfxValueType::Boolean, Keire::VfxValueType::Boolean}},
        Signature{Keire::VfxValueOpcode::BooleanNor,
                  Keire::VfxValueType::Boolean,
                  {Keire::VfxValueType::Boolean, Keire::VfxValueType::Boolean}},
        Signature{Keire::VfxValueOpcode::OneMinus, Keire::VfxValueType::Scalar, {Keire::VfxValueType::Scalar}},
        Signature{Keire::VfxValueOpcode::Reciprocal, Keire::VfxValueType::Scalar, {Keire::VfxValueType::Scalar}},
        Signature{Keire::VfxValueOpcode::SquaredDistance,
                  Keire::VfxValueType::Scalar,
                  {Keire::VfxValueType::Vector3, Keire::VfxValueType::Vector3}},
        Signature{Keire::VfxValueOpcode::SquaredLength, Keire::VfxValueType::Scalar, {Keire::VfxValueType::Vector3}},
        Signature{Keire::VfxValueOpcode::SystemSeed, Keire::VfxValueType::UnsignedInteger, {}},
        Signature{Keire::VfxValueOpcode::AttributeAlive, Keire::VfxValueType::Boolean, {}},
        Signature{Keire::VfxValueOpcode::AttributeAlpha, Keire::VfxValueType::Scalar, {}},
        Signature{Keire::VfxValueOpcode::AttributeAngle, Keire::VfxValueType::Vector3, {}},
        Signature{Keire::VfxValueOpcode::AttributeAxisX, Keire::VfxValueType::Vector3, {}},
        Signature{Keire::VfxValueOpcode::AttributeAxisY, Keire::VfxValueType::Vector3, {}},
        Signature{Keire::VfxValueOpcode::AttributeAxisZ, Keire::VfxValueType::Vector3, {}},
        Signature{Keire::VfxValueOpcode::AttributeColor, Keire::VfxValueType::Vector3, {}},
        Signature{Keire::VfxValueOpcode::AttributeOldPosition, Keire::VfxValueType::Vector3, {}},
        Signature{Keire::VfxValueOpcode::AttributeParticleCountInStrip, Keire::VfxValueType::UnsignedInteger, {}},
        Signature{Keire::VfxValueOpcode::AttributeParticleIndexInStrip, Keire::VfxValueType::UnsignedInteger, {}},
        Signature{Keire::VfxValueOpcode::AttributePosition, Keire::VfxValueType::Vector3, {}},
        Signature{Keire::VfxValueOpcode::AttributeSeed, Keire::VfxValueType::UnsignedInteger, {}},
        Signature{Keire::VfxValueOpcode::AttributeSize, Keire::VfxValueType::Scalar, {}},
        Signature{Keire::VfxValueOpcode::AttributeSpawnTime, Keire::VfxValueType::Scalar, {}},
        Signature{Keire::VfxValueOpcode::AttributeStripIndex, Keire::VfxValueType::UnsignedInteger, {}},
        Signature{Keire::VfxValueOpcode::AttributeVelocity, Keire::VfxValueType::Vector3, {}},
        Signature{Keire::VfxValueOpcode::RatioOverStrip, Keire::VfxValueType::Scalar, {}},
        Signature{Keire::VfxValueOpcode::Epsilon, Keire::VfxValueType::Scalar, {}},
        Signature{Keire::VfxValueOpcode::Pi, Keire::VfxValueType::Scalar, {}, 3U},
        Signature{Keire::VfxValueOpcode::ValueNoise,
                  Keire::VfxValueType::Vector3,
                  {Keire::VfxValueType::Vector3, Keire::VfxValueType::Scalar, Keire::VfxValueType::Integer,
                   Keire::VfxValueType::Scalar, Keire::VfxValueType::Scalar, Keire::VfxValueType::Vector2},
                  1U},
        Signature{Keire::VfxValueOpcode::PerlinNoise,
                  Keire::VfxValueType::Scalar,
                  {Keire::VfxValueType::Vector3, Keire::VfxValueType::Scalar, Keire::VfxValueType::Integer,
                   Keire::VfxValueType::Scalar, Keire::VfxValueType::Scalar, Keire::VfxValueType::Vector2}},
        Signature{Keire::VfxValueOpcode::CellularNoise,
                  Keire::VfxValueType::Scalar,
                  {Keire::VfxValueType::Vector3, Keire::VfxValueType::Scalar, Keire::VfxValueType::Integer,
                   Keire::VfxValueType::Scalar, Keire::VfxValueType::Scalar, Keire::VfxValueType::Vector2}},
        Signature{Keire::VfxValueOpcode::ValueCurlNoise,
                  Keire::VfxValueType::Vector3,
                  {Keire::VfxValueType::Vector3, Keire::VfxValueType::Scalar, Keire::VfxValueType::Integer,
                   Keire::VfxValueType::Scalar, Keire::VfxValueType::Scalar, Keire::VfxValueType::Scalar}},
        Signature{Keire::VfxValueOpcode::PerlinCurlNoise,
                  Keire::VfxValueType::Vector3,
                  {Keire::VfxValueType::Vector3, Keire::VfxValueType::Scalar, Keire::VfxValueType::Integer,
                   Keire::VfxValueType::Scalar, Keire::VfxValueType::Scalar, Keire::VfxValueType::Scalar}},
        Signature{Keire::VfxValueOpcode::CellularCurlNoise,
                  Keire::VfxValueType::Vector3,
                  {Keire::VfxValueType::Vector3, Keire::VfxValueType::Scalar, Keire::VfxValueType::Integer,
                   Keire::VfxValueType::Scalar, Keire::VfxValueType::Scalar, Keire::VfxValueType::Scalar}},
        Signature{Keire::VfxValueOpcode::PolarToRectangular,
                  Keire::VfxValueType::Vector2,
                  {Keire::VfxValueType::Scalar, Keire::VfxValueType::Scalar}},
        Signature{
            Keire::VfxValueOpcode::RectangularToPolar, Keire::VfxValueType::Scalar, {Keire::VfxValueType::Vector2}, 1U},
        Signature{Keire::VfxValueOpcode::RectangularToSpherical,
                  Keire::VfxValueType::Scalar,
                  {Keire::VfxValueType::Vector3},
                  2U},
        Signature{Keire::VfxValueOpcode::SphericalToRectangular,
                  Keire::VfxValueType::Vector3,
                  {Keire::VfxValueType::Scalar, Keire::VfxValueType::Scalar, Keire::VfxValueType::Scalar}},
        Signature{Keire::VfxValueOpcode::Rotate2D,
                  Keire::VfxValueType::Vector2,
                  {Keire::VfxValueType::Vector2, Keire::VfxValueType::Vector2, Keire::VfxValueType::Scalar}},
        Signature{Keire::VfxValueOpcode::Rotate3D,
                  Keire::VfxValueType::Vector3,
                  {Keire::VfxValueType::Vector3, Keire::VfxValueType::Vector3, Keire::VfxValueType::Vector3,
                   Keire::VfxValueType::Scalar}},
        Signature{Keire::VfxValueOpcode::Passthrough,
                  Keire::VfxValueType::Vector3,
                  {Keire::VfxValueType::Scalar, Keire::VfxValueType::Vector3},
                  1U},
        Signature{Keire::VfxValueOpcode::AttributeDirection, Keire::VfxValueType::Vector3, {}},
        Signature{Keire::VfxValueOpcode::AttributeMass, Keire::VfxValueType::Scalar, {}},
        Signature{Keire::VfxValueOpcode::AttributeTextureIndex, Keire::VfxValueType::UnsignedInteger, {}},
        Signature{Keire::VfxValueOpcode::WeightedSelect,
                  Keire::VfxValueType::Vector3,
                  {Keire::VfxValueType::Vector3, Keire::VfxValueType::Vector3, Keire::VfxValueType::Scalar,
                   Keire::VfxValueType::Scalar, Keire::VfxValueType::Scalar}},
        Signature{Keire::VfxValueOpcode::LookAtDirection,
                  Keire::VfxValueType::Vector3,
                  {Keire::VfxValueType::Vector3, Keire::VfxValueType::Vector3}},
        Signature{Keire::VfxValueOpcode::SampleBezier,
                  Keire::VfxValueType::Vector3,
                  {Keire::VfxValueType::Vector3, Keire::VfxValueType::Vector3, Keire::VfxValueType::Vector3,
                   Keire::VfxValueType::Vector3, Keire::VfxValueType::Scalar}},
        Signature{Keire::VfxValueOpcode::Swizzle,
                  Keire::VfxValueType::Vector4,
                  {Keire::VfxValueType::Vector4, Keire::VfxValueType::UnsignedInteger,
                   Keire::VfxValueType::UnsignedInteger, Keire::VfxValueType::UnsignedInteger,
                   Keire::VfxValueType::UnsignedInteger}},
        Signature{Keire::VfxValueOpcode::AreaCircle, Keire::VfxValueType::Scalar, {Keire::VfxValueType::Scalar}},
        Signature{Keire::VfxValueOpcode::DistanceLine,
                  Keire::VfxValueType::Scalar,
                  {Keire::VfxValueType::Vector3, Keire::VfxValueType::Vector3, Keire::VfxValueType::Vector3}},
        Signature{Keire::VfxValueOpcode::DistanceSphere,
                  Keire::VfxValueType::Scalar,
                  {Keire::VfxValueType::Vector3, Keire::VfxValueType::Vector3, Keire::VfxValueType::Scalar}},
        Signature{
            Keire::VfxValueOpcode::VolumeAxisAlignedBox, Keire::VfxValueType::Scalar, {Keire::VfxValueType::Vector3}},
        Signature{Keire::VfxValueOpcode::VolumeCone,
                  Keire::VfxValueType::Scalar,
                  {Keire::VfxValueType::Scalar, Keire::VfxValueType::Scalar}},
        Signature{Keire::VfxValueOpcode::SpawnState, Keire::VfxValueType::Boolean, {}, 0U},
        Signature{Keire::VfxValueOpcode::SpawnState, Keire::VfxValueType::Scalar, {}, 1U},
        Signature{Keire::VfxValueOpcode::SpawnState, Keire::VfxValueType::UnsignedInteger, {}, 2U},
    };
    static_assert(static_cast<std::uint8_t>(Keire::VfxValueOpcode::Rotate3D) == 108);
    static_assert(static_cast<std::uint8_t>(Keire::VfxValueOpcode::SpawnState) == 131);

    for (const auto& signature : signatures)
    {
        Keire::VfxGpuExecutionPayload payload;
        payload.ValueProgram.RegisterCount = 1;
        for (const auto type : signature.Inputs)
        {
            const auto constant = static_cast<std::uint32_t>(payload.ValueProgram.Constants.size());
            payload.ValueProgram.Constants.emplace_back();
            payload.ValueProgram.Sources.push_back({static_cast<std::uint32_t>(Keire::VfxGpuValueSourceKind::Literal),
                                                    static_cast<std::uint32_t>(type), constant, 0U});
        }
        Keire::VfxGpuValueInstruction instruction;
        instruction.Header = {static_cast<std::uint32_t>(signature.Opcode),
                              static_cast<std::uint32_t>(signature.Output),
                              static_cast<std::uint32_t>(Keire::VfxContextType::Update),
                              static_cast<std::uint32_t>(Keire::VfxEvaluationDomain::PerParticleUpdate)};
        instruction.Output = {0U, signature.OutputIndex, 0U, static_cast<std::uint32_t>(signature.Inputs.size())};
        payload.ValueProgram.Instructions.push_back(instruction);

        CAPTURE(static_cast<std::uint32_t>(signature.Opcode));
        CHECK_FALSE(Keire::RenderBackend::ValidateGpuVfxExecutionPayload(payload).has_value());
    }
}

TEST_CASE("GPU VFX renderer validation rejects malformed expression and Custom HLSL register references")
{
    Keire::VfxGpuExecutionPayload payload;
    payload.ValueProgram.RegisterCount = 1;
    payload.ValueProgram.Constants.push_back({{std::bit_cast<std::uint32_t>(2.0F), 0U, 0U, 0U}, {}});
    payload.ValueProgram.Sources.push_back({static_cast<std::uint32_t>(Keire::VfxGpuValueSourceKind::Literal),
                                            static_cast<std::uint32_t>(Keire::VfxValueType::Scalar), 0U, 0U});
    Keire::VfxGpuValueInstruction instruction;
    instruction.Header = {static_cast<std::uint32_t>(Keire::VfxValueOpcode::Constant),
                          static_cast<std::uint32_t>(Keire::VfxValueType::Scalar),
                          static_cast<std::uint32_t>(Keire::VfxContextType::Update),
                          static_cast<std::uint32_t>(Keire::VfxEvaluationDomain::PerParticleUpdate)};
    instruction.Output = {0U, 0U, 0U, 1U};
    payload.ValueProgram.Instructions.push_back(instruction);
    payload.CustomInstructions.push_back({Keire::VfxContextType::Update,
                                          Keire::VfxCustomTarget::Position,
                                          Keire::VfxCustomOperation::Add,
                                          false,
                                          Keire::VfxValueType::Scalar,
                                          {},
                                          0U});
    payload.ParticleOperations.push_back(
        {Keire::VfxContextType::Update, Keire::VfxGpuParticleOperationKind::CustomHlsl, 0U});
    CHECK_FALSE(Keire::RenderBackend::ValidateGpuVfxExecutionPayload(payload).has_value());

    payload.CustomInstructions.front().ValueRegister = 1U;
    const auto invalidRegister = Keire::RenderBackend::ValidateGpuVfxExecutionPayload(payload);
    REQUIRE(invalidRegister);
    CHECK(invalidRegister->find("unwritten expression register") != std::string::npos);

    payload.CustomInstructions.front().ValueRegister = std::numeric_limits<std::uint32_t>::max();
    payload.ValueProgram.Sources.front().Reserved = 1U;
    const auto invalidSource = Keire::RenderBackend::ValidateGpuVfxExecutionPayload(payload);
    REQUIRE(invalidSource);
    CHECK(invalidSource->find("invalid header") != std::string::npos);
}

TEST_CASE("SDL frame scheduling follows its bounded device queue")
{
    using Keire::RenderBackend::SdlAllowedFramesInFlight;

    CHECK(SdlAllowedFramesInFlight(0) == 1);
    CHECK(SdlAllowedFramesInFlight(1) == 1);
    CHECK(SdlAllowedFramesInFlight(2) == 2);
    CHECK(SdlAllowedFramesInFlight(3) == 3);
    CHECK(SdlAllowedFramesInFlight(4) == 3);
    CHECK(SdlAllowedFramesInFlight(8) == 3);
}
