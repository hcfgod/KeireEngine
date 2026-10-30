#pragma once

#include "Keire/Api.h"
#include "Keire/Scenes/SceneRuntimeWorld.h"
#include "KeireInternal/Spatial/FrameRotation.h"
#include "KeireInternal/Spatial/SectorPosition.h"

#include <cstdint>

namespace Keire
{
    class SceneRuntimeWorld;
    class Entity;

    namespace Detail
    {
        class FrameRegistry;
        class LiveFrame;

        struct SceneFrameMotion
        {
            SectorPosition PreviousOrigin;
            SectorPosition NextOrigin;
            FrameRotation PreviousRotation;
            FrameRotation NextRotation;
        };

        // Private migration boundary, not a public spatial API or persisted identity allocation policy.
        class SceneRuntimeSpatialAccess final
        {
          public:
            static constexpr std::uint64_t DefaultLogicalWorld = 1;
            static constexpr std::uint64_t RootFrame = 1;

            // Owner-thread access only. The borrowed registry requires the world object to remain alive and no
            // successful startup planet reconfiguration; acquire it after SceneRuntimePlanetAccess::Configure.
            // Logical keys are local to this runtime context; live handles distinguish separate instances.
            [[nodiscard]] static KEIRE_API const FrameRegistry& Frames(const SceneRuntimeWorld& world);

            // Validates the live entity's scene instance, not only its stable entity ID.
            // The returned frame handle does not extend or authorize the entity's lifetime.
            [[nodiscard]] static KEIRE_API LiveFrame FrameForEntity(const SceneRuntimeWorld& world,
                                                                    const Entity& entity);

            // Ownership drives fixed-step non-dynamic motion; stored transforms and solver axes remain root-local.
            [[nodiscard]] static KEIRE_API LiveFrame OwnerFrameForEntity(const SceneRuntimeWorld& world,
                                                                         const Entity& entity);

            // Preserves current poses and solver state. Static colliders and parented physics are currently rejected.
            static KEIRE_API void TransferSceneOwner(SceneRuntimeWorld& world, SceneHandle scene,
                                                     const LiveFrame& destination, double snapshotTime);

            static KEIRE_API void ValidateFrameFollowing(const SceneRuntimeSession& session,
                                                         bool validateContent = true);
            static KEIRE_API void PrepareFrameMotion(SceneRuntimeSession& session, const SceneFrameMotion& motion);
        };
    } // namespace Detail
} // namespace Keire
