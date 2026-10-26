#pragma once

#include "Keire/Api.h"

#include <cstdint>

namespace Keire
{
    class SceneRuntimeWorld;
    class Entity;

    namespace Detail
    {
        class FrameRegistry;
        class LiveFrame;

        // Private migration boundary, not a public spatial API or persisted identity allocation policy.
        class SceneRuntimeSpatialAccess final
        {
          public:
            static constexpr std::uint64_t DefaultLogicalWorld = 1;
            static constexpr std::uint64_t RootFrame = 1;

            // Owner-thread access only. The borrowed registry requires the world object to remain alive.
            // Logical keys are local to this runtime context; live handles distinguish separate instances.
            [[nodiscard]] static KEIRE_API const FrameRegistry& Frames(const SceneRuntimeWorld& world);

            // Validates the live entity's scene instance, not only its stable entity ID.
            // The returned frame handle does not extend or authorize the entity's lifetime.
            [[nodiscard]] static KEIRE_API LiveFrame FrameForEntity(const SceneRuntimeWorld& world,
                                                                    const Entity& entity);
        };
    } // namespace Detail
} // namespace Keire
