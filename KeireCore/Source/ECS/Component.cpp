#include "Keire/ECS/Component.h"

#include "Keire/ECS/Components/CameraComponent.h"
#include "Keire/ECS/Components/DirectionalLightComponent.h"
#include "Keire/ECS/Components/MeshRendererComponent.h"
#include "Keire/ECS/Components/PointLightComponent.h"
#include "Keire/ECS/Components/SpotLightComponent.h"
#include "Keire/ECS/Components/TransformComponent.h"
#include "Keire/ECS/Entity.h"
#include "KeireInternal/SceneState.h"

#include <algorithm>
#include <ranges>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <utility>

namespace Keire
{
    class Component::Impl final
    {
      public:
        explicit Impl(const ComponentTypeId type) : Type(type) {}

        ComponentTypeId Type;
        WeakRef<Detail::SceneState> State;
        EntityId Owner;
        bool Enabled = true;
        bool Awakened = false;
        bool Started = false;
        bool LifecycleActive = false;
        bool Destroyed = false;
    };

    Component::Component(const ComponentTypeId type) : m_Impl(std::make_unique<Impl>(type))
    {
        if (!type)
            throw std::invalid_argument("Component type ID must not be empty.");
    }

    Component::~Component() = default;

    ComponentTypeId Component::Type() const noexcept { return m_Impl->Type; }

    Entity Component::Owner() const noexcept
    {
        const auto state = m_Impl->State.Lock();
        return state && state->Contains(m_Impl->Owner) ? Entity(m_Impl->State, m_Impl->Owner) : Entity{};
    }

    bool Component::IsAttached() const noexcept
    {
        const auto state = m_Impl->State.Lock();
        return state && state->Contains(m_Impl->Owner);
    }

    bool Component::Enabled() const noexcept { return m_Impl->Enabled; }

    void Component::SetEnabled(const bool enabled)
    {
        if (const auto state = m_Impl->State.Lock())
            state->SetComponentEnabled(*this, enabled);
    }

    void Component::NotifyChanged()
    {
        if (const auto state = m_Impl->State.Lock())
            state->ComponentChanged(*this);
    }

    Matrix4 Component::OwnerWorldMatrix() const
    {
        const auto state = m_Impl->State.Lock();
        return state && state->Contains(m_Impl->Owner) ? state->WorldMatrix(m_Impl->Owner) : Matrix4{};
    }

    void Component::Attach(WeakRef<Detail::SceneState> state, const EntityId owner)
    {
        if (!m_Impl->State.Expired())
            throw std::logic_error("Component is already attached to an entity.");
        m_Impl->State = std::move(state);
        m_Impl->Owner = owner;
    }

    void Component::Detach() noexcept
    {
        m_Impl->State.Reset();
        m_Impl->Owner = {};
    }

    void Component::InvokeAwake()
    {
        if (!m_Impl->Awakened && !m_Impl->Destroyed)
        {
            Awake();
            m_Impl->Awakened = true;
        }
    }

    void Component::InvokeEnable()
    {
        if (!m_Impl->LifecycleActive && !m_Impl->Destroyed)
        {
            OnEnable();
            m_Impl->LifecycleActive = true;
        }
    }

    void Component::InvokeStart()
    {
        if (!m_Impl->Started && !m_Impl->Destroyed)
        {
            Start();
            m_Impl->Started = true;
        }
    }

    void Component::InvokeFixedUpdate(const float deltaSeconds)
    {
        if (m_Impl->LifecycleActive && !m_Impl->Destroyed)
            FixedUpdate(deltaSeconds);
    }

    void Component::InvokeUpdate(const float deltaSeconds)
    {
        if (m_Impl->LifecycleActive && !m_Impl->Destroyed)
            Update(deltaSeconds);
    }

    void Component::InvokeDisable()
    {
        if (m_Impl->LifecycleActive && !m_Impl->Destroyed)
        {
            OnDisable();
            m_Impl->LifecycleActive = false;
        }
    }

    void Component::InvokeDestroy()
    {
        if (m_Impl->Destroyed)
            return;
        InvokeDisable();
        OnDestroy();
        m_Impl->Destroyed = true;
        Detach();
    }

    void Component::ApplyEnabled(const bool enabled) noexcept { m_Impl->Enabled = enabled; }

    bool Component::LifecycleActive() const noexcept { return m_Impl->LifecycleActive; }

    class ComponentRegistry::Impl final
    {
      public:
        void RequireOwner(const char* operation) const
        {
            if (std::this_thread::get_id() != OwnerThread)
                throw std::logic_error(std::string("ComponentRegistry::") + operation +
                                       " must run on the owner thread.");
        }

        std::thread::id OwnerThread = std::this_thread::get_id();
        std::unordered_map<ComponentTypeId, ComponentRegistration> Registrations;
    };

    ComponentRegistry::ComponentRegistry() : m_Impl(std::make_unique<Impl>()) {}

    ComponentRegistry::~ComponentRegistry() = default;

    void ComponentRegistry::Register(ComponentRegistration registration)
    {
        m_Impl->RequireOwner("Register");
        if (!registration.Type || registration.Name.empty() || registration.Name.size() > 128 ||
            registration.SchemaVersion == 0 || !registration.Factory || !registration.Serialize ||
            !registration.Deserialize)
            throw std::invalid_argument("Component registration is incomplete or invalid.");
        if (std::ranges::any_of(registration.RequiredComponents,
                                [&](const auto type) { return !type || type == registration.Type; }))
            throw std::invalid_argument("Component dependencies must be non-empty and cannot reference themselves.");
        const auto [_, inserted] = m_Impl->Registrations.emplace(registration.Type, std::move(registration));
        if (!inserted)
            throw std::invalid_argument("A component registration already uses this stable type ID.");
    }

    bool ComponentRegistry::Contains(const ComponentTypeId type) const noexcept
    {
        return m_Impl->Registrations.contains(type);
    }

    std::optional<ComponentRegistration> ComponentRegistry::Find(const ComponentTypeId type) const
    {
        m_Impl->RequireOwner("Find");
        const auto found = m_Impl->Registrations.find(type);
        return found == m_Impl->Registrations.end() ? std::nullopt
                                                    : std::optional<ComponentRegistration>(found->second);
    }

    std::vector<ComponentRegistration> ComponentRegistry::Registrations() const
    {
        m_Impl->RequireOwner("Registrations");
        std::vector<ComponentRegistration> result;
        result.reserve(m_Impl->Registrations.size());
        for (const auto& [_, registration] : m_Impl->Registrations)
            result.push_back(registration);
        std::ranges::sort(result, {}, &ComponentRegistration::Name);
        return result;
    }

    Ref<ComponentRegistry> ComponentRegistry::CreateDefault()
    {
        auto result = CreateRef<ComponentRegistry>();
        result->Register(CreateTransformComponentRegistration());
        result->Register(CreateCameraComponentRegistration());
        result->Register(CreateDirectionalLightComponentRegistration());
        result->Register(CreatePointLightComponentRegistration());
        result->Register(CreateSpotLightComponentRegistration());
        result->Register(CreateMeshRendererComponentRegistration());
        return result;
    }
} // namespace Keire
