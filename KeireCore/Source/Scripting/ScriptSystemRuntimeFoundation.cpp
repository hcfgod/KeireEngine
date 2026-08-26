#include "KeireInternal/Scripting/ScriptSystemInternal.h"

namespace Keire
{
    [[nodiscard]] Entity ScriptSystem::Impl::ResolveRuntimeEntity(const std::uint64_t world, const std::uint64_t high,
                                                                  const std::uint64_t low) noexcept
    {
        if (!CurrentRuntime)
            return {};
        const AssetId id(high, low);
        const auto found = std::ranges::find_if(CurrentRuntime->Instances, [world](const auto& entry)
                                                { return entry.second.World == world && entry.second.NativeEntity; });
        return found == CurrentRuntime->Instances.end() ? Entity{} : found->second.NativeEntity.Resolve(EntityId(id));
    }

    std::uint8_t ScriptSystem::Impl::RuntimeRequestManagedAssetLoad(const std::uint64_t generation,
                                                                    const std::uint64_t high,
                                                                    const std::uint64_t low) noexcept
    {
        if (!CurrentRuntime || !CurrentRuntime->Assets)
            return 0;
        try
        {
            {
                std::scoped_lock lock(CurrentRuntime->Mutex);
                const bool active = CurrentRuntime->Reload.Generation == generation &&
                                    CurrentRuntime->Reload.State == ManagedReloadState::Active;
                const bool candidate = CurrentRuntime->Reload.State == ManagedReloadState::Prepared &&
                                       CurrentRuntime->Reload.Generation != std::numeric_limits<std::uint64_t>::max() &&
                                       CurrentRuntime->Reload.Generation + 1 == generation;
                if (!active && !candidate)
                    return 0;
            }
            const AssetId id(high, low);
            if (!id)
                return 0;
            std::scoped_lock lock(CurrentRuntime->ManagedAssetMutex);
            if (CurrentRuntime->PendingManagedAssetLoads.contains(id) ||
                CurrentRuntime->ManagedAssetSources.contains(id) ||
                CurrentRuntime->PendingManagedAssetLoads.size() >=
                    CurrentRuntime->Specification.MaximumManagedDataLoads)
            {
                return 0;
            }
            CurrentRuntime->PendingManagedAssetLoads.emplace(
                id, PendingManagedAssetLoad{CurrentRuntime->Assets->Load<ManagedDataAsset>(id, AssetPriority::High),
                                            generation});
            return 1;
        }
        catch (...)
        {
            return 0;
        }
    }

    void ScriptSystem::Impl::RuntimeCancelManagedAssetLoad(const std::uint64_t generation, const std::uint64_t high,
                                                           const std::uint64_t low) noexcept
    {
        if (!CurrentRuntime)
            return;
        try
        {
            std::scoped_lock lock(CurrentRuntime->ManagedAssetMutex);
            const auto found = CurrentRuntime->PendingManagedAssetLoads.find(AssetId(high, low));
            if (found != CurrentRuntime->PendingManagedAssetLoads.end() && found->second.Generation == generation)
                CurrentRuntime->PendingManagedAssetLoads.erase(found);
        }
        catch (...)
        {
        }
    }

    void ScriptSystem::Impl::RuntimeReleaseManagedAsset(const std::uint64_t generation, const std::uint64_t high,
                                                        const std::uint64_t low) noexcept
    {
        if (!CurrentRuntime)
            return;
        try
        {
            {
                std::scoped_lock lock(CurrentRuntime->Mutex);
                if (CurrentRuntime->Reload.Generation != generation)
                    return;
            }
            std::scoped_lock lock(CurrentRuntime->ManagedAssetMutex);
            const AssetId id(high, low);
            CurrentRuntime->PendingManagedAssetLoads.erase(id);
            CurrentRuntime->ManagedAssetSources.erase(id);
        }
        catch (...)
        {
        }
    }

    std::uint64_t ScriptSystem::Impl::RuntimeSubmitManagedJob(const std::uint64_t* dependencyIds,
                                                              const std::int32_t dependencyCount,
                                                              const std::uint8_t priority, const std::uint8_t jobClass,
                                                              const Coral::String name, void* state,
                                                              const ManagedJobCallback callback) noexcept
    {
        auto* runtime = CurrentRuntime;
        if (!runtime || !callback || !state || dependencyCount < 0 || dependencyCount > 1024 ||
            priority > static_cast<std::uint8_t>(JobPriority::Background) ||
            jobClass > static_cast<std::uint8_t>(JobClass::Blocking))
            return 0;
        JobHandle work;
        bool completionInstalled = false;
        try
        {
            std::uint64_t generation = 0;
            {
                std::scoped_lock lock(runtime->Mutex);
                if (runtime->Reload.State != ManagedReloadState::Active)
                    return 0;
                generation = runtime->Reload.Generation;
            }
            JobDescription description;
            description.Name = static_cast<std::string>(name);
            if (description.Name.empty())
                description.Name = "Managed callback";
            description.Priority = static_cast<JobPriority>(priority);
            description.Class = static_cast<JobClass>(jobClass);
            std::uint64_t id = 0;
            const auto lifetime = std::weak_ptr<Impl*>(runtime->Lifetime);
            std::scoped_lock lock(runtime->ManagedJobMutex);
            if (!runtime->ManagedJobs)
                return 0;
            if (runtime->ManagedJobRecords.size() >= 65536)
            {
                std::set<std::uint64_t> retainedDependencies;
                for (std::int32_t index = 0; index < dependencyCount; ++index)
                    retainedDependencies.emplace(dependencyIds[index]);
                std::erase_if(runtime->ManagedJobRecords,
                              [&](const auto& entry)
                              {
                                  const auto status = entry.second.Work.Status();
                                  return !retainedDependencies.contains(entry.first) &&
                                         (status == JobStatus::Succeeded || status == JobStatus::Failed ||
                                          status == JobStatus::Cancelled);
                              });
                if (runtime->ManagedJobRecords.size() >= 65536)
                    return 0;
            }
            for (std::int32_t index = 0; index < dependencyCount; ++index)
            {
                const auto found = runtime->ManagedJobRecords.find(dependencyIds[index]);
                if (found == runtime->ManagedJobRecords.end() || found->second.Generation != generation)
                    return 0;
                description.Dependencies.push_back(found->second.Work);
            }
            if (runtime->NextManagedJob == 0)
                return 0;
            id = runtime->NextManagedJob++;
            work = runtime->ManagedJobs->Submit(std::move(description),
                                                [lifetime, callback, state](JobContext& context)
                                                {
                                                    const auto locked = lifetime.lock();
                                                    if (!locked || !*locked)
                                                        throw std::runtime_error("Managed job runtime is unavailable.");
                                                    RuntimeScope scope(**locked);
                                                    std::stop_callback stop(context.StopToken(),
                                                                            [&] { (void)callback(state, 4, 0); });
                                                    if (callback(state, 0, context.StopRequested() ? 1 : 0) != 0)
                                                        throw std::runtime_error("Managed job callback failed.");
                                                });
            work.OnComplete(
                [lifetime, callback, state](const JobResult& result)
                {
                    const auto locked = lifetime.lock();
                    if (!locked || !*locked)
                        return;
                    RuntimeScope scope(**locked);
                    const auto phase = result.Status == JobStatus::Succeeded ? std::uint8_t{1}
                                       : result.Status == JobStatus::Failed  ? std::uint8_t{2}
                                                                             : std::uint8_t{3};
                    (void)callback(state, phase, 0);
                });
            completionInstalled = true;
            runtime->ManagedJobRecords.emplace(id, ManagedJobRecord{work, generation});
            return id;
        }
        catch (...)
        {
            if (work)
            {
                work.Cancel();
                (void)work.Wait();
                if (!completionInstalled)
                {
                    const auto result = work.Result();
                    const auto phase = result.Status == JobStatus::Succeeded ? std::uint8_t{1}
                                       : result.Status == JobStatus::Failed  ? std::uint8_t{2}
                                                                             : std::uint8_t{3};
                    (void)callback(state, phase, 0);
                }
            }
            return 0;
        }
    }

    void ScriptSystem::Impl::RuntimeCancelManagedJob(const std::uint64_t id) noexcept
    {
        if (!CurrentRuntime || id == 0)
            return;
        JobHandle job;
        {
            std::scoped_lock lock(CurrentRuntime->ManagedJobMutex);
            const auto found = CurrentRuntime->ManagedJobRecords.find(id);
            if (found != CurrentRuntime->ManagedJobRecords.end())
                job = found->second.Work;
        }
        job.Cancel();
    }

    void ScriptSystem::Impl::RuntimeWriteLog(const std::uint8_t level, const Coral::String message) noexcept
    {
        if (!CurrentRuntime || !CurrentRuntime->Specification.RuntimeServices)
            return;
        try
        {
            const auto clamped = std::min(level, static_cast<std::uint8_t>(ManagedLogLevel::Critical));
            CurrentRuntime->Specification.RuntimeServices->WriteManagedLog(static_cast<ManagedLogLevel>(clamped),
                                                                           static_cast<std::string>(message));
        }
        catch (...)
        {
        }
    }

    void ScriptSystem::Impl::RuntimeRegisterProfileName(const std::uint64_t id, const Coral::String name) noexcept
    {
        if (!CurrentRuntime || id == 0)
            return;
        try
        {
            std::scoped_lock lock(CurrentRuntime->Mutex);
            CurrentRuntime->ProfileNames.insert_or_assign(id, static_cast<std::string>(name));
        }
        catch (...)
        {
        }
    }

    void ScriptSystem::Impl::RuntimeRecordProfileSpan(const std::uint64_t id, const double startMicroseconds,
                                                      const double durationMicroseconds) noexcept
    {
        if (!CurrentRuntime || !CurrentRuntime->Specification.RuntimeServices)
            return;
        try
        {
            std::scoped_lock lock(CurrentRuntime->Mutex);
            const auto found = CurrentRuntime->ProfileNames.find(id);
            if (found != CurrentRuntime->ProfileNames.end())
            {
                CurrentRuntime->Specification.RuntimeServices->RecordManagedProfileSpan(
                    found->second, startMicroseconds, durationMicroseconds);
            }
        }
        catch (...)
        {
        }
    }

    void ScriptSystem::Impl::RuntimeSetProfileCounter(const std::uint64_t id, const double value) noexcept
    {
        if (!CurrentRuntime || !CurrentRuntime->Specification.RuntimeServices)
            return;
        try
        {
            std::scoped_lock lock(CurrentRuntime->Mutex);
            const auto found = CurrentRuntime->ProfileNames.find(id);
            if (found != CurrentRuntime->ProfileNames.end())
                CurrentRuntime->Specification.RuntimeServices->SetManagedProfileCounter(found->second, value);
        }
        catch (...)
        {
        }
    }

    [[nodiscard]] float ScriptSystem::Impl::RuntimeDeltaTime() noexcept
    {
        return CurrentRuntime && CurrentRuntime->Specification.RuntimeServices
                   ? CurrentRuntime->Specification.RuntimeServices->ManagedDeltaTime()
                   : 0.0F;
    }

    [[nodiscard]] float ScriptSystem::Impl::RuntimeFixedDeltaTime() noexcept
    {
        return CurrentRuntime && CurrentRuntime->Specification.RuntimeServices
                   ? CurrentRuntime->Specification.RuntimeServices->ManagedFixedDeltaTime()
                   : 1.0F / 60.0F;
    }

    [[nodiscard]] float ScriptSystem::Impl::RuntimeUnscaledDeltaTime() noexcept
    {
        return CurrentRuntime && CurrentRuntime->Specification.RuntimeServices
                   ? CurrentRuntime->Specification.RuntimeServices->ManagedUnscaledDeltaTime()
                   : 0.0F;
    }

    [[nodiscard]] double ScriptSystem::Impl::RuntimeElapsedTime() noexcept
    {
        return CurrentRuntime && CurrentRuntime->Specification.RuntimeServices
                   ? CurrentRuntime->Specification.RuntimeServices->ManagedElapsedTime()
                   : 0.0;
    }

    void ScriptSystem::Impl::RuntimeInputAxis2D(const Coral::String action, float* x, float* y) noexcept
    {
        if (!x || !y)
            return;
        *x = 0.0F;
        *y = 0.0F;
        if (!CurrentRuntime || !CurrentRuntime->Specification.RuntimeServices)
            return;
        try
        {
            const auto value =
                CurrentRuntime->Specification.RuntimeServices->ReadManagedInput(static_cast<std::string>(action));
            *x = value.X;
            *y = value.Y;
        }
        catch (...)
        {
        }
    }

    [[nodiscard]] std::uint8_t ScriptSystem::Impl::RuntimeInputState(const Coral::String action) noexcept
    {
        if (!CurrentRuntime || !CurrentRuntime->Specification.RuntimeServices)
            return 0;
        try
        {
            return static_cast<std::uint8_t>(
                CurrentRuntime->Specification.RuntimeServices->ReadManagedInputState(static_cast<std::string>(action)));
        }
        catch (...)
        {
            return 0;
        }
    }

    void ScriptSystem::Impl::RuntimeSetCursorVisible(const std::uint8_t visible) noexcept
    {
        if (CurrentRuntime && CurrentRuntime->Specification.RuntimeServices)
            CurrentRuntime->Specification.RuntimeServices->SetManagedCursorVisible(visible != 0);
    }

    void ScriptSystem::Impl::RuntimeSetCursorLocked(const std::uint8_t locked) noexcept
    {
        if (CurrentRuntime && CurrentRuntime->Specification.RuntimeServices)
            CurrentRuntime->Specification.RuntimeServices->SetManagedCursorLocked(locked != 0);
    }

    [[nodiscard]] std::uint8_t ScriptSystem::Impl::RuntimeIsCursorVisible() noexcept
    {
        return CurrentRuntime && CurrentRuntime->Specification.RuntimeServices &&
                       CurrentRuntime->Specification.RuntimeServices->IsManagedCursorVisible()
                   ? 1
                   : 0;
    }

    [[nodiscard]] std::uint8_t ScriptSystem::Impl::RuntimeIsCursorLocked() noexcept
    {
        return CurrentRuntime && CurrentRuntime->Specification.RuntimeServices &&
                       CurrentRuntime->Specification.RuntimeServices->IsManagedCursorLocked()
                   ? 1
                   : 0;
    }

    [[nodiscard]] Vector3 ScriptSystem::Impl::RuntimeGetLocalPosition(const std::uint64_t world,
                                                                      const std::uint64_t high,
                                                                      const std::uint64_t low) noexcept
    {
        const auto entity = ResolveRuntimeEntity(world, high, low);
        const auto transform = entity ? entity.GetComponent<TransformComponent>() : Ref<TransformComponent>{};
        return transform ? transform->LocalPosition() : Vector3{};
    }

    void ScriptSystem::Impl::RuntimeSetLocalPosition(const std::uint64_t world, const std::uint64_t high,
                                                     const std::uint64_t low, const Vector3 value) noexcept
    {
        const auto entity = ResolveRuntimeEntity(world, high, low);
        if (const auto transform = entity ? entity.GetComponent<TransformComponent>() : Ref<TransformComponent>{})
        {
            try
            {
                transform->SetLocalPosition(value);
            }
            catch (...)
            {
            }
        }
    }

    [[nodiscard]] Quaternion ScriptSystem::Impl::RuntimeGetLocalRotation(const std::uint64_t world,
                                                                         const std::uint64_t high,
                                                                         const std::uint64_t low) noexcept
    {
        const auto entity = ResolveRuntimeEntity(world, high, low);
        const auto transform = entity ? entity.GetComponent<TransformComponent>() : Ref<TransformComponent>{};
        return transform ? transform->LocalRotation() : Quaternion{};
    }

    void ScriptSystem::Impl::RuntimeSetLocalRotation(const std::uint64_t world, const std::uint64_t high,
                                                     const std::uint64_t low, const Quaternion value) noexcept
    {
        const auto entity = ResolveRuntimeEntity(world, high, low);
        if (const auto transform = entity ? entity.GetComponent<TransformComponent>() : Ref<TransformComponent>{})
        {
            try
            {
                transform->SetLocalRotation(value);
            }
            catch (...)
            {
            }
        }
    }

    [[nodiscard]] Quaternion ScriptSystem::Impl::RuntimeGetWorldRotation(const std::uint64_t world,
                                                                         const std::uint64_t high,
                                                                         const std::uint64_t low) noexcept
    {
        const auto entity = ResolveRuntimeEntity(world, high, low);
        const auto transform = entity ? entity.GetComponent<TransformComponent>() : Ref<TransformComponent>{};
        return transform ? transform->WorldRotation() : Quaternion{};
    }

    void ScriptSystem::Impl::RuntimeSetWorldPosition(const std::uint64_t world, const std::uint64_t high,
                                                     const std::uint64_t low, const Vector3 value) noexcept
    {
        try
        {
            const auto entity = ResolveRuntimeEntity(world, high, low);
            if (const auto transform = entity ? entity.GetComponent<TransformComponent>() : Ref<TransformComponent>{})
                transform->SetWorldPosition(value);
        }
        catch (...)
        {
        }
    }

    void ScriptSystem::Impl::RuntimeSetWorldRotation(const std::uint64_t world, const std::uint64_t high,
                                                     const std::uint64_t low, const Quaternion value) noexcept
    {
        try
        {
            const auto entity = ResolveRuntimeEntity(world, high, low);
            if (const auto transform = entity ? entity.GetComponent<TransformComponent>() : Ref<TransformComponent>{})
                transform->SetWorldRotation(value);
        }
        catch (...)
        {
        }
    }

    [[nodiscard]] Ref<CharacterControllerComponent>
    ScriptSystem::Impl::RuntimeCharacterController(const std::uint64_t world, const std::uint64_t high,
                                                   const std::uint64_t low) noexcept
    {
        const auto entity = ResolveRuntimeEntity(world, high, low);
        return entity ? entity.GetComponent<CharacterControllerComponent>() : Ref<CharacterControllerComponent>{};
    }

    [[nodiscard]] std::uint8_t ScriptSystem::Impl::RuntimeMoveCharacterController(const std::uint64_t world,
                                                                                  const std::uint64_t high,
                                                                                  const std::uint64_t low,
                                                                                  const Vector3 displacement) noexcept
    {
        try
        {
            const auto character = RuntimeCharacterController(world, high, low);
            return character && character->Enabled() && character->QueueDesiredMovement(displacement) ? 1 : 0;
        }
        catch (...)
        {
            return 0;
        }
    }

    [[nodiscard]] std::uint8_t
    ScriptSystem::Impl::RuntimeGetCharacterControllerState(const std::uint64_t world, const std::uint64_t high,
                                                           const std::uint64_t low, std::uint8_t* grounded,
                                                           Vector3* normal, Vector3* velocity) noexcept
    {
        const auto character = RuntimeCharacterController(world, high, low);
        if (!character || !grounded || !normal || !velocity)
            return 0;
        const auto state = character->RuntimeState();
        *grounded = state.Grounded ? 1 : 0;
        *normal = state.GroundNormal;
        *velocity = state.Velocity;
        return 1;
    }

    [[nodiscard]] Ref<RigidBodyComponent> ScriptSystem::Impl::RuntimeRigidBody(const std::uint64_t world,
                                                                               const std::uint64_t high,
                                                                               const std::uint64_t low) noexcept
    {
        const auto entity = ResolveRuntimeEntity(world, high, low);
        return entity ? entity.GetComponent<RigidBodyComponent>() : Ref<RigidBodyComponent>{};
    }

    [[nodiscard]] std::uint8_t ScriptSystem::Impl::RuntimeGetRigidBodyProperties(
        const std::uint64_t world, const std::uint64_t high, const std::uint64_t low, std::uint8_t* motion, float* mass,
        Vector3* velocity, std::uint8_t* continuous, std::uint8_t* gravity) noexcept
    {
        const auto body = RuntimeRigidBody(world, high, low);
        if (!body || !motion || !mass || !velocity || !continuous || !gravity)
            return 0;
        *motion = static_cast<std::uint8_t>(body->Motion());
        *mass = body->Mass();
        *velocity = body->LinearVelocity();
        *continuous = body->Continuous() ? 1 : 0;
        *gravity = body->UseGravity() ? 1 : 0;
        return 1;
    }

    [[nodiscard]] std::uint8_t ScriptSystem::Impl::RuntimeSetRigidBodyMotion(const std::uint64_t world,
                                                                             const std::uint64_t high,
                                                                             const std::uint64_t low,
                                                                             const std::uint8_t motion) noexcept
    {
        try
        {
            const auto body = RuntimeRigidBody(world, high, low);
            if (!body || motion > static_cast<std::uint8_t>(PhysicsMotionType::Kinematic))
                return 0;
            body->SetMotion(static_cast<PhysicsMotionType>(motion));
            return 1;
        }
        catch (...)
        {
            return 0;
        }
    }

    [[nodiscard]] std::uint8_t ScriptSystem::Impl::RuntimeSetRigidBodyMass(const std::uint64_t world,
                                                                           const std::uint64_t high,
                                                                           const std::uint64_t low,
                                                                           const float mass) noexcept
    {
        try
        {
            const auto body = RuntimeRigidBody(world, high, low);
            if (!body)
                return 0;
            body->SetMass(mass);
            return 1;
        }
        catch (...)
        {
            return 0;
        }
    }

    [[nodiscard]] std::uint8_t ScriptSystem::Impl::RuntimeSetRigidBodyVelocity(const std::uint64_t world,
                                                                               const std::uint64_t high,
                                                                               const std::uint64_t low,
                                                                               const Vector3 velocity) noexcept
    {
        try
        {
            const auto body = RuntimeRigidBody(world, high, low);
            if (!body)
                return 0;
            body->SetLinearVelocity(velocity);
            return 1;
        }
        catch (...)
        {
            return 0;
        }
    }

    [[nodiscard]] std::uint8_t ScriptSystem::Impl::RuntimeSetRigidBodyFlag(const std::uint64_t world,
                                                                           const std::uint64_t high,
                                                                           const std::uint64_t low,
                                                                           const std::uint8_t property,
                                                                           const std::uint8_t value) noexcept
    {
        const auto body = RuntimeRigidBody(world, high, low);
        if (!body)
            return 0;
        if (property == 0)
            body->SetContinuous(value != 0);
        else if (property == 1)
            body->SetUseGravity(value != 0);
        else
            return 0;
        return 1;
    }

    [[nodiscard]] std::uint8_t ScriptSystem::Impl::RuntimeAddRigidBodyForce(const std::uint64_t world,
                                                                            const std::uint64_t high,
                                                                            const std::uint64_t low,
                                                                            const Vector3 force,
                                                                            const std::uint8_t mode) noexcept
    {
        try
        {
            const auto body = RuntimeRigidBody(world, high, low);
            if (!body || body->Motion() != PhysicsMotionType::Dynamic || mode > 3)
                return 0;
            const auto timeStep = RuntimeFixedDeltaTime();
            auto scale = 1.0F;
            if (mode == 0)
                scale = timeStep / body->Mass();
            else if (mode == 1)
                scale = timeStep;
            else if (mode == 2)
                scale = 1.0F / body->Mass();
            const auto current = body->LinearVelocity();
            body->SetLinearVelocity(
                {current.X + force.X * scale, current.Y + force.Y * scale, current.Z + force.Z * scale});
            return 1;
        }
        catch (...)
        {
            return 0;
        }
    }
} // namespace Keire
