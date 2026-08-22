#pragma once

#include "Keire/ECS/Component.h"
#include "Keire/Scripting/ScriptSystem.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace Keire::Detail
{
    struct ManagedBehaviourComponentCallbacks final
    {
        std::function<ManagedBehaviourInstanceId(ComponentTypeId, std::string_view, Entity)> Create;
        std::function<void(ManagedBehaviourInstanceId, ManagedBehaviourCallback, float)> Invoke;
        std::function<void(ManagedBehaviourInstanceId, const AnimationEventMessage&)> AnimationEvent;
        std::function<void(ManagedBehaviourInstanceId, const ProceduralMotionEvent&)> ProceduralMotionEvent;
        std::function<void(ManagedBehaviourInstanceId, PhysicsContactPhase, const PhysicsContactMessage&)>
            PhysicsContact;
        std::function<void(ManagedBehaviourInstanceId)> Destroy;
        std::function<std::optional<std::string>(ManagedBehaviourInstanceId)> CaptureState;
        std::function<void(ManagedBehaviourInstanceId, std::string_view)> RestoreState;
    };

    class ManagedBehaviourComponent final : public Component
    {
      public:
        ManagedBehaviourComponent(ComponentTypeId componentType, std::string managedType,
                                  std::weak_ptr<ManagedBehaviourComponentCallbacks> callbacks);

        [[nodiscard]] std::string SerializedState() const;
        void SetSerializedState(std::string state);

      protected:
        void Prepare() override;
        void Awake() override;
        void OnEnable() override;
        void Start() override;
        void FixedUpdate(float deltaSeconds) override;
        void Update(float deltaSeconds) override;
        void LateUpdate() override;
        void OnAnimationEvent(const AnimationEventMessage& event) override;
        void OnProceduralMotionEvent(const ProceduralMotionEvent& event) override;
        void OnAnimatorIk(const AnimationIkMessage& context) override;
        void OnCollisionEnter(const PhysicsContactMessage& contact) override;
        void OnCollisionStay(const PhysicsContactMessage& contact) override;
        void OnCollisionExit(const PhysicsContactMessage& contact) override;
        void OnTriggerEnter(const PhysicsContactMessage& contact) override;
        void OnTriggerStay(const PhysicsContactMessage& contact) override;
        void OnTriggerExit(const PhysicsContactMessage& contact) override;
        void OnDisable() override;
        void OnDestroy() override;

      private:
        void Invoke(ManagedBehaviourCallback callback, float deltaSeconds = 0.0F);
        void InvokeContact(PhysicsContactPhase phase, const PhysicsContactMessage& contact);

        std::string m_ManagedType;
        std::weak_ptr<ManagedBehaviourComponentCallbacks> m_Callbacks;
        ManagedBehaviourInstanceId m_Instance;
        std::string m_State = "{\"Version\":1,\"Fields\":[]}";
    };
} // namespace Keire::Detail
