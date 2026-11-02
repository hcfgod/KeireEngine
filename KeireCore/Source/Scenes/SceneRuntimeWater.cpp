#include "KeireInternal/Scenes/SceneRuntimeSessionImpl.h"

#include "Keire/ECS/Components/BuoyancyComponent.h"
#include "Keire/ECS/Components/WaterBodyComponent.h"
#include "Keire/Water/WaterHydrodynamics.h"
#include "KeireInternal/Physics/GravityField.h"
#include "KeireInternal/Physics/PhysicsFrameAccess.h"
#include "KeireInternal/Water/WaterAnchorVelocity.h"
#include "KeireInternal/Water/WaterRuntimeAccess.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <vector>

namespace Keire
{
    namespace
    {
        Vector3d DoubleVector(const Vector3 value) { return {value.X, value.Y, value.Z}; }

        std::array<double, 4> DoubleRotation(const Quaternion value) { return {value.X, value.Y, value.Z, value.W}; }

        struct WaterFrameVelocity
        {
            Vector3d Linear;
            Vector3d Angular;
        };

        WaterFrameVelocity FramePointVelocity(const Detail::SceneFrameMotion& motion, const Vector3d worldPoint,
                                              const double dt)
        {
            const Detail::SectorPosition point(motion.PreviousOrigin.Identity(), {},
                                               {worldPoint.X, worldPoint.Y, worldPoint.Z});
            const auto local = point.RelativeTo(motion.PreviousOrigin);
            const auto deltaRotation = motion.NextRotation.Compose(motion.PreviousRotation.Inverse());
            const auto rotated = deltaRotation.Apply(local);
            const auto translation = motion.NextOrigin.RelativeTo(motion.PreviousOrigin);
            WaterFrameVelocity result;
            result.Linear = {(translation[0] + rotated[0] - local[0]) / dt,
                             (translation[1] + rotated[1] - local[1]) / dt,
                             (translation[2] + rotated[2] - local[2]) / dt};
            auto quaternion = deltaRotation.Components();
            if (quaternion[3] < 0.0)
            {
                for (auto& component : quaternion)
                {
                    component = -component;
                }
            }
            const double sine = std::hypot(quaternion[0], quaternion[1], quaternion[2]);
            if (sine > 0.0)
            {
                const double scale = 2.0 * std::atan2(sine, quaternion[3]) / (sine * dt);
                result.Angular = {quaternion[0] * scale, quaternion[1] * scale, quaternion[2] * scale};
            }
            if (!Math::IsFinite(result.Linear) || !Math::IsFinite(result.Angular))
            {
                throw std::out_of_range("Water frame velocity is not finite.");
            }
            return result;
        }
    } // namespace

    void SceneRuntimeSession::Impl::PublishWaterSimulationTime()
    {
        if (!Runtime)
        {
            return;
        }
        for (const auto& entity : Runtime->Query<WaterBodyComponent>())
        {
            Detail::WaterRuntimeAccess::SetSimulationTime(*entity.GetComponent<WaterBodyComponent>(), WaterTimeSeconds);
        }
    }

    void SceneRuntimeSession::Impl::ApplyWaterForces(const float deltaSeconds,
                                                     const std::optional<Detail::SceneFrameMotion>& frameMotion)
    {
        if (!Runtime || !PhysicsWorldService)
        {
            return;
        }
        // A checkpoint can contain loads already accepted for its not-yet-integrated tick. Resume that step
        // without duplicating the restored contributions. Native Step enforces the same accepted duration.
        const auto checkpoint = Detail::PhysicsFrameAccess::CaptureLoadCheckpoint(*PhysicsWorldService);
        if (checkpoint.AcceptedWaterTick == checkpoint.Tick)
        {
            if (checkpoint.AcceptedWaterDeltaSeconds != deltaSeconds)
            {
                throw std::logic_error("Restored water loads require their captured fixed-step duration.");
            }
            return;
        }
        struct AuthoredBody
        {
            EntityId Entity;
            PhysicsBodyId Body;
            WaterHydrodynamicSettings Settings;
        };
        std::vector<AuthoredBody> authored;
        std::vector<PhysicsBodyId> ids;
        for (const auto& [id, runtimeBody] : PhysicsBodies)
        {
            const auto entity = Runtime->FindEntity(id);
            if (!entity || !entity.ActiveInHierarchy() || !runtimeBody.Body ||
                runtimeBody.Definition.Motion != PhysicsMotionType::Dynamic)
            {
                continue;
            }
            const auto buoyancy = entity.GetComponent<BuoyancyComponent>();
            const auto rigid = entity.GetComponent<RigidBodyComponent>();
            if (!buoyancy || !buoyancy->Enabled() || !rigid || !rigid->Enabled())
            {
                continue;
            }
            authored.push_back({id, runtimeBody.Body, buoyancy->Settings()});
            ids.push_back(runtimeBody.Body);
        }
        if (ids.empty())
        {
            return;
        }
        if (WaterCaptureEpoch == std::numeric_limits<std::uint64_t>::max())
        {
            throw std::overflow_error("Water capture epoch exhausted.");
        }
        const Detail::PhysicsLoadFrameContext context{Runtime->PhysicsRevision(), ++WaterCaptureEpoch, deltaSeconds};
        const auto capture = Detail::PhysicsFrameAccess::CapturePreparedLoads(PhysicsWorldService, context,
                                                                              Detail::PhysicsLoadSource::Water, ids);
        const auto origin = capture->WorldOrigin();
        std::vector<WaterMedium> media;
        for (const auto& entity : Runtime->Query<WaterBodyComponent>())
        {
            const auto water = entity.GetComponent<WaterBodyComponent>();
            const auto transform = entity.GetComponent<TransformComponent>();
            if (!entity.ActiveInHierarchy() || !water->Enabled() || !transform)
            {
                continue;
            }
            const auto worldPosition = transform->WorldPositionPrecise();
            WaterFrameVelocity velocity;
            bool anchored = false;
            for (auto ancestor = entity; ancestor; ancestor = ancestor.Parent())
            {
                const auto native = PhysicsBodies.find(ancestor.Id());
                if (native == PhysicsBodies.end() || !native->second.Body)
                {
                    continue;
                }
                const auto motion =
                    Detail::CaptureWaterAnchorVelocity(*PhysicsWorldService, native->second.Body, worldPosition);
                velocity = {motion.Linear, motion.Angular};
                anchored = true;
                break;
            }
            if (!anchored && frameMotion)
            {
                velocity = FramePointVelocity(*frameMotion, worldPosition, deltaSeconds);
            }
            // Work in the captured world's translated double frame. Do not add tiny native COM offsets
            // to a trillion-metre origin only to subtract that origin again in the sampler.
            const Vector3d relative{worldPosition.X - origin.X, worldPosition.Y - origin.Y, worldPosition.Z - origin.Z};
            media.emplace_back(entity.Id().Value(),
                               water->CreateSurfaceDefinition(relative, DoubleRotation(transform->WorldRotation()),
                                                              velocity.Linear, velocity.Angular),
                               water->Settings().Waves, 0, entity.Layer());
        }
        std::vector<Detail::PreparedPhysicsBodyLoad> loads;
        loads.reserve(authored.size());
        for (std::size_t index = 0; index < authored.size(); ++index)
        {
            const auto& native = capture->Bodies()[index];
            if (native.State.Body != authored[index].Body)
            {
                throw std::logic_error("Water native capture changed body order.");
            }
            WaterBodyKinematics body;
            body.CenterOfMass = native.CenterOfMass;
            body.BodyRotation = DoubleRotation(native.State.Rotation);
            body.LinearVelocity = DoubleVector(native.State.LinearVelocity);
            body.AngularVelocity = DoubleVector(native.State.AngularVelocity);
            body.InverseMass = native.InverseMass;
            body.WorldInverseInertia = native.WorldInverseInertia;
            const auto rotation = Detail::FrameRotation::Normalize(body.BodyRotation[0], body.BodyRotation[1],
                                                                   body.BodyRotation[2], body.BodyRotation[3]);
            std::vector<Vector3d> gravity;
            gravity.reserve(authored[index].Settings.Probes.size());
            for (const auto& probe : authored[index].Settings.Probes)
            {
                Vector3d acceleration;
                if (native.UseGravity)
                {
                    const auto offset = rotation.Apply({probe.Offset.X, probe.Offset.Y, probe.Offset.Z});
                    const Vector3d point{body.CenterOfMass.X + offset[0], body.CenterOfMass.Y + offset[1],
                                         body.CenterOfMass.Z + offset[2]};
                    const auto local = Math::RelativeFloat(point, {}, 0.001);
                    acceleration = DoubleVector(Detail::CombineGravity(
                        Detail::AccumulateGravitySources(capture->GravitySources(), origin, local),
                        capture->UniformGravity()));
                }
                gravity.push_back(acceleration);
            }
            const auto result = EvaluateWaterBodyLoads(body, authored[index].Settings, media, gravity, WaterTimeSeconds,
                                                       capture->Context().DeltaSeconds);
            loads.push_back({native.State.Body, result.ForceNewtons, result.TorqueNewtonMetres});
        }
        // No callbacks, awaits or scene mutations occur between capture and this commit. The native boundary
        // revalidates lifetime, origin, exact dt, tick, state/inertia and all additive sums under one multi-lock.
        const Detail::PhysicsLoadFrameContext current{Runtime->PhysicsRevision(), WaterCaptureEpoch, deltaSeconds};
        Detail::PhysicsFrameAccess::ApplyPreparedLoads(*PhysicsWorldService, current, capture, loads);
    }
} // namespace Keire
