#pragma once

#include "Keire/ECS/Entity.h"
#include "Keire/Scenes/SceneAsset.h"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Keire
{
    struct SceneHierarchySnapshot;
}

namespace Keire::Detail
{
    class SceneState final : public RefCounted
    {
      public:
        SceneState(AssetId asset, SceneDefinition definition, Ref<ComponentRegistry> components);
        ~SceneState() override;

        void Initialize(WeakRef<SceneState> self);

        void RequireOwner(const char* operation) const;
        [[nodiscard]] AssetId Asset() const noexcept;
        [[nodiscard]] bool IsOpen() const noexcept;
        [[nodiscard]] bool Dirty() const noexcept;
        void MarkDirty() noexcept;
        void MarkSaved() noexcept;
        [[nodiscard]] std::string Name() const;
        void SetName(std::string name);

        [[nodiscard]] bool Contains(EntityId id) const noexcept;
        [[nodiscard]] std::size_t Count() const noexcept;
        [[nodiscard]] std::optional<SceneObjectDefinition> SnapshotObject(EntityId id) const;
        [[nodiscard]] SceneHierarchySnapshot HierarchySnapshot() const;
        [[nodiscard]] SceneDefinition Snapshot() const;
        [[nodiscard]] LightingBakeSettings LightingBakeConfiguration() const;
        void SetLightingBakeConfiguration(LightingBakeSettings settings);
        [[nodiscard]] AssetId BakedLighting() const;
        void SetBakedLighting(AssetId asset);
        [[nodiscard]] std::vector<Entity> Entities() const;
        [[nodiscard]] Entity Find(EntityId id) const noexcept;
        [[nodiscard]] Entity Create(std::string name, EntityId parent = {});
        [[nodiscard]] Entity Duplicate(EntityId id);
        [[nodiscard]] bool Destroy(EntityId id);
        [[nodiscard]] std::vector<Entity> Query(ComponentTypeId type) const;
        [[nodiscard]] std::string EntityName(EntityId id) const;
        void SetEntityName(EntityId id, std::string name);
        [[nodiscard]] std::uint32_t EntityLayer(EntityId id) const;
        void SetEntityLayer(EntityId id, std::uint32_t layer);
        [[nodiscard]] bool ActiveSelf(EntityId id) const;
        [[nodiscard]] bool ActiveInHierarchy(EntityId id) const;
        void SetActive(EntityId id, bool active);
        [[nodiscard]] Entity Parent(EntityId id) const noexcept;
        [[nodiscard]] std::vector<Entity> Children(EntityId id) const;
        void SetParent(EntityId id, EntityId parent, bool preserveWorldTransform);
        void Move(EntityId id, EntityId parent, EntityId beforeSibling, bool preserveWorldTransform);

        [[nodiscard]] Ref<Component> AddComponent(EntityId id, ComponentTypeId type);
        [[nodiscard]] Ref<Component> GetComponent(EntityId id, ComponentTypeId type) const noexcept;
        [[nodiscard]] std::vector<Ref<Component>> GetComponents(EntityId id, ComponentTypeId type = {}) const;
        [[nodiscard]] bool RemoveComponent(EntityId id, ComponentTypeId type);
        void SetComponentEnabled(Component& component, bool enabled);
        void ComponentChanged(const Component& component);
        [[nodiscard]] Matrix4 WorldMatrix(EntityId id) const;

        void BeginPlay();
        void FixedUpdate(float deltaSeconds);
        void Update(float deltaSeconds);
        void LateUpdate();
        void DispatchAnimationEvent(EntityId entity, const AnimationEventMessage& event);
        void DispatchAnimatorIk(EntityId entity, const AnimationIkMessage& context);
        void DispatchPhysicsContact(EntityId entity, PhysicsContactPhase phase, const PhysicsContactMessage& contact);
        void EndPlay() noexcept;
        void FlushDeferred();
        void Close() noexcept;
        [[nodiscard]] Ref<ComponentRegistry> Components() const noexcept;

      private:
        void SynchronizeEntityLayer(EntityId id);
        class Impl;
        std::unique_ptr<Impl> m_Impl;
    };
} // namespace Keire::Detail
