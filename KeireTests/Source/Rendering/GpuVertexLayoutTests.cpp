#include "KeireInternal/Rendering/RenderBackendInternal.h"

#include <doctest/doctest.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>

TEST_CASE("GPU skinning vertex storage uses explicit 16-byte lanes")
{
    using Keire::RenderBackend::GpuMeshVertex;
    using Keire::RenderBackend::GpuRenderVertex;

    CHECK(alignof(GpuMeshVertex) == 16);
    CHECK(sizeof(GpuMeshVertex) == 80);
    CHECK(offsetof(GpuMeshVertex, Position) == 0);
    CHECK(offsetof(GpuMeshVertex, Normal) == 16);
    CHECK(offsetof(GpuMeshVertex, UV0) == 32);
    CHECK(offsetof(GpuMeshVertex, VertexColor) == 48);
    CHECK(offsetof(GpuMeshVertex, Tangent) == 64);

    CHECK(alignof(GpuRenderVertex) == 16);
    CHECK(sizeof(GpuRenderVertex) == 48);
    CHECK(offsetof(GpuRenderVertex, Position) == 0);
    CHECK(offsetof(GpuRenderVertex, Color) == 16);
    CHECK(offsetof(GpuRenderVertex, Normal) == 32);
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

TEST_CASE("GPU VFX execution wire records remain explicit 16-byte lanes")
{
    using Keire::RenderBackend::VfxGpuCustomInstructionRecord;
    using Keire::RenderBackend::VfxGpuParticleOperationRecord;

    CHECK(alignof(VfxGpuCustomInstructionRecord) == 16);
    CHECK(sizeof(VfxGpuCustomInstructionRecord) == 32);
    CHECK(offsetof(VfxGpuCustomInstructionRecord, Metadata) == 0);
    CHECK(offsetof(VfxGpuCustomInstructionRecord, Operand) == 16);
    CHECK(alignof(VfxGpuParticleOperationRecord) == 16);
    CHECK(sizeof(VfxGpuParticleOperationRecord) == 16);
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
    Keire::VfxGpuExecutionPayload payload;
    payload.ParticleOperations = {
        {Keire::VfxContextType::Initialize, Keire::VfxGpuParticleOperationKind::Shape, 0U},
        {Keire::VfxContextType::Initialize, Keire::VfxGpuParticleOperationKind::Initialize, 0U},
        {Keire::VfxContextType::Update, Keire::VfxGpuParticleOperationKind::Force, 0U},
        {Keire::VfxContextType::Update, Keire::VfxGpuParticleOperationKind::Size, 0U},
        {Keire::VfxContextType::Update, Keire::VfxGpuParticleOperationKind::Color, 0U},
        {Keire::VfxContextType::Update, Keire::VfxGpuParticleOperationKind::Collision, 0U},
        {Keire::VfxContextType::Output, Keire::VfxGpuParticleOperationKind::Renderer, 0U},
    };

    CHECK_FALSE(Keire::RenderBackend::ValidateGpuVfxExecutionPayload(payload).has_value());
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
