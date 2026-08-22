# Managed API Index

This index covers the Kéire 0.4.0 Unity-shaped authoring surface. Native IDs and internal-call records are advanced
interop details and are not part of normal gameplay code.

## Object Model

| Type | Purpose |
| --- | --- |
| `EngineObject` | Base identity, `Name`, `IsValid`, equality, `Instantiate`, `Destroy`, `DontDestroyOnLoad` |
| `Entity` | Scene object, hierarchy, active state, tags, cloning, and component lookup/mutation |
| `Component` | Base for everything attached to an entity; mirrors the entity lookup family |
| `Behaviour` | User script component with lifecycle, `Enabled`, coroutines, and reload support |
| `Asset` | Base for stable native assets, `Prefab`, `SceneAsset`, and persistent ScriptableObjects |
| `ScriptableObject` | Persistent managed data asset or transient runtime data object |

## Component Lookup

Available on both `Entity` and `Component`/`Behaviour`:

- `GetComponent<T>()`, `GetComponent(Type)`
- `TryGetComponent<T>(out T?)`, `TryGetComponent(Type, out Component?)`
- `GetComponents<T>()`, `GetComponents(Type)` and allocation-free list overloads
- `GetComponent(s)InChildren<T>(bool includeInactive = false)` plus `Type` and list overloads
- `GetComponent(s)InParent<T>(bool includeInactive = false)` plus `Type` and list overloads
- `AddComponent<T>()`, `AddComponent(Type)`, `Destroy(component)`

## Lifecycle

`Awake`, `OnEnable`, `Start`, `FixedUpdate`, `Update`, `LateUpdate`, `OnDisable`, `OnDestroy`, collision/trigger
callbacks, animation/procedural callbacks, `OnBeforeReload`, and `OnAfterReload`.

Active additions and prefab instances complete required `Awake`/`OnEnable` work before returning. Inactive entities
defer `Awake`; disabled behaviours on active entities receive `Awake` but not `OnEnable`. `Start` runs once before the
first enabled update. Destruction commits after the current update loop.

## Native Components

| Area | Components |
| --- | --- |
| Transform | `Transform` |
| Rendering | `Camera`, `MeshRenderer`, `DirectionalLight`, `PointLight`, `SpotLight` |
| Probes | `ReflectionProbe`, `LightProbeVolume` |
| Animation | `Animator` |
| Physics | `Collider`, `RigidBody`, `CharacterController`, `FixedJoint`, `HingeJoint`, `DistanceJoint`, `SpringJoint` |
| Audio | `AudioSource`, `AudioListener`, `AudioReverbZone` |
| VFX | `VfxEmitter` |
| Scene UI | `Canvas`, `RectTransform`, `UiText`, `UiImage`, `UiButton`, `UiLayout`, `UiSlider`, `UiToggle`, `UiInputField`, `UiScrollView`, `UiAccessibility` |

Programmatic overlay UI uses `RuntimeUiText`, `RuntimeUiImage`, `RuntimeUiButton`, and the other `RuntimeUi*` names.

## Assets

Direct `Asset` subclasses include audio clips/mixers, textures, meshes, materials and graphs, animation assets, VFX
assets/subgraphs, physics materials, render profiles, lighting/probe data, `SceneAsset`, `Prefab`, and native text or
binary assets. Declare these exact types in Inspector fields.

`AssetLoadOperation<T>` owns optional explicit runtime residency and supports status, progress, fallback/revision,
diagnostics, yield/await, cancellation, and disposal.

## Scenes And Prefabs

`Scene`, `SceneAsset`, `SceneManager`, `SceneLoadOperation`, `SceneLoadMode`, `SceneQuery`, `RenderSettings`, and
`Prefab`. `Instantiate(Prefab, ...)` and `Prefab.Instantiate(...)` return the root `Entity`.

## Materials

`Material`, `DynamicMaterial`, `MaterialPropertyBlock`, `MaterialParameterCollection`, and
`MaterialParameterCollectionInstance`. Renderer-created runtime state is never a public native handle.

## Jobs And Async

`Job`, `Jobs.Submit`, `Jobs.Run`, `JobStatus`, `JobPriority`, `JobClass`, `Coroutine`, `WaitForSeconds`,
`WaitForFixedUpdate`, and `WaitForEndOfFrame`. Job dependencies accept `IReadOnlyList<Job>`.

## Global Facilities

- `Application`, `Time`, `Screen`, `PlayerPreferences`
- `Input`, `Cursor`
- `Physics`, navigation APIs
- one-shot `Audio`
- `SceneManager`, `RenderSettings`
- `Assets`
- `Debug`, profiling APIs

Entity-scoped audio, animation, VFX, rendering, UI, and physics operations belong to their component instances.

## Serialization Attributes

`SerializeField`, `NonSerialized`, `StableComponentId`, `StableFieldId`, `StableAssetTypeId`, `HotReloadState`,
`Range`, `Min`, `Max`, `InspectorStep`, `Multiline`, `InspectorName`, `Header`, `Tooltip`, `Group`, `ReadOnly`,
`HideInInspector`, `ExecutionOrder`, and `RequireComponent`.

Use standard `[Serializable]` for supported nested values. `[SerializedField]`, `[SerializeReference]`, dictionaries,
multidimensional/jagged arrays, and nested collection containers are not supported in 0.4.0.

## Source Files

| File | Surface |
| --- | --- |
| [`Handles.cs`](../../KeireManaged/Handles.cs) | `EngineObject`, `Asset`, `Entity`, `Component`, `Transform`, lookup |
| [`Behaviour.cs`](../../KeireManaged/Behaviour.cs) | Behaviour lifecycle and generation-local registry |
| [`BuiltInComponents.cs`](../../KeireManaged/BuiltInComponents.cs) | Probes, collider, joints, physics material |
| [`NativeAssets.cs`](../../KeireManaged/NativeAssets.cs) | Direct native asset types |
| [`RuntimeApi.cs`](../../KeireManaged/RuntimeApi.cs) | Physics, animation, audio, VFX components/services |
| [`Rendering.cs`](../../KeireManaged/Rendering.cs) | Cameras, renderers, lights, dynamic materials |
| [`RuntimeUiControls.cs`](../../KeireManaged/RuntimeUiControls.cs) | Scene UI components |
| [`RuntimeUi.cs`](../../KeireManaged/RuntimeUi.cs) | Programmatic `RuntimeUi*` overlays |
| [`RuntimeWorld.cs`](../../KeireManaged/RuntimeWorld.cs) | Scenes and render settings |
| [`Jobs.cs`](../../KeireManaged/Jobs.cs) | Managed jobs |
| [`RuntimeAssets.cs`](../../KeireManaged/RuntimeAssets.cs) | Asset load operations |
