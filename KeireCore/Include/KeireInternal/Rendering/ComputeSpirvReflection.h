#pragma once

#include "Keire/Api.h"

#include <cstddef>
#include <span>

namespace Keire
{
    struct ProgramReflection;

    namespace Detail
    {
        /// Checks the supported compute buffer/uniform ABI, not general SPIR-V instruction semantics.
        /// Unsupported resource layouts and missing reflection names fail explicitly.
        KEIRE_API void ValidateComputeSpirvReflection(std::span<const std::byte> binary,
                                                      const ProgramReflection& expected);
    } // namespace Detail
} // namespace Keire
