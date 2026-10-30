#include "Keire/Scenes/Scene.h"

#include "Keire/Animation/AnimationSystem.h"
#include "Keire/Animation/RiggingSystem.h"
#include "Keire/Assets/AssetSystem.h"
#include "Keire/Assets/PhysicsMaterialAsset.h"
#include "Keire/Assets/RenderingAssets.h"
#include "Keire/ECS/Components/AnimatorComponent.h"
#include "Keire/ECS/Components/CharacterControllerComponent.h"
#include "Keire/ECS/Components/ColliderComponent.h"
#include "Keire/ECS/Components/RigidBodyComponent.h"
#include "Keire/ECS/Components/TransformComponent.h"
#include "Keire/ECS/Components/VfxEmitterComponent.h"
#include "Keire/Log.h"
#include "Keire/Scenes/ScenePresentationRuntime.h"
#include "Keire/Vfx/VfxSystem.h"
#include "Keire/Vfx/VfxVolumeAsset.h"
#include "KeireInternal/Physics/GravityField.h"
#include "KeireInternal/Physics/MotionSettings.h"
#include "KeireInternal/Physics/PhysicsOriginTranslation.h"
#include "KeireInternal/Scenes/SceneRuntimeSessionImpl.h"
#include "KeireInternal/Spatial/SimulationOriginPolicy.h"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <exception>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace Keire
{

    SceneRuntimeSession::SceneRuntimeSession(Ref<Scene> editScene, Ref<AssetSystem> assets, Ref<AudioSystem> audio,
                                             Ref<PhysicsSystem> physics)
        : m_Impl(std::make_unique<Impl>(std::move(editScene), std::move(assets), std::move(audio), std::move(physics)))
    {
    }

    SceneRuntimeSession::~SceneRuntimeSession() { Stop(); }

    ScenePlayState SceneRuntimeSession::State() const noexcept { return m_Impl->PlayState; }
    Ref<Scene> SceneRuntimeSession::EditScene() const noexcept { return m_Impl->Edit; }
    Ref<Scene> SceneRuntimeSession::RuntimeScene() const noexcept { return m_Impl->Runtime; }
    SceneRuntimeDiagnostic SceneRuntimeSession::Diagnostic() const { return m_Impl->Failure; }
    Ref<ScenePresentationRuntime> SceneRuntimeSession::Presentation() const noexcept { return m_Impl->Presentation; }
    Ref<PhysicsWorld> SceneRuntimeSession::Physics() const noexcept { return m_Impl->PhysicsWorldService; }
    Ref<VfxWorld> SceneRuntimeSession::Vfx() const noexcept { return m_Impl->VfxWorldService; }

    void Detail::SceneRuntimeSpatialAccess::ValidateFrameFollowing(const SceneRuntimeSession& session,
                                                                   const bool validateContent)
    {
        const auto& state = *session.m_Impl;
        state.RequireOwner("ValidateFrameFollowing");
        if (state.InvokingCallback || !state.Runtime || !state.PhysicsWorldService ||
            (state.PlayState != ScenePlayState::Playing && state.PlayState != ScenePlayState::Paused))
        {
            throw std::logic_error("Frame following requires an active session outside callbacks.");
        }
        if (!validateContent)
        {
            return;
        }
        for (const auto& entity : state.Runtime->Entities())
        {
            const auto character = entity.GetComponent<CharacterControllerComponent>();
            if (character && character->Enabled())
            {
                throw std::invalid_argument("Planet owner transfer does not yet support character controllers.");
            }
            const auto collider = entity.GetComponent<ColliderComponent>();
            if (!collider || !collider->Enabled())
            {
                continue;
            }
            const auto body = entity.GetComponent<RigidBodyComponent>();
            if (entity.Parent() || !body || !body->Enabled() || body->Motion() == PhysicsMotionType::Static)
            {
                throw std::invalid_argument(
                    "Planet owner transfer requires unparented dynamic or kinematic colliders.");
            }
        }
    }

    void Detail::SceneRuntimeSpatialAccess::PrepareFrameMotion(SceneRuntimeSession& session,
                                                               const SceneFrameMotion& motion)
    {
        session.m_Impl->PendingFrameMotion = motion;
    }

    std::vector<ScenePhysicsCheckpointBody> SceneRuntimeSession::CapturePhysicsCheckpoint() const
    {
        m_Impl->RequireOwner("CapturePhysicsCheckpoint");
        std::vector<ScenePhysicsCheckpointBody> result;
        if (!m_Impl->PhysicsWorldService)
            return result;
        result.reserve(m_Impl->PhysicsBodies.size());
        for (const auto& [entity, runtime] : m_Impl->PhysicsBodies)
        {
            if (!runtime.Body)
                continue;
            const auto state = m_Impl->PhysicsWorldService->TryGetBody(runtime.Body);
            if (!state)
                throw std::runtime_error("A scene physics checkpoint could not resolve a runtime body.");
            result.push_back({entity, state->Position, state->Rotation, state->LinearVelocity, state->AngularVelocity,
                              state->Sleeping, m_Impl->PhysicsWorldService->WorldOrigin(),
                              m_Impl->PhysicsWorldService->GetMotionSettings(runtime.Body)});
        }
        return result;
    }

    void SceneRuntimeSession::RestorePhysicsCheckpoint(const std::span<const ScenePhysicsCheckpointBody> bodies)
    {
        RestorePhysicsCheckpointState(bodies, std::nullopt);
    }

    ScenePhysicsWorldCheckpoint SceneRuntimeSession::CapturePhysicsWorldCheckpoint() const
    {
        m_Impl->RequireOwner("CapturePhysicsWorldCheckpoint");
        ScenePhysicsWorldCheckpoint result;
        if (m_Impl->PhysicsWorldService)
        {
            result.Gravity = m_Impl->PhysicsWorldService->Gravity();
            result.GravitySources = m_Impl->PhysicsWorldService->GravitySources();
        }
        result.Bodies = CapturePhysicsCheckpoint();
        return result;
    }

    void SceneRuntimeSession::RestorePhysicsWorldCheckpoint(const ScenePhysicsWorldCheckpoint& checkpoint)
    {
        m_Impl->RequireOwner("RestorePhysicsWorldCheckpoint");
        if (checkpoint.Gravity.has_value() != static_cast<bool>(m_Impl->PhysicsWorldService))
        {
            throw std::runtime_error("Physics checkpoint world presence does not match the runtime session.");
        }
        RestorePhysicsCheckpointState(checkpoint.Bodies, checkpoint.Gravity, &checkpoint.GravitySources);
    }

    void SceneRuntimeSession::RestorePhysicsCheckpointState(const std::span<const ScenePhysicsCheckpointBody> bodies,
                                                            const std::optional<Vector3> gravity,
                                                            const std::vector<PhysicsGravitySource>* sources)
    {
        m_Impl->RequireOwner("RestorePhysicsCheckpoint");
        if (m_Impl->InvokingCallback)
        {
            throw std::logic_error("Physics checkpoint restore requires a boundary between callbacks.");
        }
        if (gravity && !Math::IsFinite(*gravity))
        {
            throw std::invalid_argument("Physics checkpoint gravity must be finite.");
        }
        if (!m_Impl->PhysicsWorldService)
        {
            if (!bodies.empty() || (sources && !sources->empty()))
                throw std::runtime_error("A physics checkpoint cannot be restored without an active physics world.");
            return;
        }

        if (sources)
        {
            if (sources->size() > 64)
            {
                throw std::invalid_argument("Physics checkpoint contains too many gravity sources.");
            }
            for (const auto& source : *sources)
            {
                Detail::ValidateGravitySource(source);
            }
        }

        std::set<EntityId> identities;
        std::vector<PhysicsBodyState> restoredBodies;
        restoredBodies.reserve(bodies.size());
        const auto destinationOrigin = m_Impl->PhysicsWorldService->WorldOrigin();
        for (const auto& body : bodies)
        {
            const auto found = m_Impl->PhysicsBodies.find(body.Entity);
            if (!body.Entity || !identities.insert(body.Entity).second || found == m_Impl->PhysicsBodies.end() ||
                !found->second.Body || !Math::IsFinite(body.Position) || !Math::IsFinite(body.Rotation) ||
                !Math::IsFinite(body.LinearVelocity) || !Math::IsFinite(body.AngularVelocity) ||
                !Math::IsFinite(body.WorldOrigin) ||
                (body.MotionSettings && !Detail::ValidMotionSettings(*body.MotionSettings)))
            {
                throw std::runtime_error("A scene physics checkpoint is incompatible with the runtime scene.");
            }
            const Vector3d originDelta{body.WorldOrigin.X - destinationOrigin.X,
                                       body.WorldOrigin.Y - destinationOrigin.Y,
                                       body.WorldOrigin.Z - destinationOrigin.Z};
            constexpr double maximumPositionErrorMetres = 0.001;
            const auto position =
                Detail::TranslatePhysicsPosition(body.Position, originDelta, maximumPositionErrorMetres);
            restoredBodies.push_back({found->second.Body, position, body.Rotation, body.LinearVelocity,
                                      body.AngularVelocity, body.Sleeping});
        }
        const auto liveBodyCount = static_cast<std::size_t>(std::ranges::count_if(
            m_Impl->PhysicsBodies, [](const auto& entry) { return static_cast<bool>(entry.second.Body); }));
        if (identities.size() != liveBodyCount)
            throw std::runtime_error("A scene physics checkpoint does not contain every runtime body.");

        if (sources)
        {
            m_Impl->PhysicsWorldService->SetGravitySources(*sources);
        }
        if (gravity)
        {
            // Gravity changes wake bodies; restore their checkpoint sleep states afterward.
            m_Impl->PhysicsWorldService->SetGravity(*gravity);
        }
        for (std::size_t index = 0; index < restoredBodies.size(); ++index)
        {
            const auto& checkpoint = bodies[index];
            const auto& body = restoredBodies[index];
            if (checkpoint.MotionSettings)
            {
                m_Impl->PhysicsWorldService->SetMotionSettings(body.Body, *checkpoint.MotionSettings);
                auto& runtime = m_Impl->PhysicsBodies.at(checkpoint.Entity);
                runtime.Definition.MotionSettings = *checkpoint.MotionSettings;
                const auto entity = m_Impl->Runtime->FindEntity(checkpoint.Entity);
                if (const auto rigidBody = entity.GetComponent<RigidBodyComponent>())
                {
                    rigidBody->SetMotionSettings(*checkpoint.MotionSettings);
                }
            }
            m_Impl->PhysicsWorldService->SetBodyState(body.Body, body);
        }
    }

    std::vector<SceneAnimatorCheckpoint> SceneRuntimeSession::CaptureAnimatorCheckpoint() const
    {
        m_Impl->RequireOwner("CaptureAnimatorCheckpoint");
        std::vector<SceneAnimatorCheckpoint> result;
        result.reserve(m_Impl->Animators.size());
        for (const auto& [entity, runtime] : m_Impl->Animators)
        {
            if (runtime && runtime->Instance)
                result.push_back({entity, runtime->Instance->CaptureCheckpoint()});
        }
        return result;
    }

    void SceneRuntimeSession::RestoreAnimatorCheckpoint(const std::span<const SceneAnimatorCheckpoint> animators)
    {
        m_Impl->RequireOwner("RestoreAnimatorCheckpoint");
        std::map<EntityId, AnimatorInstance*> live;
        for (const auto& [entity, runtime] : m_Impl->Animators)
            if (runtime && runtime->Instance)
                live.emplace(entity, runtime->Instance.get());
        std::set<EntityId> identities;
        for (const auto& animator : animators)
            if (!animator.Entity || !identities.insert(animator.Entity).second || !live.contains(animator.Entity))
                throw std::runtime_error("An animator checkpoint is incompatible with the runtime scene.");
        if (identities.size() != live.size())
            throw std::runtime_error("An animator checkpoint does not contain every runtime animator.");

        std::vector<SceneAnimatorCheckpoint> rollback;
        rollback.reserve(animators.size());
        try
        {
            for (const auto& animator : animators)
            {
                auto* instance = live.at(animator.Entity);
                rollback.push_back({animator.Entity, instance->CaptureCheckpoint()});
                instance->RestoreCheckpoint(animator.State);
            }
        }
        catch (...)
        {
            const auto original = std::current_exception();
            for (auto iterator = rollback.rbegin(); iterator != rollback.rend(); ++iterator)
            {
                try
                {
                    live.at(iterator->Entity)->RestoreCheckpoint(iterator->State);
                }
                catch (...)
                {
                }
            }
            std::rethrow_exception(original);
        }
    }

    void SceneRuntimeSession::SetDeterministicSimulation(const bool enabled)
    {
        m_Impl->RequireOwner("SetDeterministicSimulation");
        if (m_Impl->DeterministicSimulation == enabled)
            return;
        m_Impl->DeterministicSimulation = enabled;
        if (m_Impl->PlayState != ScenePlayState::Stopped && m_Impl->Runtime)
        {
            m_Impl->InitializeVfx();
            m_Impl->SynchronizeVfx(0.0F);
        }
    }

    std::vector<std::byte> SceneRuntimeSession::CaptureVfxCheckpoint() const
    {
        m_Impl->RequireOwner("CaptureVfxCheckpoint");
        if (!m_Impl->VfxWorldService)
            throw std::logic_error("Scene VFX checkpoint state is unavailable.");
        return m_Impl->VfxWorldService->CaptureCheckpoint();
    }

    void SceneRuntimeSession::RestoreVfxCheckpoint(const std::span<const std::byte> checkpoint)
    {
        m_Impl->RequireOwner("RestoreVfxCheckpoint");
        if (!m_Impl->VfxWorldService)
            throw std::logic_error("Scene VFX checkpoint state is unavailable.");
        m_Impl->VfxWorldService->RestoreCheckpoint(checkpoint);
    }

    bool SceneRuntimeSession::PlayVfx(const EntityId entityId, const AssetId effect, const bool restart)
    {
        if (!m_Impl->Runtime || !m_Impl->VfxWorldService || !effect)
            return false;
        auto entity = m_Impl->Runtime->FindEntity(entityId);
        if (!entity)
            return false;
        auto emitter = entity.GetComponent<VfxEmitterComponent>();
        if (!emitter)
            emitter = entity.AddComponent<VfxEmitterComponent>();
        if (!emitter)
            return false;
        if (restart)
        {
            const auto state = m_Impl->VfxEmitters.find(entityId);
            if (state != m_Impl->VfxEmitters.end())
            {
                if (state->second.Handle)
                    m_Impl->VfxWorldService->Stop(state->second.Handle);
                m_Impl->VfxEmitters.erase(state);
            }
        }
        emitter->SetEffect(effect);
        emitter->SetPlayOnAwake(true);
        emitter->SetSimulationSpeed(1.0F);
        emitter->SetEnabled(true);
        return true;
    }

    bool SceneRuntimeSession::StopVfx(const EntityId entityId)
    {
        if (!m_Impl->Runtime || !m_Impl->VfxWorldService)
            return false;
        const auto entity = m_Impl->Runtime->FindEntity(entityId);
        const auto emitter = entity ? entity.GetComponent<VfxEmitterComponent>() : Ref<VfxEmitterComponent>{};
        if (!emitter)
            return false;
        emitter->SetPlayOnAwake(false);
        if (const auto state = m_Impl->VfxEmitters.find(entityId); state != m_Impl->VfxEmitters.end())
        {
            if (state->second.Handle)
                m_Impl->VfxWorldService->Stop(state->second.Handle);
            m_Impl->VfxEmitters.erase(state);
        }
        return true;
    }

    bool SceneRuntimeSession::PauseVfx(const EntityId entityId, const bool paused)
    {
        if (!m_Impl->Runtime)
            return false;
        const auto entity = m_Impl->Runtime->FindEntity(entityId);
        const auto emitter = entity ? entity.GetComponent<VfxEmitterComponent>() : Ref<VfxEmitterComponent>{};
        if (!emitter)
            return false;
        emitter->SetSimulationSpeed(paused ? 0.0F : 1.0F);
        return true;
    }

    bool SceneRuntimeSession::IsVfxAlive(const EntityId entityId) const noexcept
    {
        if (!m_Impl->VfxWorldService)
            return false;
        const auto state = m_Impl->VfxEmitters.find(entityId);
        return state != m_Impl->VfxEmitters.end() && state->second.Handle &&
               m_Impl->VfxWorldService->IsAlive(state->second.Handle);
    }

    bool SceneRuntimeSession::SendVfxEvent(const EntityId entityId, const std::string_view eventName,
                                           const std::uint32_t spawnCount)
    {
        m_Impl->RequireOwner("SendVfxEvent");
        if (!m_Impl->VfxWorldService)
            return false;
        const auto state = m_Impl->VfxEmitters.find(entityId);
        return state != m_Impl->VfxEmitters.end() && state->second.Handle &&
               m_Impl->VfxWorldService->SendEvent(state->second.Handle, eventName, spawnCount);
    }

    bool SceneRuntimeSession::SetVfxParameter(const EntityId entityId, const VfxParameterOverride& value)
    {
        m_Impl->RequireOwner("SetVfxParameter");
        if (!m_Impl->Runtime || !m_Impl->VfxWorldService || !value.Parameter)
            return false;

        const auto entity = m_Impl->Runtime->FindEntity(entityId);
        const auto emitter = entity ? entity.GetComponent<VfxEmitterComponent>() : Ref<VfxEmitterComponent>{};
        const auto state = m_Impl->VfxEmitters.find(entityId);
        if (!emitter || state == m_Impl->VfxEmitters.end() || state->second.Effect != emitter->Effect() ||
            !state->second.Handle || !m_Impl->VfxWorldService->IsAlive(state->second.Handle))
        {
            return false;
        }

        const auto effect = state->second.EffectHandle.TryGetLoaded();
        if (!effect)
            return false;
        const auto parameter =
            std::ranges::find(effect->Definition().Blackboard, value.Parameter, &VfxBlackboardParameter::Id);
        if (parameter == effect->Definition().Blackboard.end() || !parameter->Exposed ||
            !VfxOverrideMatches(parameter->Type, value.Value))
        {
            return false;
        }

        const auto authored = emitter->ParameterOverrides();
        std::vector<VfxParameterOverride> componentCandidate(authored.begin(), authored.end());
        const auto existing =
            std::ranges::lower_bound(componentCandidate, value.Parameter, {}, &VfxParameterOverride::Parameter);
        if (existing == componentCandidate.end() || existing->Parameter != value.Parameter)
        {
            if (componentCandidate.size() >= 1024)
                return false;
            componentCandidate.insert(existing, value);
        }
        else
        {
            *existing = value;
        }

        auto liveCandidate = CompatibleVfxOverrides(effect->Definition(), componentCandidate);
        if (std::ranges::find(liveCandidate, value.Parameter, &VfxParameterOverride::Parameter) == liveCandidate.end())
            return false;
        auto trackedCandidate = liveCandidate;
        try
        {
            m_Impl->VfxWorldService->SetParameterOverrides(state->second.Handle, liveCandidate);
        }
        catch (...)
        {
            return false;
        }

        emitter->CommitRuntimeParameterOverrides(std::move(componentCandidate));
        state->second.Overrides.swap(trackedCandidate);
        return true;
    }

    std::vector<ScenePhysicsQueryHit> SceneRuntimeSession::RayCast(const PhysicsRayQuery& query,
                                                                   const EntityId ignoredEntity) const
    {
        m_Impl->RequireOwner("RayCast");
        std::vector<ScenePhysicsQueryHit> result;
        if (!m_Impl->PhysicsWorldService)
            return result;
        const auto directionSquared = query.Direction.X * query.Direction.X + query.Direction.Y * query.Direction.Y +
                                      query.Direction.Z * query.Direction.Z;
        if (!Math::IsFinite(query.Origin) || !Math::IsFinite(query.Direction) || !std::isfinite(directionSquared) ||
            !std::isfinite(query.MaximumDistance) || query.MaximumDistance <= 0 ||
            directionSquared <= std::numeric_limits<float>::epsilon() || !std::has_single_bit(query.Layer))
            throw std::invalid_argument("Physics ray query is invalid.");
        constexpr double maximumPositionErrorMetres = 0.001;
        auto candidate = query;
        candidate.Origin = m_Impl->PhysicsWorldService->ToLocalPosition(
            {query.Origin.X, query.Origin.Y, query.Origin.Z}, maximumPositionErrorMetres);
        m_Impl->SynchronizePhysicsQueries();
        for (auto hit : m_Impl->PhysicsWorldService->RayCast(candidate))
        {
            const auto entity = m_Impl->EntityForBody(hit.Body);
            if (entity && *entity != ignoredEntity)
            {
                hit.Position = Math::RelativeFloat(m_Impl->PhysicsWorldService->ToWorldPosition(hit.Position), {},
                                                   std::numeric_limits<double>::max());
                result.push_back({*entity, hit});
            }
        }
        return result;
    }

    std::vector<ScenePhysicsQueryHitPrecise>
    SceneRuntimeSession::RayCastPrecise(const ScenePhysicsRayQueryPrecise& query, const EntityId ignoredEntity) const
    {
        m_Impl->RequireOwner("RayCastPrecise");
        std::vector<ScenePhysicsQueryHitPrecise> result;
        if (!m_Impl->PhysicsWorldService)
        {
            return result;
        }
        const auto directionSquared = query.Direction.X * query.Direction.X + query.Direction.Y * query.Direction.Y +
                                      query.Direction.Z * query.Direction.Z;
        if (!Math::IsFinite(query.Origin) || !Math::IsFinite(query.Direction) || !std::isfinite(directionSquared) ||
            !std::isfinite(query.MaximumDistance) || query.MaximumDistance <= 0.0F ||
            directionSquared <= std::numeric_limits<float>::epsilon() || !std::has_single_bit(query.Layer))
        {
            throw std::invalid_argument("Precise physics ray query is invalid.");
        }
        constexpr double maximumPositionErrorMetres = 0.001;
        const auto origin = m_Impl->PhysicsWorldService->ToLocalPosition(query.Origin, maximumPositionErrorMetres);
        m_Impl->SynchronizePhysicsQueries();
        for (const auto& hit : m_Impl->PhysicsWorldService->RayCast(
                 {origin, query.Direction, query.MaximumDistance, query.Mask, query.IncludeTriggers, query.Layer}))
        {
            const auto entity = m_Impl->EntityForBody(hit.Body);
            if (entity && *entity != ignoredEntity)
            {
                result.push_back({*entity, hit.Body, m_Impl->PhysicsWorldService->ToWorldPosition(hit.Position),
                                  hit.Normal, hit.Distance});
            }
        }
        return result;
    }

    std::optional<ScenePhysicsQueryHit> SceneRuntimeSession::CastCapsule(const PhysicsCapsuleCastQuery& query,
                                                                         const EntityId ignoredEntity) const
    {
        m_Impl->RequireOwner("CastCapsule");
        if (!m_Impl->PhysicsWorldService)
            return std::nullopt;
        const auto displacementSquared = query.Displacement.X * query.Displacement.X +
                                         query.Displacement.Y * query.Displacement.Y +
                                         query.Displacement.Z * query.Displacement.Z;
        if (!Math::IsFinite(query.Origin) || !Math::IsFinite(query.Rotation) || !Math::IsFinite(query.Displacement) ||
            !std::isfinite(query.Radius) || query.Radius <= 0 || !std::isfinite(query.Height) ||
            query.Height < 2 * query.Radius || !std::isfinite(displacementSquared) ||
            displacementSquared <= std::numeric_limits<float>::epsilon() || !std::has_single_bit(query.Layer))
            throw std::invalid_argument("Physics capsule cast is invalid.");
        constexpr double maximumPositionErrorMetres = 0.001;
        auto candidate = query;
        candidate.Origin = m_Impl->PhysicsWorldService->ToLocalPosition(
            {query.Origin.X, query.Origin.Y, query.Origin.Z}, maximumPositionErrorMetres);
        m_Impl->SynchronizePhysicsQueries();
        if (ignoredEntity)
        {
            const auto found = m_Impl->PhysicsBodies.find(ignoredEntity);
            candidate.IgnoreBody = found == m_Impl->PhysicsBodies.end() ? PhysicsBodyId{} : found->second.Body;
        }
        auto hit = m_Impl->PhysicsWorldService->CastCapsule(candidate);
        const auto entity = hit ? m_Impl->EntityForBody(hit->Body) : std::nullopt;
        if (hit && entity)
        {
            hit->Position = Math::RelativeFloat(m_Impl->PhysicsWorldService->ToWorldPosition(hit->Position), {},
                                                std::numeric_limits<double>::max());
        }
        return hit && entity ? std::optional{ScenePhysicsQueryHit{*entity, *hit}} : std::nullopt;
    }

    std::vector<EntityId> SceneRuntimeSession::OverlapSphere(const PhysicsSphereOverlapQuery& query,
                                                             const EntityId ignoredEntity) const
    {
        m_Impl->RequireOwner("OverlapSphere");
        std::vector<EntityId> result;
        if (!m_Impl->PhysicsWorldService)
            return result;
        if (!Math::IsFinite(query.Center) || !std::isfinite(query.Radius) || query.Radius <= 0 ||
            !std::has_single_bit(query.Layer))
            throw std::invalid_argument("Physics sphere overlap is invalid.");
        constexpr double maximumPositionErrorMetres = 0.001;
        auto candidate = query;
        candidate.Center = m_Impl->PhysicsWorldService->ToLocalPosition(
            {query.Center.X, query.Center.Y, query.Center.Z}, maximumPositionErrorMetres);
        m_Impl->SynchronizePhysicsQueries();
        if (ignoredEntity)
        {
            const auto found = m_Impl->PhysicsBodies.find(ignoredEntity);
            candidate.IgnoreBody = found == m_Impl->PhysicsBodies.end() ? PhysicsBodyId{} : found->second.Body;
        }
        for (const auto body : m_Impl->PhysicsWorldService->OverlapSphere(candidate))
        {
            if (const auto entity = m_Impl->EntityForBody(body); entity && *entity != ignoredEntity)
                result.push_back(*entity);
        }
        return result;
    }

    std::optional<ScenePhysicsQueryHitPrecise>
    SceneRuntimeSession::CastCapsulePrecise(const ScenePhysicsCapsuleCastQueryPrecise& query,
                                            const EntityId ignoredEntity) const
    {
        m_Impl->RequireOwner("CastCapsulePrecise");
        if (!m_Impl->PhysicsWorldService)
        {
            return std::nullopt;
        }
        const auto distanceSquared = query.Displacement.X * query.Displacement.X +
                                     query.Displacement.Y * query.Displacement.Y +
                                     query.Displacement.Z * query.Displacement.Z;
        if (!Math::IsFinite(query.Origin) || !Math::IsFinite(query.Rotation) || !Math::IsFinite(query.Displacement) ||
            !std::isfinite(distanceSquared) || distanceSquared <= std::numeric_limits<float>::epsilon() ||
            !std::isfinite(query.Radius) || query.Radius <= 0.0F || !std::isfinite(query.Height) ||
            query.Height < 2.0F * query.Radius || !std::has_single_bit(query.Layer))
        {
            throw std::invalid_argument("Precise physics capsule cast is invalid.");
        }
        constexpr double maximumPositionErrorMetres = 0.001;
        PhysicsCapsuleCastQuery candidate{
            m_Impl->PhysicsWorldService->ToLocalPosition(query.Origin, maximumPositionErrorMetres),
            query.Rotation,
            query.Radius,
            query.Height,
            query.Displacement,
            query.Mask,
            query.IncludeTriggers,
            query.Layer};
        m_Impl->SynchronizePhysicsQueries();
        if (ignoredEntity)
        {
            const auto found = m_Impl->PhysicsBodies.find(ignoredEntity);
            candidate.IgnoreBody = found == m_Impl->PhysicsBodies.end() ? PhysicsBodyId{} : found->second.Body;
        }
        const auto hit = m_Impl->PhysicsWorldService->CastCapsule(candidate);
        const auto entity = hit ? m_Impl->EntityForBody(hit->Body) : std::nullopt;
        if (!hit || !entity)
        {
            return std::nullopt;
        }
        return ScenePhysicsQueryHitPrecise{*entity, hit->Body,
                                           m_Impl->PhysicsWorldService->ToWorldPosition(hit->Position), hit->Normal,
                                           hit->Distance};
    }

    std::vector<EntityId> SceneRuntimeSession::OverlapSpherePrecise(const ScenePhysicsSphereOverlapQueryPrecise& query,
                                                                    const EntityId ignoredEntity) const
    {
        m_Impl->RequireOwner("OverlapSpherePrecise");
        std::vector<EntityId> result;
        if (!m_Impl->PhysicsWorldService)
        {
            return result;
        }
        if (!Math::IsFinite(query.Center) || !std::isfinite(query.Radius) || query.Radius <= 0.0F ||
            !std::has_single_bit(query.Layer))
        {
            throw std::invalid_argument("Precise physics sphere overlap is invalid.");
        }
        constexpr double maximumPositionErrorMetres = 0.001;
        PhysicsSphereOverlapQuery candidate{
            m_Impl->PhysicsWorldService->ToLocalPosition(query.Center, maximumPositionErrorMetres), query.Radius,
            query.Mask, query.IncludeTriggers, query.Layer};
        m_Impl->SynchronizePhysicsQueries();
        if (ignoredEntity)
        {
            const auto found = m_Impl->PhysicsBodies.find(ignoredEntity);
            candidate.IgnoreBody = found == m_Impl->PhysicsBodies.end() ? PhysicsBodyId{} : found->second.Body;
        }
        for (const auto body : m_Impl->PhysicsWorldService->OverlapSphere(candidate))
        {
            if (const auto entity = m_Impl->EntityForBody(body); entity && *entity != ignoredEntity)
            {
                result.push_back(*entity);
            }
        }
        return result;
    }

    void SceneRuntimeSession::SetPresentationViewport(const float width, const float height,
                                                      const RuntimeUiInsets safeArea)
    {
        m_Impl->RequireOwner("SetPresentationViewport");
        if (!std::isfinite(width) || !std::isfinite(height) || width <= 0.0F || height <= 0.0F)
            throw std::invalid_argument("Scene presentation viewport dimensions must be finite and positive.");
        if (m_Impl->PresentationWidth == width && m_Impl->PresentationHeight == height &&
            m_Impl->SafeArea.Left == safeArea.Left && m_Impl->SafeArea.Top == safeArea.Top &&
            m_Impl->SafeArea.Right == safeArea.Right && m_Impl->SafeArea.Bottom == safeArea.Bottom)
        {
            return;
        }
        m_Impl->PresentationWidth = width;
        m_Impl->PresentationHeight = height;
        m_Impl->SafeArea = safeArea;
        if (m_Impl->Presentation && m_Impl->Runtime)
            m_Impl->Presentation->Synchronize(m_Impl->Runtime, width, height, true, safeArea);
    }

    void SceneRuntimeSession::Play()
    {
        m_Impl->RequireOwner("Play");
        if (m_Impl->PlayState != ScenePlayState::Stopped)
            return;
        const auto startupBegan = std::chrono::steady_clock::now();
        const auto elapsedMilliseconds = [](const auto began)
        { return std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - began).count(); };
        m_Impl->Failure = {};
        m_Impl->ClearAnimation();
        const auto cloneBegan = std::chrono::steady_clock::now();
        m_Impl->Runtime = CreateRef<Scene>(m_Impl->Edit->Asset(), m_Impl->Edit->Snapshot(), m_Impl->Edit->Components());
        m_Impl->Runtime->MarkSaved();
        const float cloneMilliseconds = elapsedMilliseconds(cloneBegan);
        m_Impl->PlayState = ScenePlayState::Playing;
        const auto physicsBegan = std::chrono::steady_clock::now();
        m_Impl->Invoke("Physics initialization", [&] { m_Impl->InitializePhysics(); });
        const float physicsMilliseconds = elapsedMilliseconds(physicsBegan);
        const auto scriptsBegan = std::chrono::steady_clock::now();
        if (m_Impl->PlayState != ScenePlayState::Faulted)
            m_Impl->Invoke("Awake/OnEnable", [&] { m_Impl->Runtime->BeginPlay(); });
        const float scriptsMilliseconds = elapsedMilliseconds(scriptsBegan);
        const auto vfxBegan = std::chrono::steady_clock::now();
        if (m_Impl->PlayState != ScenePlayState::Faulted)
            m_Impl->Invoke("VFX initialization", [&] { m_Impl->InitializeVfx(); });
        const float vfxMilliseconds = elapsedMilliseconds(vfxBegan);
        const auto presentationBegan = std::chrono::steady_clock::now();
        if (m_Impl->Presentation)
            m_Impl->Presentation->Synchronize(m_Impl->Runtime, m_Impl->PresentationWidth, m_Impl->PresentationHeight,
                                              true, m_Impl->SafeArea);
        const float presentationMilliseconds = elapsedMilliseconds(presentationBegan);
        const float totalMilliseconds = elapsedMilliseconds(startupBegan);
        if (totalMilliseconds >= 100.0F)
        {
            KEIRE_CORE_WARN("Play Mode startup {:.2f} ms (scene clone {:.2f}, physics {:.2f}, scripts {:.2f}, VFX "
                            "{:.2f}, presentation {:.2f}).",
                            totalMilliseconds, cloneMilliseconds, physicsMilliseconds, scriptsMilliseconds,
                            vfxMilliseconds, presentationMilliseconds);
        }
        else
        {
            KEIRE_CORE_INFO("Play Mode startup {:.2f} ms (scene clone {:.2f}, physics {:.2f}, scripts {:.2f}, VFX "
                            "{:.2f}, presentation {:.2f}).",
                            totalMilliseconds, cloneMilliseconds, physicsMilliseconds, scriptsMilliseconds,
                            vfxMilliseconds, presentationMilliseconds);
        }
    }

    void SceneRuntimeSession::Pause(const bool paused)
    {
        m_Impl->RequireOwner("Pause");
        if (m_Impl->PlayState == ScenePlayState::Playing && paused)
            m_Impl->PlayState = ScenePlayState::Paused;
        else if (m_Impl->PlayState == ScenePlayState::Paused && !paused)
            m_Impl->PlayState = ScenePlayState::Playing;
    }

    void SceneRuntimeSession::TogglePause() { Pause(m_Impl->PlayState != ScenePlayState::Paused); }

    bool SceneRuntimeSession::Step(const float fixedDeltaSeconds)
    {
        m_Impl->RequireOwner("Step");
        if (m_Impl->PlayState != ScenePlayState::Paused)
            return false;
        if (!std::isfinite(fixedDeltaSeconds) || fixedDeltaSeconds <= 0.0F)
            throw std::invalid_argument("Scene step delta must be finite and positive.");
        AdvanceSimulationOrigin();
        if (m_Impl->PlayState == ScenePlayState::Faulted)
        {
            return false;
        }
        m_Impl->Invoke("Fixed presentation begin", [&] { m_Impl->CaptureCustomFixedPresentation(true); });
        if (m_Impl->PlayState != ScenePlayState::Faulted)
            m_Impl->Invoke("FixedUpdate", [&] { m_Impl->Runtime->FixedUpdate(fixedDeltaSeconds); });
        if (m_Impl->PlayState != ScenePlayState::Faulted)
            AdvanceSimulationOrigin();
        if (m_Impl->PlayState != ScenePlayState::Faulted)
            m_Impl->Invoke("Physics", [&] { m_Impl->StepPhysics(fixedDeltaSeconds); });
        if (m_Impl->PlayState != ScenePlayState::Faulted)
            m_Impl->Invoke("Procedural Animation", [&] { m_Impl->AdvanceProceduralAnimation(fixedDeltaSeconds); });
        if (m_Impl->DeterministicSimulation && m_Impl->PlayState != ScenePlayState::Faulted)
        {
            m_Impl->Invoke("Fixed VFX", [&] { m_Impl->SynchronizeVfx(fixedDeltaSeconds); });
        }
        if (m_Impl->PlayState != ScenePlayState::Faulted)
        {
            m_Impl->Invoke("Fixed presentation capture", [&] { m_Impl->CaptureCustomFixedPresentation(false); });
            if (m_Impl->PlayState != ScenePlayState::Faulted)
                m_Impl->AdvanceFrame(fixedDeltaSeconds, 1.0F);
        }
        return m_Impl->PlayState != ScenePlayState::Faulted;
    }

    void SceneRuntimeSession::FixedUpdate(const float deltaSeconds)
    {
        m_Impl->RequireOwner("FixedUpdate");
        if (m_Impl->PlayState == ScenePlayState::Playing)
        {
            AdvanceSimulationOrigin();
            if (m_Impl->PlayState == ScenePlayState::Faulted)
            {
                return;
            }
            m_Impl->Invoke("Fixed presentation begin", [&] { m_Impl->CaptureCustomFixedPresentation(true); });
            if (m_Impl->PlayState != ScenePlayState::Faulted)
                m_Impl->Invoke("FixedUpdate", [&] { m_Impl->Runtime->FixedUpdate(deltaSeconds); });
            if (m_Impl->PlayState != ScenePlayState::Faulted)
                AdvanceSimulationOrigin();
            if (m_Impl->PlayState != ScenePlayState::Faulted)
                m_Impl->Invoke("Physics", [&] { m_Impl->StepPhysics(deltaSeconds); });
            if (m_Impl->PlayState != ScenePlayState::Faulted)
                m_Impl->Invoke("Procedural Animation", [&] { m_Impl->AdvanceProceduralAnimation(deltaSeconds); });
            if (m_Impl->DeterministicSimulation && m_Impl->PlayState != ScenePlayState::Faulted)
            {
                m_Impl->Invoke("Fixed VFX", [&] { m_Impl->SynchronizeVfx(deltaSeconds); });
            }
            if (m_Impl->PlayState != ScenePlayState::Faulted)
                m_Impl->Invoke("Fixed presentation capture", [&] { m_Impl->CaptureCustomFixedPresentation(false); });
        }
    }

    void SceneRuntimeSession::Update(const float deltaSeconds) { Update(deltaSeconds, 1.0F); }

    void SceneRuntimeSession::Update(const float deltaSeconds, const float interpolationAlpha)
    {
        m_Impl->RequireOwner("Update");
        if (!std::isfinite(interpolationAlpha) || interpolationAlpha < 0.0F || interpolationAlpha > 1.0F)
            throw std::invalid_argument("Scene presentation interpolation alpha must be finite and in the range 0..1.");
        if (m_Impl->PlayState == ScenePlayState::Playing)
            m_Impl->AdvanceFrame(deltaSeconds, interpolationAlpha);
    }

    void SceneRuntimeSession::Impl::CaptureCustomFixedPresentation(const bool begin)
    {
        for (const auto entity : Runtime->FixedPresentationEntities())
        {
            const auto transform = entity.GetComponent<TransformComponent>();
            if (!transform || !transform->FixedPresentationInterpolation())
                continue;
            const auto rigidBody = entity.GetComponent<RigidBodyComponent>();
            const auto character = entity.GetComponent<CharacterControllerComponent>();
            const auto collider = entity.GetComponent<ColliderComponent>();
            const bool physicsOwned =
                (character && character->Enabled()) ||
                (collider && collider->Enabled() && rigidBody && rigidBody->Motion() == PhysicsMotionType::Dynamic);
            if (!entity.ActiveInHierarchy() || physicsOwned)
                transform->ClearRuntimeFixedPresentation();
            else if (begin)
                transform->BeginRuntimeFixedPresentationSample();
            else
                transform->EndRuntimeFixedPresentationSample();
        }
    }

    void SceneRuntimeSession::Impl::ApplyCustomFixedPresentation(const float alpha)
    {
        for (const auto entity : Runtime->FixedPresentationEntities())
        {
            const auto transform = entity.GetComponent<TransformComponent>();
            if (!transform || !transform->FixedPresentationInterpolation())
                continue;
            const auto rigidBody = entity.GetComponent<RigidBodyComponent>();
            const auto character = entity.GetComponent<CharacterControllerComponent>();
            const auto collider = entity.GetComponent<ColliderComponent>();
            const bool physicsOwned =
                (character && character->Enabled()) ||
                (collider && collider->Enabled() && rigidBody && rigidBody->Motion() == PhysicsMotionType::Dynamic);
            if (!entity.ActiveInHierarchy() || physicsOwned)
                transform->ClearRuntimeFixedPresentation();
            else
                transform->ApplyRuntimeFixedPresentation(alpha);
        }
    }

    void SceneRuntimeSession::Impl::AdvanceFrame(const float deltaSeconds, const float interpolationAlpha)
    {
        PresentationInterpolationAlpha = interpolationAlpha;
        ApplyPhysicsPresentationInterpolation(interpolationAlpha);
        Invoke("Fixed presentation", [&] { ApplyCustomFixedPresentation(interpolationAlpha); });
        if (PlayState != ScenePlayState::Faulted)
            Invoke("Update", [&] { Runtime->Update(deltaSeconds); });
        if (PlayState != ScenePlayState::Faulted)
            Invoke("Animation", [&] { SynchronizeAnimation(deltaSeconds); });
        if (PlayState != ScenePlayState::Faulted)
            Invoke("LateUpdate", [&] { Runtime->LateUpdate(); });
        if (NamedIkInspection.Enabled())
            Invoke("Named IK inspection", [&] { NamedIkInspection.Observe(deltaSeconds); });
        // Strict replay must not capture VFX clocks advanced by variable render-frame timing.
        if (!DeterministicSimulation && PlayState != ScenePlayState::Faulted)
        {
            Invoke("VFX", [&] { SynchronizeVfx(deltaSeconds); });
        }
        if (Presentation && PlayState != ScenePlayState::Faulted)
        {
            Presentation->Synchronize(Runtime, PresentationWidth, PresentationHeight, true, SafeArea);
            Presentation->AdvanceUi(std::max(deltaSeconds, 0.0F));
        }
    }

    void SceneRuntimeSession::ReplaceRuntime(SceneDefinition definition)
    {
        m_Impl->RequireOwner("ReplaceRuntime");
        if (m_Impl->PlayState == ScenePlayState::Stopped || !m_Impl->Runtime)
            throw std::logic_error("SceneRuntimeSession::ReplaceRuntime requires an active Play session.");
        auto replacement = CreateRef<Scene>(m_Impl->Edit->Asset(), std::move(definition), m_Impl->Edit->Components());
        replacement->MarkSaved();
        const auto resumeState =
            m_Impl->PlayState == ScenePlayState::Faulted ? m_Impl->StateBeforeFailure : m_Impl->PlayState;
        m_Impl->ClearAnimation();
        m_Impl->ClearVfx();
        m_Impl->ClearPhysics();
        m_Impl->Runtime->EndPlay();
        m_Impl->Runtime->Close();
        if (m_Impl->Presentation)
            m_Impl->Presentation->Clear();
        m_Impl->Runtime = std::move(replacement);
        m_Impl->SimulationFocus = {};
        m_Impl->Failure = {};
        m_Impl->PlayState = resumeState;
        const auto initialize = [&](const char* phase, auto&& operation)
        {
            m_Impl->Invoke(phase, std::forward<decltype(operation)>(operation));
            if (m_Impl->PlayState == ScenePlayState::Faulted)
            {
                throw std::runtime_error(std::string("Runtime replacement failed during ") + phase + ": " +
                                         m_Impl->Failure.Message);
            }
        };
        initialize("Physics initialization", [&] { m_Impl->InitializePhysics(); });
        initialize("Awake/OnEnable", [&] { m_Impl->Runtime->BeginPlay(); });
        initialize("VFX initialization", [&] { m_Impl->InitializeVfx(); });
        initialize("Presentation initialization",
                   [&]
                   {
                       if (m_Impl->Presentation)
                       {
                           m_Impl->Presentation->Synchronize(m_Impl->Runtime, m_Impl->PresentationWidth,
                                                             m_Impl->PresentationHeight, true, m_Impl->SafeArea);
                       }
                   });
    }

    void SceneRuntimeSession::SetSimulationFocus(const Entity focus, const double rebaseDistanceMetres,
                                                 const double gridSpacingMetres)
    {
        m_Impl->RequireOwner("SetSimulationFocus");
        if (m_Impl->InvokingCallback || !m_Impl->Runtime ||
            (m_Impl->PlayState != ScenePlayState::Playing && m_Impl->PlayState != ScenePlayState::Paused))
        {
            throw std::logic_error("Simulation focus requires an active session between callbacks.");
        }
        (void)Detail::SelectSimulationOrigin({}, {}, {rebaseDistanceMetres, gridSpacingMetres});
        if (focus.Id() && (!focus || m_Impl->Runtime->FindEntity(focus.Id()) != focus))
        {
            throw std::invalid_argument("Simulation focus must belong to this live runtime scene.");
        }
        m_Impl->SimulationFocus = focus;
        m_Impl->OriginRebaseDistanceMetres = rebaseDistanceMetres;
        m_Impl->OriginGridSpacingMetres = gridSpacingMetres;
    }

    Entity SceneRuntimeSession::SimulationFocus() const
    {
        m_Impl->RequireOwner("SimulationFocus");
        return m_Impl->SimulationFocus ? m_Impl->SimulationFocus : Entity{};
    }

    void SceneRuntimeSession::AdvanceSimulationOrigin()
    {
        if (!m_Impl->SimulationFocus)
        {
            m_Impl->SimulationFocus = {};
            return;
        }
        try
        {
            if (!m_Impl->SimulationFocus.ActiveInHierarchy())
            {
                return;
            }
            const auto transform = m_Impl->SimulationFocus.GetComponent<TransformComponent>();
            if (!transform)
            {
                throw std::logic_error("Simulation focus requires a transform.");
            }
            const auto origin = m_Impl->PhysicsWorldService ? m_Impl->PhysicsWorldService->WorldOrigin()
                                                            : m_Impl->VfxWorldService->WorldOrigin();
            const auto selected =
                Detail::SelectSimulationOrigin(origin, transform->WorldPositionPrecise(),
                                               {m_Impl->OriginRebaseDistanceMetres, m_Impl->OriginGridSpacingMetres});
            if (selected)
            {
                RebaseSimulationOrigin(*selected);
            }
        }
        catch (const std::exception& exception)
        {
            m_Impl->RecordFailure("Simulation origin", exception.what());
        }
    }

    void SceneRuntimeSession::RebaseSimulationOrigin(const Vector3d origin)
    {
        m_Impl->RequireOwner("RebaseSimulationOrigin");
        if (m_Impl->InvokingCallback ||
            (m_Impl->PlayState != ScenePlayState::Playing && m_Impl->PlayState != ScenePlayState::Paused))
        {
            throw std::logic_error("Simulation origin changes require an active session between callbacks.");
        }
        if (!Math::IsFinite(origin))
        {
            throw std::invalid_argument("Simulation origin must be finite.");
        }
        const auto rebasePhysics = [&]
        {
            if (m_Impl->PhysicsWorldService)
            {
                RebasePhysicsOrigin(origin);
            }
        };
        if (m_Impl->VfxWorldService)
        {
            m_Impl->VfxWorldService->RebaseOriginAfter(origin, 0.001, rebasePhysics);
        }
        else
        {
            rebasePhysics();
        }
    }

    void SceneRuntimeSession::RebasePhysicsOrigin(const Vector3d origin)
    {
        m_Impl->RequireOwner("RebasePhysicsOrigin");
        if (m_Impl->InvokingCallback || !m_Impl->PhysicsWorldService ||
            (m_Impl->PlayState != ScenePlayState::Playing && m_Impl->PlayState != ScenePlayState::Paused))
        {
            throw std::logic_error("Physics origin changes require an active session between callbacks.");
        }
        if (!Math::IsFinite(origin))
        {
            throw std::invalid_argument("Physics origin must be finite.");
        }
        const auto previous = m_Impl->PhysicsWorldService->WorldOrigin();
        if (previous == origin)
        {
            return;
        }
        const Vector3d delta{previous.X - origin.X, previous.Y - origin.Y, previous.Z - origin.Z};
        constexpr double maximumErrorMetres = 0.001;
        std::vector<std::pair<Vector3*, Vector3>> positions;
        std::vector<std::pair<Matrix4*, Matrix4>> matrices;
        const auto preparePosition = [&](Vector3& position)
        { positions.emplace_back(&position, Detail::TranslatePhysicsPosition(position, delta, maximumErrorMetres)); };
        const auto prepareMatrix = [&](Matrix4& matrix)
        { matrices.emplace_back(&matrix, Detail::TranslatePhysicsMatrix(matrix, delta, maximumErrorMetres)); };
        for (auto& [entity, state] : m_Impl->PhysicsBodies)
        {
            (void)entity;
            if (state.HasDefinition)
            {
                preparePosition(state.Definition.Position);
            }
            prepareMatrix(state.MovementWorld);
            if (state.CharacterSupport.Entity)
            {
                preparePosition(state.CharacterSupport.Definition.Position);
                preparePosition(state.CharacterSupport.ResolvedRiderPosition);
                prepareMatrix(state.CharacterSupport.ReferenceWorld);
            }
        }
        const auto prepareFoot = [&](Impl::AnimationRuntimeState::FootPlantRuntimeState& foot)
        {
            preparePosition(foot.Plant.Position);
            preparePosition(foot.SurfacePosition);
            preparePosition(foot.ReleasePosition);
            preparePosition(foot.AnimationPlantPosition);
            if (foot.ReconciliationSupportState.Previous)
            {
                prepareMatrix(*foot.ReconciliationSupportState.Previous);
            }
        };
        for (auto& [entity, state] : m_Impl->Animators)
        {
            (void)entity;
            if (!state)
            {
                continue;
            }
            prepareFoot(state->LeftFootPlantState);
            prepareFoot(state->RightFootPlantState);
            if (state->LeftFootGroundingSmoothingState.Initialized)
            {
                preparePosition(state->LeftFootGroundingSmoothingState.Position);
            }
            if (state->RightFootGroundingSmoothingState.Initialized)
            {
                preparePosition(state->RightFootGroundingSmoothingState.Position);
            }
        }

        m_Impl->PhysicsWorldService->RebaseOrigin(origin, maximumErrorMetres);
        for (const auto& [destination, position] : positions)
        {
            *destination = position;
        }
        for (const auto& [destination, matrix] : matrices)
        {
            *destination = matrix;
        }
    }

    void SceneRuntimeSession::Stop() noexcept
    {
        if (!m_Impl || m_Impl->PlayState == ScenePlayState::Stopped)
            return;
        m_Impl->SimulationFocus = {};
        if (m_Impl->Runtime)
        {
            m_Impl->ClearAnimation();
            m_Impl->ClearVfx();
            m_Impl->ClearPhysics();
            m_Impl->Runtime->EndPlay();
            m_Impl->Runtime->Close();
            if (m_Impl->Presentation)
                m_Impl->Presentation->Clear();
            m_Impl->Runtime.Reset();
        }
        m_Impl->PlayState = ScenePlayState::Stopped;
        m_Impl->Failure = {};
    }
} // namespace Keire
