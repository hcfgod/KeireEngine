#pragma once

#include "Keire/Vfx/VfxSystem.h"

#include <span>

namespace Keire
{
    namespace Internal
    {
        /// Applies one already type-checked compiled value to a Runtime Module property.
        void ApplyVfxModuleProperty(VfxModuleDefinition& module, VfxModuleProperty property,
                                    const VfxParameterValue& value);
        /// Materializes the executable module subset and applies compiled bindings using the supplied parameter slots.
        /// The returned legacy definition has been fully validated after binding resolution.
        [[nodiscard]] VfxEffectDefinition ResolveVfxExecutableDefinition(const VfxEffectDefinition& source,
                                                                         const VfxCompiledProgram& program,
                                                                         std::span<const VfxParameterValue> parameters);
        /// Revalidates backend-specific constraints after runtime parameter values have been materialized.
        /// Throws std::invalid_argument on the first unsupported schema-4 value; legacy compatibility warnings remain
        /// non-fatal.
        void ValidateVfxResolvedBackendCapabilities(const VfxEffectDefinition& definition,
                                                    const VfxCompiledProgram& program, VfxBackend backend,
                                                    bool strictSchemaFour);
    } // namespace Internal
} // namespace Keire
