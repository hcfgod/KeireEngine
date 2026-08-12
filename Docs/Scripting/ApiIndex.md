# Managed API Index

This index is a discovery map, not a replacement for the workflow guides or source declarations.

## Core Types

| Type | Purpose | Guide |
| --- | --- | --- |
| `Behaviour` | Entity-attached gameplay component and lifecycle surface | [Lifecycle](BehavioursAndLifecycle.md) |
| `Entity`, `EntityId` | Runtime scene object identity and operations | [Entities](EntitiesComponentsAndTransforms.md) |
| `ComponentTypeId` | Stable component type identity | [Entities](EntitiesComponentsAndTransforms.md) |
| `ComponentHandle`, `ComponentHandle<T>` | Presence, enabled-state, and removal views | [Entities](EntitiesComponentsAndTransforms.md) |
| `TransformHandle` | Local and world transform mutation plus direction vectors | [Entities](EntitiesComponentsAndTransforms.md) |
| `CharacterControllerHandle`, `CharacterControllerState` | Collision-resolved movement and grounded-state access | [Gameplay Services](GameplayServices.md) |
| `RigidBodyHandle`, `RigidBodyProperties` | Runtime body state, forces, and impulses | [Gameplay Services](GameplayServices.md) |
| `AssetId` | Stable untyped asset identity | [Assets](AssetsAndScriptableObjects.md) |
| `AssetReference<T>` | Typed serialized asset identity | [Assets](AssetsAndScriptableObjects.md) |
| `ScriptableObject` | Managed data base type and transient clone API | [Assets](AssetsAndScriptableObjects.md) |
| `Vector2`, `Vector3`, `Vector4` | Engine math values | [Gameplay Services](GameplayServices.md) |
| `Quaternion`, `Color` | Rotation and color values | [Gameplay Services](GameplayServices.md) |

`Behaviour.Enabled` and `Entity.GetComponentHandle<T>().Enabled` read and write the same native component state.

## Behaviour Callbacks

```text
Awake
OnEnable
Start
FixedUpdate
Update
LateUpdate
OnDisable
OnDestroy
OnCollisionEnter / OnCollisionStay / OnCollisionExit
OnTriggerEnter / OnTriggerStay / OnTriggerExit
OnAnimationEvent
OnAnimatorIk
OnBeforeReload
OnAfterReload
```

`OnAnimatorIk` runs after pose sampling and before named IK goals are solved for the frame.

Callback payloads:

| Type | Members |
| --- | --- |
| `CollisionContact` | `Other`, `Point`, `Normal`, `Impulse`, `Trigger` |
| `AnimationEvent` | `Name`, `NormalizedTime`, `Integer`, `Scalar`, `Text` |
| `AnimationIkContext` | `LayerWeight` |

## Serialization Attributes

| Attribute | Target |
| --- | --- |
| `SerializeField` | Field, or eligible managed-data property |
| `HotReloadState` | `Behaviour` field |
| `SerializableType` | Nested managed-data class or struct |
| `HideInInspector` | Field or property |
| `Range` | Numeric field or property |
| `Tooltip` | Field or property |
| `InspectorGroup` | Field or property |
| `ReadOnlyInInspector` | Managed-data field or property |
| `FormerlySerializedAs` | Field or property; multiple allowed |
| `StableFieldId` | Serialized field or property |
| `RequireComponent` | `Behaviour` type; multiple allowed |
| `ExecutionOrder` | `Behaviour` type |
| `StableComponentId` | Managed component type |
| `StableAssetTypeId` | Managed data or asset marker type |
| `CreateAssetMenu` | Concrete `ScriptableObject` type |
| `Header` | Managed-data field or property |

See [Serialization And The Inspector](SerializationAndInspector.md) for persistence rules.

`RequireComponent` is enforced transactionally during managed registration and attachment. Dependencies are added
before the requested component, dependency cycles fail reload, and a dependency cannot be removed while a dependent
component remains attached.

## Entity API

Properties:

```text
IsValid
Name
Active
ActiveInHierarchy
Layer
Parent
Children
Transform
Animator
AudioSource
CharacterController
RigidBody
```

Methods:

```text
GetComponent / TryGetComponent / GetComponentHandle
HasComponent / AddComponent / RemoveComponent
GetBehaviour / TryGetBehaviour
SetParent
FindChild / Find
Instantiate
Destroy
```

## Coroutines

`Behaviour` exposes `StartCoroutine`, `StopCoroutine`, and `StopAllCoroutines`. Routines may yield `null`, nested
enumerators, `Task`, `ValueTask`, `WaitForSeconds`, `WaitForSecondsRealtime`, `WaitForFixedUpdate`, `WaitForEndOfFrame`,
`WaitUntil`, `WaitWhile`, or a custom `CustomYieldInstruction`. `Coroutine.IsRunning` and `Coroutine.Stop()` provide a
value-handle view without exposing scheduler ownership.

## Built-In Component Markers

| Area | Types |
| --- | --- |
| Scene/rendering | `TransformComponent`, `CameraComponent`, `MeshRendererComponent`, `AnimatorComponent` |
| Physics | `ColliderComponent`, `RigidBodyComponent`, `CharacterControllerComponent` |
| Audio | `AudioSourceComponent`, `AudioListenerComponent` |
| VFX | `VfxEmitterComponent` |
| Lighting | `DirectionalLightComponent`, `PointLightComponent`, `SpotLightComponent` |
| UI | `CanvasComponent`, `RectTransformComponent`, `UiTextComponent`, `UiImageComponent`, `UiButtonComponent`, `UiLayoutComponent` |

These types identify native components. Their layout is intentionally not exposed to C#.

## Frame And Gameplay Façades

| API | Main members |
| --- | --- |
| `Time` | `DeltaTime`, `FixedDeltaTime`, `UnscaledDeltaTime`, `Elapsed` |
| `Input` | `Axis2D`, `Axis`, `Held`, `Pressed`, `Released`, `Button` |
| `Physics` | `TryRaycast`, `Raycast` |
| `Navigation` | `FindPathAsync` |
| `Prefab` | `Instantiate` |
| `Cursor` | `Visible`, `Locked`, `VisibilityRequested`, `RequestCapture`, `RequestVisible`, `Hide`, `Show`, `Lock`, `Unlock` |
| `Debug` | `Log`, `Warn`, `Error`, `LogException`, `Assert`, `DrawLine` |
| `Log` | `Trace`, `Debug`, `Info`, `Warning`, `Error`, `Critical` |
| `Profiler` | `Sample`, `Counter` |

Result values include `RaycastHit`, `NavigationPath`, `PrefabInstance`, and `ProfileSample`.

## Audio

Asset markers:

```text
AudioClip
AudioMixer
```

State and options:

```text
AudioPlaybackState
AudioSourceStatus
AudioPlaybackOptions
AudioSourceHandle
```

`Audio` methods:

```text
Play
Pause
Resume
Seek
Stop
GetStatus
```

See [Audio](Audio.md).

## Animation

Asset markers:

```text
AnimationClip
AnimatorController
```

Types:

```text
AnimatorHandle
AnimatorStateInfo
AnimatorIkSpace
AnimationEvent
AnimationIkContext
```

`Animator` operations:

```text
Play / CrossFade / Pause / Resume / Stop
SetSpeed / GetStateInfo
SetFloat / SetInteger / SetBool / SetTrigger / ResetTrigger
TryGetFloat / TryGetInteger / TryGetBool
GetFloat / GetInteger / GetBool
SetLayerWeight / TryGetLayerWeight / GetLayerWeight
SetTwoBoneIK / SetFabrikIK / ClearIK
```

See [Animation](Animation.md).

## VFX

```text
VfxEffect
VfxEmitterHandle
Vfx.Play
Vfx.Pause
Vfx.Resume
Vfx.Stop
Vfx.IsAlive
Vfx.SendEvent
Vfx.SetParameter (VfxRange<T>)
```

See [Gameplay Services](GameplayServices.md#vfx).

## UI

Scene-facing API:

```text
UiButton
RuntimeUi.GetButton
RuntimeUi.SetText
RuntimeUi.WasClicked
RuntimeCanvas.SetText
RuntimeCanvas.WasClicked
```

In-memory layout API:

```text
RuntimeCanvas
UiElement
UiPanel
UiText
UiImage
UiButton
UiRect
UiThickness
UiScaleMode
UiAxisAlignment
```

Events:

```text
KeireEvent
KeireEvent<T0>
KeireEvent<T0, T1>
KeireEvent<T0, T1, T2>
KeireEvent<T0, T1, T2, T3>
```

See [UI And Events](UiAndEvents.md).

## Managed Data

`ScriptableObject`:

```text
Name
RuntimeInstanceId
CreateInstance<T>
Instantiate<T>
OnEnable
OnDisable
OnValidate
```

`Assets`:

```text
Load<T>
TryLoad<T>
LoadAsync<T>
Unload
```

`Assets.Register` exists for runtime/host integration. Ordinary gameplay normally creates managed data through the
Project panel or `ScriptableObject.CreateInstance`.

See [Assets And ScriptableObjects](AssetsAndScriptableObjects.md).

## Weapons, Damage, And Ballistics

Kéire includes two related managed gameplay surfaces:

- the `Keire` namespace contains `WeaponDefinition`, `WeaponRuntime`, `WeaponInventory`, `BallisticWorld`, `Damage`,
  `IDamageReceiver`, `WeaponPresentationRig`, and `WeaponHudModel`, which are used by the sandbox sample;
- `Keire.Production.Weapons` contains the production authoring/runtime split:
  `ProductionAmmoDefinition`, `ProductionMagazineDefinition`, `ProductionRecoilDefinition`,
  `ProductionWeaponDefinition`, `PhysicalAmmunitionInventory`, `ProductionWeaponRuntime`,
  `ProductionBallisticWorld`, `WeaponLoadout`, `ProductionWeaponPresentation`, feedback pooling/commands, HUD adapters,
  pickup transactions, and `ProductionWeaponValidator`.

Use [Weapon Authoring](../WeaponAuthoring.md) for the production ownership and validation contract. The declarations
live in [`WeaponSystem.cs`](../../KeireManaged/WeaponSystem.cs),
[`ProductionWeapons.cs`](../../KeireManaged/ProductionWeapons.cs), and
[`WeaponPresentation.cs`](../../KeireManaged/WeaponPresentation.cs).

## Advanced Runtime Bridge

`IRuntimeBridge` and `RuntimeBridge` are public host/test integration boundaries used by selected managed foundations.
Normal in-engine gameplay should use `Entity`, `NativeRuntime`-backed façades, and the service APIs documented above.
Gameplay code should not replace the engine-installed bridge.

## Source Files

| Source | Defines |
| --- | --- |
| [`Behaviour.cs`](../../KeireManaged/Behaviour.cs) | Lifecycle, callbacks, synchronization context integration |
| [`Handles.cs`](../../KeireManaged/Handles.cs) | Entity, component, transform, asset handles |
| [`RuntimeApi.cs`](../../KeireManaged/RuntimeApi.cs) | Time, input, physics, navigation, animation, audio, VFX, prefab, cursor, diagnostics |
| [`RuntimeUi.cs`](../../KeireManaged/RuntimeUi.cs) | UI wrappers and in-memory layout |
| [`Events.cs`](../../KeireManaged/Events.cs) | Persistent and runtime events |
| [`ScriptableObject.cs`](../../KeireManaged/ScriptableObject.cs) | Managed data lifecycle |
| [`ManagedAssetRuntime.cs`](../../KeireManaged/ManagedAssetRuntime.cs) | Managed data registry and load façade |
| [`SerializationAttributes.cs`](../../KeireManaged/SerializationAttributes.cs) | Inspector and identity attributes |
| [`Profiler.cs`](../../KeireManaged/Profiler.cs) | Managed profile samples and counters |
| [`MathTypes.cs`](../../KeireManaged/MathTypes.cs) | Managed vectors, quaternion, and color |
| [`BuiltInComponents.cs`](../../KeireManaged/BuiltInComponents.cs) | Built-in component marker IDs |
