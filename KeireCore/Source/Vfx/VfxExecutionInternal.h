#pragma once

#include "Keire/Vfx/VfxSystem.h"

#include <span>

namespace Keire
{
    namespace Internal
    {
        /// Materializes the executable module subset and applies compiled bindings using the supplied parameter slots.
        /// The returned legacy definition has been fully validated after binding resolution.
        [[nodiscard]] VfxEffectDefinition ResolveVfxExecutableDefinition(const VfxEffectDefinition& source,
                                                                         const VfxCompiledProgram& program,
                                                                         std::span<const VfxParameterValue> parameters);
    } // namespace Internal
} // namespace Keire
