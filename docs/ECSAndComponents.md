# ECS And Components

Kéire exposes a Unity-style entity/component model while keeping EnTT and GLM private. Engine and SDK code use stable
`EntityId` and `ComponentTypeId` values, `Entity` handles, reference-counted `Component` objects, and Kéire math types.
No registry entity, GLM vector, or dependency header crosses the public boundary.

## Ownership And Registration

Create a `ComponentRegistry` on the application owner thread, register project components, and pass it through
`SceneSystemSpecification::Components`. `SceneSystem::Components()` returns the exact registry used by loaded scenes.
The default registry contains `TransformComponent` and `DirectionalLightComponent`.

A `ComponentRegistration` owns the stable type ID, display metadata, schema version, factory, serialization and
deserialization callbacks, optional migration callback, dependencies, multiplicity, removability, and execution order.
Registration rejects incomplete records and duplicate type IDs. One instance per entity is the default; a registration
must deliberately opt into multiple instances.

```cpp
class HealthComponent final : public Keire::Component
{
  public:
    HealthComponent() : Component(StaticType()) {}

    [[nodiscard]] static constexpr Keire::ComponentTypeId StaticType() noexcept
    {
        return Keire::ComponentTypeId(Keire::AssetId(0x4845414c54480001ULL, 0x434f4d504f4e454eULL));
    }

    float Value = 100.0F;
};
```

Component classes derive from `Component` and are created through `CreateRef`. Retained component references become
inert after removal or scene destruction: `IsAttached()` becomes false and `Owner()` returns an empty entity. Entity
handles are weak stable-ID views and likewise become false after their entity or scene is destroyed.

## Entities And Queries

Every entity receives one mandatory, non-removable Transform. Use typed `AddComponent`, `GetComponent`,
`HasComponent`, and `RemoveComponent` helpers for registered C++ component types. `Scene::Query<T>()` returns matching
entities in deterministic hierarchy order. Structural changes requested from a component lifecycle callback are queued
until traversal reaches the next safe boundary.

Every entity also owns one layer index in the inclusive `0..31` range. `Entity::Layer()` and `Entity::SetLayer()` are
the canonical native API; `EntityLayerCount`, `IsValidEntityLayer`, and `EntityLayerBit` support validated layer-mask
work. Collider and Character Controller filtering consumes the owning entity's layer, so those components cannot drift
onto a different layer. Their legacy single-bit layer setters remain source-compatible and forward to the entity.

## Lifecycle

Play mode invokes `Awake`, `OnEnable`, `Start`, `FixedUpdate`, `Update`, `OnDisable`, and `OnDestroy` in deterministic
component execution order. `Awake` and `Start` run once. Entity activation includes every ancestor; disabling a parent
disables active descendant components. Pausing a runtime session skips normal updates without invoking disable. Step
runs exactly one fixed update while paused.

Callback exceptions fault and pause the runtime session, preserve the authored scene, and expose a diagnostic naming
the failed phase. Stop closes the runtime clone, invokes teardown, and discards every play-mode mutation.

## Transform Contract

Transform stores local position, a normalized quaternion rotation, and scale. Euler degrees are an editor convenience
converted through `Keire::Math`. World matrices are cached and dirtied through descendants when a local or hierarchy
value changes. Reparenting rejects cycles and preserves the world transform by default; failure restores the original
hierarchy transactionally. Every local scale axis must be finite and have a magnitude of at least
`TransformComponent::MinimumScaleMagnitude`; setters reject an invalid scale before mutation so world transforms stay
invertible. Negative axes remain supported for intentional mirroring.

Directional Light enabled state, linear color, intensity, optional color temperature, shadow mode, strength, and bias
are authorable and serializable. The built-in renderer consumes transform orientation, color, intensity, and temperature
for one ambient-plus-Lambert light. Shadow mode, strength, and bias remain serialized authoring data until shadow passes land.

## Scene Serialization

Scene schema v4 stores entities with stable IDs, a validated layer index, and component records containing stable type
ID, schema version, enabled state, and bounded JSON data. Schemas v1-v3 remain readable. Legacy inline transforms are
migrated automatically, and a v2/v3 entity without an entity layer inherits the first valid Collider or Character
Controller collision layer before falling back to Default. Saves are always canonical v4. Unregistered records remain
attached as Missing Components and round-trip their type, version, enabled state, and complete data; cooking can reject
unresolved types without destroying editor content.
