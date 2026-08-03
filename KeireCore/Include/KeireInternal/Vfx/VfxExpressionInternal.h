#pragma once

#include "Keire/Vfx/VfxSystem.h"

#include <map>

namespace Keire::Internal
{
    struct VfxExpressionCompilation
    {
        std::vector<VfxCompiledValueInstruction> Instructions;
        std::map<AssetId, VfxCompiledValueSource> SourcesByOutputPin;
        std::uint32_t RegisterCount = 0;
    };

    struct VfxExpressionEvaluationContext
    {
        std::uint32_t EffectSeed = 1;
        std::uint32_t SeedOffset = 0;
        AssetId System;
        VfxContextType Context = VfxContextType::Update;
        std::uint64_t ParticleId = 0;
        std::uint64_t StripId = 0;
        std::uint64_t SpawnIndex = 0;
        std::uint64_t SimulationStep = 0;
        float EffectTime = 0.0F;
        float DeltaTime = 0.0F;
        float Age = 0.0F;
        float Lifetime = 1.0F;
        Vector3 Position;
        Vector3 PreviousPosition;
        Vector3 Velocity;
        Vector3 Rotation;
        Color Tint;
        float Size = 1.0F;
        std::uint32_t ParticleIndexInStrip = 0;
        std::uint32_t ParticlesPerStrip = 1;
    };

    [[nodiscard]] VfxExpressionCompilation CompileVfxExpressions(const VfxGraphSystem& system,
                                                                 const std::map<AssetId, std::uint32_t>& parameterSlots,
                                                                 std::span<const AssetId> requiredOutputPins);

    [[nodiscard]] bool EvaluateVfxExpressions(const VfxCompiledProgram& program,
                                              std::span<const VfxParameterValue> parameters,
                                              const VfxExpressionEvaluationContext& context,
                                              std::span<VfxParameterValue> registers) noexcept;

    [[nodiscard]] const VfxParameterValue* ResolveVfxValueSource(const VfxCompiledValueSource& source,
                                                                 std::span<const VfxParameterValue> parameters,
                                                                 std::span<const VfxParameterValue> registers) noexcept;

    /// Converts one validated graph value to the two-lane shader representation. Parameter callers use this after
    /// resolving overrides; compilation uses it for immutable literal values.
    [[nodiscard]] bool PackVfxGpuValue(VfxValueType type, const VfxParameterValue& value, VfxGpuValue& packed) noexcept;
} // namespace Keire::Internal
