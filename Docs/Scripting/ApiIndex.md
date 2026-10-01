# Managed API Index

[Scripting home](README.md) · [Workflow map](WorkflowMap.md) · [Cookbook](Cookbook.md) · [API index](ApiIndex.md)

This index covers the checked-in Kéire managed authoring surface. Native IDs and internal-call records are advanced
interop details and are not part of normal gameplay code.

## On This Page

- [Find Exact Members](#find-exact-members)
- [Object Model](#object-model)
- [Component Lookup](#component-lookup)
- [Lifecycle](#lifecycle)
- [Native Components](#native-components)
- [Assets](#assets)
- [Maintaining The Member Reference](#maintaining-the-member-reference)
- [Scenes And Prefabs](#scenes-and-prefabs)
- [Materials](#materials)
- [Jobs And Async](#jobs-and-async)
- [Global Facilities](#global-facilities)
- [Serialization Attributes](#serialization-attributes)
- [Editor Extensions](#editor-extensions)
- [Source Files](#source-files)

## Find Exact Members

The [runtime reference](RuntimeReference.md) and [Editor reference](EditorReference.md) contain the complete declared
public type/member catalog plus protected extension points, including signatures, defaults, constraints, accessors,
enum values, and defining source links. Use this page to select a system; use the catalog when you need an overload.
For setup and working code, use the [workflow map](WorkflowMap.md) and [cookbook](Cookbook.md).

| Area | Main reference entries | Usage guide |
| --- | --- | --- |
| Objects | [Entity](RuntimeReference.md#keireentity), [Component](RuntimeReference.md#keirecomponent), [Transform](RuntimeReference.md#keiretransform) | [Entities/components](EntitiesComponentsAndTransforms.md) |
| Scripts | [Behaviour](RuntimeReference.md#keirebehaviour), [Time](RuntimeReference.md#keiretime), [Coroutine](RuntimeReference.md#keirecoroutine) | [Lifecycle](BehavioursAndLifecycle.md) |
| Input | [Input](RuntimeReference.md#keireinput), [InputAction](RuntimeReference.md#keireinputaction), [InputActionContext](RuntimeReference.md#keireinputactioncontext) | [Gameplay services](GameplayServices.md) |
| Physics | [Physics](RuntimeReference.md#keirephysics), [RigidBody](RuntimeReference.md#keirerigidbody), [CharacterController](RuntimeReference.md#keirecharactercontroller) | [Gameplay services](GameplayServices.md) |
| Assets/data | [Assets](RuntimeReference.md#keireassets), [ScriptableObject](RuntimeReference.md#keirescriptableobject), [AssetLoadOperation](RuntimeReference.md#keireassetloadoperation-of-t) | [Assets/data](AssetsAndScriptableObjects.md) |
| Scenes | [SceneManager](RuntimeReference.md#keirescenemanager), [SceneQuery](RuntimeReference.md#keirescenequery), [SceneLoadOperation](RuntimeReference.md#keiresceneloadoperation) | [Scenes](ScenesAndRenderSettings.md) |
| Audio | [AudioSource](RuntimeReference.md#keireaudiosource), [Audio](RuntimeReference.md#keireaudio), [AudioPlaybackOptions](RuntimeReference.md#keireaudioplaybackoptions) | [Audio](Audio.md) |
| Animation | [Animator](RuntimeReference.md#keireanimator), [ProceduralLocomotionIntent](RuntimeReference.md#keireprocedurallocomotionintent) | [Animation](Animation.md) |
| VFX | [VfxEmitter](RuntimeReference.md#keirevfxemitter), [Vfx](RuntimeReference.md#keirevfx) | [VFX](GameplayServices.md#vfx) |
| Rendering | [Camera](RuntimeReference.md#keirecamera), [MeshRenderer](RuntimeReference.md#keiremeshrenderer), [MaterialPropertyBlock](RuntimeReference.md#keirematerialpropertyblock), [RenderSettings](RuntimeReference.md#keirerendersettings) | [Rendering](RenderingAndMaterials.md) |
| UI | [UIDocument](RuntimeReference.md#keireuiuidocument), [RuntimeVisualElement](RuntimeReference.md#keireuiruntimevisualelement), [VisualElement](RuntimeReference.md#keireuivisualelement) | [UI/events](UiAndEvents.md) |
| Compute | [ComputeDevice](RuntimeReference.md#keirecomputedevice), [ComputeSubmission](RuntimeReference.md#keirecomputesubmission) | [GPU compute](Compute.md) |
| Async/diagnostics | [Jobs](RuntimeReference.md#keirejobs), [Debug](RuntimeReference.md#keiredebug), [Profiler](RuntimeReference.md#keireprofiler) | [Async/reload](AsyncReloadAndDiagnostics.md) |
| Application/options | [Application](RuntimeReference.md#keireapplication), [Screen](RuntimeReference.md#keirescreen), [PlayerPreferences](RuntimeReference.md#keireplayerpreferences) | [Gameplay services](GameplayServices.md) |
| Extensibility | [IRuntimeService](RuntimeReference.md#keireiruntimeservice), [Editor SDK directory](EditorReference.md#type-directory) | [Managed extensibility](ManagedExtensibility.md) |

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

Serialization lifecycle also includes `ISerializationCallbackReceiver.OnBeforeSerialize`,
`ISerializationCallbackReceiver.OnAfterDeserialize`, and the Editor-authored `Behaviour.OnValidate` callback.

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
| Scene UI | `Keire.UI.UIDocument` plus `VisualTreeAsset`, `StyleSheet`, and `PanelSettings` |

Managed retained trees use `Keire.UI.VisualElement`, the control family, `UQueryBuilder<T>`, events, binding, and
explicit `[UxmlElement]` / `[UxmlAttribute]` custom-control registration.

## Assets

Direct `Asset` subclasses include audio clips/mixers, textures, meshes, materials and graphs, animation assets, VFX
assets/subgraphs, physics materials, render profiles, lighting/probe data, `SceneAsset`, `Prefab`, and native text or
binary assets. Declare these exact types in Inspector fields.

`AssetLoadOperation<T>` owns optional explicit runtime residency and exposes state, readiness, fallback/revision,
diagnostics, coroutine yielding, `WaitUntilReadyAsync(cancellation)`, and disposal. The operation itself is not an
awaiter; use its asynchronous wait method. Scene operations separately expose progress and cancellation.

## Maintaining The Member Reference

The reference tool uses the selected SDK's Roslyn parser to catalog visible declarations from `KeireManaged` and
`KeireEditorManaged`. It also synchronizes marked cookbook blocks with their compile-checked source files.
It needs a .NET 10 SDK and no extra NuGet package. Run from the repository root:

```powershell
dotnet run --project Docs/Scripting/Tools/Keire.ApiReference.csproj --artifacts-path Build/DocValidation/Reference -- .
dotnet run --project Docs/Scripting/Tools/Keire.ApiReference.csproj --artifacts-path Build/DocValidation/Reference -- . --check
dotnet build Docs/Scripting/Examples/Keire.ScriptingExamples.csproj --artifacts-path Build/DocValidation/Examples
dotnet build Docs/Scripting/EditorExamples/Keire.EditorExamples.csproj --artifacts-path Build/DocValidation/EditorExamples
node Scripts/Tests/test-website-docs.mjs
node Services/KeireDistributionService/DocumentationSite/scripts/test-manual-examples.mjs
```

The same commands work in a Unix shell. On a bootstrapped Windows checkout, use
`./Build/Dependencies/dotnet-sdk/dotnet.exe` when the SDK is not on PATH. `--check` reports stale catalogs or excerpts
without rewriting them. Generated build outputs go under ignored `Build`; the Markdown catalogs are maintained
documentation. After a public API change, regenerate the catalogs and update the relevant explanations and examples.

## Scenes And Prefabs

`Scene`, `SceneAsset`, `SceneManager`, `SceneLoadOperation`, `SceneLoadMode`, `SceneQuery`, `RenderSettings`, `LightingQuality`, and
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

Use `[Serializable]` for supported by-value nested data. A field-only `[SerializeReference]` graph supports stable-ID
polymorphic concrete types, cycles, sharing, exact dictionaries, exact lists, and recursively nested one-dimensional
arrays within the documented limits. Multidimensional arrays and unstable/custom dictionary-key contracts remain
unsupported and reject the candidate generation without replacing the previous valid state.

Atomic custom values use `ManagedSerializedValue`, `ManagedValueConverter<T>`,
`CustomManagedValueConverterAttribute`, `IManagedValueMigration`, and `ManagedValueMigrationAttribute`. Runtime
services use `IRuntimeService`, `RuntimeServiceAttribute`, `RuntimeServiceDependencyAttribute`,
`RuntimeServiceContext`, and `RuntimeServiceUpdateContext`. Native source-module contracts use
`NativeServiceContractAttribute`, `NativeMethodAttribute`, and optional `NativeBufferAttribute`; typed stubs come from
the packaged `Keire.Managed.Generators` analyzer.

## Editor Extensions

Editor `.keireasm` assemblies may use `Keire.Editor.Managed.dll`. The main surfaces are `SerializedObject`,
`SerializedProperty`, property drawers/decorators, custom `Editor` implementations, `ScriptedImporter`,
`AssetImportContext`, `AssetPostprocessor`, `EditorWindow`, `SettingsProvider`, `EditorTool`, `Selection`, `Undo`,
`EditorApplication`, `AssetDatabase`, and ordered build processor interfaces. Every discovered extension type requires
an `EditorExtensionIdAttribute`; retained UI is created with `CreatePropertyGUI`, `CreateInspectorGUI`, or `CreateGUI`.

## Source Files

| File | Surface |
| --- | --- |
| [`Handles.cs`](../../KeireManaged/Handles.cs) | `EngineObject`, `Asset`, `Entity`, `Component`, `Transform`, lookup |
| [`Behaviour.cs`](../../KeireManaged/Behaviour.cs) | Behaviour lifecycle and generation-local registry |
| [`BuiltInComponents.cs`](../../KeireManaged/BuiltInComponents.cs) | Probes, collider, joints, physics material |
| [`NativeAssets.cs`](../../KeireManaged/NativeAssets.cs) | Direct native asset types |
| [`RuntimeApi.cs`](../../KeireManaged/RuntimeApi.cs) | Physics, animation, audio, VFX components/services |
| [`Rendering.cs`](../../KeireManaged/Rendering.cs) | Cameras, renderers, lights, dynamic materials |
| [`UiToolkit.cs`](../../KeireManaged/UiToolkit.cs) | UI documents, visual trees, events, queries, binding, custom elements |
| [`UiToolkitControls.cs`](../../KeireManaged/UiToolkitControls.cs) | Retained UI Toolkit controls and virtualization |
| [`RuntimeWorld.cs`](../../KeireManaged/RuntimeWorld.cs) | Scenes and render settings |
| [`Jobs.cs`](../../KeireManaged/Jobs.cs) | Managed jobs |
| [`RuntimeAssets.cs`](../../KeireManaged/RuntimeAssets.cs) | Asset load operations |
| [`ManagedCustomSerialization.cs`](../../KeireManaged/ManagedCustomSerialization.cs) | Custom values, converters, migrations, callbacks |
| [`RuntimeServices.cs`](../../KeireManaged/RuntimeServices.cs) | Application-owned managed services |
| [`NativeServiceContracts.cs`](../../KeireManaged/NativeServiceContracts.cs) | Stable native source-module ABI declarations |
| [`KeireEditorManaged`](../../KeireEditorManaged) | Editor-only inspectors, importers, tools, windows, and build hooks |
