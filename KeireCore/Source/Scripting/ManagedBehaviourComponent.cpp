#include "KeireInternal/Scripting/ManagedBehaviourComponent.h"

#include "Keire/ECS/Entity.h"

#include <utility>

namespace Keire::Detail
{
    ManagedBehaviourComponent::ManagedBehaviourComponent(const ComponentTypeId componentType, std::string managedType,
                                                         std::weak_ptr<ManagedBehaviourComponentCallbacks> callbacks)
        : Component(componentType), m_ManagedType(std::move(managedType)), m_Callbacks(std::move(callbacks))
    {
    }

    void ManagedBehaviourComponent::Prepare()
    {
        const auto callbacks = m_Callbacks.lock();
        if (!callbacks || !callbacks->Create)
            return;
        m_Instance = callbacks->Create(Type(), m_ManagedType, Owner());
        if (!m_Instance)
            return;
        if (!m_State.empty() && callbacks->RestoreState)
            callbacks->RestoreState(m_Instance, m_State);
    }

    void ManagedBehaviourComponent::Awake()
    {
        const auto callbacks = m_Callbacks.lock();
        if (callbacks && m_Instance && callbacks->Invoke)
            callbacks->Invoke(m_Instance, ManagedBehaviourCallback::Awake, 0.0F);
    }

    void ManagedBehaviourComponent::OnEnable() { Invoke(ManagedBehaviourCallback::Enable); }
    void ManagedBehaviourComponent::Start() { Invoke(ManagedBehaviourCallback::Start); }
    void ManagedBehaviourComponent::FixedUpdate(const float deltaSeconds)
    {
        Invoke(ManagedBehaviourCallback::FixedUpdate, deltaSeconds);
    }
    void ManagedBehaviourComponent::Update(const float deltaSeconds)
    {
        Invoke(ManagedBehaviourCallback::Update, deltaSeconds);
    }
    void ManagedBehaviourComponent::LateUpdate() { Invoke(ManagedBehaviourCallback::LateUpdate); }

    void ManagedBehaviourComponent::OnAnimationEvent(const AnimationEventMessage& event)
    {
        const auto callbacks = m_Callbacks.lock();
        if (callbacks && m_Instance && callbacks->AnimationEvent)
            callbacks->AnimationEvent(m_Instance, event);
    }

    void ManagedBehaviourComponent::OnProceduralMotionEvent(const ProceduralMotionEvent& event)
    {
        const auto callbacks = m_Callbacks.lock();
        if (callbacks && m_Instance && callbacks->ProceduralMotionEvent)
            callbacks->ProceduralMotionEvent(m_Instance, event);
    }

    void ManagedBehaviourComponent::OnAnimatorIk(const AnimationIkMessage& context)
    {
        Invoke(ManagedBehaviourCallback::AnimatorIk, context.LayerWeight);
    }

    void ManagedBehaviourComponent::OnCollisionEnter(const PhysicsContactMessage& contact)
    {
        InvokeContact(PhysicsContactPhase::Enter, contact);
    }

    void ManagedBehaviourComponent::OnCollisionStay(const PhysicsContactMessage& contact)
    {
        InvokeContact(PhysicsContactPhase::Stay, contact);
    }

    void ManagedBehaviourComponent::OnCollisionExit(const PhysicsContactMessage& contact)
    {
        InvokeContact(PhysicsContactPhase::Exit, contact);
    }

    void ManagedBehaviourComponent::OnTriggerEnter(const PhysicsContactMessage& contact)
    {
        InvokeContact(PhysicsContactPhase::Enter, contact);
    }

    void ManagedBehaviourComponent::OnTriggerStay(const PhysicsContactMessage& contact)
    {
        InvokeContact(PhysicsContactPhase::Stay, contact);
    }

    void ManagedBehaviourComponent::OnTriggerExit(const PhysicsContactMessage& contact)
    {
        InvokeContact(PhysicsContactPhase::Exit, contact);
    }

    void ManagedBehaviourComponent::OnDisable() { Invoke(ManagedBehaviourCallback::Disable); }

    void ManagedBehaviourComponent::OnDestroy()
    {
        const auto instance = std::exchange(m_Instance, {});
        const auto callbacks = m_Callbacks.lock();
        if (callbacks && instance && callbacks->Destroy)
            callbacks->Destroy(instance);
    }

    void ManagedBehaviourComponent::Invoke(const ManagedBehaviourCallback callback, const float deltaSeconds)
    {
        const auto callbacks = m_Callbacks.lock();
        if (callbacks && m_Instance && callbacks->Invoke)
            callbacks->Invoke(m_Instance, callback, deltaSeconds);
    }

    void ManagedBehaviourComponent::InvokeContact(const PhysicsContactPhase phase, const PhysicsContactMessage& contact)
    {
        const auto callbacks = m_Callbacks.lock();
        if (callbacks && m_Instance && callbacks->PhysicsContact)
            callbacks->PhysicsContact(m_Instance, phase, contact);
    }

    std::string ManagedBehaviourComponent::SerializedState() const
    {
        const auto callbacks = m_Callbacks.lock();
        if (callbacks && m_Instance && callbacks->CaptureState)
            if (auto state = callbacks->CaptureState(m_Instance))
                const_cast<ManagedBehaviourComponent&>(*this).m_State = std::move(*state);
        return m_State;
    }

    void ManagedBehaviourComponent::SetSerializedState(std::string state)
    {
        m_State = std::move(state);
        const auto callbacks = m_Callbacks.lock();
        if (callbacks && m_Instance && callbacks->RestoreState)
            callbacks->RestoreState(m_Instance, m_State);
    }
} // namespace Keire::Detail
