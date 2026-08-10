#pragma once

#include "Keire/Vfx/VfxSystem.h"

#include <cstdint>
#include <span>

namespace Keire::Internal
{
    [[nodiscard]] bool ValidVfxExtendedGpuInstructionSignature(VfxValueOpcode opcode, VfxValueType output,
                                                               std::uint32_t outputIndex,
                                                               std::span<const VfxValueType> inputs) noexcept;
} // namespace Keire::Internal
