#pragma once

#include "Keire/Animation/AnimationSystem.h"
#include "Keire/Audio/AudioAssets.h"
#include "Keire/ECS/Component.h"
#include "Keire/ECS/Components/AnimatorComponent.h"
#include "Keire/ECS/Components/AudioComponents.h"
#include "Keire/ECS/Components/CharacterControllerComponent.h"
#include "Keire/ECS/Components/RigidBodyComponent.h"
#include "Keire/ECS/Components/TransformComponent.h"
#include "Keire/ECS/Entity.h"
#include "Keire/Jobs/JobSystem.h"
#include "Keire/Scripting/ScriptSystem.h"
#include "Keire/Vfx/VfxSystem.h"
#include "KeireInternal/FileSystem.h"
#include "KeireInternal/Process.h"
#include "KeireInternal/Scripting/CoralLog.h"
#include "KeireInternal/Scripting/ManagedBehaviourComponent.h"
#include "KeireInternal/Scripting/ManagedBuildWorkspace.h"
#include "KeireInternal/Scripting/ManagedBuiltinComponents.h"
#include "KeireInternal/Scripting/ManagedGenerationSequence.h"
#include "KeireInternal/Scripting/ManagedReflection.h"
#include "KeireInternal/Scripting/ManagedRuntimeBindings.h"
#include "KeireInternal/Scripting/ManagedRuntimeInterop.h"
#include "KeireInternal/Scripting/ManagedSdk.h"
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4146)
#endif
#include <Coral/Assembly.hpp>
#include <Coral/Attribute.hpp>
#include <Coral/ManagedObject.hpp>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <exception>
#include <limits>
#include <map>
#include <mutex>
#include <nlohmann/json.hpp>
#include <optional>
#include <ranges>
#include <set>
#include <stdexcept>
#include <thread>
#include <tuple>
#include <unordered_map>
#include <utility>

namespace Keire
{
    using Detail::ApplyManagedState;
    using Detail::GenerateManagedApiDesignTimeProject;
    using Detail::GenerateManagedBuildAggregator;
    using Detail::GenerateProject;
    using Detail::GenerateSolution;
    using Detail::ManagedApiSourceFingerprint;
    using Detail::ManagedInspectorAttributeTypes, Detail::ResolveManagedInspectorAttributeTypes;
    using Detail::ManagedTypeName;
    using Detail::ParseDiagnostics;
    using Detail::ParseManagedAssetMetadata;
    using Detail::PathText;
    using Detail::ProjectManagedState;
    using Detail::ReflectManagedMethods, Detail::ReflectManagedProperties, Detail::WriteText;

    class ScriptSystem::Impl final
    {
      public:
        struct BehaviourType final
        {
            std::string Name;
            ComponentTypeId ComponentType;
            std::int32_t ExecutionOrder = 0;
            const Coral::Type* Type = nullptr;
            std::vector<ComponentProperty> Properties;
            std::vector<ComponentMethod> Methods;
            std::vector<ComponentTypeId> RequiredComponents;
        };
        struct BehaviourInstance final
        {
            std::string TypeName;
            ComponentTypeId ComponentType;
            std::uint64_t World = 0;
            AssetId Entity;
            Coral::ManagedObject Object;
            std::string State = "{\"version\":1,\"fields\":[]}";
            Detail::ManagedCallbackProfile CallbackProfiles[Detail::ManagedCallbackProfileCount]{};
            bool Enabled = true;
            bool Faulted = false;
            Keire::Entity NativeEntity;
            std::uint32_t CallbackMask = ~std::uint32_t{0};
        };
        struct ManagedAssetSource final
        {
            AssetHandle<ManagedDataAsset> Handle;
            std::uint64_t ObservedRevision = 0;
        };

        struct PendingManagedAssetLoad final
        {
            AssetHandle<ManagedDataAsset> Handle;
            std::uint64_t Generation = 0;
        };

        using ManagedJobCallback = std::uint8_t (*)(void*, std::uint8_t, std::uint8_t);

        struct ManagedJobRecord final
        {
            JobHandle Work;
            std::uint64_t Generation = 0;
        };

        static constexpr std::uint32_t FixedUpdateCallback = 1U << 0, UpdateCallback = 1U << 1;
        static constexpr std::uint32_t LateUpdateCallback = 1U << 2;
        static constexpr std::uint32_t AnimatorIkCallback = 1U << 3;
        static constexpr std::uint32_t AnimationEventCallback = 1U << 4;
        static constexpr std::uint32_t ProceduralMotionEventCallback = 1U << 5;
        class RuntimeScope final
        {
          public:
            explicit RuntimeScope(Impl& runtime) noexcept
                : m_Previous(CurrentRuntime), m_Bindings(runtime.Specification.RuntimeServices),
                  m_BuiltinComponents(&Impl::ResolveRuntimeEntity)
            {
                CurrentRuntime = &runtime;
            }
            ~RuntimeScope() { CurrentRuntime = m_Previous; }
            RuntimeScope(const RuntimeScope&) = delete;
            RuntimeScope& operator=(const RuntimeScope&) = delete;

          private:
            Impl* m_Previous;
            Detail::ManagedRuntimeBindingsScope m_Bindings;
            Detail::ManagedBuiltinComponentResolverScope m_BuiltinComponents;
        };
        Impl(ScriptSystemSpecification value, Ref<JobSystem> jobs);

        ~Impl();

        void RequireOwner() const;

        [[nodiscard]] Coral::ManagedObject
        HydrateManagedAsset(const ManagedDataAsset& source,
                            const std::map<ManagedTypeId, const Coral::Type*>& runtimeTypes);

        void InstallManagedAssetGeneration(Coral::Type& nativeRuntime, const std::uint64_t generation);
        void ResetManagedAssetGeneration(const Coral::Type* nativeRuntime, const std::uint64_t generation) noexcept;
        void ReleaseManagedAssetGenerationServices(const std::uint64_t generation) noexcept;
        void CancelManagedExtensionGeneration(const Coral::Type* runtimeServices, const Coral::Type* editorExtensions,
                                              std::uint64_t generation) noexcept;
        void ShutdownManagedExtensionGeneration(const Coral::Type* runtimeServices, const Coral::Type* editorExtensions,
                                                std::uint64_t generation) noexcept;
        void ResumeGenerationSequence();

        [[nodiscard]] std::filesystem::path FindManagedApiProject() const;

        void StopWorker() noexcept;

        void InitializeRuntime();

        void Unload(std::unique_ptr<Coral::AssemblyLoadContext>& context) noexcept;

        void ShutdownRuntime() noexcept;

        void DrainManagedJobs(const bool recreate) noexcept;

        void ClearRuntimeException();

        void ThrowRuntimeException();

        [[nodiscard]] static Entity ResolveRuntimeEntity(const std::uint64_t world, const std::uint64_t high,
                                                         const std::uint64_t low) noexcept;

        static std::uint8_t RuntimeRequestManagedAssetLoad(const std::uint64_t generation, const std::uint64_t high,
                                                           const std::uint64_t low) noexcept;

        static void RuntimeCancelManagedAssetLoad(const std::uint64_t generation, const std::uint64_t high,
                                                  const std::uint64_t low) noexcept;

        static void RuntimeReleaseManagedAsset(const std::uint64_t generation, const std::uint64_t high,
                                               const std::uint64_t low) noexcept;

        static std::uint64_t RuntimeSubmitManagedJob(const std::uint64_t* dependencyIds,
                                                     const std::int32_t dependencyCount, const std::uint8_t priority,
                                                     const std::uint8_t jobClass, const Coral::String name, void* state,
                                                     const ManagedJobCallback callback) noexcept;

        static void RuntimeCancelManagedJob(const std::uint64_t id) noexcept;

        static void RuntimeWriteLog(const std::uint8_t level, const Coral::String message) noexcept;

        static void RuntimeDrawDebugLine(Vector3 start, Vector3 end, Color color, float duration) noexcept;

        static void RuntimeRegisterProfileName(const std::uint64_t id, const Coral::String name) noexcept;

        static void RuntimeRecordProfileSpan(const std::uint64_t id, const double startMicroseconds,
                                             const double durationMicroseconds) noexcept;

        static void RuntimeSetProfileCounter(const std::uint64_t id, const double value) noexcept;

        [[nodiscard]] static float RuntimeDeltaTime() noexcept;

        [[nodiscard]] static float RuntimeFixedDeltaTime() noexcept;

        [[nodiscard]] static float RuntimeUnscaledDeltaTime() noexcept;

        [[nodiscard]] static double RuntimeElapsedTime() noexcept;

        static void RuntimeInputAxis2D(const Coral::String action, float* x, float* y) noexcept;

        [[nodiscard]] static std::uint8_t RuntimeInputState(const Coral::String action) noexcept;

        static void RuntimeSetCursorVisible(const std::uint8_t visible) noexcept;

        static void RuntimeSetCursorLocked(const std::uint8_t locked) noexcept;

        [[nodiscard]] static std::uint8_t RuntimeIsCursorVisible() noexcept;

        [[nodiscard]] static std::uint8_t RuntimeIsCursorLocked() noexcept;

        [[nodiscard]] static Vector3 RuntimeGetLocalPosition(const std::uint64_t world, const std::uint64_t high,
                                                             const std::uint64_t low) noexcept;

        static void RuntimeSetLocalPosition(const std::uint64_t world, const std::uint64_t high,
                                            const std::uint64_t low, const Vector3 value) noexcept;

        [[nodiscard]] static Quaternion RuntimeGetLocalRotation(const std::uint64_t world, const std::uint64_t high,
                                                                const std::uint64_t low) noexcept;

        static void RuntimeSetLocalRotation(const std::uint64_t world, const std::uint64_t high,
                                            const std::uint64_t low, const Quaternion value) noexcept;

        [[nodiscard]] static Quaternion RuntimeGetWorldRotation(const std::uint64_t world, const std::uint64_t high,
                                                                const std::uint64_t low) noexcept;

        static void RuntimeSetWorldPosition(const std::uint64_t world, const std::uint64_t high,
                                            const std::uint64_t low, const Vector3 value) noexcept;

        static void RuntimeSetWorldRotation(const std::uint64_t world, const std::uint64_t high,
                                            const std::uint64_t low, const Quaternion value) noexcept;

        [[nodiscard]] static Ref<CharacterControllerComponent>
        RuntimeCharacterController(const std::uint64_t world, const std::uint64_t high,
                                   const std::uint64_t low) noexcept;

        [[nodiscard]] static std::uint8_t RuntimeMoveCharacterController(const std::uint64_t world,
                                                                         const std::uint64_t high,
                                                                         const std::uint64_t low,
                                                                         const Vector3 displacement) noexcept;
        [[nodiscard]] static std::uint8_t
        RuntimeGetCharacterControllerState(const std::uint64_t world, const std::uint64_t high, const std::uint64_t low,
                                           std::uint8_t* grounded, Vector3* normal, Vector3* velocity) noexcept;

        [[nodiscard]] static Ref<RigidBodyComponent>
        RuntimeRigidBody(const std::uint64_t world, const std::uint64_t high, const std::uint64_t low) noexcept;

        [[nodiscard]] static std::uint8_t
        RuntimeGetRigidBodyProperties(const std::uint64_t world, const std::uint64_t high, const std::uint64_t low,
                                      std::uint8_t* motion, float* mass, Vector3* velocity, std::uint8_t* continuous,
                                      std::uint8_t* gravity) noexcept;

        [[nodiscard]] static std::uint8_t RuntimeSetRigidBodyMotion(const std::uint64_t world, const std::uint64_t high,
                                                                    const std::uint64_t low,
                                                                    const std::uint8_t motion) noexcept;

        [[nodiscard]] static std::uint8_t RuntimeSetRigidBodyMass(const std::uint64_t world, const std::uint64_t high,
                                                                  const std::uint64_t low, const float mass) noexcept;

        [[nodiscard]] static std::uint8_t RuntimeSetRigidBodyVelocity(const std::uint64_t world,
                                                                      const std::uint64_t high, const std::uint64_t low,
                                                                      const Vector3 velocity) noexcept;

        [[nodiscard]] static std::uint8_t RuntimeSetRigidBodyFlag(const std::uint64_t world, const std::uint64_t high,
                                                                  const std::uint64_t low, const std::uint8_t property,
                                                                  const std::uint8_t value) noexcept;

        [[nodiscard]] static std::uint8_t RuntimeAddRigidBodyForce(const std::uint64_t world, const std::uint64_t high,
                                                                   const std::uint64_t low, const Vector3 force,
                                                                   const std::uint8_t mode) noexcept;

        [[nodiscard]] static Ref<AnimatorComponent> RuntimeAnimator(const std::uint64_t world, const std::uint64_t high,
                                                                    const std::uint64_t low) noexcept;

        [[nodiscard]] static std::uint8_t RuntimeSetAnimatorFloat(const std::uint64_t world, const std::uint64_t high,
                                                                  const std::uint64_t low,
                                                                  const Coral::String parameter,
                                                                  const float value) noexcept;

        [[nodiscard]] static std::uint8_t RuntimeSetAnimatorInteger(const std::uint64_t world, const std::uint64_t high,
                                                                    const std::uint64_t low,
                                                                    const Coral::String parameter,
                                                                    const std::int32_t value) noexcept;

        [[nodiscard]] static std::uint8_t RuntimeSetAnimatorBoolean(const std::uint64_t world, const std::uint64_t high,
                                                                    const std::uint64_t low,
                                                                    const Coral::String parameter,
                                                                    const std::uint8_t value) noexcept;

        [[nodiscard]] static std::uint8_t RuntimeSetAnimatorTrigger(const std::uint64_t world, const std::uint64_t high,
                                                                    const std::uint64_t low,
                                                                    const Coral::String parameter,
                                                                    const std::uint8_t set) noexcept;

        [[nodiscard]] static std::uint8_t
        RuntimeSetAnimatorLayerWeight(const std::uint64_t world, const std::uint64_t high, const std::uint64_t low,
                                      const Coral::String layer, const float value) noexcept;

        [[nodiscard]] static std::uint8_t RuntimePlayAnimator(const std::uint64_t world, const std::uint64_t high,
                                                              const std::uint64_t low, const Coral::String state,
                                                              const Coral::String layer,
                                                              const float normalizedTime) noexcept;

        [[nodiscard]] static std::uint8_t RuntimeCrossFadeAnimator(const std::uint64_t world, const std::uint64_t high,
                                                                   const std::uint64_t low, const Coral::String state,
                                                                   const Coral::String layer, const float duration,
                                                                   const float normalizedTime) noexcept;

        [[nodiscard]] static std::uint8_t RuntimePauseAnimator(const std::uint64_t world, const std::uint64_t high,
                                                               const std::uint64_t low,
                                                               const std::uint8_t paused) noexcept;

        [[nodiscard]] static std::uint8_t RuntimeStopAnimator(const std::uint64_t world, const std::uint64_t high,
                                                              const std::uint64_t low) noexcept;

        [[nodiscard]] static std::uint8_t RuntimeSetAnimatorSpeed(const std::uint64_t world, const std::uint64_t high,
                                                                  const std::uint64_t low, const float speed) noexcept;

        [[nodiscard]] static std::uint8_t RuntimeSetAnimatorFootGroundingWeight(const std::uint64_t world,
                                                                                const std::uint64_t high,
                                                                                const std::uint64_t low,
                                                                                const float weight) noexcept;

        [[nodiscard]] static std::uint8_t
        RuntimeSetProceduralLocomotion(const std::uint64_t world, const std::uint64_t high, const std::uint64_t low,
                                       const Vector3 desiredWorldVelocity, const Vector3 facingWorldDirection,
                                       const Vector3 lookWorldDirection, const float crouchAmount, const float runBlend,
                                       const std::uint8_t jumpRequested) noexcept;

        [[nodiscard]] static std::uint8_t
        RuntimeGetProceduralLocomotionState(const std::uint64_t world, const std::uint64_t high,
                                            const std::uint64_t low,
                                            Detail::NativeProceduralLocomotionState* result) noexcept;

        [[nodiscard]] static std::uint8_t RuntimeGetAnimatorState(const std::uint64_t world, const std::uint64_t high,
                                                                  const std::uint64_t low,
                                                                  Detail::NativeAnimatorState* state) noexcept;

        [[nodiscard]] static std::int32_t RuntimeGetAnimatorStateName(const std::uint64_t world,
                                                                      const std::uint64_t high, const std::uint64_t low,
                                                                      char* destination,
                                                                      const std::int32_t capacity) noexcept;

        [[nodiscard]] static std::uint8_t
        RuntimeSetAnimatorTwoBoneIk(const std::uint64_t world, const std::uint64_t high, const std::uint64_t low,
                                    const Coral::String goal, const Coral::String root, const Coral::String middle,
                                    const Coral::String end, const Vector3 target, const Vector3 pole,
                                    const float weight, const std::uint8_t space) noexcept;

        [[nodiscard]] static std::uint8_t
        RuntimeSetAnimatorFabrikIk(const std::uint64_t world, const std::uint64_t high, const std::uint64_t low,
                                   const Coral::String goal, const Coral::String encodedBones, const Vector3 target,
                                   const float weight, const std::uint32_t maximumIterations, const float tolerance,
                                   const std::uint8_t space) noexcept;

        [[nodiscard]] static std::uint8_t RuntimeClearAnimatorIk(const std::uint64_t world, const std::uint64_t high,
                                                                 const std::uint64_t low,
                                                                 const Coral::String goal) noexcept;

        [[nodiscard]] static std::uint8_t RuntimeTryGetAnimatorFloat(const std::uint64_t world,
                                                                     const std::uint64_t high, const std::uint64_t low,
                                                                     const Coral::String parameter,
                                                                     float* value) noexcept;

        [[nodiscard]] static std::uint8_t
        RuntimeTryGetAnimatorInteger(const std::uint64_t world, const std::uint64_t high, const std::uint64_t low,
                                     const Coral::String parameter, std::int32_t* value) noexcept;

        [[nodiscard]] static std::uint8_t
        RuntimeTryGetAnimatorBoolean(const std::uint64_t world, const std::uint64_t high, const std::uint64_t low,
                                     const Coral::String parameter, std::uint8_t* value) noexcept;

        [[nodiscard]] static std::uint8_t
        RuntimeTryGetAnimatorLayerWeight(const std::uint64_t world, const std::uint64_t high, const std::uint64_t low,
                                         const Coral::String layer, float* value) noexcept;

        [[nodiscard]] static std::uint8_t RuntimeEntityExists(const std::uint64_t world, const std::uint64_t high,
                                                              const std::uint64_t low) noexcept;

        [[nodiscard]] static std::uint8_t RuntimeGetEntityActive(const std::uint64_t world, const std::uint64_t high,
                                                                 const std::uint64_t low) noexcept;

        static void RuntimeSetEntityActive(const std::uint64_t world, const std::uint64_t high, const std::uint64_t low,
                                           const std::uint8_t active) noexcept;

        [[nodiscard]] static std::uint32_t RuntimeGetEntityLayer(const std::uint64_t world, const std::uint64_t high,
                                                                 const std::uint64_t low) noexcept;

        [[nodiscard]] static std::uint8_t RuntimeSetEntityLayer(const std::uint64_t world, const std::uint64_t high,
                                                                const std::uint64_t low,
                                                                const std::uint32_t layer) noexcept;

        [[nodiscard]] static std::uint8_t RuntimeGetEntityActiveInHierarchy(const std::uint64_t world,
                                                                            const std::uint64_t high,
                                                                            const std::uint64_t low) noexcept;

        [[nodiscard]] static std::int32_t RuntimeGetEntityName(const std::uint64_t world, const std::uint64_t high,
                                                               const std::uint64_t low, char* destination,
                                                               const std::int32_t capacity) noexcept;

        [[nodiscard]] static std::uint8_t RuntimeSetEntityName(const std::uint64_t world, const std::uint64_t high,
                                                               const std::uint64_t low,
                                                               const Coral::String name) noexcept;

        [[nodiscard]] static std::uint8_t RuntimeGetEntityParent(const std::uint64_t world, const std::uint64_t high,
                                                                 const std::uint64_t low, std::uint64_t* parentHigh,
                                                                 std::uint64_t* parentLow) noexcept;

        [[nodiscard]] static std::uint8_t RuntimeSetEntityParent(const std::uint64_t world, const std::uint64_t high,
                                                                 const std::uint64_t low,
                                                                 const std::uint64_t parentHigh,
                                                                 const std::uint64_t parentLow,
                                                                 const std::uint8_t preserveWorld) noexcept;

        [[nodiscard]] static std::int32_t RuntimeGetEntityChildCount(const std::uint64_t world,
                                                                     const std::uint64_t high,
                                                                     const std::uint64_t low) noexcept;

        [[nodiscard]] static std::uint8_t RuntimeGetEntityChild(const std::uint64_t world, const std::uint64_t high,
                                                                const std::uint64_t low, const std::int32_t index,
                                                                std::uint64_t* childHigh,
                                                                std::uint64_t* childLow) noexcept;

        [[nodiscard]] static std::uint8_t
        RuntimeComponentExists(const std::uint64_t world, const std::uint64_t entityHigh, const std::uint64_t entityLow,
                               const std::uint64_t typeHigh, const std::uint64_t typeLow) noexcept;
        [[nodiscard]] static std::uint8_t RuntimeAddComponent(const std::uint64_t world, const std::uint64_t entityHigh,
                                                              const std::uint64_t entityLow,
                                                              const std::uint64_t typeHigh,
                                                              const std::uint64_t typeLow) noexcept;
        [[nodiscard]] static std::uint8_t
        RuntimeRemoveComponent(const std::uint64_t world, const std::uint64_t entityHigh, const std::uint64_t entityLow,
                               const std::uint64_t typeHigh, const std::uint64_t typeLow) noexcept;
        [[nodiscard]] static std::uint8_t RuntimeGetComponentEnabled(const std::uint64_t world,
                                                                     const std::uint64_t entityHigh,
                                                                     const std::uint64_t entityLow,
                                                                     const std::uint64_t typeHigh,
                                                                     const std::uint64_t typeLow) noexcept;

        [[nodiscard]] static std::uint8_t
        RuntimeSetComponentEnabled(const std::uint64_t world, const std::uint64_t entityHigh,
                                   const std::uint64_t entityLow, const std::uint64_t typeHigh,
                                   const std::uint64_t typeLow, const std::uint8_t enabled) noexcept;

        [[nodiscard]] static Vector3 RuntimeGetLocalScale(const std::uint64_t world, const std::uint64_t high,
                                                          const std::uint64_t low) noexcept;

        static void RuntimeSetLocalScale(const std::uint64_t world, const std::uint64_t high, const std::uint64_t low,
                                         const Vector3 value) noexcept;

        [[nodiscard]] static Vector3 RuntimeGetWorldPosition(const std::uint64_t world, const std::uint64_t high,
                                                             const std::uint64_t low) noexcept;

        [[nodiscard]] static Vector3 RuntimeGetPresentationWorldPosition(const std::uint64_t world,
                                                                         const std::uint64_t high,
                                                                         const std::uint64_t low) noexcept;

        [[nodiscard]] static Quaternion RuntimeGetPresentationWorldRotation(const std::uint64_t world,
                                                                            const std::uint64_t high,
                                                                            const std::uint64_t low) noexcept;

        static void RuntimeResetPresentationInterpolation(const std::uint64_t world, const std::uint64_t high,
                                                          const std::uint64_t low) noexcept;

        static void RuntimeCloneEntity(const std::uint64_t world, const std::uint64_t high, const std::uint64_t low,
                                       std::uint64_t* resultHigh, std::uint64_t* resultLow) noexcept;

        static void RuntimeDestroyEntity(const std::uint64_t world, const std::uint64_t high,
                                         const std::uint64_t low) noexcept;

        [[nodiscard]] static std::uint8_t RuntimePlayAudio(const std::uint64_t world, const std::uint64_t entityHigh,
                                                           const std::uint64_t entityLow, const std::uint64_t clipHigh,
                                                           const std::uint64_t clipLow, const float gain) noexcept;

        [[nodiscard]] static std::uint8_t
        RuntimePlayAudioAdvanced(const std::uint64_t world, const std::uint64_t entityHigh,
                                 const std::uint64_t entityLow, const std::uint64_t clipHigh,
                                 const std::uint64_t clipLow, const std::uint64_t mixerHigh,
                                 const std::uint64_t mixerLow, const std::uint64_t busHigh, const std::uint64_t busLow,
                                 const Coral::String bus, const float gain, const float pitch,
                                 const std::uint32_t priority, const std::uint8_t loop, const std::uint8_t spatial,
                                 const float minimumDistance, const float maximumDistance) noexcept;

        [[nodiscard]] static std::uint8_t RuntimeStopAudio(const std::uint64_t world, const std::uint64_t entityHigh,
                                                           const std::uint64_t entityLow) noexcept;

        [[nodiscard]] static std::uint8_t RuntimePlayAudioSource(const std::uint64_t world,
                                                                 const std::uint64_t entityHigh,
                                                                 const std::uint64_t entityLow) noexcept;

        [[nodiscard]] static std::uint8_t RuntimePauseAudio(const std::uint64_t world, const std::uint64_t entityHigh,
                                                            const std::uint64_t entityLow,
                                                            const std::uint8_t paused) noexcept;

        [[nodiscard]] static std::uint8_t RuntimeSeekAudio(const std::uint64_t world, const std::uint64_t entityHigh,
                                                           const std::uint64_t entityLow,
                                                           const float positionSeconds) noexcept;

        [[nodiscard]] static std::uint8_t
        RuntimeGetAudioSourceProperties(const std::uint64_t world, const std::uint64_t entityHigh,
                                        const std::uint64_t entityLow,
                                        Detail::NativeAudioSourceProperties* properties) noexcept;

        [[nodiscard]] static std::uint8_t RuntimeSetAudioSourceClip(const std::uint64_t world,
                                                                    const std::uint64_t entityHigh,
                                                                    const std::uint64_t entityLow,
                                                                    const std::uint64_t clipHigh,
                                                                    const std::uint64_t clipLow) noexcept;

        [[nodiscard]] static std::uint8_t
        RuntimeSetAudioSourceRouting(const std::uint64_t world, const std::uint64_t entityHigh,
                                     const std::uint64_t entityLow, const std::uint64_t mixerHigh,
                                     const std::uint64_t mixerLow, const std::uint64_t busHigh,
                                     const std::uint64_t busLow) noexcept;

        [[nodiscard]] static std::uint8_t RuntimeSetAudioSourceScalar(const std::uint64_t world,
                                                                      const std::uint64_t entityHigh,
                                                                      const std::uint64_t entityLow,
                                                                      const std::uint8_t property,
                                                                      const float value) noexcept;

        [[nodiscard]] static std::uint8_t RuntimeSetAudioSourceFlag(const std::uint64_t world,
                                                                    const std::uint64_t entityHigh,
                                                                    const std::uint64_t entityLow,
                                                                    const std::uint8_t property,
                                                                    const std::uint8_t value) noexcept;

        [[nodiscard]] static std::uint8_t
        RuntimeGetAudioListenerProperties(const std::uint64_t world, const std::uint64_t entityHigh,
                                          const std::uint64_t entityLow,
                                          Detail::NativeAudioListenerProperties* properties) noexcept;

        [[nodiscard]] static std::uint8_t
        RuntimeSetAudioListenerProperties(const std::uint64_t world, const std::uint64_t entityHigh,
                                          const std::uint64_t entityLow,
                                          const Detail::NativeAudioListenerProperties* properties) noexcept;

        [[nodiscard]] static std::uint8_t
        RuntimeGetAudioReverbZoneProperties(const std::uint64_t world, const std::uint64_t entityHigh,
                                            const std::uint64_t entityLow,
                                            Detail::NativeAudioReverbZoneProperties* properties) noexcept;

        [[nodiscard]] static std::uint8_t
        RuntimeSetAudioReverbZoneProperties(const std::uint64_t world, const std::uint64_t entityHigh,
                                            const std::uint64_t entityLow,
                                            const Detail::NativeAudioReverbZoneProperties* properties) noexcept;

        [[nodiscard]] static std::uint8_t RuntimePlayVfx(const std::uint64_t world, const std::uint64_t entityHigh,
                                                         const std::uint64_t entityLow, const std::uint64_t effectHigh,
                                                         const std::uint64_t effectLow,
                                                         const std::uint8_t restart) noexcept;

        [[nodiscard]] static std::uint8_t RuntimeStopVfx(const std::uint64_t world, const std::uint64_t entityHigh,
                                                         const std::uint64_t entityLow) noexcept;

        [[nodiscard]] static std::uint8_t RuntimePauseVfx(const std::uint64_t world, const std::uint64_t entityHigh,
                                                          const std::uint64_t entityLow,
                                                          const std::uint8_t paused) noexcept;

        [[nodiscard]] static std::uint8_t RuntimeIsVfxAlive(const std::uint64_t world, const std::uint64_t entityHigh,
                                                            const std::uint64_t entityLow) noexcept;

        [[nodiscard]] static std::uint8_t RuntimeSendVfxEvent(const std::uint64_t world, const std::uint64_t entityHigh,
                                                              const std::uint64_t entityLow,
                                                              const Coral::String eventName,
                                                              const std::uint32_t spawnCount) noexcept;

        [[nodiscard]] static std::uint8_t
        RuntimeSetVfxParameter(const std::uint64_t world, const std::uint64_t entityHigh, const std::uint64_t entityLow,
                               const std::uint64_t parameterHigh, const std::uint64_t parameterLow,
                               VfxParameterValue value) noexcept;

        [[nodiscard]] static std::uint8_t
        RuntimeSetVfxScalarRange(const std::uint64_t world, const std::uint64_t entityHigh,
                                 const std::uint64_t entityLow, const std::uint64_t parameterHigh,
                                 const std::uint64_t parameterLow, const float minimum, const float maximum) noexcept;

        [[nodiscard]] static std::uint8_t
        RuntimeSetVfxIntegerRange(const std::uint64_t world, const std::uint64_t entityHigh,
                                  const std::uint64_t entityLow, const std::uint64_t parameterHigh,
                                  const std::uint64_t parameterLow, const std::int64_t minimum,
                                  const std::int64_t maximum) noexcept;

        [[nodiscard]] static std::uint8_t
        RuntimeSetVfxUnsignedIntegerRange(const std::uint64_t world, const std::uint64_t entityHigh,
                                          const std::uint64_t entityLow, const std::uint64_t parameterHigh,
                                          const std::uint64_t parameterLow, const std::uint64_t minimum,
                                          const std::uint64_t maximum) noexcept;

        [[nodiscard]] static std::uint8_t
        RuntimeSetVfxVector2Range(const std::uint64_t world, const std::uint64_t entityHigh,
                                  const std::uint64_t entityLow, const std::uint64_t parameterHigh,
                                  const std::uint64_t parameterLow, const Vector2 minimum,
                                  const Vector2 maximum) noexcept;

        [[nodiscard]] static std::uint8_t
        RuntimeSetVfxVector3Range(const std::uint64_t world, const std::uint64_t entityHigh,
                                  const std::uint64_t entityLow, const std::uint64_t parameterHigh,
                                  const std::uint64_t parameterLow, const Vector3 minimum,
                                  const Vector3 maximum) noexcept;

        [[nodiscard]] static std::uint8_t
        RuntimeSetVfxVector4Range(const std::uint64_t world, const std::uint64_t entityHigh,
                                  const std::uint64_t entityLow, const std::uint64_t parameterHigh,
                                  const std::uint64_t parameterLow, const Vector4 minimum,
                                  const Vector4 maximum) noexcept;

        [[nodiscard]] static std::uint8_t
        RuntimeSetVfxColorRange(const std::uint64_t world, const std::uint64_t entityHigh,
                                const std::uint64_t entityLow, const std::uint64_t parameterHigh,
                                const std::uint64_t parameterLow, const Color minimum, const Color maximum) noexcept;

        struct RuntimeRaycastResult
        {
            std::uint64_t EntityHigh = 0;
            std::uint64_t EntityLow = 0;
            Vector3 Point;
            Vector3 Normal;
            float Distance = 0.0F;
        };

        [[nodiscard]] static std::uint8_t RuntimeRaycast(const std::uint64_t world, const Vector3 origin,
                                                         const Vector3 direction, const float maximumDistance,
                                                         const std::uint32_t mask, const std::uint64_t ignoredHigh,
                                                         const std::uint64_t ignoredLow,
                                                         RuntimeRaycastResult* result) noexcept;

        void Invoke(Coral::ManagedObject& object, const std::string_view method);

        void Invoke(Coral::ManagedObject& object, const std::string_view method, const float value);

        [[nodiscard]] std::uint32_t ReadCallbackMask(Coral::ManagedObject& object);

        [[nodiscard]] std::string CaptureState(Coral::ManagedObject& object, const bool persistent);

        void RestoreState(Coral::ManagedObject& object, const std::string& state, const bool persistent);

        [[nodiscard]] Coral::ManagedObject CreateObject(const BehaviourType& type, const std::uint64_t world,
                                                        const AssetId entity);

        [[nodiscard]] const BehaviourType* FindType(const std::vector<BehaviourType>& types,
                                                    const std::string_view name) const;

        [[nodiscard]] const BehaviourType* FindType(const std::vector<BehaviourType>& types,
                                                    const ComponentTypeId componentType) const;

        void InvokeInstance(const std::uint64_t id, const ManagedBehaviourCallback callback,
                            const float deltaSeconds = 0.0F);

        void InvokeAnimationEvent(const std::uint64_t id, const AnimationEventMessage& event);

        void InvokeProceduralMotionEvent(const std::uint64_t id, const ProceduralMotionEvent& event);

        void InvokePhysicsContact(const std::uint64_t id, const PhysicsContactPhase phase,
                                  const PhysicsContactMessage& contact);

        void SetState(const ManagedBuildState state);

        void RunBuild(const std::stop_token& cancellation, const ManagedBuildRequest& request,
                      const ManagedBuildOperationId operation) noexcept;

        ScriptSystemSpecification Specification;
        std::thread::id Owner;
        std::atomic<bool> Open{true};
        std::filesystem::path ProjectRoot;
        std::filesystem::path OutputRoot;
        std::filesystem::path Dotnet;
        std::filesystem::path ManagedApi;
        std::filesystem::path ManagedEditorApi;
        std::filesystem::path ManagedGenerator;
        mutable std::mutex Mutex;
        mutable std::condition_variable StatusChanged;
        ManagedBuildStatus Status;
        Coral::HostInstance RuntimeHost;
        std::unique_ptr<Coral::AssemblyLoadContext> ActiveContext;
        std::unique_ptr<Coral::AssemblyLoadContext> CandidateContext;
        std::vector<BehaviourType> ActiveTypes;
        std::vector<BehaviourType> CandidateTypes;
        std::shared_ptr<const ManagedAssetTypeCatalog> ActiveManagedAssetCatalog =
            std::make_shared<const ManagedAssetTypeCatalog>();
        std::shared_ptr<const ManagedAssetTypeCatalog> CandidateManagedAssetCatalog;
        std::vector<ManagedAssetTypeDiagnostic> ManagedAssetRuntimeDiagnostics;
        std::map<ManagedTypeId, const Coral::Type*> ActiveManagedAssetRuntimeTypes;
        std::map<ManagedTypeId, const Coral::Type*> CandidateManagedAssetRuntimeTypes;
        const Coral::Type* ActiveNativeRuntimeType = nullptr;
        const Coral::Type* CandidateNativeRuntimeType = nullptr;
        const Coral::Type* ActiveRuntimeServiceBridgeType = nullptr;
        const Coral::Type* CandidateRuntimeServiceBridgeType = nullptr;
        const Coral::Type* ActiveEditorExtensionBridgeType = nullptr;
        const Coral::Type* CandidateEditorExtensionBridgeType = nullptr;
        Ref<AssetSystem> Assets;
        std::mutex ManagedAssetMutex;
        std::map<AssetId, PendingManagedAssetLoad> PendingManagedAssetLoads;
        std::map<AssetId, ManagedAssetSource> ManagedAssetSources;
        std::unordered_map<std::uint64_t, BehaviourInstance> Instances;
        std::vector<ComponentTypeId> InstalledComponentTypes;
        std::vector<ManagedRuntimeDiagnostic> RuntimeDiagnostics;
        std::uint64_t CallbackInvocations = 0;
        std::uint64_t SkippedCallbacks = 0;
        std::uint64_t ManagedInteropCalls = 0;
        double CallbackMilliseconds = 0.0;
        double MaximumCallbackMilliseconds = 0.0;
        std::unordered_map<std::uint64_t, std::string> ProfileNames;
        std::uint64_t NextInstance = 1;
        std::shared_ptr<Impl*> Lifetime;
        std::shared_ptr<Detail::ManagedBehaviourComponentCallbacks> ComponentCallbacks;
        ManagedReloadStatus Reload;
        std::string RuntimeException;
        bool RuntimeInitialized = false;
        std::uint64_t NextReload = 1;
        Ref<JobSystem> Scheduler;
        Ref<JobScope> WorkScope;
        Ref<JobScope> ManagedJobs;
        std::mutex ManagedJobMutex;
        std::unordered_map<std::uint64_t, ManagedJobRecord> ManagedJobRecords;
        std::uint64_t NextManagedJob = 1;
        JobHandle Worker;
        std::stop_source BuildCancellation;
        bool OwnScheduler = false;
        std::uint64_t NextOperation = 1;
        static inline thread_local Impl* CurrentRuntime = nullptr;
    };
} // namespace Keire
