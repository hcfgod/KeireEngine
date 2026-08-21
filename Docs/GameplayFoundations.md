# Gameplay-production foundations

Kéire now exposes middleware-free foundations for prefabs, managed builds, physics, audio graphs, navigation queries,
and profiling. These APIs preserve the engine's owner-thread and scene-isolation rules while the pinned Coral, Jolt,
miniaudio, and Recast adapters are integrated behind them.

## Service ownership

`ApplicationSpecification` enables scripting, physics, audio, navigation, and profiling independently. Disabled modes
remain the default for low-level SDK consumers and tests. `Application` constructs enabled services after assets and
closes them in reverse dependency order. Physics and navigation create scene-owned worlds; a surviving world becomes
inert as soon as its application service closes.

The profiler accepts typed native or managed spans and counters from worker threads. Frame begin/end and service/world
mutation remain owner-thread operations.

## Prefabs and scene schema v6

`.keireprefab` stores stable local object IDs, component data, nested prefab instances, an optional base prefab, and
typed override records. Composition resolves bases recursively, applies variants deterministically, validates the
canonical scene, and reports missing targets or cycles without mutating either source document.

Scene schema v6 persists prefab object mappings, typed overrides, a validated entity layer index, bounded entity tags,
lighting-bake settings, and an optional baked `LightingSet` reference. Readers migrate v1-v5 documents to v6; v2/v3
physics-component layers become the entity layer instead of being discarded, while later fields carry forward.
Writers always emit v6. Layer and tag changes use first-class prefab overrides and therefore survive variant
composition and apply/revert. Editor authoring helpers implement create-from-selection, variant creation,
transactional instantiation, and one-level or complete unpacking over `SceneDefinition` values, which makes undo/redo
boundaries explicit.
The Project panel exposes create-from-selection, prefab source opening, and drag-to-Scene instantiation. Instance
override inspection, apply-to-source, revert, one-level unpack, and complete unpack remain explicit operations. Prefab
sources open in an isolated document with Save/Discard boundaries. Root prefab updates replace their canonical template;
variants regenerate typed overrides against the composed base. Scene-owned root placement is excluded from source apply.
Nested ownership is never flattened: root prefabs containing nested instances require a variant for composed edits.

## Managed assemblies

`.keireasm` defines a named runtime, editor, or test assembly using project-relative source roots and asset-ID
references. The graph validator rejects duplicate names, missing references, and cycles before compilation.

`KeireManaged/Keire.Managed.csproj` builds the `Keire.Managed` API assembly. It provides `Behaviour`, stable handles,
supported math/asset references, serialization and migration attributes, collision/trigger/animation/reload hooks,
and the Time, Input, Physics, Navigation, Animator, Audio, Prefab, Debug, and Log façades. Starter projects receive a
runtime `Gameplay.keireasm` definition and an initial `GameRoot.cs` script.
The Project panel creates managed assembly definitions with a transactional starter source and creates C# Behaviour
scripts only beneath declared source roots. Script and assembly sources open in a configured external editor or the
operating-system default application.
Before a C# source opens, the editor regenerates a project-root Visual Studio solution and one SDK-style project per
`.keireasm`. Visual Studio receives the solution plus `/Edit <script>`, matching Unity's solution-context workflow
instead of opening an isolated source file. The generated projects are authoring artifacts; managed builds continue to
regenerate and compile from the canonical assembly definitions.

`ScriptSystem::StartBuild` generates SDK-style `net8.0` gameplay projects under a staging directory, references the
staged `Keire.Managed.dll`, runs the verified .NET 10 SDK on a cancellable worker, parses source/line/column diagnostics, and
atomically publishes `Library/ScriptAssemblies/Active` only after a successful build. A failed or cancelled build
leaves the previous active directory untouched.

The application-owned patched Coral host discovers hostfxr through nethost and accepts an explicit bundled .NET root.
Reload creates a candidate collectible context, loads `Keire.Managed` and gameplay DLLs, validates the Behaviour type
registry, instantiates stable-ID script components through the normal scene lifecycle, migrates live Behaviour objects
through the before/after reload hooks, retains the reload request's migration payload, and swaps only after preparation
succeeds. The Kéire Coral patch scopes unload-time reflection caches and handle tracking to the retiring load context
so an overlapping candidate remains valid. Failure or cancellation unloads the candidate and leaves the active
generation intact. Cook builds runtime `.keireasm` definitions and
publishes DLL/PDB/deps files under cooked content; standalone startup loads those DLLs from the manifest. Packaged games
carry hostfxr/CoreCLR and do not require a system .NET installation, while project compilation still requires the SDK.

## Physics, audio, and navigation contracts

`PhysicsWorld` provides Jolt-backed stable bodies, validated primitive/convex/static-triangle colliders,
static/dynamic/kinematic motion, triggers, layer/mask filtering, deterministic Kéire-ordered ray and overlap queries,
fixed stepping, and enter/stay/exit contacts. Collision cooking is cancellable, hashes canonical data, rejects invalid
dynamic triangle use, and never partially registers failed bodies.

Audio processing is submitted as immutable, monotonically revisioned `AudioGraphSnapshot` values. Validation covers
node identity, finite parameters, source references, the output contract, and cycle rejection; feedback is accepted
only when it passes through an explicitly delayed `Delay` input. The miniaudio engine owns device or headless output,
bounded resident voices, priority virtualization, spatial listener/source state, doppler/attenuation, snapshots,
meters, and deterministic offline rendering. Headless mixer rendering executes effect racks, pre/post sends, hierarchy,
ducking, and automatic meters; priority-blended Reverb Zones apply authored snapshots and restore base routing on exit.
Submission is synchronized and never invokes game callbacks from the device thread. See
[Audio Production Workflow And Audit](AudioProduction.md) for device-DSP gaps and release budgets.

`BakeNavigationMesh` uses Recast to build deterministic polygon data and a Detour tile payload from explicit finite
geometry. `NavigationWorld` atomically publishes validated revisioned meshes and supports Detour-backed synchronous
or cancellable asynchronous paths. Async results are marked stale when a newer mesh is published before completion and
cancelled when their world or service closes. Crowd agents provide bounded acceleration/separation steering, while
obstacle edits invalidate active paths.

These contracts intentionally expose no Coral, CoreCLR, Jolt, miniaudio, Recast/Detour, JSON, mutex, or platform
types. Skeleton, skinned-mesh, animation-clip, and animation-graph assets likewise expose first-party data only;
FBX/glTF/GLB import emits stable skeletal subassets with bounded normalized influences, and `AnimatorComponent`
produces transition samples, root motion, events, and skin palettes. The editor includes prefab override,
build/managed-diagnostic, and profiler panels. The packaged sandbox supplies managed controller/navigation scripts and
base/variant prefab sources, and its deterministic cook/runtime route validates managed publication and CoreCLR load.

## Cooked runtime manifests

Runtime manifests use schema 4 and include build identity, the ordered enabled build-scene IDs and matching startup
scene, required source modules, managed assembly roots, default input and mixer identities, subsystem flags, the
32-layer physics configuration, rendering environment values, and streaming settings. Duplicate, empty, oversized, or
startup-mismatched scene lists fail before assets mount. Older content fails with a direct recook diagnostic; manifests
from a newer schema fail without partially starting runtime services.
