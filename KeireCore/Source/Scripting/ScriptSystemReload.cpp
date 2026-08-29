#include "KeireInternal/Scripting/ManagedAssemblySnapshot.h"
#include "KeireInternal/Scripting/ScriptSystemInternal.h"

#include <algorithm>

namespace Keire
{
    bool ScriptSystem::PrepareReload(ManagedReloadRequest request)
    {
        m_Impl->RequireOwner();
        if (!IsOpen())
            throw std::logic_error("ScriptSystem is closed.");
        if (!m_Impl->RuntimeInitialized)
            throw std::logic_error("The managed runtime host is unavailable.");
        if (request.Assemblies.empty())
            throw std::invalid_argument("Managed reload requires at least one assembly.");

        m_Impl->ResetManagedAssetGeneration(
            m_Impl->CandidateNativeRuntimeType,
            m_Impl->Reload.Generation == std::numeric_limits<std::uint64_t>::max() ? 0 : m_Impl->Reload.Generation + 1);
        m_Impl->CandidateManagedAssetRuntimeTypes.clear();
        m_Impl->CandidateNativeRuntimeType = nullptr;
        m_Impl->Unload(m_Impl->CandidateContext);
        {
            std::scoped_lock lock(m_Impl->Mutex);
            m_Impl->Reload.State = ManagedReloadState::Preparing;
            m_Impl->Reload.Diagnostic.clear();
            m_Impl->Reload.RetainedState = std::move(request.State);
            m_Impl->RuntimeException.clear();
        }

        try
        {
            std::set<std::string, std::less<>> dependencyDirectories;
            const auto addDependencyDirectory = [&](std::filesystem::path path)
            {
                if (path.empty())
                    return;
                if (path.is_relative())
                    path = m_Impl->ProjectRoot / path;
                path = std::filesystem::absolute(path).lexically_normal();
                if (path.has_filename())
                    path = path.parent_path();
                if (!path.empty())
                    dependencyDirectories.insert(PathText(path));
            };
            for (const auto& assembly : request.Assemblies)
                addDependencyDirectory(assembly);
            addDependencyDirectory(request.ManagedApiAssembly.empty() ? m_Impl->ManagedApi
                                                                      : request.ManagedApiAssembly);
            std::string dependencyPathList;
#if defined(_WIN32)
            constexpr char dependencyPathSeparator = ';';
#else
            constexpr char dependencyPathSeparator = ':';
#endif
            for (const auto& directory : dependencyDirectories)
            {
                if (!dependencyPathList.empty())
                    dependencyPathList.push_back(dependencyPathSeparator);
                dependencyPathList.append(directory);
            }
            auto candidate = m_Impl->RuntimeHost.CreateAssemblyLoadContext(
                "Keire.Reload." + std::to_string(m_Impl->NextReload++), dependencyPathList);
            m_Impl->CandidateContext = std::make_unique<Coral::AssemblyLoadContext>(candidate);
            Coral::Type* behaviourType = nullptr;
            Coral::Type* stableComponentIdType = nullptr;
            Coral::Type* executionOrderType = nullptr;
            Coral::Type* requireComponentType = nullptr;
            ManagedInspectorAttributeTypes inspectorAttributeTypes;
            Coral::Type* managedAssetMetadataType = nullptr;
            Coral::Type* nativeRuntimeType = nullptr;
            std::map<std::string, const Coral::Type*, std::less<>> managedRuntimeTypesByName;
            auto managedApiPath =
                request.ManagedApiAssembly.empty() ? m_Impl->ManagedApi : std::move(request.ManagedApiAssembly);
            if (!managedApiPath.empty())
            {
                if (managedApiPath.is_relative())
                    managedApiPath = m_Impl->ProjectRoot / managedApiPath;
                managedApiPath = std::filesystem::absolute(managedApiPath).lexically_normal();
                const auto managedApiSnapshot = Detail::CaptureManagedAssemblySnapshot(managedApiPath);
                auto& managedApi = Detail::LoadManagedAssemblySnapshot(*m_Impl->CandidateContext, managedApiSnapshot);
                if (managedApi.GetLoadStatus() != Coral::AssemblyLoadStatus::Success)
                {
                    std::string managedException;
                    {
                        std::scoped_lock lock(m_Impl->Mutex);
                        managedException = m_Impl->RuntimeException;
                    }
                    throw std::runtime_error(Detail::ManagedAssemblyLoadFailure(
                        "Keire.Managed", managedApiSnapshot, managedApi.GetLoadStatus(), managedException));
                }
                managedApi.AddInternalCall("Keire.NativeRuntime", "WriteLogIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeWriteLog));
                managedApi.AddInternalCall("Keire.NativeRuntime", "RegisterProfileNameIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeRegisterProfileName));
                managedApi.AddInternalCall("Keire.NativeRuntime", "RecordProfileSpanIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeRecordProfileSpan));
                managedApi.AddInternalCall("Keire.NativeRuntime", "SetProfileCounterIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeSetProfileCounter));
                managedApi.AddInternalCall("Keire.NativeRuntime", "RequestManagedAssetLoadIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeRequestManagedAssetLoad));
                managedApi.AddInternalCall("Keire.NativeRuntime", "CancelManagedAssetLoadIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeCancelManagedAssetLoad));
                managedApi.AddInternalCall("Keire.NativeRuntime", "ReleaseManagedAssetIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeReleaseManagedAsset));
                managedApi.AddInternalCall("Keire.NativeRuntime", "SubmitManagedJobIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeSubmitManagedJob));
                managedApi.AddInternalCall("Keire.NativeRuntime", "CancelManagedJobIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeCancelManagedJob));
                managedApi.AddInternalCall("Keire.NativeRuntime", "DeltaTimeIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeDeltaTime));
                managedApi.AddInternalCall("Keire.NativeRuntime", "FixedDeltaTimeIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeFixedDeltaTime));
                managedApi.AddInternalCall("Keire.NativeRuntime", "UnscaledDeltaTimeIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeUnscaledDeltaTime));
                managedApi.AddInternalCall("Keire.NativeRuntime", "ElapsedTimeIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeElapsedTime));
                managedApi.AddInternalCall("Keire.NativeRuntime", "InputAxis2DIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeInputAxis2D));
                managedApi.AddInternalCall("Keire.NativeRuntime", "InputStateIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeInputState));
                managedApi.AddInternalCall("Keire.NativeRuntime", "SetCursorVisibleIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeSetCursorVisible));
                managedApi.AddInternalCall("Keire.NativeRuntime", "SetCursorLockedIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeSetCursorLocked));
                managedApi.AddInternalCall("Keire.NativeRuntime", "IsCursorVisibleIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeIsCursorVisible));
                managedApi.AddInternalCall("Keire.NativeRuntime", "IsCursorLockedIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeIsCursorLocked));
                managedApi.AddInternalCall("Keire.NativeRuntime", "GetLocalPositionIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeGetLocalPosition));
                managedApi.AddInternalCall("Keire.NativeRuntime", "SetLocalPositionIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeSetLocalPosition));
                managedApi.AddInternalCall("Keire.NativeRuntime", "GetLocalRotationIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeGetLocalRotation));
                managedApi.AddInternalCall("Keire.NativeRuntime", "SetLocalRotationIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeSetLocalRotation));
                managedApi.AddInternalCall("Keire.NativeRuntime", "MoveCharacterControllerIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeMoveCharacterController));
                managedApi.AddInternalCall("Keire.NativeRuntime", "GetCharacterControllerStateIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeGetCharacterControllerState));
                managedApi.AddInternalCall("Keire.NativeRuntime", "GetRigidBodyPropertiesIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeGetRigidBodyProperties));
                managedApi.AddInternalCall("Keire.NativeRuntime", "SetRigidBodyMotionIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeSetRigidBodyMotion));
                managedApi.AddInternalCall("Keire.NativeRuntime", "SetRigidBodyMassIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeSetRigidBodyMass));
                managedApi.AddInternalCall("Keire.NativeRuntime", "SetRigidBodyVelocityIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeSetRigidBodyVelocity));
                managedApi.AddInternalCall("Keire.NativeRuntime", "SetRigidBodyFlagIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeSetRigidBodyFlag));
                managedApi.AddInternalCall("Keire.NativeRuntime", "AddRigidBodyForceIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeAddRigidBodyForce));
                managedApi.AddInternalCall("Keire.NativeRuntime", "SetAnimatorFloatIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeSetAnimatorFloat));
                managedApi.AddInternalCall("Keire.NativeRuntime", "SetAnimatorIntegerIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeSetAnimatorInteger));
                managedApi.AddInternalCall("Keire.NativeRuntime", "SetAnimatorBooleanIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeSetAnimatorBoolean));
                managedApi.AddInternalCall("Keire.NativeRuntime", "SetAnimatorTriggerIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeSetAnimatorTrigger));
                managedApi.AddInternalCall("Keire.NativeRuntime", "SetAnimatorLayerWeightIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeSetAnimatorLayerWeight));
                managedApi.AddInternalCall("Keire.NativeRuntime", "PlayAnimatorIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimePlayAnimator));
                managedApi.AddInternalCall("Keire.NativeRuntime", "CrossFadeAnimatorIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeCrossFadeAnimator));
                managedApi.AddInternalCall("Keire.NativeRuntime", "PauseAnimatorIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimePauseAnimator));
                managedApi.AddInternalCall("Keire.NativeRuntime", "StopAnimatorIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeStopAnimator));
                managedApi.AddInternalCall("Keire.NativeRuntime", "SetAnimatorSpeedIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeSetAnimatorSpeed));
                managedApi.AddInternalCall("Keire.NativeRuntime", "SetAnimatorFootGroundingWeightIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeSetAnimatorFootGroundingWeight));
                managedApi.AddInternalCall("Keire.NativeRuntime", "SetProceduralLocomotionIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeSetProceduralLocomotion));
                managedApi.AddInternalCall("Keire.NativeRuntime", "GetProceduralLocomotionStateIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeGetProceduralLocomotionState));
                managedApi.AddInternalCall("Keire.NativeRuntime", "GetAnimatorStateIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeGetAnimatorState));
                managedApi.AddInternalCall("Keire.NativeRuntime", "GetAnimatorStateNameIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeGetAnimatorStateName));
                managedApi.AddInternalCall("Keire.NativeRuntime", "SetAnimatorTwoBoneIkIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeSetAnimatorTwoBoneIk));
                managedApi.AddInternalCall("Keire.NativeRuntime", "SetAnimatorFabrikIkIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeSetAnimatorFabrikIk));
                managedApi.AddInternalCall("Keire.NativeRuntime", "ClearAnimatorIkIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeClearAnimatorIk));
                managedApi.AddInternalCall("Keire.NativeRuntime", "TryGetAnimatorFloatIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeTryGetAnimatorFloat));
                managedApi.AddInternalCall("Keire.NativeRuntime", "TryGetAnimatorIntegerIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeTryGetAnimatorInteger));
                managedApi.AddInternalCall("Keire.NativeRuntime", "TryGetAnimatorBooleanIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeTryGetAnimatorBoolean));
                managedApi.AddInternalCall("Keire.NativeRuntime", "TryGetAnimatorLayerWeightIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeTryGetAnimatorLayerWeight));
                managedApi.AddInternalCall("Keire.NativeRuntime", "EntityExistsIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeEntityExists));
                managedApi.AddInternalCall("Keire.NativeRuntime", "GetEntityActiveIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeGetEntityActive));
                managedApi.AddInternalCall("Keire.NativeRuntime", "GetEntityActiveInHierarchyIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeGetEntityActiveInHierarchy));
                managedApi.AddInternalCall("Keire.NativeRuntime", "SetEntityActiveIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeSetEntityActive));
                managedApi.AddInternalCall("Keire.NativeRuntime", "GetEntityLayerIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeGetEntityLayer));
                managedApi.AddInternalCall("Keire.NativeRuntime", "SetEntityLayerIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeSetEntityLayer));
                managedApi.AddInternalCall("Keire.NativeRuntime", "GetEntityNameIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeGetEntityName));
                managedApi.AddInternalCall("Keire.NativeRuntime", "SetEntityNameIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeSetEntityName));
                managedApi.AddInternalCall("Keire.NativeRuntime", "GetEntityParentIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeGetEntityParent));
                managedApi.AddInternalCall("Keire.NativeRuntime", "SetEntityParentIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeSetEntityParent));
                managedApi.AddInternalCall("Keire.NativeRuntime", "GetEntityChildCountIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeGetEntityChildCount));
                managedApi.AddInternalCall("Keire.NativeRuntime", "GetEntityChildIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeGetEntityChild));
                managedApi.AddInternalCall("Keire.NativeRuntime", "ComponentExistsIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeComponentExists));
                managedApi.AddInternalCall("Keire.NativeRuntime", "AddComponentIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeAddComponent));
                managedApi.AddInternalCall("Keire.NativeRuntime", "RemoveComponentIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeRemoveComponent));
                managedApi.AddInternalCall("Keire.NativeRuntime", "GetComponentEnabledIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeGetComponentEnabled));
                managedApi.AddInternalCall("Keire.NativeRuntime", "GetBuiltinComponentPropertyIcall",
                                           reinterpret_cast<void*>(&Detail::GetManagedBuiltinComponentPropertyIcall));
                managedApi.AddInternalCall("Keire.NativeRuntime", "SetBuiltinComponentPropertyIcall",
                                           reinterpret_cast<void*>(&Detail::SetManagedBuiltinComponentPropertyIcall));
                managedApi.AddInternalCall("Keire.NativeRuntime", "GetBuiltinComponentTextIcall",
                                           reinterpret_cast<void*>(&Detail::GetManagedBuiltinComponentTextIcall));
                managedApi.AddInternalCall("Keire.NativeRuntime", "SetBuiltinComponentTextIcall",
                                           reinterpret_cast<void*>(&Detail::SetManagedBuiltinComponentTextIcall));
                managedApi.AddInternalCall("Keire.NativeRuntime", "SetComponentEnabledIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeSetComponentEnabled));
                managedApi.AddInternalCall("Keire.NativeRuntime", "GetLocalScaleIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeGetLocalScale));
                managedApi.AddInternalCall("Keire.NativeRuntime", "SetLocalScaleIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeSetLocalScale));
                managedApi.AddInternalCall("Keire.NativeRuntime", "GetWorldPositionIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeGetWorldPosition));
                managedApi.AddInternalCall("Keire.NativeRuntime", "GetPresentationWorldPositionIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeGetPresentationWorldPosition));
                managedApi.AddInternalCall("Keire.NativeRuntime", "GetPresentationWorldRotationIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeGetPresentationWorldRotation));
                managedApi.AddInternalCall("Keire.NativeRuntime", "ResetPresentationInterpolationIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeResetPresentationInterpolation));
                managedApi.AddInternalCall("Keire.NativeRuntime", "GetWorldRotationIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeGetWorldRotation));
                managedApi.AddInternalCall("Keire.NativeRuntime", "SetWorldPositionIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeSetWorldPosition));
                managedApi.AddInternalCall("Keire.NativeRuntime", "SetWorldRotationIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeSetWorldRotation));
                managedApi.AddInternalCall("Keire.NativeRuntime", "CloneEntityIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeCloneEntity));
                managedApi.AddInternalCall("Keire.NativeRuntime", "DestroyEntityIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeDestroyEntity));
                managedApi.AddInternalCall("Keire.NativeRuntime", "RaycastIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeRaycast));
                managedApi.AddInternalCall("Keire.NativeRuntime", "PlayAudioIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimePlayAudio));
                managedApi.AddInternalCall("Keire.NativeRuntime", "PlayAudioAdvancedIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimePlayAudioAdvanced));
                managedApi.AddInternalCall("Keire.NativeRuntime", "StopAudioIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeStopAudio));
                managedApi.AddInternalCall("Keire.NativeRuntime", "PlayAudioSourceIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimePlayAudioSource));
                managedApi.AddInternalCall("Keire.NativeRuntime", "PauseAudioIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimePauseAudio));
                managedApi.AddInternalCall("Keire.NativeRuntime", "SeekAudioIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeSeekAudio));
                managedApi.AddInternalCall("Keire.NativeRuntime", "GetAudioSourcePropertiesIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeGetAudioSourceProperties));
                managedApi.AddInternalCall("Keire.NativeRuntime", "SetAudioSourceClipIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeSetAudioSourceClip));
                managedApi.AddInternalCall("Keire.NativeRuntime", "SetAudioSourceRoutingIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeSetAudioSourceRouting));
                managedApi.AddInternalCall("Keire.NativeRuntime", "SetAudioSourceScalarIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeSetAudioSourceScalar));
                managedApi.AddInternalCall("Keire.NativeRuntime", "SetAudioSourceFlagIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeSetAudioSourceFlag));
                managedApi.AddInternalCall("Keire.NativeRuntime", "GetAudioListenerPropertiesIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeGetAudioListenerProperties));
                managedApi.AddInternalCall("Keire.NativeRuntime", "SetAudioListenerPropertiesIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeSetAudioListenerProperties));
                managedApi.AddInternalCall("Keire.NativeRuntime", "GetAudioReverbZonePropertiesIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeGetAudioReverbZoneProperties));
                managedApi.AddInternalCall("Keire.NativeRuntime", "SetAudioReverbZonePropertiesIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeSetAudioReverbZoneProperties));
                managedApi.AddInternalCall("Keire.NativeRuntime", "PlayVfxIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimePlayVfx));
                managedApi.AddInternalCall("Keire.NativeRuntime", "StopVfxIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeStopVfx));
                managedApi.AddInternalCall("Keire.NativeRuntime", "PauseVfxIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimePauseVfx));
                managedApi.AddInternalCall("Keire.NativeRuntime", "IsVfxAliveIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeIsVfxAlive));
                managedApi.AddInternalCall("Keire.NativeRuntime", "SendVfxEventIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeSendVfxEvent));
                managedApi.AddInternalCall("Keire.NativeRuntime", "SetVfxScalarRangeIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeSetVfxScalarRange));
                managedApi.AddInternalCall("Keire.NativeRuntime", "SetVfxIntegerRangeIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeSetVfxIntegerRange));
                managedApi.AddInternalCall("Keire.NativeRuntime", "SetVfxUnsignedIntegerRangeIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeSetVfxUnsignedIntegerRange));
                managedApi.AddInternalCall("Keire.NativeRuntime", "SetVfxVector2RangeIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeSetVfxVector2Range));
                managedApi.AddInternalCall("Keire.NativeRuntime", "SetVfxVector3RangeIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeSetVfxVector3Range));
                managedApi.AddInternalCall("Keire.NativeRuntime", "SetVfxVector4RangeIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeSetVfxVector4Range));
                managedApi.AddInternalCall("Keire.NativeRuntime", "SetVfxColorRangeIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeSetVfxColorRange));
                managedApi.AddInternalCall("Keire.NativeRuntime", "SetUiTextIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeSetUiText));
                managedApi.AddInternalCall("Keire.NativeRuntime", "ConsumeUiClickIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeConsumeUiClick));
                Detail::RegisterManagedRuntimeBindings(managedApi);
                managedApi.UploadInternalCalls();
                behaviourType = &managedApi.GetLocalType("Keire.Behaviour");
                if (!*behaviourType)
                    throw std::runtime_error("Keire.Managed does not expose Keire.Behaviour.");
                const auto hasMethod = [behaviourType](const std::string_view expected)
                {
                    return std::ranges::any_of(behaviourType->GetMethods(),
                                               [expected](const Coral::MethodInfo& method)
                                               {
                                                   const Coral::ScopedString name(method.GetName());
                                                   return static_cast<std::string>(name) == expected;
                                               });
                };
                const auto hasField = [behaviourType](const std::string_view expected)
                {
                    return std::ranges::any_of(behaviourType->GetFields(),
                                               [expected](const Coral::FieldInfo& field)
                                               {
                                                   const Coral::ScopedString name(field.GetName());
                                                   return static_cast<std::string>(name) == expected;
                                               });
                };
                if (!hasField("RuntimeSerializedState") || !hasMethod("RuntimeCapturePersistentState") ||
                    !hasMethod("RuntimeRestorePersistentState") || !hasMethod("RuntimeCaptureReloadState") ||
                    !hasMethod("RuntimeRestoreReloadState"))
                {
                    throw std::runtime_error(
                        "Keire.Managed is stale and does not provide the managed state runtime contract. Rebuild the "
                        "KeireManaged project or regenerate the native workspace.");
                }
                stableComponentIdType = &managedApi.GetLocalType("Keire.StableComponentIdAttribute");
                executionOrderType = &managedApi.GetLocalType("Keire.ExecutionOrderAttribute");
                requireComponentType = &managedApi.GetLocalType("Keire.RequireComponentAttribute");
                inspectorAttributeTypes = ResolveManagedInspectorAttributeTypes(managedApi);
                managedAssetMetadataType = &managedApi.GetLocalType("Keire.ManagedAssetMetadata");
                nativeRuntimeType = &managedApi.GetLocalType("Keire.NativeRuntime");
                if (!*stableComponentIdType || !*executionOrderType || !*requireComponentType)
                    throw std::runtime_error("Keire.Managed does not expose managed component metadata.");
                if (!*managedAssetMetadataType)
                    throw std::runtime_error("Keire.Managed does not expose managed asset metadata.");
                if (!*nativeRuntimeType)
                    throw std::runtime_error("Keire.Managed does not expose its managed asset runtime.");
                for (const auto& type : managedApi.GetLocalTypes())
                    if (type)
                        managedRuntimeTypesByName.emplace(ManagedTypeName(const_cast<Coral::Type&>(type)),
                                                          std::addressof(type));
            }
            std::vector<std::string> availableTypes;
            std::vector<Impl::BehaviourType> candidateTypes;
            for (auto path : request.Assemblies)
            {
                if (path.is_relative())
                    path = m_Impl->ProjectRoot / path;
                path = std::filesystem::absolute(path).lexically_normal();
                if (!std::filesystem::is_regular_file(path))
                    throw std::runtime_error("Managed reload assembly does not exist: " + PathText(path));
                const auto snapshot = Detail::CaptureManagedAssemblySnapshot(path);
                const auto& assembly = Detail::LoadManagedAssemblySnapshot(*m_Impl->CandidateContext, snapshot);
                if (assembly.GetLoadStatus() != Coral::AssemblyLoadStatus::Success)
                {
                    std::string managedException;
                    {
                        std::scoped_lock lock(m_Impl->Mutex);
                        managedException = m_Impl->RuntimeException;
                    }
                    throw std::runtime_error(Detail::ManagedAssemblyLoadFailure(
                        "assembly '" + PathText(path) + "'", snapshot, assembly.GetLoadStatus(), managedException));
                }
                for (const auto& type : assembly.GetLocalTypes())
                    if (type)
                        managedRuntimeTypesByName.emplace(ManagedTypeName(const_cast<Coral::Type&>(type)),
                                                          std::addressof(type));
                if (behaviourType)
                {
                    for (const auto& type : assembly.GetLocalTypes())
                    {
                        if (!type || !type.IsSubclassOf(*behaviourType))
                            continue;
                        const Coral::ScopedString name(type.GetFullName());
                        const auto typeName = static_cast<std::string>(name);
                        availableTypes.push_back(typeName);

                        ComponentTypeId componentType;
                        std::int32_t executionOrder = 0;
                        std::vector<ComponentTypeId> requiredComponents;
                        for (auto attribute : type.GetAttributes())
                        {
                            if (attribute.GetType() == *stableComponentIdType)
                            {
                                componentType = ComponentTypeId(AssetId(attribute.GetFieldValue<std::uint64_t>("High"),
                                                                        attribute.GetFieldValue<std::uint64_t>("Low")));
                            }
                            else if (attribute.GetType() == *executionOrderType)
                            {
                                executionOrder = attribute.GetFieldValue<std::int32_t>("Order");
                            }
                            else if (attribute.GetType() == *requireComponentType)
                            {
                                requiredComponents.emplace_back(AssetId(attribute.GetFieldValue<std::uint64_t>("High"),
                                                                        attribute.GetFieldValue<std::uint64_t>("Low")));
                            }
                        }
                        if (componentType)
                        {
                            std::ranges::sort(requiredComponents);
                            if (std::ranges::adjacent_find(requiredComponents) != requiredComponents.end())
                                throw std::runtime_error("Managed Behaviour declares a duplicate required component.");
                            if (std::ranges::find(requiredComponents, componentType) != requiredComponents.end())
                                throw std::runtime_error("Managed Behaviour cannot require itself.");
                            Impl::BehaviourType behaviour;
                            behaviour.Name = typeName;
                            behaviour.ComponentType = componentType;
                            behaviour.ExecutionOrder = executionOrder;
                            behaviour.Type = std::addressof(type);
                            behaviour.Properties =
                                ReflectManagedProperties(type, *behaviourType, inspectorAttributeTypes);
                            behaviour.Methods = ReflectManagedMethods(type);
                            behaviour.RequiredComponents = std::move(requiredComponents);
                            candidateTypes.push_back(std::move(behaviour));
                        }
                    }
                }
            }
            Detail::PopulateManagedBehaviourReferenceCompatibility(candidateTypes, managedRuntimeTypesByName);
            if (!managedAssetMetadataType)
                throw std::runtime_error("Managed asset discovery requires Keire.Managed metadata.");
            const auto exportedManagedAssetMetadata =
                managedAssetMetadataType->InvokeStaticMethod<Coral::String>("Export");
            if (!exportedManagedAssetMetadata.Data())
            {
                throw std::runtime_error(
                    "Managed asset metadata export returned no document. A managed metadata exception occurred; "
                    "review the managed reload diagnostics and validate the exact Behaviour or ScriptableObject "
                    "field contract.");
            }
            const Coral::ScopedString managedAssetMetadata(exportedManagedAssetMetadata);
            auto discoveredManagedAssets = ParseManagedAssetMetadata(static_cast<std::string>(managedAssetMetadata));
            for (const auto& diagnostic : discoveredManagedAssets.Diagnostics)
            {
                if (std::ranges::find(candidateTypes, diagnostic.TypeName, &Impl::BehaviourType::Name) !=
                    candidateTypes.end())
                {
                    throw std::runtime_error(diagnostic.Message);
                }
            }
            for (auto& graphMetadata : discoveredManagedAssets.Behaviours)
            {
                const auto behaviour =
                    std::ranges::find(candidateTypes, graphMetadata.FullName, &Impl::BehaviourType::Name);
                if (behaviour == candidateTypes.end())
                    continue;
                for (auto& graph : graphMetadata.Fields)
                {
                    const auto nestedPrefix = graph.Root.Name + ".";
                    if (std::ranges::any_of(
                            behaviour->Properties, [&](const ComponentProperty& property)
                            { return property.Key == graph.Root.Name && !property.Key.starts_with(nestedPrefix); }))
                    {
                        throw std::runtime_error("Managed Behaviour graph field metadata duplicates property '" +
                                                 graph.Root.Name + "'.");
                    }
                    std::erase_if(behaviour->Properties, [&](const ComponentProperty& property)
                                  { return property.Key.starts_with(nestedPrefix); });
                    ComponentProperty property;
                    property.Key = graph.Root.Name;
                    property.DisplayName = graph.Root.DisplayName;
                    property.Kind = ComponentPropertyKind::ManagedReferenceGraph;
                    property.ReadOnly = graph.Root.ReadOnly;
                    property.Tooltip = graph.Root.Tooltip;
                    property.Header = graph.Root.Header;
                    property.DeclaredManagedType = graph.Root.ManagedTypeName;
                    property.ReferenceGraph = std::make_shared<ManagedReferenceGraphDescriptor>(std::move(graph));
                    behaviour->Properties.push_back(std::move(property));
                }
            }
            std::map<ManagedTypeId, const Coral::Type*> discoveredManagedRuntimeTypes;
            for (const auto& descriptor : discoveredManagedAssets.Types)
            {
                const auto found = managedRuntimeTypesByName.find(descriptor.FullName);
                if (found == managedRuntimeTypesByName.end())
                    throw std::runtime_error("Managed asset metadata references an unavailable runtime type.");
                discoveredManagedRuntimeTypes.emplace(descriptor.StableTypeId, found->second);
            }
            std::uint64_t candidateGeneration = 0;
            {
                std::scoped_lock lock(m_Impl->Mutex);
                if (m_Impl->Reload.Generation == std::numeric_limits<std::uint64_t>::max())
                    throw std::overflow_error("Managed reload generation is exhausted.");
                candidateGeneration = m_Impl->Reload.Generation + 1;
            }
            m_Impl->CandidateNativeRuntimeType = nativeRuntimeType;
            m_Impl->InstallManagedAssetGeneration(*nativeRuntimeType, candidateGeneration);
            std::ranges::sort(availableTypes);
            if (std::adjacent_find(availableTypes.begin(), availableTypes.end()) != availableTypes.end())
                throw std::runtime_error("Managed reload contains duplicate Behaviour type names.");
            std::ranges::sort(candidateTypes, {}, &Impl::BehaviourType::ComponentType);
            if (std::ranges::adjacent_find(candidateTypes, {}, &Impl::BehaviourType::ComponentType) !=
                candidateTypes.end())
            {
                throw std::runtime_error("Managed reload contains duplicate stable component IDs.");
            }
            std::ranges::sort(candidateTypes, {}, &Impl::BehaviourType::Name);
            std::string runtimeException;
            {
                std::scoped_lock lock(m_Impl->Mutex);
                runtimeException = m_Impl->RuntimeException;
                if (runtimeException.empty())
                {
                    m_Impl->CandidateTypes = std::move(candidateTypes);
                    m_Impl->CandidateManagedAssetTypes = std::move(discoveredManagedAssets.Types);
                    m_Impl->CandidateManagedAssetDiagnostics = std::move(discoveredManagedAssets.Diagnostics);
                    m_Impl->CandidateManagedAssetRuntimeTypes = std::move(discoveredManagedRuntimeTypes);
                    m_Impl->CandidateNativeRuntimeType = nativeRuntimeType;
                    m_Impl->Reload.AvailableTypes = std::move(availableTypes);
                    m_Impl->Reload.State = ManagedReloadState::Prepared;
                }
            }
            if (!runtimeException.empty())
                throw std::runtime_error(runtimeException);
            return true;
        }
        catch (const std::exception& error)
        {
            m_Impl->ResetManagedAssetGeneration(m_Impl->CandidateNativeRuntimeType,
                                                m_Impl->Reload.Generation == std::numeric_limits<std::uint64_t>::max()
                                                    ? 0
                                                    : m_Impl->Reload.Generation + 1);
            m_Impl->CandidateTypes.clear();
            m_Impl->CandidateManagedAssetTypes.clear();
            m_Impl->CandidateManagedAssetDiagnostics.clear();
            m_Impl->CandidateManagedAssetRuntimeTypes.clear();
            m_Impl->CandidateNativeRuntimeType = nullptr;
            m_Impl->Unload(m_Impl->CandidateContext);
            std::scoped_lock lock(m_Impl->Mutex);
            m_Impl->Reload.State = ManagedReloadState::Failed;
            m_Impl->Reload.Diagnostic = error.what();
            return false;
        }
    }

    void ScriptSystem::CommitReload()
    {
        m_Impl->RequireOwner();
        if (!IsOpen())
            throw std::logic_error("ScriptSystem is closed.");
        {
            std::scoped_lock lock(m_Impl->Mutex);
            if (m_Impl->Reload.State != ManagedReloadState::Prepared || !m_Impl->CandidateContext)
                throw std::logic_error("No prepared managed reload is available.");
        }

        std::uint64_t candidateGeneration = 0;
        {
            std::scoped_lock lock(m_Impl->Mutex);
            candidateGeneration = m_Impl->Reload.Generation + 1;
        }
        try
        {
            std::vector<std::pair<AssetId, AssetHandle<ManagedDataAsset>>> sources;
            {
                std::scoped_lock lock(m_Impl->ManagedAssetMutex);
                sources.reserve(m_Impl->ManagedAssetSources.size());
                for (const auto& [id, source] : m_Impl->ManagedAssetSources)
                    sources.emplace_back(id, source.Handle);
            }
            for (const auto& [id, handle] : sources)
            {
                const auto asset = handle.TryGetLoaded();
                if (!asset)
                    throw std::runtime_error("A loaded managed data source became unavailable during script reload.");
                auto object = m_Impl->HydrateManagedAsset(*asset, m_Impl->CandidateManagedAssetRuntimeTypes);
                const Impl::RuntimeScope scope(*m_Impl);
                if (object.InvokeMethod<Coral::Bool32>("RuntimeRegisterManagedAsset", candidateGeneration, id.High(),
                                                       id.Low()) == 0)
                    throw std::runtime_error("The candidate managed asset registry rejected a hydrated object.");
            }
        }
        catch (...)
        {
            m_Impl->ResetManagedAssetGeneration(m_Impl->CandidateNativeRuntimeType, candidateGeneration);
            m_Impl->CandidateTypes.clear();
            m_Impl->CandidateManagedAssetTypes.clear();
            m_Impl->CandidateManagedAssetDiagnostics.clear();
            m_Impl->CandidateManagedAssetRuntimeTypes.clear();
            m_Impl->CandidateNativeRuntimeType = nullptr;
            m_Impl->Unload(m_Impl->CandidateContext);
            std::scoped_lock lock(m_Impl->Mutex);
            m_Impl->Reload.State = ManagedReloadState::Failed;
            m_Impl->Reload.Diagnostic =
                "Managed data hydration failed; the last-good script and asset generation remains active.";
            throw;
        }

        std::unordered_map<std::uint64_t, Impl::BehaviourInstance> migrated;
        std::unordered_map<std::uint64_t, std::string> rollback;
        migrated.reserve(m_Impl->Instances.size());
        try
        {
            for (auto& [id, instance] : m_Impl->Instances)
            {
                if (instance.Object.IsValid())
                {
                    rollback.emplace(id, m_Impl->CaptureState(instance.Object, false));
                    m_Impl->Invoke(instance.Object, "RuntimeBeforeReload");
                    instance.State = m_Impl->CaptureState(instance.Object, false);
                }
                Impl::BehaviourInstance replacement{instance.TypeName,
                                                    instance.ComponentType,
                                                    instance.World,
                                                    instance.Entity,
                                                    {},
                                                    instance.State,
                                                    {},
                                                    instance.Enabled,
                                                    false};
                replacement.NativeEntity = instance.NativeEntity;
                auto* type = m_Impl->FindType(m_Impl->CandidateTypes, instance.ComponentType);
                if (!type)
                    type = m_Impl->FindType(m_Impl->CandidateTypes, instance.TypeName);
                if (type)
                {
                    replacement.TypeName = type->Name;
                    replacement.ComponentType = type->ComponentType;
                    replacement.Object = m_Impl->CreateObject(*type, instance.World, instance.Entity);
                    replacement.CallbackMask = m_Impl->ReadCallbackMask(replacement.Object);
                    m_Impl->RestoreState(replacement.Object, replacement.State, false);
                }
                migrated.emplace(id, std::move(replacement));
            }
        }
        catch (...)
        {
            const auto original = std::current_exception();
            for (auto& [id, state] : rollback)
            {
                const auto found = m_Impl->Instances.find(id);
                if (found != m_Impl->Instances.end() && found->second.Object.IsValid())
                {
                    try
                    {
                        m_Impl->RestoreState(found->second.Object, state, false);
                        m_Impl->Invoke(found->second.Object, "RuntimeResumeAfterFailedReload");
                    }
                    catch (...)
                    {
                    }
                }
            }
            m_Impl->CandidateTypes.clear();
            m_Impl->CandidateManagedAssetTypes.clear();
            m_Impl->CandidateManagedAssetDiagnostics.clear();
            m_Impl->ResetManagedAssetGeneration(m_Impl->CandidateNativeRuntimeType, candidateGeneration);
            m_Impl->CandidateManagedAssetRuntimeTypes.clear();
            m_Impl->CandidateNativeRuntimeType = nullptr;
            m_Impl->Unload(m_Impl->CandidateContext);
            std::scoped_lock lock(m_Impl->Mutex);
            m_Impl->Reload.State = ManagedReloadState::Failed;
            m_Impl->Reload.Diagnostic = "Managed reload migration failed; the last-good generation remains active.";
            std::rethrow_exception(original);
        }

        // Reverse-P/Invoke callbacks must finish while the old assembly load context is still alive.
        m_Impl->DrainManagedJobs(true);
        auto previous = std::move(m_Impl->ActiveContext);
        const auto* previousNativeRuntime = m_Impl->ActiveNativeRuntimeType;
        const auto previousGeneration = m_Impl->Reload.Generation;
        m_Impl->Instances = std::move(migrated);
        m_Impl->ActiveContext = std::move(m_Impl->CandidateContext);
        m_Impl->ActiveTypes = std::move(m_Impl->CandidateTypes);
        m_Impl->ActiveManagedAssetTypes = std::move(m_Impl->CandidateManagedAssetTypes);
        m_Impl->ActiveManagedAssetDiagnostics = std::move(m_Impl->CandidateManagedAssetDiagnostics);
        m_Impl->ActiveManagedAssetRuntimeTypes = std::move(m_Impl->CandidateManagedAssetRuntimeTypes);
        m_Impl->ActiveNativeRuntimeType = m_Impl->CandidateNativeRuntimeType;
        m_Impl->CandidateNativeRuntimeType = nullptr;
        {
            std::scoped_lock lock(m_Impl->ManagedAssetMutex);
            std::erase_if(m_Impl->PendingManagedAssetLoads, [previousGeneration](const auto& entry)
                          { return entry.second.Generation == previousGeneration; });
        }
        m_Impl->ResetManagedAssetGeneration(previousNativeRuntime, previousGeneration);
        m_Impl->Unload(previous);
        {
            std::scoped_lock lock(m_Impl->Mutex);
            ++m_Impl->Reload.Generation;
            m_Impl->Reload.State = ManagedReloadState::Active;
            m_Impl->Reload.Diagnostic.clear();
        }
        for (const auto& [id, instance] : m_Impl->Instances)
        {
            if (instance.Object.IsValid())
                m_Impl->InvokeInstance(id, ManagedBehaviourCallback::AfterReload);
        }
    }

    void ScriptSystem::CancelReload()
    {
        m_Impl->RequireOwner();
        m_Impl->ResetManagedAssetGeneration(
            m_Impl->CandidateNativeRuntimeType,
            m_Impl->Reload.Generation == std::numeric_limits<std::uint64_t>::max() ? 0 : m_Impl->Reload.Generation + 1);
        m_Impl->CandidateTypes.clear();
        m_Impl->CandidateManagedAssetTypes.clear();
        m_Impl->CandidateManagedAssetDiagnostics.clear();
        m_Impl->CandidateManagedAssetRuntimeTypes.clear();
        m_Impl->CandidateNativeRuntimeType = nullptr;
        m_Impl->Unload(m_Impl->CandidateContext);
        std::scoped_lock lock(m_Impl->Mutex);
        if (m_Impl->Reload.State == ManagedReloadState::Preparing ||
            m_Impl->Reload.State == ManagedReloadState::Prepared)
            m_Impl->Reload.State = ManagedReloadState::Cancelled;
    }

    ManagedReloadStatus ScriptSystem::ReloadStatus() const
    {
        std::scoped_lock lock(m_Impl->Mutex);
        return m_Impl->Reload;
    }

    std::vector<ManagedBehaviourTypeDescriptor> ScriptSystem::BehaviourTypes() const
    {
        m_Impl->RequireOwner();
        std::vector<ManagedBehaviourTypeDescriptor> result;
        result.reserve(m_Impl->ActiveTypes.size());
        for (const auto& type : m_Impl->ActiveTypes)
        {
            const auto separator = type.Name.find_last_of('.');
            result.push_back({type.Name, separator == std::string::npos ? type.Name : type.Name.substr(separator + 1),
                              type.ComponentType, type.ExecutionOrder, type.RequiredComponents});
        }
        return result;
    }

    std::vector<ManagedAssetTypeDescriptor> ScriptSystem::ManagedAssetTypes() const
    {
        m_Impl->RequireOwner();
        return m_Impl->ActiveManagedAssetTypes;
    }

    std::vector<ManagedAssetTypeDiagnostic> ScriptSystem::ManagedAssetTypeDiagnostics() const
    {
        m_Impl->RequireOwner();
        return m_Impl->ActiveManagedAssetDiagnostics;
    }

    void ScriptSystem::SetAssetSystem(Ref<AssetSystem> assets)
    {
        m_Impl->RequireOwner();
        if (!IsOpen())
            throw std::logic_error("ScriptSystem is closed.");
        if (!assets || !assets->IsOpen())
            throw std::invalid_argument("Managed data integration requires an open AssetSystem.");
        {
            std::scoped_lock lock(m_Impl->ManagedAssetMutex);
            if ((!m_Impl->PendingManagedAssetLoads.empty() || !m_Impl->ManagedAssetSources.empty()) &&
                m_Impl->Assets != assets)
            {
                throw std::logic_error("The managed data AssetSystem cannot change while assets are active.");
            }
        }
        m_Impl->Assets = std::move(assets);
    }

    void ScriptSystem::PumpManagedAssets()
    {
        m_Impl->RequireOwner();
        if (!IsOpen() || !m_Impl->ActiveContext || !m_Impl->ActiveNativeRuntimeType)
            return;
        std::uint64_t generation = 0;
        {
            std::scoped_lock lock(m_Impl->Mutex);
            if (m_Impl->Reload.State != ManagedReloadState::Active)
                return;
            generation = m_Impl->Reload.Generation;
        }

        struct Completion final
        {
            AssetId Id;
            AssetHandle<ManagedDataAsset> Handle;
            bool Failed = false;
            std::string Diagnostic;
        };
        std::vector<Completion> completions;
        {
            std::scoped_lock lock(m_Impl->ManagedAssetMutex);
            for (auto iterator = m_Impl->PendingManagedAssetLoads.begin();
                 iterator != m_Impl->PendingManagedAssetLoads.end();)
            {
                if (iterator->second.Generation != generation)
                {
                    ++iterator;
                    continue;
                }
                const auto state = iterator->second.Handle.State();
                if (state == AssetState::Ready || state == AssetState::Reloading)
                {
                    completions.push_back({iterator->first, iterator->second.Handle});
                    iterator = m_Impl->PendingManagedAssetLoads.erase(iterator);
                }
                else if (state == AssetState::Failed || state == AssetState::Cancelled)
                {
                    const auto diagnostic = iterator->second.Handle.Diagnostic();
                    completions.push_back(
                        {iterator->first, iterator->second.Handle, true,
                         diagnostic.Message.empty() ? "Managed data asset loading failed." : diagnostic.Message});
                    iterator = m_Impl->PendingManagedAssetLoads.erase(iterator);
                }
                else
                {
                    ++iterator;
                }
            }
        }

        for (auto& completion : completions)
        {
            try
            {
                const Impl::RuntimeScope scope(*m_Impl);
                if (completion.Failed)
                {
                    (void)m_Impl->ActiveNativeRuntimeType->InvokeStaticMethod<Coral::Bool32>(
                        "FailManagedAssetLoad", generation, completion.Id.High(), completion.Id.Low(),
                        completion.Diagnostic);
                    continue;
                }
                const auto asset = completion.Handle.TryGetLoaded();
                if (!asset)
                    throw std::runtime_error("Managed data asset completed without a loaded object.");
                auto object = m_Impl->HydrateManagedAsset(*asset, m_Impl->ActiveManagedAssetRuntimeTypes);
                if (object.InvokeMethod<Coral::Bool32>("RuntimeCompleteManagedAssetLoad", generation,
                                                       completion.Id.High(), completion.Id.Low()) == 0)
                {
                    continue;
                }
                std::scoped_lock lock(m_Impl->ManagedAssetMutex);
                m_Impl->ManagedAssetSources.insert_or_assign(
                    completion.Id, Impl::ManagedAssetSource{completion.Handle, completion.Handle.Revision()});
            }
            catch (const std::exception& exception)
            {
                const Impl::RuntimeScope scope(*m_Impl);
                (void)m_Impl->ActiveNativeRuntimeType->InvokeStaticMethod<Coral::Bool32>(
                    "FailManagedAssetLoad", generation, completion.Id.High(), completion.Id.Low(),
                    std::string(exception.what()));
            }
        }

        std::vector<std::pair<AssetId, Impl::ManagedAssetSource>> reloads;
        {
            std::scoped_lock lock(m_Impl->ManagedAssetMutex);
            for (const auto& [id, source] : m_Impl->ManagedAssetSources)
                if (source.Handle.Revision() != source.ObservedRevision && source.Handle.TryGetLoaded())
                    reloads.emplace_back(id, source);
        }
        for (const auto& [id, source] : reloads)
        {
            try
            {
                const auto asset = source.Handle.TryGetLoaded();
                if (!asset)
                    continue;
                auto candidate = m_Impl->HydrateManagedAsset(*asset, m_Impl->ActiveManagedAssetRuntimeTypes);
                const Impl::RuntimeScope scope(*m_Impl);
                if (candidate.InvokeMethod<Coral::Bool32>("RuntimeReloadManagedAsset", generation, id.High(),
                                                          id.Low()) == 0)
                    continue;
                std::scoped_lock lock(m_Impl->ManagedAssetMutex);
                if (const auto found = m_Impl->ManagedAssetSources.find(id); found != m_Impl->ManagedAssetSources.end())
                    found->second.ObservedRevision = source.Handle.Revision();
            }
            catch (const std::exception& exception)
            {
                std::scoped_lock lock(m_Impl->ManagedAssetMutex);
                m_Impl->ActiveManagedAssetDiagnostics.push_back(
                    {"Asset " + id.ToString(),
                     std::string("Hot reload kept the last-good object: ") + exception.what()});
                if (const auto found = m_Impl->ManagedAssetSources.find(id); found != m_Impl->ManagedAssetSources.end())
                    found->second.ObservedRevision = source.Handle.Revision();
            }
        }
    }

} // namespace Keire
