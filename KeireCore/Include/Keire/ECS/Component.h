#pragma once

#include "Keire/Api.h"
#include "Keire/Assets/Asset.h"
#include "Keire/Math/Curves.h"
#include "Keire/Math/Math.h"
#include "Keire/Ref.h"

#include <compare>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace Keire
{
    struct ProceduralMotionEvent;
    class Entity;
    class Component;
    struct ManagedReferenceGraphDescriptor;

    namespace Detail
    {
        class SceneState;
    }

    class KEIRE_API EntityId final
    {
      public:
        constexpr EntityId() noexcept = default;
        explicit constexpr EntityId(AssetId value) noexcept : m_Value(value) {}

        [[nodiscard]] static EntityId Generate() { return EntityId(AssetId::Generate()); }
        [[nodiscard]] static EntityId Parse(std::string_view value) { return EntityId(AssetId::Parse(value)); }
        [[nodiscard]] std::string ToString() const { return m_Value.ToString(); }
        [[nodiscard]] constexpr const AssetId& Value() const noexcept { return m_Value; }
        [[nodiscard]] constexpr explicit operator bool() const noexcept { return static_cast<bool>(m_Value); }
        [[nodiscard]] auto operator<=>(const EntityId&) const noexcept = default;

      private:
        AssetId m_Value;
    };

    class KEIRE_API ComponentTypeId final
    {
      public:
        constexpr ComponentTypeId() noexcept = default;
        explicit constexpr ComponentTypeId(AssetId value) noexcept : m_Value(value) {}

        [[nodiscard]] static ComponentTypeId Parse(std::string_view value)
        {
            return ComponentTypeId(AssetId::Parse(value));
        }
        [[nodiscard]] std::string ToString() const { return m_Value.ToString(); }
        [[nodiscard]] constexpr const AssetId& Value() const noexcept { return m_Value; }
        [[nodiscard]] constexpr explicit operator bool() const noexcept { return static_cast<bool>(m_Value); }
        [[nodiscard]] auto operator<=>(const ComponentTypeId&) const noexcept = default;

      private:
        AssetId m_Value;
    };

    enum class ComponentPropertyKind : std::uint8_t
    {
        Boolean,
        Integer,
        Scalar,
        Text,
        Vector2,
        Vector3,
        Vector4,
        Quaternion,
        Color,
        Asset,
        Entity,
        Event,
        Curve,
        Gradient,
        ManagedReferenceGraph
    };

    enum class ManagedReferenceKind : std::uint8_t
    {
        None,
        Entity,
        Component,
        Behaviour,
        Asset,
        Prefab,
        SceneAsset,
        ScriptableObject
    };

    struct ComponentEventListener
    {
        bool Enabled = true;
        EntityId Target;
        ComponentTypeId Component;
        std::string Method;

        [[nodiscard]] bool operator==(const ComponentEventListener&) const = default;
    };

    struct ComponentEventValue
    {
        std::vector<ComponentEventListener> Listeners;

        [[nodiscard]] bool operator==(const ComponentEventValue&) const = default;
    };

    struct ComponentReferenceValue
    {
        EntityId Entity;
        ComponentTypeId Component;

        [[nodiscard]] bool operator==(const ComponentReferenceValue&) const = default;
    };

    using ComponentPropertyValue =
        std::variant<bool, std::int64_t, double, std::string, Vector2, Vector3, Vector4, Quaternion, Color, AssetId,
                     EntityId, ComponentEventValue, Curve1D, ColorGradient, ComponentReferenceValue>;
    using ComponentPropertyBag = std::map<std::string, ComponentPropertyValue, std::less<>>;

    struct ComponentProperty
    {
        std::string Key;
        std::string DisplayName;
        std::string Group;
        ComponentPropertyKind Kind = ComponentPropertyKind::Scalar;
        bool ReadOnly = false;
        std::optional<double> Minimum;
        std::optional<double> Maximum;
        double Step = 0.1;
        std::optional<AssetTypeId> ExpectedAssetType;
        std::string Tooltip;
        std::size_t EventArgumentCount = 0;
        std::string Header;
        bool Slider = false;
        std::uint32_t TextLines = 1;
        ManagedReferenceKind ReferenceKind = ManagedReferenceKind::None;
        std::string DeclaredManagedType;
        std::vector<ComponentTypeId> CompatibleComponentTypes;
        std::vector<std::string> CompatibleBehaviourTypes;
        std::shared_ptr<const ManagedReferenceGraphDescriptor> ReferenceGraph;
    };

    struct ComponentMethod
    {
        std::string Name;
        std::string DisplayName;
        std::vector<std::string> ParameterTypes;
    };

    struct AnimationEventMessage
    {
        std::string Name;
        float NormalizedTime = 0.0F;
        std::int32_t Integer = 0;
        float Scalar = 0.0F;
        std::string Text;
    };

    struct AnimationIkMessage
    {
        float LayerWeight = 1.0F;
    };

    enum class PhysicsContactPhase : std::uint8_t
    {
        Enter,
        Stay,
        Exit
    };

    struct PhysicsContactMessage
    {
        EntityId Other;
        Vector3 Point;
        Vector3 Normal;
        float Impulse = 0.0F;
        bool Trigger = false;
    };

    class KEIRE_API Component : public RefCounted
    {
      public:
        ~Component() override;

        [[nodiscard]] ComponentTypeId Type() const noexcept;
        [[nodiscard]] Entity Owner() const noexcept;
        [[nodiscard]] bool IsAttached() const noexcept;
        [[nodiscard]] bool Enabled() const noexcept;
        void SetEnabled(bool enabled);

      protected:
        explicit Component(ComponentTypeId type);

        virtual void Prepare() {}
        virtual void Awake() {}
        virtual void OnEnable() {}
        virtual void Start() {}
        virtual void FixedUpdate(float) {}
        virtual void Update(float) {}
        virtual void LateUpdate() {}
        virtual void OnAnimationEvent(const AnimationEventMessage&) {}
        virtual void OnProceduralMotionEvent(const ProceduralMotionEvent&) {}
        virtual void OnAnimatorIk(const AnimationIkMessage&) {}
        virtual void OnCollisionEnter(const PhysicsContactMessage&) {}
        virtual void OnCollisionStay(const PhysicsContactMessage&) {}
        virtual void OnCollisionExit(const PhysicsContactMessage&) {}
        virtual void OnTriggerEnter(const PhysicsContactMessage&) {}
        virtual void OnTriggerStay(const PhysicsContactMessage&) {}
        virtual void OnTriggerExit(const PhysicsContactMessage&) {}
        virtual void OnDisable() {}
        virtual void OnDestroy() {}

        void NotifyChanged();
        [[nodiscard]] Matrix4 OwnerWorldMatrix() const;

      private:
        friend class Detail::SceneState;
        void Attach(WeakRef<Detail::SceneState> state, EntityId owner);
        void Detach() noexcept;
        void InvokePrepare();
        void InvokeAwake();
        void InvokeEnable();
        void InvokeStart();
        void InvokeFixedUpdate(float deltaSeconds);
        void InvokeUpdate(float deltaSeconds);
        void InvokeLateUpdate();
        void InvokeAnimationEvent(const AnimationEventMessage& event);
        void InvokeProceduralMotionEvent(const ProceduralMotionEvent& event);
        void InvokeAnimatorIk(const AnimationIkMessage& context);
        void InvokePhysicsContact(PhysicsContactPhase phase, const PhysicsContactMessage& contact);
        void InvokeDisable();
        void InvokeDestroy();
        void ApplyEnabled(bool enabled) noexcept;
        [[nodiscard]] bool LifecycleActive() const noexcept;
        class Impl;
        std::unique_ptr<Impl> m_Impl;
    };

    struct ComponentRegistration
    {
        ComponentTypeId Type;
        std::string Name;
        std::string Category = "Scripts";
        std::uint32_t SchemaVersion = 1;
        std::int32_t ExecutionOrder = 0;
        bool AllowMultiple = false;
        bool Removable = true;
        std::vector<ComponentTypeId> RequiredComponents;
        std::vector<ComponentProperty> Properties;
        std::function<Ref<Component>()> Factory;
        std::function<ComponentPropertyBag(const Component&)> Serialize;
        std::function<void(Component&, const ComponentPropertyBag&, std::uint32_t)> Deserialize;
        std::function<ComponentPropertyBag(const ComponentPropertyBag&, std::uint32_t)> Migrate;
        std::shared_ptr<const std::vector<ComponentMethod>> Methods;
    };

    class KEIRE_API ComponentRegistry final : public RefCounted
    {
      public:
        ComponentRegistry();
        ~ComponentRegistry() override;

        ComponentRegistry(const ComponentRegistry&) = delete;
        ComponentRegistry& operator=(const ComponentRegistry&) = delete;

        void Register(ComponentRegistration registration);
        void ReplaceBatch(std::span<const ComponentTypeId> removals, std::vector<ComponentRegistration> registrations);
        [[nodiscard]] bool Contains(ComponentTypeId type) const noexcept;
        [[nodiscard]] static bool IsReservedType(ComponentTypeId type) noexcept;
        [[nodiscard]] std::uint64_t Revision() const noexcept;
        [[nodiscard]] std::optional<ComponentRegistration> Find(ComponentTypeId type) const;
        [[nodiscard]] std::vector<ComponentRegistration> Registrations() const;
        [[nodiscard]] static Ref<ComponentRegistry> CreateDefault();

      private:
        class Impl;
        std::unique_ptr<Impl> m_Impl;
    };
} // namespace Keire

template <> struct std::hash<Keire::EntityId>
{
    std::size_t operator()(const Keire::EntityId& value) const noexcept
    {
        return std::hash<Keire::AssetId>{}(value.Value());
    }
};

template <> struct std::hash<Keire::ComponentTypeId>
{
    std::size_t operator()(const Keire::ComponentTypeId& value) const noexcept
    {
        return std::hash<Keire::AssetId>{}(value.Value());
    }
};
