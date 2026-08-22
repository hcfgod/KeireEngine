# C# API Quick Reference

This page maps common Unity-shaped concepts to Kéire 0.4.0's supported managed types. Use the linked scripting guides
for lifecycle, validation, and failure behavior; this is a navigation aid, not a promise of complete Unity API parity.

## Objects And Lifecycle

| Need | Kéire API |
| --- | --- |
| Gameplay component | `Behaviour` plus `[StableComponentId]` |
| Inspector field | `[SerializeField]`, `[StableFieldId]`, range/label/header attributes |
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
| Input | `Input.Held/Pressed/Released`, `Axis/Axis2D`, devices, schemes, rumble, rebinding |
| Physics | `Physics.TryRaycast`, `TryCapsuleCast`, `OverlapSphere`; collision/trigger callbacks |
| Audio | `Audio`, `AudioSource`, `AudioListener`, `AudioReverbZone`, mixer/bus/parameter APIs |
| Animation | `Animator`, `AnimatorApi`, IK, animation/procedural-motion events |
| VFX | `Vfx`, `VfxEffect`, `VfxEmitter` |
| Navigation | `Navigation`, async path requests and agents |
| Cursor | `Cursor.RequestCapture`, `RequestVisible` scoped tokens |

## Presentation And Work

| Need | Kéire API |
| --- | --- |
| Runtime UI | `RuntimeUi`, `UiButton`, `UiSlider`, `UiToggle`, `UiInputField`, `UiScrollView` |
| Rendering | `Camera`, `MeshRenderer`, typed lights, `MaterialPropertyBlock`, `DynamicMaterial` |
| Global material data | `MaterialParameterCollection`, `GlobalMaterialParameters` |
| Jobs | `Jobs.Submit/Run`, `Job`, `JobDescription`, `JobContext`, `Job.Completion` |
| Logging | `Debug`, `Log` |
| Profiling | `Profiler.Sample`, `Profiler.Counter` |
| Preferences | `PlayerPreferences` |

## Important Non-Parity Boundaries

- Kéire exposes stable managed handles, not native pointers.
- A valid asset reference and a held runtime residency lease are different concepts.
- Runtime scene changes are transactional and limited to cooked player content.
- Shader Graph is not a material and cannot be assigned directly to a Mesh Renderer.
- VFX Subgraphs are explicit Operator/Block/System assets; collapsed comments are visual organization only.
- The current API matrix names unsupported or partial Unity-shaped areas rather than emulating them silently.

For exact signatures and status, use the [Managed API Index](../Scripting/ApiIndex.md) and
[Managed API Capability Matrix](../Scripting/ManagedApiMatrix.md).
