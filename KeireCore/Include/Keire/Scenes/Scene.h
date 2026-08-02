#pragma once

#include "Keire/Scenes/ScenePresentationRuntime.h"

#include "Keire/Api.h"
#include "Keire/ECS/Entity.h"
#include "Keire/Physics/PhysicsSystem.h"
#include "Keire/Ref.h"
#include "Keire/Scenes/SceneAsset.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Keire
{
    class Scene;
    class VfxWorld;
    struct VfxParameterOverride;

    namespace Detail
    {
        class SceneState;
    }

    class KEIRE_API SceneObjectHandle final
    {
      public:
        SceneObjectHandle() noexcept = default;
        [[nodiscard]] explicit operator bool() const noexcept;
        [[nodiscard]] AssetId Id() const noexcept { return m_Id; }
        [[nodiscard]] std::optional<SceneObjectDefinition> Snapshot() const;

      private:
        friend class Scene;
        SceneObjectHandle(WeakRef<Detail::SceneState> state, AssetId id) noexcept;
        WeakRef<Detail::SceneState> m_State;
        AssetId m_Id;
    };

    struct SceneHierarchySnapshot
    {
        std::vector<SceneObjectDefinition> Objects;
        std::vector<PrefabInstanceDefinition> PrefabInstances;
    };

    class KEIRE_API Scene final : public RefCounted
    {
      public:
        explicit Scene(AssetId asset, SceneDefinition definition, Ref<ComponentRegistry> components = {});
        ~Scene() override;

        Scene(const Scene&) = delete;
        Scene& operator=(const Scene&) = delete;

        [[nodiscard]] AssetId Asset() const noexcept;
        [[nodiscard]] std::string Name() const;
        void SetName(std::string name);
        [[nodiscard]] bool IsOpen() const noexcept;
        [[nodiscard]] bool Dirty() const noexcept;
        void MarkDirty() noexcept;
        void MarkSaved() noexcept;
        [[nodiscard]] std::size_t ObjectCount() const noexcept;
        [[nodiscard]] std::vector<SceneObjectDefinition> Objects() const;
        [[nodiscard]] SceneHierarchySnapshot HierarchySnapshot() const;
        [[nodiscard]] SceneDefinition Snapshot() const;
        [[nodiscard]] SceneObjectHandle Find(AssetId id) const noexcept;
        [[nodiscard]] SceneObjectHandle CreateObject(std::string name = "GameObject", AssetId parent = {});
        [[nodiscard]] SceneObjectHandle DuplicateObject(AssetId id);
        [[nodiscard]] bool DestroyObject(AssetId id);
        [[nodiscard]] bool RenameObject(AssetId id, std::string name);
        [[nodiscard]] bool SetObjectActive(AssetId id, bool active);
        [[nodiscard]] bool SetObjectTransform(AssetId id, SceneTransform transform);
        [[nodiscard]] bool ReparentObject(AssetId id, AssetId parent);

        [[nodiscard]] std::vector<Entity> Entities() const;
        [[nodiscard]] Entity FindEntity(EntityId id) const noexcept;
        [[nodiscard]] Entity CreateEntity(std::string name = "GameObject", Entity parent = {});
        [[nodiscard]] Entity DuplicateEntity(EntityId id);
        [[nodiscard]] bool DestroyEntity(EntityId id);
        void MoveEntity(EntityId id, EntityId parent = {}, EntityId beforeSibling = {},
                        bool preserveWorldTransform = true);
        [[nodiscard]] std::vector<Entity> Query(ComponentTypeId type) const;

        template <std::derived_from<Component> T> [[nodiscard]] std::vector<Entity> Query() const
        {
            return Query(T::StaticType());
        }

        [[nodiscard]] Ref<ComponentRegistry> Components() const noexcept;
        void BeginPlay();
        void FixedUpdate(float deltaSeconds);
        void Update(float deltaSeconds);
        void LateUpdate();
        void DispatchAnimationEvent(EntityId entity, const AnimationEventMessage& event);
        void DispatchPhysicsContact(EntityId entity, PhysicsContactPhase phase, const PhysicsContactMessage& contact);
        void EndPlay() noexcept;
        void Close() noexcept;

      private:
        friend class SceneObjectHandle;
        class Impl;
        [[nodiscard]] std::optional<SceneObjectDefinition> SnapshotObject(AssetId id) const;
        std::unique_ptr<Impl> m_Impl;
    };

    enum class ScenePlayState : std::uint8_t
    {
        Stopped,
        Playing,
        Paused,
        Faulted
    };

    struct SceneRuntimeDiagnostic
    {
        std::string Callback;
        std::string Message;
    };

    struct ScenePhysicsQueryHit
    {
        EntityId Entity;
        PhysicsQueryHit Hit;
    };

    class KEIRE_API SceneRuntimeSession final : public RefCounted
    {
      public:
        explicit SceneRuntimeSession(Ref<Scene> editScene, Ref<AssetSystem> assets = {}, Ref<AudioSystem> audio = {},
                                     Ref<PhysicsSystem> physics = {});
        ~SceneRuntimeSession() override;

        SceneRuntimeSession(const SceneRuntimeSession&) = delete;
        SceneRuntimeSession& operator=(const SceneRuntimeSession&) = delete;

        [[nodiscard]] ScenePlayState State() const noexcept;
        [[nodiscard]] Ref<Scene> EditScene() const noexcept;
        [[nodiscard]] Ref<Scene> RuntimeScene() const noexcept;
        [[nodiscard]] SceneRuntimeDiagnostic Diagnostic() const;
        [[nodiscard]] Ref<ScenePresentationRuntime> Presentation() const noexcept;
        [[nodiscard]] Ref<PhysicsWorld> Physics() const noexcept;
        /// Returns the Play Mode scene-owned VFX world, or null when the runtime session is not active.
        [[nodiscard]] Ref<VfxWorld> Vfx() const noexcept;
        /// Assigns effect to the runtime entity and requests playback. restart replaces only that entity's live
        /// instance; other emitters in the scene-owned VfxWorld continue unchanged.
        [[nodiscard]] bool PlayVfx(EntityId entity, AssetId effect, bool restart = false);
        /// Stops only the runtime entity's VFX instance without removing its component.
        [[nodiscard]] bool StopVfx(EntityId entity);
        /// Pauses with a zero simulation multiplier or resumes at 1.0.
        [[nodiscard]] bool PauseVfx(EntityId entity, bool paused);
        /// Reports whether the runtime entity currently owns a live native VfxHandle.
        [[nodiscard]] bool IsVfxAlive(EntityId entity) const noexcept;
        /// Queues a bounded named event for every matching system owned by this entity's live effect instance.
        [[nodiscard]] bool SendVfxEvent(EntityId entity, std::string_view eventName, std::uint32_t spawnCount = 1);
        /// Atomically updates an exposed Blackboard override on the runtime component and its live native VFX handle.
        /// Returns false without changing either side when the entity, handle, parameter, exposure, type, or range is
        /// incompatible.
        [[nodiscard]] bool SetVfxParameter(EntityId entity, const VfxParameterOverride& value);
        [[nodiscard]] std::vector<ScenePhysicsQueryHit> RayCast(const PhysicsRayQuery& query,
                                                                EntityId ignoredEntity = {}) const;
        void SetPresentationViewport(float width, float height, RuntimeUiInsets safeArea = {});
        void Play();
        void Pause(bool paused = true);
        void TogglePause();
        [[nodiscard]] bool Step(float fixedDeltaSeconds);
        void FixedUpdate(float deltaSeconds);
        void Update(float deltaSeconds);
        void ReplaceRuntime(SceneDefinition definition);
        void Stop() noexcept;

      private:
        class Impl;
        std::unique_ptr<Impl> m_Impl;
    };
} // namespace Keire
