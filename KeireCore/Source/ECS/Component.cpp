#include "Keire/ECS/Component.h"

#include "Keire/Animation/ProceduralMotion.h"
#include "Keire/ECS/Components/AnimatorComponent.h"
#include "Keire/ECS/Components/AudioComponents.h"
#include "Keire/ECS/Components/CameraComponent.h"
#include "Keire/ECS/Components/CharacterControllerComponent.h"
#include "Keire/ECS/Components/ColliderComponent.h"
#include "Keire/ECS/Components/DirectionalLightComponent.h"
#include "Keire/ECS/Components/JointComponents.h"
#include "Keire/ECS/Components/LightProbeVolumeComponent.h"
#include "Keire/ECS/Components/MeshRendererComponent.h"
#include "Keire/ECS/Components/PointLightComponent.h"
#include "Keire/ECS/Components/ReflectionProbeComponent.h"
#include "Keire/ECS/Components/RigidBodyComponent.h"
#include "Keire/ECS/Components/RuntimeUiComponents.h"
#include "Keire/ECS/Components/SpotLightComponent.h"
#include "Keire/ECS/Components/TransformComponent.h"
#include "Keire/ECS/Components/VfxEmitterComponent.h"
#include "Keire/ECS/Entity.h"
#include "KeireInternal/SceneState.h"

#include <algorithm>
#include <atomic>
#include <functional>
#include <ranges>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <unordered_set>
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
        bool Prepared = false;
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

    void Component::InvokePrepare()
    {
        if (!m_Impl->Prepared && !m_Impl->Destroyed)
        {
            Prepare();
            m_Impl->Prepared = true;
        }
    }

    void Component::InvokeAwake()
    {
        if (!m_Impl->Awakened && !m_Impl->Destroyed)
        {
            InvokePrepare();
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

    void Component::InvokeLateUpdate()
    {
        if (m_Impl->LifecycleActive && !m_Impl->Destroyed)
            LateUpdate();
    }

    void Component::InvokeAnimationEvent(const AnimationEventMessage& event)
    {
        if (m_Impl->LifecycleActive && !m_Impl->Destroyed)
            OnAnimationEvent(event);
    }

    void Component::InvokeProceduralMotionEvent(const ProceduralMotionEvent& event)
    {
        if (m_Impl->LifecycleActive && !m_Impl->Destroyed)
            OnProceduralMotionEvent(event);
    }

    void Component::InvokeAnimatorIk(const AnimationIkMessage& context)
    {
        if (m_Impl->LifecycleActive && !m_Impl->Destroyed)
            OnAnimatorIk(context);
    }

    void Component::InvokePhysicsContact(const PhysicsContactPhase phase, const PhysicsContactMessage& contact)
    {
        if (!m_Impl->LifecycleActive || m_Impl->Destroyed)
            return;
        if (contact.Trigger)
        {
            switch (phase)
            {
            case PhysicsContactPhase::Enter:
                OnTriggerEnter(contact);
                break;
            case PhysicsContactPhase::Stay:
                OnTriggerStay(contact);
                break;
            case PhysicsContactPhase::Exit:
                OnTriggerExit(contact);
                break;
            }
            return;
        }
        switch (phase)
        {
        case PhysicsContactPhase::Enter:
            OnCollisionEnter(contact);
            break;
        case PhysicsContactPhase::Stay:
            OnCollisionStay(contact);
            break;
        case PhysicsContactPhase::Exit:
            OnCollisionExit(contact);
            break;
        }
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
        std::atomic<std::uint64_t> Revision{0};
    };

    namespace
    {
        void ValidateComponentRegistration(const ComponentRegistration& registration)
        {
            if (!registration.Type || registration.Name.empty() || registration.Name.size() > 128 ||
                registration.SchemaVersion == 0 || !registration.Factory || !registration.Serialize ||
                !registration.Deserialize)
                throw std::invalid_argument("Component registration is incomplete or invalid.");
            if (std::ranges::any_of(registration.RequiredComponents,
                                    [&](const auto type) { return !type || type == registration.Type; }))
                throw std::invalid_argument(
                    "Component dependencies must be non-empty and cannot reference themselves.");
            auto dependencies = registration.RequiredComponents;
            std::ranges::sort(dependencies);
            if (std::ranges::adjacent_find(dependencies) != dependencies.end())
                throw std::invalid_argument("Component registration contains duplicate dependencies.");
        }

        void ValidateComponentDependencyGraph(
            const std::unordered_map<ComponentTypeId, ComponentRegistration>& registrations)
        {
            enum class VisitState : std::uint8_t
            {
                Visiting,
                Complete
            };

            std::unordered_map<ComponentTypeId, VisitState> visits;
            std::function<void(ComponentTypeId)> visit = [&](const ComponentTypeId type)
            {
                if (const auto existing = visits.find(type); existing != visits.end())
                {
                    if (existing->second == VisitState::Visiting)
                        throw std::invalid_argument("Component registration dependencies contain a cycle.");
                    return;
                }

                const auto registration = registrations.find(type);
                if (registration == registrations.end())
                    throw std::invalid_argument("Component registration batch leaves an unresolved dependency.");
                visits.emplace(type, VisitState::Visiting);
                for (const auto required : registration->second.RequiredComponents)
                    visit(required);
                visits[type] = VisitState::Complete;
            };

            for (const auto& [type, _] : registrations)
                visit(type);
        }
    } // namespace

    ComponentRegistry::ComponentRegistry() : m_Impl(std::make_unique<Impl>()) {}

    ComponentRegistry::~ComponentRegistry() = default;

    void ComponentRegistry::Register(ComponentRegistration registration)
    {
        m_Impl->RequireOwner("Register");
        ValidateComponentRegistration(registration);
        const auto [_, inserted] = m_Impl->Registrations.emplace(registration.Type, std::move(registration));
        if (!inserted)
            throw std::invalid_argument("A component registration already uses this stable type ID.");
        m_Impl->Revision.fetch_add(1, std::memory_order_release);
    }

    void ComponentRegistry::ReplaceBatch(const std::span<const ComponentTypeId> removals,
                                         std::vector<ComponentRegistration> registrations)
    {
        m_Impl->RequireOwner("ReplaceBatch");
        auto replacement = m_Impl->Registrations;
        std::unordered_set<ComponentTypeId> removed;
        for (const auto type : removals)
        {
            if (!type || !removed.insert(type).second)
                throw std::invalid_argument("Component registration removal set is invalid.");
            replacement.erase(type);
        }

        std::unordered_set<ComponentTypeId> inserted;
        for (const auto& registration : registrations)
        {
            ValidateComponentRegistration(registration);
            if (!inserted.insert(registration.Type).second)
                throw std::invalid_argument("Component registration batch contains a duplicate stable type ID.");
        }
        for (auto& registration : registrations)
            replacement.insert_or_assign(registration.Type, std::move(registration));

        ValidateComponentDependencyGraph(replacement);

        m_Impl->Registrations.swap(replacement);
        m_Impl->Revision.fetch_add(1, std::memory_order_release);
    }

    bool ComponentRegistry::Contains(const ComponentTypeId type) const noexcept
    {
        return m_Impl->Registrations.contains(type);
    }

    std::uint64_t ComponentRegistry::Revision() const noexcept
    {
        return m_Impl->Revision.load(std::memory_order_acquire);
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
        result->Register(CreateReflectionProbeComponentRegistration());
        result->Register(CreateLightProbeVolumeComponentRegistration());
        result->Register(CreateMeshRendererComponentRegistration());
        result->Register(CreateAnimatorComponentRegistration());
        result->Register(CreateColliderComponentRegistration());
        result->Register(CreateRigidBodyComponentRegistration());
        result->Register(CreateCharacterControllerComponentRegistration());
        result->Register(CreateFixedJointComponentRegistration());
        result->Register(CreateHingeJointComponentRegistration());
        result->Register(CreateDistanceJointComponentRegistration());
        result->Register(CreateSpringJointComponentRegistration());
        result->Register(CreateAudioSourceComponentRegistration());
        result->Register(CreateAudioReverbZoneComponentRegistration());
        result->Register(CreateAudioListenerComponentRegistration());
        result->Register(CreateVfxEmitterComponentRegistration());
        result->Register(CreateCanvasComponentRegistration());
        result->Register(CreateRectTransformComponentRegistration());
        result->Register(CreateUiTextComponentRegistration());
        result->Register(CreateUiImageComponentRegistration());
        result->Register(CreateUiButtonComponentRegistration());
        result->Register(CreateUiLayoutComponentRegistration());
        result->Register(CreateUiSliderComponentRegistration());
        result->Register(CreateUiToggleComponentRegistration());
        result->Register(CreateUiInputFieldComponentRegistration());
        result->Register(CreateUiScrollViewComponentRegistration());
        result->Register(CreateUiAccessibilityComponentRegistration());
        return result;
    }
} // namespace Keire
