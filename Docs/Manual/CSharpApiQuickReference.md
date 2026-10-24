# C# API Quick Reference

This page maps common Unity-shaped concepts to Kéire's supported managed types in this checkout. Use the linked scripting guides
for lifecycle, validation, and failure behavior; this is a navigation aid, not a promise of complete Unity API parity.

## Objects And Lifecycle

| Need | Kéire API |
| --- | --- |
| Gameplay component | `Behaviour` plus `[StableComponentId]` |
| Inspector field | `[SerializeField]`, `[StableFieldId]`, range/label/header attributes |
| Reference graph | Field `[SerializeReference]`; concrete type `[Serializable]` plus `[StableSerializedTypeId]` |
| Enable state | `Behaviour.Enabled`, `Entity.Active`, `Entity.ActiveInHierarchy` |
| Lifecycle | `Awake`, `OnEnable`, `Start`, `FixedUpdate`, `Update`, `LateUpdate`, `OnDisable`, `OnDestroy` |
| Reload | `OnBeforeReload`, `OnAfterReload`, `[HotReloadState]`, `LifetimeToken` |
| Coroutine | `StartCoroutine`, `Coroutine`, time/fixed/end-of-frame/predicate waits |
| Events | `KeireEvent` through `KeireEvent<T1,T2,T3,T4>` |

## World And Content

| Need | Kéire API |
| --- | --- |
| Entity/component | `Entity`, `Component`, `Transform`, `GetComponent<T>`, hierarchy queries |
| Instantiate/destroy | `Prefab.Instantiate`, `Entity.Instantiate`, `EngineObject.Instantiate/Destroy` |
| Persistent data | `ScriptableObject`, `[CreateAssetMenu]`, `Assets.LoadAsync<T>` |
| Native asset lease | `Assets.LoadRuntime<T>`, `AssetLoadOperation<T>` |
| Scene query/load | `SceneManager`, `SceneQuery`, `SceneLoadOperation`, `SceneAsset` |
| Preserve entity | `SceneManager.Preserve`, `EngineObject.DontDestroyOnLoad` |

## Gameplay Services

| Need | Kéire API |
| --- | --- |
| Time | `Time.DeltaTime`, `FixedDeltaTime`, `UnscaledDeltaTime`, `Elapsed`, `TimeScale`, `Paused` |
| Input | `InputActionAsset`, `InputActionContext`, `InputAction.ReadValue<T>()`; `Input.Keyboard/Mouse/Gamepad`, schemes, rumble, rebinding |
| Physics | `Physics.TryRaycast`, `TryCapsuleCast`, `OverlapSphere`; collision/trigger callbacks |
| Audio | `Audio`, `AudioSource`, `AudioListener`, `AudioReverbZone`, mixer/bus/parameter APIs |
| Animation | `Animator`, `AnimatorApi`, IK, animation/procedural-motion events |
| VFX | `Vfx`, `VfxEffect`, `VfxEmitter` |
| Navigation | `Navigation`, async path requests and agents |
| Cursor | `Cursor.RequestCapture`, `RequestVisible` scoped tokens |

## Presentation And Work

| Need | Kéire API |
| --- | --- |
| Authored runtime UI | `Keire.UI.UIDocument`, `RuntimeVisualElement`, `Q`, binding values, consumptive activation events |
| Managed UI trees | `VisualElement`, typed controls, queries, event callbacks, bindings, and custom elements |
| Rendering | `Camera`, `MeshRenderer`, typed lights, `MaterialPropertyBlock`, `DynamicMaterial` |
| Camera effects | `Camera.EffectBeforeTonemapping`, `EffectAfterTonemapping`, `EffectAfterUi`; nullable Fullscreen materials |
| Global material data | `MaterialParameterCollection`, `GlobalMaterialParameters` |
| Jobs | `Jobs.Submit/Run`, `Job`, `JobDescription`, `JobContext`, `Job.Completion` |
| Logging | `Debug`, `Log` |
| Profiling | `Profiler.Sample`, `Profiler.Counter` |
| Preferences | `PlayerPreferences` |

The [UI Toolkit and Events](../Scripting/UiAndEvents.md) guide covers UI Builder authoring, panel targets, live Play
drafts, runtime queries/events, and the current text/accessibility limits.

## Important Non-Parity Boundaries

- Kéire exposes stable managed handles, not native pointers.
- A valid asset reference and a held runtime residency lease are different concepts.
- Runtime scene changes are transactional and limited to cooked player content.
- Materials use a Shader picker and property Inspector. Shader Graph defines a shader; assign its material to a Mesh Renderer.
- Legacy `MaterialGraph` and `MaterialInstance` reference types do not mean a current Material Graph authoring workflow exists.
- String-based `Input.Axis2D`/`Pressed`/`Held` helpers are obsolete; prefer owned actions or direct controls.
- Creating a managed `VisualElement` tree does not replace a scene document's source-backed tree.
- VFX Subgraphs are explicit Operator/Block/System assets; collapsed comments are visual organization only.
- The current API matrix names unsupported or partial Unity-shaped areas rather than emulating them silently.

For exact signatures and status, use the [Managed API Index](../Scripting/ApiIndex.md) and
[Managed API Capability Matrix](../Scripting/ManagedApiMatrix.md).
