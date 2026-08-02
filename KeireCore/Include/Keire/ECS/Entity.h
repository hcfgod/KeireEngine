#pragma once

#include "Keire/Api.h"
#include "Keire/ECS/Component.h"
#include "Keire/ECS/EntityLayer.h"

#include <concepts>
#include <string>
#include <string_view>
#include <vector>

namespace Keire
{
    namespace Detail
    {
        class SceneState;
    }

    class KEIRE_API Entity final
    {
      public:
        Entity() noexcept = default;

        [[nodiscard]] explicit operator bool() const noexcept;
        [[nodiscard]] EntityId Id() const noexcept { return m_Id; }
        [[nodiscard]] std::uint64_t World() const noexcept;
        [[nodiscard]] Entity Resolve(EntityId id) const noexcept;
        [[nodiscard]] std::string Name() const;
        void SetName(std::string name);
        [[nodiscard]] std::uint32_t Layer() const;
        void SetLayer(std::uint32_t layer);
        [[nodiscard]] bool ActiveSelf() const;
        [[nodiscard]] bool ActiveInHierarchy() const;
        void SetActive(bool active);
        [[nodiscard]] Entity Parent() const noexcept;
        [[nodiscard]] std::vector<Entity> Children() const;
        void SetParent(Entity parent = {}, bool preserveWorldTransform = true);

        [[nodiscard]] Ref<Component> AddComponent(ComponentTypeId type);
        [[nodiscard]] Ref<Component> GetComponent(ComponentTypeId type) const noexcept;
        [[nodiscard]] std::vector<Ref<Component>> GetComponents(ComponentTypeId type = {}) const;
        [[nodiscard]] bool HasComponent(ComponentTypeId type) const noexcept;
        [[nodiscard]] bool RemoveComponent(ComponentTypeId type);
        [[nodiscard]] Entity Clone();
        [[nodiscard]] bool Destroy();

        template <std::derived_from<Component> T> [[nodiscard]] Ref<T> AddComponent()
        {
            return DynamicRefCast<T>(AddComponent(T::StaticType()));
        }

        template <std::derived_from<Component> T> [[nodiscard]] Ref<T> GetComponent() const noexcept
        {
            return DynamicRefCast<T>(GetComponent(T::StaticType()));
        }

        template <std::derived_from<Component> T> [[nodiscard]] bool HasComponent() const noexcept
        {
            return HasComponent(T::StaticType());
        }

        template <std::derived_from<Component> T> [[nodiscard]] bool RemoveComponent()
        {
            return RemoveComponent(T::StaticType());
        }

        [[nodiscard]] bool operator==(const Entity& other) const noexcept
        {
            return m_Id == other.m_Id && m_State.Lock() == other.m_State.Lock();
        }

      private:
        friend class Scene;
        friend class Component;
        friend class Detail::SceneState;
        Entity(WeakRef<Detail::SceneState> state, EntityId id) noexcept;

        WeakRef<Detail::SceneState> m_State;
        EntityId m_Id;
    };
} // namespace Keire
