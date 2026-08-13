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
#include "KeireInternal/Scenes/SceneRuntimeSessionImpl.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <exception>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <span>
#include <stdexcept>
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
                              state->Sleeping});
        }
        return result;
    }

    void SceneRuntimeSession::RestorePhysicsCheckpoint(const std::span<const ScenePhysicsCheckpointBody> bodies)
    {
        m_Impl->RequireOwner("RestorePhysicsCheckpoint");
        if (!m_Impl->PhysicsWorldService)
        {
            if (!bodies.empty())
                throw std::runtime_error("A physics checkpoint cannot be restored without an active physics world.");
            return;
        }

        std::set<EntityId> identities;
        for (const auto& body : bodies)
        {
            const auto found = m_Impl->PhysicsBodies.find(body.Entity);
            if (!body.Entity || !identities.insert(body.Entity).second || found == m_Impl->PhysicsBodies.end() ||
                !found->second.Body || !Math::IsFinite(body.Position) || !Math::IsFinite(body.Rotation) ||
                !Math::IsFinite(body.LinearVelocity) || !Math::IsFinite(body.AngularVelocity))
            {
                throw std::runtime_error("A scene physics checkpoint is incompatible with the runtime scene.");
            }
        }
        const auto liveBodyCount = static_cast<std::size_t>(std::ranges::count_if(
            m_Impl->PhysicsBodies, [](const auto& entry) { return static_cast<bool>(entry.second.Body); }));
        if (identities.size() != liveBodyCount)
            throw std::runtime_error("A scene physics checkpoint does not contain every runtime body.");

        for (const auto& body : bodies)
        {
            const auto runtimeBody = m_Impl->PhysicsBodies.at(body.Entity).Body;
            m_Impl->PhysicsWorldService->SetBodyState(
                runtimeBody,
                {runtimeBody, body.Position, body.Rotation, body.LinearVelocity, body.AngularVelocity, body.Sleeping});
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
        for (const auto& hit : m_Impl->PhysicsWorldService->RayCast(query))
        {
            const auto entity = m_Impl->EntityForBody(hit.Body);
            if (entity && *entity != ignoredEntity)
                result.push_back({*entity, hit});
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
        if (fixedDeltaSeconds <= 0.0F)
            throw std::invalid_argument("Scene step delta must be positive.");
        m_Impl->Invoke("FixedUpdate", [&] { m_Impl->Runtime->FixedUpdate(fixedDeltaSeconds); });
        if (m_Impl->PlayState != ScenePlayState::Faulted)
            m_Impl->Invoke("Physics", [&] { m_Impl->StepPhysics(fixedDeltaSeconds); });
        return m_Impl->PlayState != ScenePlayState::Faulted;
    }

    void SceneRuntimeSession::FixedUpdate(const float deltaSeconds)
    {
        m_Impl->RequireOwner("FixedUpdate");
        if (m_Impl->PlayState == ScenePlayState::Playing)
        {
            m_Impl->Invoke("FixedUpdate", [&] { m_Impl->Runtime->FixedUpdate(deltaSeconds); });
            if (m_Impl->PlayState != ScenePlayState::Faulted)
                m_Impl->Invoke("Physics", [&] { m_Impl->StepPhysics(deltaSeconds); });
        }
    }

    void SceneRuntimeSession::Update(const float deltaSeconds)
    {
        m_Impl->RequireOwner("Update");
        if (m_Impl->PlayState == ScenePlayState::Playing)
        {
            m_Impl->Invoke("Update", [&] { m_Impl->Runtime->Update(deltaSeconds); });
            if (m_Impl->PlayState != ScenePlayState::Faulted)
                m_Impl->Invoke("Animation", [&] { m_Impl->SynchronizeAnimation(deltaSeconds); });
            if (m_Impl->PlayState != ScenePlayState::Faulted)
                m_Impl->Invoke("LateUpdate", [&] { m_Impl->Runtime->LateUpdate(); });
            if (m_Impl->PlayState != ScenePlayState::Faulted)
                m_Impl->Invoke("VFX", [&] { m_Impl->SynchronizeVfx(deltaSeconds); });
            if (m_Impl->Presentation && m_Impl->PlayState != ScenePlayState::Faulted)
                m_Impl->Presentation->Synchronize(m_Impl->Runtime, m_Impl->PresentationWidth,
                                                  m_Impl->PresentationHeight, true, m_Impl->SafeArea);
        }
    }

    void SceneRuntimeSession::ReplaceRuntime(SceneDefinition definition)
    {
        m_Impl->RequireOwner("ReplaceRuntime");
        if (m_Impl->PlayState == ScenePlayState::Stopped || !m_Impl->Runtime)
            throw std::logic_error("SceneRuntimeSession::ReplaceRuntime requires an active Play session.");
        auto replacement = CreateRef<Scene>(m_Impl->Edit->Asset(), std::move(definition), m_Impl->Edit->Components());
        replacement->MarkSaved();
        m_Impl->ClearAnimation();
        m_Impl->ClearVfx();
        m_Impl->ClearPhysics();
        m_Impl->Runtime->EndPlay();
        m_Impl->Runtime->Close();
        if (m_Impl->Presentation)
            m_Impl->Presentation->Clear();
        m_Impl->Runtime = std::move(replacement);
        m_Impl->Failure = {};
        m_Impl->Invoke("Physics initialization", [&] { m_Impl->InitializePhysics(); });
        if (m_Impl->PlayState != ScenePlayState::Faulted)
            m_Impl->Invoke("Awake/OnEnable", [&] { m_Impl->Runtime->BeginPlay(); });
        if (m_Impl->PlayState != ScenePlayState::Faulted)
            m_Impl->Invoke("VFX initialization", [&] { m_Impl->InitializeVfx(); });
        if (m_Impl->Presentation)
            m_Impl->Presentation->Synchronize(m_Impl->Runtime, m_Impl->PresentationWidth, m_Impl->PresentationHeight,
                                              true, m_Impl->SafeArea);
    }

    void SceneRuntimeSession::Stop() noexcept
    {
        if (!m_Impl || m_Impl->PlayState == ScenePlayState::Stopped)
            return;
        if (m_Impl->Runtime)
        {
            m_Impl->ClearAnimation();
            m_Impl->ClearVfx();
            m_Impl->ClearPhysics();
            if (m_Impl->Presentation)
                m_Impl->Presentation->Clear();
            m_Impl->Runtime->EndPlay();
            m_Impl->Runtime->Close();
            m_Impl->Runtime.Reset();
        }
        m_Impl->PlayState = ScenePlayState::Stopped;
        m_Impl->Failure = {};
    }
} // namespace Keire
