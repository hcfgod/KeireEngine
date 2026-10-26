#pragma once

#include "Keire/Api.h"
#include "Keire/Rendering/EnvironmentLighting.h"
#include "KeireInternal/Environment/EnvironmentClock.h"
#include "KeireInternal/Environment/EnvironmentLightingProfile.h"

namespace Keire
{
    class SceneRuntimeWorld;

    namespace Detail
    {
        // Private migration boundary. Copies do not borrow the world's lifetime.
        class SceneRuntimeEnvironmentAccess final
        {
          public:
            [[nodiscard]] static KEIRE_API EnvironmentClockState State(const SceneRuntimeWorld& world);

            [[nodiscard]] static KEIRE_API std::uint64_t Revision(const SceneRuntimeWorld& world);

            // Owner-thread only, outside simulation callbacks. Invalid states leave the clock unchanged.
            static KEIRE_API void Restore(SceneRuntimeWorld& world, EnvironmentClockState state);

            [[nodiscard]] static KEIRE_API EnvironmentLightingProfile Lighting(const SceneRuntimeWorld& world);

            [[nodiscard]] static KEIRE_API EnvironmentLightingSnapshot CaptureLighting(const SceneRuntimeWorld& world);

            // Publishes time and lighting together; rejection leaves both and the revision unchanged.
            static KEIRE_API void Restore(SceneRuntimeWorld& world, EnvironmentClockState state,
                                          EnvironmentLightingProfile lighting);
        };
    } // namespace Detail
} // namespace Keire
