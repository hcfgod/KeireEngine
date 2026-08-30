#include "KeireInternal/Scripting/ScriptSystemInternal.h"

#include <algorithm>

namespace Keire
{
    ScriptSystem::Impl::Impl(ScriptSystemSpecification value, Ref<JobSystem> jobs)
        : Specification(std::move(value)), Owner(std::this_thread::get_id()), Lifetime(std::make_shared<Impl*>(this)),
          Scheduler(std::move(jobs))
    {
        if (!Scheduler)
        {
            JobSystemSpecification jobsSpecification;
            jobsSpecification.WorkerCount = 1;
            jobsSpecification.BlockingWorkerCount = 1;
            Scheduler = CreateRef<JobSystem>(jobsSpecification);
            OwnScheduler = true;
        }
        WorkScope = Scheduler->CreateScope("Managed builds");
        ManagedJobs = Scheduler->CreateScope("Managed assembly jobs");
        ComponentCallbacks = std::make_shared<Detail::ManagedBehaviourComponentCallbacks>();
        ComponentCallbacks->Create = [this](const ComponentTypeId componentType, const std::string_view managedType,
                                            const Entity& owner) -> ManagedBehaviourInstanceId
        {
            if (!Open.load(std::memory_order_acquire))
                return {};
            const auto* type = FindType(ActiveTypes, managedType);
            if (!type)
                return {};
            const auto id = NextInstance++;
            auto object = CreateObject(*type, owner.World(), owner.Id().Value());
            auto [instance, inserted] =
                Instances.emplace(id, BehaviourInstance{std::string(managedType), componentType, owner.World(),
                                                        owner.Id().Value(), std::move(object)});
            (void)inserted;
            instance->second.NativeEntity = owner;
            instance->second.CallbackMask = ReadCallbackMask(instance->second.Object);
            return ManagedBehaviourInstanceId(id);
        };
        ComponentCallbacks->Invoke = [this](const ManagedBehaviourInstanceId instance,
                                            const ManagedBehaviourCallback callback, const float deltaSeconds)
        {
            if (!Open.load(std::memory_order_acquire))
                return;
            const auto found = Instances.find(instance.Value());
            if (found != Instances.end() && found->second.Object.IsValid())
                InvokeInstance(found->first, callback, deltaSeconds);
        };
        ComponentCallbacks->AnimationEvent =
            [this](const ManagedBehaviourInstanceId instance, const AnimationEventMessage& event)
        {
            if (Open.load(std::memory_order_acquire))
                InvokeAnimationEvent(instance.Value(), event);
        };
        ComponentCallbacks->ProceduralMotionEvent =
            [this](const ManagedBehaviourInstanceId instance, const ProceduralMotionEvent& event)
        {
            if (Open.load(std::memory_order_acquire))
                InvokeProceduralMotionEvent(instance.Value(), event);
        };
        ComponentCallbacks->PhysicsContact = [this](const ManagedBehaviourInstanceId instance,
                                                    const PhysicsContactPhase phase,
                                                    const PhysicsContactMessage& contact)
        {
            if (Open.load(std::memory_order_acquire))
                InvokePhysicsContact(instance.Value(), phase, contact);
        };
        ComponentCallbacks->Destroy = [this](const ManagedBehaviourInstanceId instance)
        {
            if (!Open.load(std::memory_order_acquire))
                return;
            const auto found = Instances.find(instance.Value());
            if (found == Instances.end())
                return;
            std::exception_ptr failure;
            try
            {
                if (found->second.Object.IsValid())
                    InvokeInstance(found->first, ManagedBehaviourCallback::Destroy);
            }
            catch (...)
            {
                failure = std::current_exception();
            }
            Instances.erase(found);
            if (failure)
                std::rethrow_exception(failure);
        };
        ComponentCallbacks->CaptureState =
            [this](const ManagedBehaviourInstanceId instance) -> std::optional<std::string>
        {
            if (!Open.load(std::memory_order_acquire))
                return std::nullopt;
            const auto found = Instances.find(instance.Value());
            if (found == Instances.end() || !found->second.Object.IsValid())
                return std::nullopt;
            found->second.State = CaptureState(found->second.Object, true);
            return found->second.State;
        };
        ComponentCallbacks->RestoreState =
            [this](const ManagedBehaviourInstanceId instance, const std::string_view state)
        {
            if (!Open.load(std::memory_order_acquire))
                return;
            const auto found = Instances.find(instance.Value());
            if (found == Instances.end() || !found->second.Object.IsValid())
                return;
            found->second.State = state;
            RestoreState(found->second.Object, found->second.State, true);
        };
    }

    ScriptSystem::Impl::~Impl()
    {
        StopWorker();
        if (WorkScope)
        {
            WorkScope->Cancel();
            WorkScope->Wait();
        }
        DrainManagedJobs(false);
        if (OwnScheduler && Scheduler)
            Scheduler->Close();
        ComponentCallbacks.reset();
        *Lifetime = nullptr;
        ShutdownRuntime();
    }

    void ScriptSystem::Impl::RequireOwner() const
    {
        if (std::this_thread::get_id() != Owner)
            throw std::logic_error("ScriptSystem operation must run on the owner thread.");
    }

    [[nodiscard]] Coral::ManagedObject
    ScriptSystem::Impl::HydrateManagedAsset(const ManagedDataAsset& source,
                                            const std::map<ManagedTypeId, const Coral::Type*>& runtimeTypes)
    {
        const auto found = runtimeTypes.find(source.Definition().ManagedType);
        if (found == runtimeTypes.end() || !found->second)
        {
            throw std::runtime_error("Managed data type '" + source.Definition().ManagedTypeName +
                                     "' is unavailable in the target script generation.");
        }
        auto object = const_cast<Coral::Type*>(found->second)->CreateInstance();
        if (!object.IsValid())
            throw std::runtime_error("Managed data type '" + source.Definition().ManagedTypeName +
                                     "' could not be constructed.");
        const auto encoded = ManagedDataAsset::Encode(source.Definition());
        const std::string document(reinterpret_cast<const char*>(encoded.data()), encoded.size());
        const RuntimeScope scope(*this);
        const Coral::ScopedString scopedDocument(Coral::String::New(document));
        auto managedDocument = static_cast<Coral::String>(scopedDocument);
        object.InvokeMethod("RuntimeHydrateManagedData", managedDocument);
        return object;
    }

    void ScriptSystem::Impl::InstallManagedAssetGeneration(Coral::Type& nativeRuntime, const std::uint64_t generation)
    {
        if (generation == 0)
            throw std::invalid_argument("Managed asset generation must be non-zero.");
        if (Specification.MaximumManagedDataAssets == 0 ||
            Specification.MaximumManagedDataAssets >
                static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()) ||
            Specification.MaximumManagedDataLoads == 0 ||
            Specification.MaximumManagedDataLoads > Specification.MaximumManagedDataAssets ||
            Specification.MaximumManagedDataLoads > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()))
        {
            throw std::invalid_argument("Managed data asset capacities are invalid.");
        }
        const RuntimeScope scope(*this);
        nativeRuntime.InvokeStaticMethod("InstallManagedAssetGeneration", generation,
                                         static_cast<std::int32_t>(Specification.MaximumManagedDataAssets),
                                         static_cast<std::int32_t>(Specification.MaximumManagedDataLoads));
    }

    void ScriptSystem::Impl::ResetManagedAssetGeneration(const Coral::Type* nativeRuntime,
                                                         const std::uint64_t generation) noexcept
    {
        if (!nativeRuntime || generation == 0)
            return;
        try
        {
            const RuntimeScope scope(*this);
            (void)nativeRuntime->InvokeStaticMethod<Coral::Bool32>("ResetManagedAssets", generation);
        }
        catch (...)
        {
        }
        ReleaseManagedAssetGenerationServices(generation);
    }

    void ScriptSystem::Impl::ReleaseManagedAssetGenerationServices(const std::uint64_t generation) noexcept
    {
        if (Specification.RuntimeServices)
        {
            Specification.RuntimeServices->ReleaseManagedRuntimeAssets(generation);
            Specification.RuntimeServices->ReleaseManagedInputContexts(generation);
        }
    }

    void ScriptSystem::Impl::ResumeGenerationSequence() { NextOperation = Detail::NextManagedGeneration(OutputRoot); }

    [[nodiscard]] std::filesystem::path ScriptSystem::Impl::FindManagedApiProject() const
    {
        auto root = ProjectRoot;
        for (std::size_t depth = 0; depth < 8 && !root.empty(); ++depth)
        {
            const auto candidate = root / "KeireManaged" / "Keire.Managed.csproj";
            if (std::filesystem::is_regular_file(candidate))
                return candidate;
            const auto parent = root.parent_path();
            if (parent == root)
                break;
            root = parent;
        }
        return {};
    }

    void ScriptSystem::Impl::StopWorker() noexcept
    {
        BuildCancellation.request_stop();
        if (Worker)
        {
            (void)Worker.Wait();
            Worker = {};
        }
    }

    void ScriptSystem::Impl::InitializeRuntime()
    {
        if (Specification.RuntimeHostDirectory.empty())
            return;
        const auto directory = std::filesystem::absolute(Specification.RuntimeHostDirectory).lexically_normal();
        if (!std::filesystem::is_directory(directory))
            throw std::invalid_argument("The managed runtime host directory does not exist.");
        auto settings = Detail::CreateCoralHostSettings(PathText(directory));
        if (!Specification.RuntimeRootDirectory.empty())
        {
            const auto runtimeRoot = std::filesystem::absolute(Specification.RuntimeRootDirectory).lexically_normal();
            if (!std::filesystem::is_directory(runtimeRoot))
                throw std::invalid_argument("The bundled .NET runtime root directory does not exist.");
            settings.DotnetRoot = PathText(runtimeRoot);
        }
        settings.ExceptionCallback = [this](const std::string_view message)
        {
            std::scoped_lock lock(Mutex);
            RuntimeException.assign(message);
        };
        const auto status = RuntimeHost.Initialize(std::move(settings));
        if (status != Coral::CoralInitStatus::Success)
            throw std::runtime_error("Coral could not initialize the bundled .NET 10 runtime host (status " +
                                     std::to_string(static_cast<int>(status)) + ").");
        RuntimeInitialized = true;
    }

    void ScriptSystem::Impl::Unload(std::unique_ptr<Coral::AssemblyLoadContext>& context) noexcept
    {
        if (!context || !RuntimeInitialized)
            return;
        try
        {
            RuntimeHost.UnloadAssemblyLoadContext(*context);
        }
        catch (...)
        {
        }
        context.reset();
    }

    void ScriptSystem::Impl::ShutdownRuntime() noexcept
    {
        DrainManagedJobs(false);
        ResetManagedAssetGeneration(CandidateNativeRuntimeType, Reload.Generation + 1);
        ResetManagedAssetGeneration(ActiveNativeRuntimeType, Reload.Generation);
        {
            std::scoped_lock lock(ManagedAssetMutex);
            PendingManagedAssetLoads.clear();
            ManagedAssetSources.clear();
            ManagedAssetRuntimeDiagnostics.clear();
        }
        Instances.clear();
        ActiveTypes.clear();
        CandidateTypes.clear();
        ActiveManagedAssetCatalog.reset();
        CandidateManagedAssetCatalog.reset();
        ActiveManagedAssetRuntimeTypes.clear();
        CandidateManagedAssetRuntimeTypes.clear();
        ActiveNativeRuntimeType = nullptr;
        CandidateNativeRuntimeType = nullptr;
        Unload(CandidateContext);
        Unload(ActiveContext);
        if (RuntimeInitialized)
        {
            try
            {
                RuntimeHost.Shutdown();
            }
            catch (...)
            {
            }
            RuntimeInitialized = false;
        }
    }

    void ScriptSystem::Impl::DrainManagedJobs(const bool recreate) noexcept
    {
        Ref<JobScope> jobs;
        {
            std::scoped_lock lock(ManagedJobMutex);
            jobs = std::move(ManagedJobs);
        }
        if (jobs)
        {
            jobs->Cancel();
            jobs->Wait();
        }
        {
            std::scoped_lock lock(ManagedJobMutex);
            ManagedJobRecords.clear();
        }
        if (recreate && Scheduler && Scheduler->IsOpen())
        {
            try
            {
                auto replacement = Scheduler->CreateScope("Managed assembly jobs");
                std::scoped_lock lock(ManagedJobMutex);
                ManagedJobs = std::move(replacement);
            }
            catch (...)
            {
            }
        }
    }

    void ScriptSystem::Impl::ClearRuntimeException()
    {
        std::scoped_lock lock(Mutex);
        RuntimeException.clear();
    }

    void ScriptSystem::Impl::ThrowRuntimeException()
    {
        std::string exception;
        {
            std::scoped_lock lock(Mutex);
            exception = std::exchange(RuntimeException, {});
        }
        if (!exception.empty())
            throw std::runtime_error(exception);
    }

    void ScriptSystem::Impl::Invoke(Coral::ManagedObject& object, const std::string_view method)
    {
        ++ManagedInteropCalls;
        const RuntimeScope scope(*this);
        ClearRuntimeException();
        object.InvokeMethod(method);
        ThrowRuntimeException();
    }

    void ScriptSystem::Impl::Invoke(Coral::ManagedObject& object, const std::string_view method, const float value)
    {
        ++ManagedInteropCalls;
        const RuntimeScope scope(*this);
        ClearRuntimeException();
        object.InvokeMethod(method, value);
        ThrowRuntimeException();
    }

    [[nodiscard]] std::uint32_t ScriptSystem::Impl::ReadCallbackMask(Coral::ManagedObject& object)
    {
        const RuntimeScope scope(*this);
        ClearRuntimeException();
        const auto result = object.InvokeMethod<std::uint32_t>("RuntimeGetCallbackMask");
        ThrowRuntimeException();
        return result;
    }

    [[nodiscard]] std::string ScriptSystem::Impl::CaptureState(Coral::ManagedObject& object, const bool persistent)
    {
        Invoke(object, persistent ? "RuntimeCapturePersistentState" : "RuntimeCaptureReloadState");
        return object.GetFieldValue<std::string>("RuntimeSerializedState");
    }

    void ScriptSystem::Impl::RestoreState(Coral::ManagedObject& object, const std::string& state, const bool persistent)
    {
        object.SetFieldValue<std::string>("RuntimeSerializedState", state);
        Invoke(object, persistent ? "RuntimeRestorePersistentState" : "RuntimeRestoreReloadState");
    }

    [[nodiscard]] Coral::ManagedObject ScriptSystem::Impl::CreateObject(const BehaviourType& type,
                                                                        const std::uint64_t world, const AssetId entity)
    {
        const RuntimeScope scope(*this);
        ClearRuntimeException();
        auto object = type.Type->CreateInstance();
        ThrowRuntimeException();
        if (!object.IsValid())
            throw std::runtime_error("Managed Behaviour construction returned an invalid object.");
        ClearRuntimeException();
        object.InvokeMethod("RuntimeAttach", world, entity.High(), entity.Low());
        ThrowRuntimeException();
        return object;
    }

    [[nodiscard]] const ScriptSystem::Impl::BehaviourType*
    ScriptSystem::Impl::FindType(const std::vector<BehaviourType>& types, const std::string_view name) const
    {
        const auto found = std::ranges::find(types, name, &BehaviourType::Name);
        return found == types.end() ? nullptr : std::addressof(*found);
    }

    [[nodiscard]] const ScriptSystem::Impl::BehaviourType*
    ScriptSystem::Impl::FindType(const std::vector<BehaviourType>& types, const ComponentTypeId componentType) const
    {
        const auto found = std::ranges::find(types, componentType, &BehaviourType::ComponentType);
        return found == types.end() ? nullptr : std::addressof(*found);
    }

    void ScriptSystem::Impl::InvokeInstance(const std::uint64_t id, const ManagedBehaviourCallback callback,
                                            const float deltaSeconds)
    {
        const auto found = Instances.find(id);
        if (found == Instances.end() || !found->second.Object.IsValid())
            return;
        auto& instance = found->second;
        if (instance.Faulted && callback != ManagedBehaviourCallback::Disable &&
            callback != ManagedBehaviourCallback::Destroy)
            return;
        auto& callbackProfile = instance.CallbackProfiles[static_cast<std::size_t>(callback)];
        if ((callback == ManagedBehaviourCallback::FixedUpdate && (instance.CallbackMask & FixedUpdateCallback) == 0) ||
            (callback == ManagedBehaviourCallback::LateUpdate && (instance.CallbackMask & LateUpdateCallback) == 0) ||
            (callback == ManagedBehaviourCallback::AnimatorIk && (instance.CallbackMask & AnimatorIkCallback) == 0))
        {
            ++SkippedCallbacks;
            ++callbackProfile.SkippedInvocations;
            return;
        }
        const auto callbackStarted = std::chrono::steady_clock::now();
        ++CallbackInvocations;
        ++callbackProfile.Invocations;
        try
        {
            switch (callback)
            {
            case ManagedBehaviourCallback::Awake:
                Invoke(instance.Object, "RuntimeAwake");
                break;
            case ManagedBehaviourCallback::Enable:
                Invoke(instance.Object, "RuntimeEnable");
                instance.Enabled = true;
                break;
            case ManagedBehaviourCallback::Start:
                Invoke(instance.Object, "RuntimeStart");
                break;
            case ManagedBehaviourCallback::FixedUpdate:
                Invoke(instance.Object, "RuntimeFixedUpdate", deltaSeconds);
                break;
            case ManagedBehaviourCallback::Update:
                Invoke(instance.Object, "RuntimeUpdate", deltaSeconds);
                break;
            case ManagedBehaviourCallback::LateUpdate:
                Invoke(instance.Object, "RuntimeLateUpdate");
                break;
            case ManagedBehaviourCallback::AnimationEvent:
                throw std::logic_error("Animation events require an event payload.");
            case ManagedBehaviourCallback::PhysicsContact:
                throw std::logic_error("Physics contacts require a contact payload.");
            case ManagedBehaviourCallback::Disable:
                Invoke(instance.Object, "RuntimeDisable");
                instance.Enabled = false;
                break;
            case ManagedBehaviourCallback::Destroy:
                Invoke(instance.Object, "RuntimeDestroy");
                break;
            case ManagedBehaviourCallback::BeforeReload:
                Invoke(instance.Object, "RuntimeBeforeReload");
                break;
            case ManagedBehaviourCallback::AfterReload:
                Invoke(instance.Object, "RuntimeAfterReload");
                break;
            case ManagedBehaviourCallback::AnimatorIk:
                Invoke(instance.Object, "RuntimeAnimatorIk", deltaSeconds);
                break;
            case ManagedBehaviourCallback::ProceduralMotionEvent:
                throw std::logic_error("Procedural motion events require an event payload.");
            }
        }
        catch (const std::exception& error)
        {
            const auto elapsed =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - callbackStarted).count();
            CallbackMilliseconds += elapsed;
            MaximumCallbackMilliseconds = std::max(MaximumCallbackMilliseconds, elapsed);
            callbackProfile.Milliseconds += elapsed;
            callbackProfile.MaximumMilliseconds = std::max(callbackProfile.MaximumMilliseconds, elapsed);
            RuntimeDiagnostics.push_back({ManagedBehaviourInstanceId(id), ManagedDiagnosticSeverity::Error, callback,
                                          Reload.Generation, instance.TypeName, instance.Entity, error.what()});
            if (Specification.ExceptionPolicy == ManagedExceptionPolicy::Propagate)
                throw;
            instance.Faulted = true;
            instance.Enabled = false;
            return;
        }
        const auto elapsed =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - callbackStarted).count();
        CallbackMilliseconds += elapsed;
        MaximumCallbackMilliseconds = std::max(MaximumCallbackMilliseconds, elapsed);
        callbackProfile.Milliseconds += elapsed;
        callbackProfile.MaximumMilliseconds = std::max(callbackProfile.MaximumMilliseconds, elapsed);
    }

    void ScriptSystem::Impl::InvokeAnimationEvent(const std::uint64_t id, const AnimationEventMessage& event)
    {
        const auto found = Instances.find(id);
        if (found == Instances.end() || !found->second.Object.IsValid() || found->second.Faulted)
            return;
        auto& instance = found->second;
        constexpr auto callback = ManagedBehaviourCallback::AnimationEvent;
        auto& callbackProfile = instance.CallbackProfiles[static_cast<std::size_t>(callback)];
        if ((instance.CallbackMask & AnimationEventCallback) == 0)
        {
            ++SkippedCallbacks;
            ++callbackProfile.SkippedInvocations;
            return;
        }
        const auto callbackStarted = std::chrono::steady_clock::now();
        ++CallbackInvocations;
        ++callbackProfile.Invocations;
        try
        {
            ++ManagedInteropCalls;
            const RuntimeScope scope(*this);
            ClearRuntimeException();
            const Coral::ScopedString scopedName(Coral::String::New(event.Name));
            const Coral::ScopedString scopedText(Coral::String::New(event.Text));
            auto managedName = static_cast<Coral::String>(scopedName);
            auto managedText = static_cast<Coral::String>(scopedText);
            instance.Object.InvokeMethod("RuntimeAnimationEvent", managedName, event.NormalizedTime, event.Integer,
                                         event.Scalar, managedText);
            ThrowRuntimeException();
        }
        catch (const std::exception& error)
        {
            const auto elapsed =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - callbackStarted).count();
            CallbackMilliseconds += elapsed;
            MaximumCallbackMilliseconds = std::max(MaximumCallbackMilliseconds, elapsed);
            callbackProfile.Milliseconds += elapsed;
            callbackProfile.MaximumMilliseconds = std::max(callbackProfile.MaximumMilliseconds, elapsed);
            RuntimeDiagnostics.push_back({ManagedBehaviourInstanceId(id), ManagedDiagnosticSeverity::Error, callback,
                                          Reload.Generation, instance.TypeName, instance.Entity, error.what()});
            if (Specification.ExceptionPolicy == ManagedExceptionPolicy::Propagate)
                throw;
            instance.Faulted = true;
            instance.Enabled = false;
            return;
        }
        const auto elapsed =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - callbackStarted).count();
        CallbackMilliseconds += elapsed;
        MaximumCallbackMilliseconds = std::max(MaximumCallbackMilliseconds, elapsed);
        callbackProfile.Milliseconds += elapsed;
        callbackProfile.MaximumMilliseconds = std::max(callbackProfile.MaximumMilliseconds, elapsed);
    }

    void ScriptSystem::Impl::InvokeProceduralMotionEvent(const std::uint64_t id, const ProceduralMotionEvent& event)
    {
        const auto found = Instances.find(id);
        if (found == Instances.end() || !found->second.Object.IsValid() || found->second.Faulted)
            return;
        auto& instance = found->second;
        constexpr auto callback = ManagedBehaviourCallback::ProceduralMotionEvent;
        auto& callbackProfile = instance.CallbackProfiles[static_cast<std::size_t>(callback)];
        if ((instance.CallbackMask & ProceduralMotionEventCallback) == 0)
        {
            ++SkippedCallbacks;
            ++callbackProfile.SkippedInvocations;
            return;
        }
        const auto callbackStarted = std::chrono::steady_clock::now();
        ++CallbackInvocations;
        ++callbackProfile.Invocations;
        try
        {
            ++ManagedInteropCalls;
            const RuntimeScope scope(*this);
            ClearRuntimeException();
            instance.Object.InvokeMethod("RuntimeProceduralMotionEvent", static_cast<std::uint8_t>(event.Type),
                                         static_cast<std::uint8_t>(event.Foot), static_cast<std::uint8_t>(event.State),
                                         event.Phase, event.Intensity, event.ContactPosition, event.ContactNormal,
                                         event.Support.Value().High(), event.Support.Value().Low(),
                                         event.PhysicsMaterial.High(), event.PhysicsMaterial.Low());
            ThrowRuntimeException();
        }
        catch (const std::exception& error)
        {
            const auto elapsed =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - callbackStarted).count();
            CallbackMilliseconds += elapsed;
            MaximumCallbackMilliseconds = std::max(MaximumCallbackMilliseconds, elapsed);
            callbackProfile.Milliseconds += elapsed;
            callbackProfile.MaximumMilliseconds = std::max(callbackProfile.MaximumMilliseconds, elapsed);
            RuntimeDiagnostics.push_back({ManagedBehaviourInstanceId(id), ManagedDiagnosticSeverity::Error, callback,
                                          Reload.Generation, instance.TypeName, instance.Entity, error.what()});
            if (Specification.ExceptionPolicy == ManagedExceptionPolicy::Propagate)
                throw;
            instance.Faulted = true;
            instance.Enabled = false;
            return;
        }
        const auto elapsed =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - callbackStarted).count();
        CallbackMilliseconds += elapsed;
        MaximumCallbackMilliseconds = std::max(MaximumCallbackMilliseconds, elapsed);
        callbackProfile.Milliseconds += elapsed;
        callbackProfile.MaximumMilliseconds = std::max(callbackProfile.MaximumMilliseconds, elapsed);
    }

    void ScriptSystem::Impl::InvokePhysicsContact(const std::uint64_t id, const PhysicsContactPhase phase,
                                                  const PhysicsContactMessage& contact)
    {
        const auto found = Instances.find(id);
        if (found == Instances.end() || !found->second.Object.IsValid() || found->second.Faulted)
            return;
        auto& instance = found->second;
        constexpr auto callback = ManagedBehaviourCallback::PhysicsContact;
        auto& callbackProfile = instance.CallbackProfiles[static_cast<std::size_t>(callback)];
        const auto callbackStarted = std::chrono::steady_clock::now();
        ++CallbackInvocations;
        ++callbackProfile.Invocations;
        try
        {
            ++ManagedInteropCalls;
            const RuntimeScope scope(*this);
            ClearRuntimeException();
            instance.Object.InvokeMethod("RuntimePhysicsContact", static_cast<std::uint8_t>(phase),
                                         contact.Trigger ? std::uint8_t{1} : std::uint8_t{0},
                                         contact.Other.Value().High(), contact.Other.Value().Low(), contact.Point,
                                         contact.Normal, contact.Impulse);
            ThrowRuntimeException();
        }
        catch (const std::exception& error)
        {
            const auto elapsed =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - callbackStarted).count();
            CallbackMilliseconds += elapsed;
            MaximumCallbackMilliseconds = std::max(MaximumCallbackMilliseconds, elapsed);
            callbackProfile.Milliseconds += elapsed;
            callbackProfile.MaximumMilliseconds = std::max(callbackProfile.MaximumMilliseconds, elapsed);
            RuntimeDiagnostics.push_back({ManagedBehaviourInstanceId(id), ManagedDiagnosticSeverity::Error, callback,
                                          Reload.Generation, instance.TypeName, instance.Entity, error.what()});
            if (Specification.ExceptionPolicy == ManagedExceptionPolicy::Propagate)
                throw;
            instance.Faulted = true;
            instance.Enabled = false;
            return;
        }
        const auto elapsed =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - callbackStarted).count();
        CallbackMilliseconds += elapsed;
        MaximumCallbackMilliseconds = std::max(MaximumCallbackMilliseconds, elapsed);
        callbackProfile.Milliseconds += elapsed;
        callbackProfile.MaximumMilliseconds = std::max(callbackProfile.MaximumMilliseconds, elapsed);
    }
} // namespace Keire
