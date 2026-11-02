#pragma once

#include "Keire/Physics/PhysicsSystem.h"
#include "KeireInternal/Physics/PhysicsPreparedLoads.h"

#include <optional>
#include <span>

namespace Keire::Detail
{
    struct PhysicsKinematicTarget
    {
        Vector3 Position;
        Quaternion Rotation;
        float DeltaSeconds = 0.0F;
    };

    struct AuthoredPhysicsBodyUpdate
    {
        PhysicsBodyId Body;
        std::optional<PhysicsBodyState> State;
        std::optional<PhysicsMotionSettings> MotionSettings;
        std::optional<bool> UseGravity;
        // Evaluated from State's staged pose when present, otherwise from the live native pose.
        std::optional<PhysicsKinematicTarget> KinematicTarget;
    };

    class PhysicsFrameAccess final
    {
      public:
        // Side-effect-free clock/load-duration preflight before scene input and pose commits.
        // This does not predict solver capacity or validate future force additions.
        static KEIRE_API void ValidateStepClock(const PhysicsWorld& world, float deltaSeconds);

        [[nodiscard]] static KEIRE_API PhysicsLoadCheckpoint CaptureLoadCheckpoint(const PhysicsWorld& world);

        static KEIRE_API void ValidateLoadCheckpoint(const PhysicsWorld& world, const PhysicsLoadCheckpoint& state);

        // Call after successful body restoration, under the same exclusive scene ownership as validation.
        static KEIRE_API void RestoreLoadCheckpoint(PhysicsWorld& world, const PhysicsLoadCheckpoint& state);

        // Capture dynamic bodies coherently on the owner thread. Computation may then use the
        // immutable values off-thread. Current frame context must come from the scene owner.
        // Capture through commit requires exclusive scene ownership of pose/settings mutations.
        // Validation compares current values; it does not detect arbitrary pose changes followed by reversal.
        // Independent additive external forces are permitted during this interval.
        [[nodiscard]] static KEIRE_API ImmutablePhysicsLoadCapture
        CapturePreparedLoads(const Ref<PhysicsWorld>& world, PhysicsLoadFrameContext context, PhysicsLoadSource source,
                             std::span<const PhysicsBodyId> bodies);

        // Validate lifetime/origin/frame/tick and captured body kinematics/inertia, then validate
        // ALL sums against live pending loads under a multi-lock before adding anything.
        // One accepted batch per source and physics tick, including an empty/zero batch.
        // Rejection consumes neither the tick slot nor any loads. Existing external loads survive.
        static KEIRE_API void ApplyPreparedLoads(PhysicsWorld& world, PhysicsLoadFrameContext currentContext,
                                                 const ImmutablePhysicsLoadCapture& capture,
                                                 std::span<const PreparedPhysicsBodyLoad> loads);

        // Validate and lock the complete owner-side batch before any native write. Shape replacement
        // resources must already be staged; this operation preserves identities and pending loads.
        static KEIRE_API void ApplyAuthoredBodyUpdates(PhysicsWorld& world,
                                                       std::span<const AuthoredPhysicsBodyUpdate> updates);

        // Clearance checks stop at the first contact, without constructing a penetration list.
        // Uses the same validation and collision filters as PhysicsWorld::OverlapCapsule.
        [[nodiscard]] static KEIRE_API bool HasCapsuleContact(const PhysicsWorld& world,
                                                              const PhysicsCapsuleOverlapQuery& query);

        // Inclusion and exclusion refine the usual mask/trigger/self filters; exclusion wins.
        // Body identities are world-local and must resolve in the supplied world.
        [[nodiscard]] static KEIRE_API bool HasCapsuleContact(const PhysicsWorld& world,
                                                              const PhysicsCapsuleOverlapQuery& query,
                                                              std::optional<PhysicsBodyId> onlyBody,
                                                              std::span<const PhysicsBodyId> excludedBodies);

        // Scene statics retain motion storage so owner changes never replace their body identity.
        [[nodiscard]] static KEIRE_API PhysicsBodyId CreateFrameBody(PhysicsWorld& world,
                                                                     const PhysicsBodyDefinition& definition);

        static KEIRE_API void ValidateFrameKinematics(const PhysicsWorld& world, std::span<const PhysicsBodyId> bodies);
        static KEIRE_API void SetFrameKinematics(PhysicsWorld& world, std::span<const PhysicsBodyId> bodies,
                                                 bool enabled);

        // Validate every target in a batch before changing any body velocity.
        static KEIRE_API void ValidateKinematicMove(const PhysicsWorld& world, PhysicsBodyId body, Vector3 position,
                                                    Quaternion rotation, float deltaSeconds);

        // Sets solver velocity to reach the target over the next step; does not teleport the current pose.
        // Submit once before Step with the same duration. Only existing kinematic bodies are accepted.
        static KEIRE_API void MoveKinematic(PhysicsWorld& world, PhysicsBodyId body, Vector3 position,
                                            Quaternion rotation, float deltaSeconds);

        // Owner-thread query in the world's local solver coordinates. LinearVelocity in PhysicsBodyState
        // belongs to this point, not necessarily to the exposed object-origin Position.
        [[nodiscard]] static KEIRE_API std::optional<Vector3> TryGetCenterOfMassPosition(const PhysicsWorld& world,
                                                                                         PhysicsBodyId body);
    };
} // namespace Keire::Detail
