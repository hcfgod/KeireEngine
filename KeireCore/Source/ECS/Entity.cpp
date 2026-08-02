#include "Keire/ECS/Entity.h"

#include "KeireInternal/SceneState.h"

#include <stdexcept>
#include <utility>

namespace Keire
{
    Entity::Entity(WeakRef<Detail::SceneState> state, const EntityId id) noexcept : m_State(std::move(state)), m_Id(id)
    {
    }

    Entity::operator bool() const noexcept
    {
        const auto state = m_State.Lock();
        return state && state->Contains(m_Id);
    }

    std::uint64_t Entity::World() const noexcept
    {
        const auto state = m_State.Lock();
        return state ? static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(state.Get())) : 0;
    }

    Entity Entity::Resolve(const EntityId id) const noexcept
    {
        const auto state = m_State.Lock();
        return state ? state->Find(id) : Entity{};
    }

    std::string Entity::Name() const
    {
        const auto state = m_State.Lock();
        if (!state)
            throw std::logic_error("Entity::Name cannot inspect a stale entity.");
        return state->EntityName(m_Id);
    }

    void Entity::SetName(std::string name)
    {
        const auto state = m_State.Lock();
        if (!state)
            throw std::logic_error("Entity::SetName cannot mutate a stale entity.");
        state->SetEntityName(m_Id, std::move(name));
    }

    std::uint32_t Entity::Layer() const
    {
        const auto state = m_State.Lock();
        if (!state)
            throw std::logic_error("Entity::Layer cannot inspect a stale entity.");
        return state->EntityLayer(m_Id);
    }

    void Entity::SetLayer(const std::uint32_t layer)
    {
        const auto state = m_State.Lock();
        if (!state)
            throw std::logic_error("Entity::SetLayer cannot mutate a stale entity.");
        state->SetEntityLayer(m_Id, layer);
    }

    bool Entity::ActiveSelf() const
    {
        const auto state = m_State.Lock();
        return state && state->ActiveSelf(m_Id);
    }

    bool Entity::ActiveInHierarchy() const
    {
        const auto state = m_State.Lock();
        return state && state->ActiveInHierarchy(m_Id);
    }

    void Entity::SetActive(const bool active)
    {
        const auto state = m_State.Lock();
        if (!state)
            throw std::logic_error("Entity::SetActive cannot mutate a stale entity.");
        state->SetActive(m_Id, active);
    }

    Entity Entity::Parent() const noexcept
    {
        const auto state = m_State.Lock();
        return state ? state->Parent(m_Id) : Entity{};
    }

    std::vector<Entity> Entity::Children() const
    {
        const auto state = m_State.Lock();
        return state ? state->Children(m_Id) : std::vector<Entity>{};
    }

    void Entity::SetParent(Entity parent, const bool preserveWorldTransform)
    {
        const auto state = m_State.Lock();
        if (!state)
            throw std::logic_error("Entity::SetParent cannot mutate a stale entity.");
        if (parent && parent.m_State.Lock() != state)
            throw std::invalid_argument("Entity parent must belong to the same scene.");
        state->SetParent(m_Id, parent.m_Id, preserveWorldTransform);
    }

    Ref<Component> Entity::AddComponent(const ComponentTypeId type)
    {
        const auto state = m_State.Lock();
        if (!state)
            throw std::logic_error("Entity::AddComponent cannot mutate a stale entity.");
        return state->AddComponent(m_Id, type);
    }

    Ref<Component> Entity::GetComponent(const ComponentTypeId type) const noexcept
    {
        const auto state = m_State.Lock();
        return state ? state->GetComponent(m_Id, type) : Ref<Component>{};
    }

    std::vector<Ref<Component>> Entity::GetComponents(const ComponentTypeId type) const
    {
        const auto state = m_State.Lock();
        return state ? state->GetComponents(m_Id, type) : std::vector<Ref<Component>>{};
    }

    bool Entity::HasComponent(const ComponentTypeId type) const noexcept
    {
        return static_cast<bool>(GetComponent(type));
    }

    bool Entity::RemoveComponent(const ComponentTypeId type)
    {
        const auto state = m_State.Lock();
        return state && state->RemoveComponent(m_Id, type);
    }

    Entity Entity::Clone()
    {
        const auto state = m_State.Lock();
        if (!state)
            throw std::logic_error("Entity::Clone cannot duplicate a stale entity.");
        return state->Duplicate(m_Id);
    }

    bool Entity::Destroy()
    {
        const auto state = m_State.Lock();
        return state && state->Destroy(m_Id);
    }
} // namespace Keire
