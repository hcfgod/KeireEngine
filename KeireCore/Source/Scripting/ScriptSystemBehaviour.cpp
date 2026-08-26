#include "KeireInternal/Scripting/ScriptSystemInternal.h"

namespace Keire
{
    ManagedBehaviourInstanceId ScriptSystem::CreateBehaviour(std::string typeName, const std::uint64_t world,
                                                             const AssetId entity)
    {
        m_Impl->RequireOwner();
        if (!IsOpen() || !m_Impl->ActiveContext)
            throw std::logic_error("The managed runtime session is not active.");
        const auto* type = m_Impl->FindType(m_Impl->ActiveTypes, typeName);
        if (!type)
            throw std::invalid_argument("The requested managed Behaviour type is unavailable.");
        const auto id = m_Impl->NextInstance++;
        auto object = m_Impl->CreateObject(*type, world, entity);
        const auto [instance, inserted] = m_Impl->Instances.emplace(
            id, Impl::BehaviourInstance{std::move(typeName), type->ComponentType, world, entity, std::move(object)});
        (void)inserted;
        instance->second.CallbackMask = m_Impl->ReadCallbackMask(instance->second.Object);
        return ManagedBehaviourInstanceId(id);
    }

    void ScriptSystem::InvokeBehaviour(const ManagedBehaviourInstanceId instance,
                                       const ManagedBehaviourCallback callback, const float deltaSeconds)
    {
        m_Impl->RequireOwner();
        if (!IsOpen())
            throw std::logic_error("ScriptSystem is closed.");
        if (!instance)
            throw std::invalid_argument("Managed Behaviour instance ID is invalid.");
        const auto found = m_Impl->Instances.find(instance.Value());
        if (found == m_Impl->Instances.end())
            throw std::invalid_argument("Managed Behaviour instance is unavailable.");
        m_Impl->InvokeInstance(found->first, callback, deltaSeconds);
    }

    bool ScriptSystem::DestroyBehaviour(const ManagedBehaviourInstanceId instance)
    {
        m_Impl->RequireOwner();
        if (!IsOpen())
            return false;
        const auto found = m_Impl->Instances.find(instance.Value());
        if (found == m_Impl->Instances.end())
            return false;
        std::exception_ptr failure;
        try
        {
            if (found->second.Object.IsValid())
                m_Impl->InvokeInstance(found->first, ManagedBehaviourCallback::Destroy);
        }
        catch (...)
        {
            failure = std::current_exception();
        }
        m_Impl->Instances.erase(found);
        if (failure)
            std::rethrow_exception(failure);
        return true;
    }

    std::vector<ManagedRuntimeDiagnostic> ScriptSystem::RuntimeDiagnostics() const
    {
        m_Impl->RequireOwner();
        return m_Impl->RuntimeDiagnostics;
    }

    ManagedRuntimeMetrics ScriptSystem::Metrics() const
    {
        m_Impl->RequireOwner();
        ManagedRuntimeMetrics result;
        result.Generation = m_Impl->Reload.Generation;
        result.ActiveInstances = m_Impl->Instances.size();
        result.Diagnostics = m_Impl->RuntimeDiagnostics.size();
        result.FaultedInstances = static_cast<std::size_t>(
            std::ranges::count_if(m_Impl->Instances, [](const auto& entry) { return entry.second.Faulted; }));
        result.CallbackInvocations = m_Impl->CallbackInvocations;
        result.SkippedCallbacks = m_Impl->SkippedCallbacks;
        result.ManagedInteropCalls = m_Impl->ManagedInteropCalls;
        result.CallbackMilliseconds = m_Impl->CallbackMilliseconds;
        result.MaximumCallbackMilliseconds = m_Impl->MaximumCallbackMilliseconds;
        return result;
    }

    ManagedCallbackMetrics ScriptSystem::CallbackMetrics() const
    {
        m_Impl->RequireOwner();
        constexpr std::size_t maximumEntries = 64;
        ManagedCallbackMetrics result;
        result.Entries.reserve(std::min(maximumEntries, m_Impl->Instances.size() * 3));
        for (const auto& [id, instance] : m_Impl->Instances)
        {
            (void)id;
            for (std::size_t callbackIndex = 0; callbackIndex < Detail::ManagedCallbackProfileCount; ++callbackIndex)
            {
                const auto& source = instance.CallbackProfiles[callbackIndex];
                if (source.Invocations == 0 && source.SkippedInvocations == 0)
                    continue;
                const auto callback = static_cast<ManagedBehaviourCallback>(callbackIndex);
                const auto found =
                    std::ranges::find_if(result.Entries, [&](const ManagedCallbackMetric& entry)
                                         { return entry.TypeName == instance.TypeName && entry.Callback == callback; });
                ManagedCallbackMetric* destination = nullptr;
                if (found == result.Entries.end())
                {
                    if (result.Entries.size() == maximumEntries)
                    {
                        result.Truncated = true;
                        continue;
                    }
                    result.Entries.push_back({instance.TypeName, callback, 0, 0, 0, 0.0, 0.0});
                    destination = std::addressof(result.Entries.back());
                }
                else
                {
                    destination = std::addressof(*found);
                }
                ++destination->InstanceCount;
                destination->Invocations += source.Invocations;
                destination->SkippedInvocations += source.SkippedInvocations;
                destination->Milliseconds += source.Milliseconds;
                destination->MaximumMilliseconds =
                    std::max(destination->MaximumMilliseconds, source.MaximumMilliseconds);
            }
        }
        std::ranges::sort(result.Entries,
                          [](const ManagedCallbackMetric& left, const ManagedCallbackMetric& right)
                          {
                              if (left.Milliseconds != right.Milliseconds)
                                  return left.Milliseconds > right.Milliseconds;
                              if (left.TypeName != right.TypeName)
                                  return left.TypeName < right.TypeName;
                              return left.Callback < right.Callback;
                          });
        return result;
    }

    bool ScriptSystem::RetryBehaviour(const ManagedBehaviourInstanceId instance)
    {
        m_Impl->RequireOwner();
        const auto found = m_Impl->Instances.find(instance.Value());
        if (found == m_Impl->Instances.end())
            return false;
        found->second.Faulted = false;
        return true;
    }

    bool ScriptSystem::SetBehaviourEnabled(const ManagedBehaviourInstanceId instance, const bool enabled)
    {
        m_Impl->RequireOwner();
        const auto found = m_Impl->Instances.find(instance.Value());
        if (found == m_Impl->Instances.end())
            return false;
        if (enabled)
            found->second.Faulted = false;
        m_Impl->InvokeInstance(found->first,
                               enabled ? ManagedBehaviourCallback::Enable : ManagedBehaviourCallback::Disable);
        return true;
    }

    std::vector<ManagedBehaviourCheckpoint> ScriptSystem::CaptureReplayCheckpoint()
    {
        m_Impl->RequireOwner();
        if (!IsOpen())
            throw std::logic_error("ScriptSystem is closed.");
        std::vector<ManagedBehaviourCheckpoint> result;
        result.reserve(m_Impl->Instances.size());
        for (auto& [id, instance] : m_Impl->Instances)
        {
            (void)id;
            if (!instance.Object.IsValid())
                continue;
            instance.State = m_Impl->CaptureState(instance.Object, true);
            result.push_back({instance.TypeName, instance.ComponentType, instance.World, instance.Entity,
                              instance.State, instance.Enabled, instance.Faulted});
        }
        std::ranges::sort(result,
                          [](const ManagedBehaviourCheckpoint& left, const ManagedBehaviourCheckpoint& right)
                          {
                              return std::tie(left.World, left.Entity, left.ComponentType, left.TypeName) <
                                     std::tie(right.World, right.Entity, right.ComponentType, right.TypeName);
                          });
        return result;
    }

    void ScriptSystem::RestoreReplayCheckpoint(const std::span<const ManagedBehaviourCheckpoint> checkpoint)
    {
        m_Impl->RequireOwner();
        if (!IsOpen())
            throw std::logic_error("ScriptSystem is closed.");
        using Key = std::tuple<std::uint64_t, AssetId, ComponentTypeId>;
        std::map<Key, std::uint64_t> instances;
        for (const auto& [id, instance] : m_Impl->Instances)
        {
            if (instance.Object.IsValid() &&
                !instances.emplace(Key{instance.World, instance.Entity, instance.ComponentType}, id).second)
            {
                throw std::runtime_error("Managed replay state contains duplicate runtime behaviour identities.");
            }
        }
        std::set<Key> checkpointKeys;
        for (const auto& state : checkpoint)
        {
            const Key key{state.World, state.Entity, state.ComponentType};
            const auto found = instances.find(key);
            if (state.World == 0 || !state.Entity || !state.ComponentType || state.TypeName.empty() ||
                !checkpointKeys.insert(key).second || found == instances.end() ||
                m_Impl->Instances.at(found->second).TypeName != state.TypeName)
            {
                throw std::runtime_error("Managed replay checkpoint is incompatible with the runtime behaviours.");
            }
        }
        if (checkpointKeys.size() != instances.size())
            throw std::runtime_error("Managed replay checkpoint does not contain every runtime behaviour.");

        struct RollbackState final
        {
            std::uint64_t Instance = 0;
            std::string State;
            bool Enabled = true;
            bool Faulted = false;
        };
        std::vector<RollbackState> rollback;
        rollback.reserve(checkpoint.size());
        try
        {
            for (const auto& state : checkpoint)
            {
                const auto id = instances.at(Key{state.World, state.Entity, state.ComponentType});
                auto& instance = m_Impl->Instances.at(id);
                rollback.push_back(
                    {id, m_Impl->CaptureState(instance.Object, true), instance.Enabled, instance.Faulted});
                m_Impl->RestoreState(instance.Object, state.State, true);
                instance.State = state.State;
                instance.Enabled = state.Enabled;
                instance.Faulted = state.Faulted;
            }
        }
        catch (...)
        {
            const auto original = std::current_exception();
            for (auto iterator = rollback.rbegin(); iterator != rollback.rend(); ++iterator)
            {
                try
                {
                    auto& instance = m_Impl->Instances.at(iterator->Instance);
                    m_Impl->RestoreState(instance.Object, iterator->State, true);
                    instance.State = iterator->State;
                    instance.Enabled = iterator->Enabled;
                    instance.Faulted = iterator->Faulted;
                }
                catch (...)
                {
                }
            }
            std::rethrow_exception(original);
        }
    }

    void ScriptSystem::InstallManagedComponents(const Ref<ComponentRegistry>& registry)
    {
        m_Impl->RequireOwner();
        if (!IsOpen() || !m_Impl->ActiveContext)
            throw std::logic_error("The managed runtime session is not active.");
        if (!registry)
            throw std::invalid_argument("Managed component installation requires a component registry.");

        std::vector<ComponentRegistration> registrations;
        registrations.reserve(m_Impl->ActiveTypes.size());
        for (const auto& type : m_Impl->ActiveTypes)
        {
            if (!type.ComponentType)
                continue;
            ComponentRegistration registration;
            registration.Type = type.ComponentType;
            const auto separator = type.Name.find_last_of('.');
            registration.Name = separator == std::string::npos ? type.Name : type.Name.substr(separator + 1);
            registration.Category = "Scripts";
            registration.ExecutionOrder = type.ExecutionOrder;
            registration.RequiredComponents = type.RequiredComponents;
            registration.Properties = type.Properties;
            registration.Methods = std::make_shared<const std::vector<ComponentMethod>>(type.Methods);
            const auto componentType = type.ComponentType;
            const auto managedType = type.Name;
            const auto properties = type.Properties;
            const std::weak_ptr<Detail::ManagedBehaviourComponentCallbacks> callbacks = m_Impl->ComponentCallbacks;
            registration.Factory = [componentType, managedType, callbacks]
            {
                return Ref<Component>(
                    CreateRef<Detail::ManagedBehaviourComponent>(componentType, managedType, callbacks));
            };
            registration.Serialize = [properties](const Component& component)
            {
                const auto& managed = dynamic_cast<const Detail::ManagedBehaviourComponent&>(component);
                return ProjectManagedState(managed.SerializedState(), properties);
            };
            registration.Deserialize =
                [properties](Component& component, const ComponentPropertyBag& values, const std::uint32_t version)
            {
                if (version != 1)
                    throw std::invalid_argument("Unsupported managed component state version.");
                auto& managed = dynamic_cast<Detail::ManagedBehaviourComponent&>(component);
                const auto found = values.find("managedState");
                if (found == values.end())
                    return;
                const auto* state = std::get_if<std::string>(&found->second);
                if (!state)
                    throw std::invalid_argument("Managed component state blob is not text.");
                managed.SetSerializedState(ApplyManagedState(*state, values, properties));
            };
            registrations.push_back(std::move(registration));
            if (std::ranges::find(m_Impl->InstalledComponentTypes, componentType) ==
                m_Impl->InstalledComponentTypes.end())
            {
                m_Impl->InstalledComponentTypes.push_back(componentType);
            }
        }
        registry->ReplaceBatch(m_Impl->InstalledComponentTypes, std::move(registrations));
    }

    void ScriptSystem::SetRuntimeServices(IScriptRuntimeServices* services)
    {
        m_Impl->RequireOwner();
        if (!services && m_Impl->Specification.RuntimeServices)
        {
            // Runtime layers unbind their native world immediately after this call. Finish managed asset lifecycle
            // callbacks while those services are still valid, then invalidate the generation entry points.
            m_Impl->DrainManagedJobs(false);
            m_Impl->ResetManagedAssetGeneration(m_Impl->CandidateNativeRuntimeType, m_Impl->Reload.Generation + 1);
            m_Impl->ResetManagedAssetGeneration(m_Impl->ActiveNativeRuntimeType, m_Impl->Reload.Generation);
            m_Impl->CandidateNativeRuntimeType = nullptr;
            m_Impl->ActiveNativeRuntimeType = nullptr;
        }
        m_Impl->Specification.RuntimeServices = services;
    }

    void ScriptSystem::Close()
    {
        m_Impl->RequireOwner();
        if (!m_Impl->Open.exchange(false, std::memory_order_acq_rel))
            return;
        m_Impl->ComponentCallbacks.reset();
        m_Impl->StopWorker();
        if (m_Impl->WorkScope)
        {
            m_Impl->WorkScope->Cancel();
            m_Impl->WorkScope->Wait();
        }
        if (m_Impl->OwnScheduler && m_Impl->Scheduler)
            m_Impl->Scheduler->Close();
        m_Impl->ShutdownRuntime();
    }
} // namespace Keire
