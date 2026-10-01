# Scripting Workflow Map

[Scripting home](README.md) · [Cookbook](Cookbook.md) · [API index](ApiIndex.md)

Use this page when you know what you want your game or tool to do but do not yet know which API to use. The system
guides explain behavior and limits; the [runtime reference](RuntimeReference.md) and
[Editor reference](EditorReference.md) list declarations and overloads. This guide describes the checked-in engine.
Check the [capability matrix](ManagedApiMatrix.md) before assuming an API from another engine exists here.

## Learn In This Order

1. [Create, build, and attach a script](GettingStarted.md#create-a-behaviour). No custom assembly is needed for a
   first script under `Assets`. Give each attachable class its own matching filename.
2. [Choose callbacks](BehavioursAndLifecycle.md#callback-reference). Initialize local references, acquire scoped
   resources, and release them on disable and before reload.
3. [Expose fields](SerializationAndInspector.md). Assign scene objects and assets in the Inspector and keep stable
   IDs when renaming persisted fields.
4. [Work with entities](EntitiesComponentsAndTransforms.md). Learn the distinction between missing references,
   destroyed wrappers, component absence, local transforms, and world transforms.
5. [Run a cookbook example](Cookbook.md). Follow its scene and asset setup before testing it in Play Mode.
6. [Package the result](../PlayerBuilds.md). Validate scene/asset dependency closure and runtime-only assembly
   boundaries in a player, then test reload and teardown separately in the Editor.

## Choose An API By Task

| I want to… | APIs to start with | Setup and workflow | Worked example |
| --- | --- | --- | --- |
| Create my first script | `Behaviour`, `StableComponentId` | [Build and attach](GettingStarted.md#build-and-attach) | [Minimal Behaviour](README.md#minimal-behaviour) |
| Split gameplay, editor, and tests | `.keireasm`, `.asmref` | [Assembly ownership and references](GettingStarted.md#assembly-definitions) | [Assembly JSON](GettingStarted.md#assembly-definitions) |
| Expose tuning and references | `SerializeField`, `StableFieldId`, Inspector attributes | [Serialization](SerializationAndInspector.md) | [Shared tuning data](Cookbook.md#shared-tuning-data) |
| Find or add functionality | `GetComponent`, `TryGetComponent`, `AddComponent` | [Entity/component lookup](EntitiesComponentsAndTransforms.md) | [Ray interaction](Cookbook.md#ray-interaction) |
| Reparent, enable, tag, or destroy objects | `Entity`, `Transform`, `EngineObject` | [Hierarchy and lifetime](EntitiesComponentsAndTransforms.md) | [Timed prefab spawning](Cookbook.md#timed-prefab-spawning) |
| Read keyboard, mouse, or gamepad | `Input.Keyboard`, `Input.Mouse`, `Input.Gamepad` | [Direct input](GameplayServices.md#input) | [Ray interaction](Cookbook.md#ray-interaction) |
| Use actions or local-player contexts | `InputActionAsset`, `InputActionContext`, `InputAction` | [Input Actions Editor](../InputActionsEditor.md) | [Fixed-step action movement](Cookbook.md#fixed-step-action-movement) |
| Rebind controls and save overrides | `InputRebindOperation`, `InputRebindOptions`, `Input` | [Rebinding](GameplayServices.md#input) | Candidate/conflict example in that guide |
| Control pointer visibility/capture | `Cursor.RequestVisible`, `Cursor.RequestCapture` | [Scoped cursor ownership](GameplayServices.md#cursor-ownership) | [Reload-safe ownership](BehavioursAndLifecycle.md#cleanup-is-part-of-the-contract) |
| Move a collision-aware character | `CharacterController.Move`, `State` | [Character controller](GameplayServices.md#character-controller) | [Action motor](Cookbook.md#fixed-step-action-movement) |
| Push a dynamic body | `RigidBody`, `ForceMode` | [Rigid bodies](GameplayServices.md#rigid-bodies) | Force and impulse examples in that guide |
| Detect surfaces or nearby targets | `Physics.TryRaycast`, `TryCapsuleCast`, `OverlapSphere` | [Physics queries](GameplayServices.md#physics-queries) | [Ray interaction](Cookbook.md#ray-interaction) |
| React to collisions or triggers | `CollisionContact`, Behaviour contact callbacks | [Contact callbacks](GameplayServices.md#collision-and-trigger-callbacks) | [Trigger feedback](Cookbook.md#trigger-feedback) |
| Request a navigation path | `Navigation.FindPathAsync`, `NavigationPath` | [Navigation](GameplayServices.md#navigation); requires a bound host provider | Cancellation example in that guide |
| Spawn, clone, or preserve objects | `Prefab.Instantiate`, `EngineObject.Instantiate`, `DontDestroyOnLoad` | [Prefabs](AssetsAndScriptableObjects.md), [scene preservation](ScenesAndRenderSettings.md) | [Timed spawner](Cookbook.md#timed-prefab-spawning) |
| Share reusable game data | `ScriptableObject`, `CreateAssetMenu`, `Assets.LoadAsync` | [Persistent managed data](AssetsAndScriptableObjects.md) | [Movement settings](Cookbook.md#shared-tuning-data) |
| Keep presentation assets resident | `Assets.LoadRuntime`, `AssetLoadOperation<T>` | [Readiness and ownership](AssetsAndScriptableObjects.md) | [Resident audio](Cookbook.md#explicit-audio-residency) |
| Play sound and route buses | `AudioSource`, `Audio`, `AudioMixer`, `AudioListener` | [Audio](Audio.md), [audio authoring](../AudioProduction.md) | [Trigger feedback](Cookbook.md#trigger-feedback) |
| Control states, layers, or parameters | `Animator`, `AnimatorStateInfo` | [Animation](Animation.md), authored controller | [Animation controller pattern](Animation.md#complete-animation-graph-controller-pattern) |
| Submit procedural motion or IK | `ProceduralLocomotionIntent`, `SetTwoBoneIK`, `SetFabrikIK` | [Animation](Animation.md), [procedural authoring](../ProceduralMotion.md) | [Procedural intent](Animation.md#procedural-humanoid-locomotion) |
| Trigger VFX and write Blackboard values | `VfxEmitter`, `VfxEffect`, parameter IDs and typed ranges | [VFX scripting](GameplayServices.md#vfx) and [graph authoring](../Vfx.md) | [Trigger feedback](Cookbook.md#trigger-feedback) |
| Change a camera, mesh, light, or probe | Rendering components, `Mesh`, `Material` | [Rendering and materials](RenderingAndMaterials.md) | [Camera follow](Cookbook.md#follow-a-presentation-transform) |
| Give one object a material effect | `MaterialPropertyBlock`, `DynamicMaterial` | Expose shader properties first | [Damage flash](Cookbook.md#per-renderer-damage-flash) |
| Share weather or lighting values | `GlobalMaterialParameters`, `RenderSettings`, `LightingQuality` | [Global collections](RenderingAndMaterials.md#global-material-parameter-collections), [scene settings](ScenesAndRenderSettings.md) | Collection and environment examples in those guides |
| Load/unload a level or room | `SceneAsset`, `SceneManager`, `SceneLoadOperation` | Cook scene dependencies; choose `Single` or `Additive` | [Owned additive room](Cookbook.md#owned-additive-room) |
| Search active, loaded, or persistent scenes | `SceneQuery`, `SceneManager.FindAll*` | [Scoped scene queries](ScenesAndRenderSettings.md) | Query examples in that guide |
| Connect gameplay to a HUD | `Keire.UI.UIDocument`, `RuntimeVisualElement`, binding values | Author `.keireui` and panel settings | [Health panel](Cookbook.md#source-backed-health-panel) |
| Build retained controls and custom editors | `VisualElement`, controls, queries, events, binding | [UI and events](UiAndEvents.md), [Editor extensions](ManagedExtensibility.md) | [Custom controls](UiAndEvents.md#custom-controls) |
| Store player options | `PlayerPreferences`, `Application.PersistentDataPath` | [Typed persistence](GameplayServices.md#application-and-player-preferences) | [Settings persistence](Cookbook.md#player-settings-and-display-options) |
| Sequence timed work | Coroutines and yield instructions | [Coroutine phases and cancellation](BehavioursAndLifecycle.md#coroutines) | [Timed spawner](Cookbook.md#timed-prefab-spawning) |
| Compute data off-thread | `Jobs.Submit`, `Job`, `JobDescription` | [Jobs and async](AsyncReloadAndDiagnostics.md) | [Prepared positions](Cookbook.md#worker-data-and-owner-thread-publication) |
| Dispatch GPU compute | `ComputeDevice`, buffer, pipeline, submission | Host-registered program and supported backend | [Compute workflow](Compute.md) |
| Preserve state during script edits | `HotReloadState`, stable fields, reload callbacks | [Reload contract](AsyncReloadAndDiagnostics.md#transactional-reload), [serialization](SerializationAndInspector.md) | `Interactable` and `DamageFlash` in the cookbook |
| Log, visualize, or measure gameplay | `Debug`, `Log`, `Profiler` | [Diagnostics](AsyncReloadAndDiagnostics.md) | [Profile result publication](Cookbook.md#worker-data-and-owner-thread-publication) |
| Add application-owned managed services | `IRuntimeService`, dependency attributes and contexts | [Managed extensibility](ManagedExtensibility.md) | Service implementation in that guide |
| Bind a native source module | Native contract attributes and generated stubs | [Managed extensibility](ManagedExtensibility.md), [source-module example](../../Examples/SourceModule/README.md) | Typed contract in the extensibility guide |
| Author inspectors, windows, tools, importers, or build hooks | `Keire.Editor` SDK | Editor assembly, stable extension IDs, current host integration limits | [Editor extension guide](ManagedExtensibility.md), [member reference](EditorReference.md) |

## Pick The Right Time And Lifetime

| Work | Preferred location | Contract to remember |
| --- | --- | --- |
| Cache required components | `Awake` or an idempotent bind method | Reload replacements need their own bind path; `Awake` does not rerun |
| Acquire input/cursor/subscriptions | `OnEnable` and active `OnAfterReload` | Release in `OnDisable` and `OnBeforeReload` |
| Read continuous input for physics | `FixedUpdate`, or store intent in `Update` | Each fixed tick in an outer frame sees the same input snapshot |
| Consume a press once per frame | `Update` | A press edge may appear in several fixed ticks; latch/consume one-shot intent |
| Submit physics movement | `FixedUpdate` | Use fixed delta; controller `Move` takes displacement, not velocity |
| Update a camera or HUD | `LateUpdate` or `Update` | Use presentation transforms for interpolated visual following |
| Await a path, asset, or job | Managed callback context | Observe cancellation and validate identity again before using results |
| Prepare expensive plain data | Job callback | Capture value snapshots; engine objects stay on the owner thread |
| Persist options | Explicit Apply/Save action | Avoid disk writes on every variable update |

`null` means unassigned/absent. A non-null scene component or entity can still be invalid. A valid asset identity does
not imply its bytes are resident. A completed operation can have failed or been canceled. A queued command can be
accepted before its effect appears in a later evaluated snapshot. These distinctions explain many apparent scripting
bugs; use the relevant system guide when choosing a readiness or success check.

## From A Script To A Packaged Game

1. Keep runtime gameplay in runtime assemblies and editor tooling in Editor-classified assemblies.
2. Assign typed asset fields rather than constructing opaque IDs for ordinary gameplay. Persisted references allow
   the cooker to discover the required dependency closure.
3. Test enable/disable, missing optional references, destroyed targets, and scene transitions in Play Mode.
4. Edit a script while running. Verify a successful reload rebinds resources and a failed candidate leaves last-good
   behavior available. Check the build generation before judging what code is running.
5. Build a player with the intended startup scene and target profile. Test input, audio, UI, and scene changes in the
   player. A compilation check alone does not verify cooked content or native runtime providers.

See [Player Builds](../PlayerBuilds.md) and [Debugging and Profiling](../Manual/DebuggingAndProfiling.md).
